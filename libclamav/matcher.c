/*
 *  Copyright (C) 2007-2009 Sourcefire, Inc.
 *
 *  Authors: Tomasz Kojm
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *  MA 02110-1301, USA.
 */

#if HAVE_CONFIG_H
#include "clamav-config.h"
#endif

#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef	HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "clamav.h"
#include "others.h"
#include "matcher-ac.h"
#include "matcher-bm.h"
#include "md5.h"
#include "filetypes.h"
#include "matcher.h"
#include "pe.h"
#include "elf.h"
#include "execs.h"
#include "special.h"
#include "str.h"
#include "cltypes.h"
#include "default.h"
#include "macho.h"

int cli_scanbuff(const unsigned char *buffer, uint32_t length, uint32_t offset, cli_ctx *ctx, cli_file_t ftype, struct cli_ac_data **acdata)
{
	int ret = CL_CLEAN;
	unsigned int i;
	struct cli_ac_data mdata;
	struct cli_matcher *groot, *troot = NULL;
	const char **virname=ctx->virname;
	const struct cl_engine *engine=ctx->engine;

    if(!engine) {
	cli_errmsg("cli_scanbuff: engine == NULL\n");
	return CL_ENULLARG;
    }

    groot = engine->root[0]; /* generic signatures */

    if(ftype) {
	for(i = 1; i < CLI_MTARGETS; i++) {
	    if(cli_mtargets[i].target == ftype) {
		troot = engine->root[i];
		break;
	    }
	}
    }

    if(troot) {

	if(!acdata && (ret = cli_ac_initdata(&mdata, troot->ac_partsigs, troot->ac_lsigs, troot->ac_reloff_num, CLI_DEFAULT_AC_TRACKLEN)))
	    return ret;

	if(troot->ac_only || (ret = cli_bm_scanbuff(buffer, length, virname, troot, offset, -1)) != CL_VIRUS)
	    ret = cli_ac_scanbuff(buffer, length, virname, NULL, NULL, troot, acdata ? (acdata[0]) : (&mdata), offset, ftype, NULL, AC_SCAN_VIR, NULL);

	if(!acdata)
	    cli_ac_freedata(&mdata);

	if(ret == CL_VIRUS)
	    return ret;
    }

    if(!acdata && (ret = cli_ac_initdata(&mdata, groot->ac_partsigs, groot->ac_lsigs, groot->ac_reloff_num, CLI_DEFAULT_AC_TRACKLEN)))
	return ret;

    if(groot->ac_only || (ret = cli_bm_scanbuff(buffer, length, virname, groot, offset, -1)) != CL_VIRUS)
	ret = cli_ac_scanbuff(buffer, length, virname, NULL, NULL, groot, acdata ? (acdata[1]) : (&mdata), offset, ftype, NULL, AC_SCAN_VIR, NULL);

    if(!acdata)
	cli_ac_freedata(&mdata);

    return ret;
}

/*
 * offdata[0]: type
 * offdata[1]: offset value
 * offdata[2]: max shift
 * offdata[3]: section number
 */
