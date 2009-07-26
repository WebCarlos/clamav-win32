/*
 * main() wrapper to handle startup code
 *
 * Copyright (c) 2008 Gianluigi Tiesi <sherpya@netfarm.it>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this software; if not, write to the
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <platform.h>
#include <cwhelpers.h>

extern int cw_main(int argc, char *argv[]);

void ConsoleCtrlHandler(DWORD ctrl_type)
{
    fprintf(stderr, "Control+C pressed, aborting...\n");
    exit(1);
}

static void StopConsoleHandler(void)
{
    SetConsoleCtrlHandler((PHANDLER_ROUTINE) ConsoleCtrlHandler, FALSE);
}

#undef main
int main(int argc, char* argv[])
{

#if defined(_MSC_VER) && !defined(_DEBUG) /* Avoid bypassing calls to Debugger */
    SetErrorMode(SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(CrashHandlerExceptionFilter);
#endif

    _setmode(_fileno(stdin), O_BINARY);

    if (!SetConsoleCtrlHandler((PHANDLER_ROUTINE) ConsoleCtrlHandler, TRUE))
        fprintf(stderr, "[cw_main] Cannot install Console Ctrl Handler (%d)\n", GetLastError());

    atexit(StopConsoleHandler);

    return cw_main(argc, argv);
}