int cli_caloff(const char *offstr, struct cli_target_info *info, int fd, unsigned int target, uint32_t *offdata, uint32_t *offset_min, uint32_t *offset_max)
{
	int (*einfo)(int, struct cli_exe_info *) = NULL;
	char offcpy[65];
	unsigned int n, val;
	char *pt;
	off_t pos;
	struct stat sb;


    if(!info) { /* decode offset string */
	if(!offstr) {
	    cli_errmsg("cli_caloff: offstr == NULL\n");
	    return CL_ENULLARG;
	}

	if(!strcmp(offstr, "*")) {
	    offdata[0] = *offset_max = *offset_min = CLI_OFF_ANY;
	    return CL_SUCCESS;
	}

	if(strlen(offstr) > 64) {
	    cli_errmsg("cli_caloff: Offset string too long\n");
	    return CL_EMALFDB;
	}
	strcpy(offcpy, offstr);

	if((pt = strchr(offcpy, ','))) {
	    if(!cli_isnumber(pt + 1)) {
		cli_errmsg("cli_caloff: Invalid offset shift value\n");
		return CL_EMALFDB;
	    }
	    offdata[2] = atoi(pt + 1);
	    *pt = 0;
	} else {
	    offdata[2] = 0;
	}

	*offset_max = *offset_min = CLI_OFF_NONE;

	if(!strncmp(offcpy, "EP+", 3) || !strncmp(offcpy, "EP-", 3)) {
	    if(offcpy[2] == '+')
		offdata[0] = CLI_OFF_EP_PLUS;
	    else
		offdata[0] = CLI_OFF_EP_MINUS;

	    if(!cli_isnumber(&offcpy[3])) {
		cli_errmsg("cli_caloff: Invalid offset value\n");
		return CL_EMALFDB;
	    }
	    offdata[1] = atoi(&offcpy[3]);

	} else if(offcpy[0] == 'S') {
	    if(!strncmp(offstr, "SL+", 3)) {
		offdata[0] = CLI_OFF_SL_PLUS;
		if(!cli_isnumber(&offcpy[3])) {
		    cli_errmsg("cli_caloff: Invalid offset value\n");
		    return CL_EMALFDB;
		}
		offdata[1] = atoi(&offcpy[3]);

	    } else if(sscanf(offcpy, "S%u+%u", &n, &val) == 2) {
		offdata[0] = CLI_OFF_SX_PLUS;
		offdata[1] = val;
		offdata[3] = n;
	    } else {
		cli_errmsg("cli_caloff: Invalid offset string\n");
		return CL_EMALFDB;
	    }

	} else if(!strncmp(offcpy, "EOF-", 4)) {
	    offdata[0] = CLI_OFF_EOF_MINUS;
	    if(!cli_isnumber(&offcpy[4])) {
		cli_errmsg("cli_caloff: Invalid offset value\n");
		return CL_EMALFDB;
	    }
	    offdata[1] = atoi(&offcpy[4]);
	} else {
	    offdata[0] = CLI_OFF_ABSOLUTE;
	    if(!cli_isnumber(offcpy)) {
		cli_errmsg("cli_caloff: Invalid offset value\n");
		return CL_EMALFDB;
	    }
	    *offset_min = offdata[1] = atoi(offcpy);
	    *offset_max = *offset_min + offdata[2];
	}

	if(offdata[0] != CLI_OFF_ANY && offdata[0] != CLI_OFF_ABSOLUTE && offdata[0] != CLI_OFF_EOF_MINUS) {
	    if(target != 1 && target != 6 && target != 9) {
		cli_errmsg("cli_caloff: Invalid offset type for target %u\n", target);
		return CL_EMALFDB;
	    }
	}

    } else {
	/* calculate relative offsets */
	if(info->status == -1) {
	    *offset_min = *offset_max = 0;
	    return CL_SUCCESS;
	}

	if((offdata[0] == CLI_OFF_EOF_MINUS)) {
	    if(!info->fsize) {
		if(fstat(fd, &sb) == -1) {
		    cli_errmsg("cli_caloff: fstat(%d) failed\n", fd);
		    return CL_ESTAT;
		}
		info->fsize = sb.st_size;
	    }

	} else if(!info->status) {
	    if(target == 1)
		einfo = cli_peheader;
	    else if(target == 6)
		einfo = cli_elfheader;
	    else if(target == 9)
		einfo = cli_machoheader;

	    if(!einfo) {
		cli_errmsg("cli_caloff: Invalid offset/filetype\n");
		return CL_EMALFDB;
	    }

	    if((pos = lseek(fd, 0, SEEK_CUR)) == -1) {
		cli_errmsg("cli_caloff: lseek(%d) failed\n", fd);
		return CL_ESEEK;
	    }

	    lseek(fd, 0, SEEK_SET);
	    if(einfo(fd, &info->exeinfo)) {
		/* einfo *may* fail */
		lseek(fd, pos, SEEK_SET);
		info->status = -1;
		*offset_min = *offset_max = 0;
		return CL_SUCCESS;
	    }
	    lseek(fd, pos, SEEK_SET);
	    info->status = 1;
	}

	switch(offdata[0]) {
	    case CLI_OFF_EOF_MINUS:
		*offset_min = info->fsize - offdata[1];
		break;

	    case CLI_OFF_EP_PLUS:
		*offset_min = info->exeinfo.ep + offdata[1];
		break;

	    case CLI_OFF_EP_MINUS:
		*offset_min = info->exeinfo.ep - offdata[1];
		break;

	    case CLI_OFF_SL_PLUS:
		*offset_min = info->exeinfo.section[info->exeinfo.nsections - 1].raw + offdata[1];
		break;

	    case CLI_OFF_SX_PLUS:
		if(offdata[3] >= info->exeinfo.nsections)
		    *offset_min = 0;
		else
		    *offset_min = info->exeinfo.section[offdata[3]].raw + offdata[1];
		break;

	    default:
		cli_errmsg("cli_caloff: Not a relative offset (type: %u)\n", offdata[0]);
		return CL_EARG;
	}

	if(!*offset_min)
	    *offset_max = 0;
	else
	    *offset_max = *offset_min + offdata[2];
    }

    return CL_SUCCESS;
}

int cli_checkfp(int fd, cli_ctx *ctx)
{
	unsigned char *digest;
	const char *virname;
	off_t pos;


    if((pos = lseek(fd, 0, SEEK_CUR)) == -1) {
	cli_errmsg("cli_checkfp(): lseek() failed\n");
	return 0;
    }

    lseek(fd, 0, SEEK_SET);

    if(ctx->engine->md5_fp) {
	if(!(digest = cli_md5digest(fd))) {
	    cli_errmsg("cli_checkfp(): Can't generate MD5 checksum\n");
	    lseek(fd, pos, SEEK_SET);
	    return 0;
	}

	if(cli_bm_scanbuff(digest, 16, &virname, ctx->engine->md5_fp, 0, -1) == CL_VIRUS) {
	    cli_dbgmsg("cli_checkfp(): Found false positive detection (fp sig: %s)\n", virname);
	    free(digest);
	    lseek(fd, pos, SEEK_SET);
	    return 1;
	}
	free(digest);
    }

    lseek(fd, pos, SEEK_SET);
    return 0;
}

int cli_scandesc(int desc, cli_ctx *ctx, cli_file_t ftype, uint8_t ftonly, struct cli_matched_type **ftoffset, unsigned int acmode)
{
 	unsigned char *buffer, *buff, *endbl, *upt;
	int ret = CL_CLEAN, type = CL_CLEAN, bytes;
	unsigned int i, evalcnt;
	uint32_t buffersize, length, maxpatlen, shift = 0, offset = 0;
	uint64_t evalids;
	struct cli_ac_data gdata, tdata;
	cli_md5_ctx md5ctx;
	unsigned char digest[16];
	struct cli_matcher *groot = NULL, *troot = NULL;


    if(!ctx->engine) {
	cli_errmsg("cli_scandesc: engine == NULL\n");
	return CL_ENULLARG;
    }

    if(!ftonly)
	groot = ctx->engine->root[0]; /* generic signatures */

    if(ftype) {
	for(i = 1; i < CLI_MTARGETS; i++) {
	    if(cli_mtargets[i].target == ftype) {
		troot = ctx->engine->root[i];
		break;
	    }
	}
    }

    if(ftonly) {
	if(!troot)
	    return CL_CLEAN;

	maxpatlen = troot->maxpatlen;
    } else {
	if(troot)
	    maxpatlen = MAX(troot->maxpatlen, groot->maxpatlen);
	else
	    maxpatlen = groot->maxpatlen;
    }

    /* prepare the buffer */
    buffersize = maxpatlen + SCANBUFF;
    if(!(buffer = (unsigned char *) cli_calloc(buffersize, sizeof(unsigned char)))) {
	cli_dbgmsg("cli_scandesc(): unable to cli_calloc(%u)\n", buffersize);
	return CL_EMEM;
    }

    if(!ftonly)
	if((ret = cli_ac_initdata(&gdata, groot->ac_partsigs, groot->ac_lsigs, groot->ac_reloff_num, CLI_DEFAULT_AC_TRACKLEN)) || (ret = cli_ac_caloff(groot, &gdata, desc)))
	    return ret;

    if(troot) {
	if((ret = cli_ac_initdata(&tdata, troot->ac_partsigs, troot->ac_lsigs, troot->ac_reloff_num, CLI_DEFAULT_AC_TRACKLEN)) || (ret = cli_ac_caloff(troot, &tdata, desc))) {
	    if(!ftonly)
		cli_ac_freedata(&gdata);
	    return ret;
	}
    }

    if(!ftonly && ctx->engine->md5_hdb)
	cli_md5_init(&md5ctx);

    buff = buffer;
    buff += maxpatlen; /* pointer to read data block */
    endbl = buff + SCANBUFF - maxpatlen; /* pointer to the last block
					  * length of maxpatlen
					  */

    upt = buff;
    while((bytes = cli_readn(desc, buff + shift, SCANBUFF - shift)) > 0) {

    if (ctx->engine->callback && !ctx->engine->callback(desc, bytes))
        return CL_EUSERABORT;

	if(ctx->scanned)
	    *ctx->scanned += bytes / CL_COUNT_PRECISION;

	length = shift + bytes;
	if(upt == buffer)
	    length += maxpatlen;

	if(troot) {
	    if(troot->ac_only || (ret = cli_bm_scanbuff(upt, length, ctx->virname, troot, offset, desc)) != CL_VIRUS)
		ret = cli_ac_scanbuff(upt, length, ctx->virname, NULL, NULL, troot, &tdata, offset, ftype, ftoffset, acmode, NULL);

	    if(ret == CL_VIRUS) {
		free(buffer);
		if(!ftonly)
		    cli_ac_freedata(&gdata);
		cli_ac_freedata(&tdata);

		if(cli_checkfp(desc, ctx))
		    return CL_CLEAN;
		else
		    return CL_VIRUS;
	    }
	}

	if(!ftonly) {
	    if(groot->ac_only || (ret = cli_bm_scanbuff(upt, length, ctx->virname, groot, offset, desc)) != CL_VIRUS)
		ret = cli_ac_scanbuff(upt, length, ctx->virname, NULL, NULL, groot, &gdata, offset, ftype, ftoffset, acmode, NULL);

	    if(ret == CL_VIRUS) {
		free(buffer);
		cli_ac_freedata(&gdata);
		if(troot)
		    cli_ac_freedata(&tdata);
		if(cli_checkfp(desc, ctx))
		    return CL_CLEAN;
		else
		    return CL_VIRUS;

	    } else if((acmode & AC_SCAN_FT) && ret >= CL_TYPENO) {
		if(ret > type)
		    type = ret;
	    }

	    if(ctx->engine->md5_hdb)
		cli_md5_update(&md5ctx, buff + shift, bytes);
	}

	if(bytes + shift == SCANBUFF) {
	    memmove(buffer, endbl, maxpatlen);
	    offset += SCANBUFF;

	    if(upt == buff) {
		upt = buffer;
		offset -= maxpatlen;
	    }

	    shift = 0;

	} else {
	    shift += bytes;
	}
    }

    free(buffer);

    if(troot) {
	for(i = 0; i < troot->ac_lsigs; i++) {
	    evalcnt = 0;
	    evalids = 0;
	    if(cli_ac_chklsig(troot->ac_lsigtable[i]->logic, troot->ac_lsigtable[i]->logic + strlen(troot->ac_lsigtable[i]->logic), tdata.lsigcnt[i], &evalcnt, &evalids, 0) == 1) {
		if(ctx->virname)
		    *ctx->virname = troot->ac_lsigtable[i]->virname;
		ret = CL_VIRUS;
		break;
	    }
	}
	cli_ac_freedata(&tdata);
    }

    if(groot) {
	if(ret != CL_VIRUS) for(i = 0; i < groot->ac_lsigs; i++) {
	    evalcnt = 0;
	    evalids = 0;
	    if(cli_ac_chklsig(groot->ac_lsigtable[i]->logic, groot->ac_lsigtable[i]->logic + strlen(groot->ac_lsigtable[i]->logic), gdata.lsigcnt[i], &evalcnt, &evalids, 0) == 1) {
		if(ctx->virname)
		    *ctx->virname = groot->ac_lsigtable[i]->virname;
		ret = CL_VIRUS;
		break;
	    }
	}
	cli_ac_freedata(&gdata);
    }

    if(ret == CL_VIRUS) {
	lseek(desc, 0, SEEK_SET);
	if(cli_checkfp(desc, ctx))
	    return CL_CLEAN;
	else
	    return CL_VIRUS;
    }

    if(!ftonly && ctx->engine->md5_hdb) {
	cli_md5_final(digest, &md5ctx);
	if(cli_bm_scanbuff(digest, 16, ctx->virname, ctx->engine->md5_hdb, 0, -1) == CL_VIRUS && (cli_bm_scanbuff(digest, 16, NULL, ctx->engine->md5_fp, 0, -1) != CL_VIRUS))
	    return CL_VIRUS;
    }

    return (acmode & AC_SCAN_FT) ? type : CL_CLEAN;
}
