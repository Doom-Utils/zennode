//----------------------------------------------------------------------------
//
// File:        console.cpp
// Date:        13-Aug-2000
// Programmer:  Marc Rousseau
//
// Description: Screen I/O routines for ZenNode
//
// Copyright (c) 2000-2002 Marc Rousseau, All Rights Reserved.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307, USA.
//
// Revision History:
//
//   06-??-95	Added Win32 support
//   07-19-95	Updated command line & screen logic
//   11-19-95	Updated command line again
//   12-06-95	Add config & customization file support
//   11-??-98	Added Linux support
//   04-21-01	Modified Linux code to match Win32 console I/O behavior
//   04-26-01	Added SIGABRT to list of 'handled' signals
//
//----------------------------------------------------------------------------

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined ( __OS2__ )
    #include <conio.h>
    #include <dos.h>
    #include <io.h>
    #define INCL_DOS
    #define INCL_SUB
    #include <os2.h>
#elif defined ( __WIN32__ )
    #include <conio.h>
    #include <dos.h>
    #include <io.h>
    #include <signal.h>
    #include <windows.h>
    #include <wincon.h>
#elif defined ( __GNUC__ )
    #include <stdarg.h>
    #include <signal.h>
    #include <sys/time.h>
    #include <termios.h>
    #include <unistd.h>
#else
    #error This program must be compiled as a 32-bit app.
#endif

#include "common.hpp"
#include "wad.hpp"
#include "level.hpp"
#include "ZenNode.hpp"

void SaveConsoleSettings ();
void RestoreConsoleSettings ();
void HideCursor ();
void ShowCursor ();

ULONG startX, startY;
char  progress [4] = { 0x7C, 0x2F, 0x2D, 0x5C };
int   progressIndex;

#if defined ( __OS2__ )

static VIOCURSORINFO vioco;
static int oldAttr;
static ULONG sx, sy;

void SaveConsoleSettings ()
{
    VioGetCurType ( &vioco, 0 );
    oldAttr = vioco.attr;
    vioco.attr = 0xFFFF;
    VioSetCurType ( &vioco, 0 );
}

void RestoreConsoleSettings ()
{
    vioco.attr = oldAttr;
    VioSetCurType ( &vioco, 0 );
}

void GetXY ( ULONG *x, ULONG *y )
{
    VioGetCurPos ( y, x, 0 );
}

void GotoXY ( ULONG x, ULONG y )
{
    VioSetCurPos ( y, x, 0 );
}

ULONG CurrentTime ()
{
    ULONG time;
    DosQuerySysInfo ( QSV_MS_COUNT, QSV_MS_COUNT, &time, 4 );
    return time;
}

void Status ( char *message )
{
    int len = strlen ( message );
    VioWrtCharStr (( BYTE * ) message, len, startY, startX, 0 );
    VioWrtNChar (( BYTE * )  " ", 80 - ( startX + len ), startY, startX + len, 0 );
    sy = startY;
    sx = startX + len;
}

void GoRight ()
{
    VioWrtNChar (( BYTE * )  "R", 1, sy, sx++, 0 );
}

void GoLeft ()
{
    VioWrtNChar (( BYTE * )  "L", 1, sy, sx - 1 , 0 );
}

void Backup ()
{
    sx--;
}

void ShowDone ()
{
    VioWrtNChar (( BYTE * )  "*", 1, sy, sx, 0 );
}

void ShowProgress ()
{
    VioWrtNChar (( BYTE * ) &progress [ progressIndex++ % SIZE ( progress )], 1, sy, sx, 0 );
}

void MoveUp ( int delta )
{
    sy -= delta;
    VioSetCurPos ( sy, 0, 0 );
}

void MoveDown ( int delta )
{
    sy += delta;
    VioSetCurPos ( sy, 0, 0 );
}

#elif defined ( __WIN32__ )

#if defined ( __GNUC__ )
#define cprintf _cprintf
#endif

static CONSOLE_SCREEN_BUFFER_INFO screenInfo;
static CONSOLE_CURSOR_INFO        cursorInfo;
static BOOL                       oldVisible;

static HANDLE                     hOutput;
static COORD                      currentPos;

static LARGE_INTEGER              timerFrequency;

long WINAPI myHandler ( PEXCEPTION_POINTERS )
{
    RestoreConsoleSettings ();

    return EXCEPTION_CONTINUE_SEARCH;
}

void SignalHandler ( int )
{
    RestoreConsoleSettings ();
    cprintf ( "\r\n" );
    exit ( -1 );
}

void RestoreConsoleSettings ()
{
    cursorInfo.bVisible = oldVisible;
    SetConsoleCursorInfo ( hOutput, &cursorInfo );
}

void SaveConsoleSettings ()
{
    hOutput = GetStdHandle ( STD_OUTPUT_HANDLE );
    GetConsoleCursorInfo ( hOutput, &cursorInfo );
    GetConsoleScreenBufferInfo ( hOutput, &screenInfo );
    oldVisible = cursorInfo.bVisible;
    SetUnhandledExceptionFilter ( myHandler );
    signal ( SIGBREAK, SignalHandler );
    signal ( SIGINT, SignalHandler );
    atexit ( RestoreConsoleSettings );

    QueryPerformanceFrequency ( &timerFrequency );
}

void HideCursor ()
{
    CONSOLE_CURSOR_INFO info = cursorInfo;
    info.bVisible = FALSE;
    SetConsoleCursorInfo ( hOutput, &info );
}

void ShowCursor ()
{
    CONSOLE_CURSOR_INFO info = cursorInfo;
    info.bVisible = TRUE;
    SetConsoleCursorInfo ( hOutput, &info );
}

#include <stdio.h>

int GetKey ()
{
    int key = 0;
    UCHAR *ptr = ( UCHAR * ) &key;
    int ch = getch ();

    if ( ch == 0xE0 ) {
        *ptr++ = 0x1B;
        *ptr++ = 0x5B;
        ch = getch ();
        switch ( ch ) {
            case 0x48 : ch = 0x41;  break;
            case 0x50 : ch = 0x42;  break;
            case 0x4D : ch = 0x43;  break;
            case 0x4B : ch = 0x44;  break;
        }
    }
    *ptr++ = ( UCHAR ) ch;

    return key;
}

bool KeyPressed ()
{
    return ( kbhit () != 0 ) ? true : false;
}

ULONG CurrentTime ()
{
    LARGE_INTEGER time;
    QueryPerformanceCounter ( &time );

    return ( ULONG ) ( 1000 * time.QuadPart / timerFrequency.QuadPart );
}

void GetXY ( ULONG *x, ULONG *y )
{
    GetConsoleScreenBufferInfo ( hOutput, &screenInfo );
    *x = screenInfo.dwCursorPosition.X;
    *y = screenInfo.dwCursorPosition.Y;
}

void GotoXY ( ULONG x, ULONG y )
{
    COORD pos;
    pos.X = ( SHORT ) x;
    pos.Y = ( SHORT ) y;
    SetConsoleCursorPosition ( hOutput, pos );
}

void Status ( char *message )
{
    DWORD count;
    int len = strlen ( message );
    currentPos.X = ( SHORT ) startX;
    currentPos.Y = ( SHORT ) startY;
    if ( len ) {
        WriteConsoleOutputCharacter ( hOutput, message, len, currentPos, &count );
        currentPos.X = ( SHORT ) ( currentPos.X + len );
    }
    FillConsoleOutputCharacter ( hOutput, ' ', screenInfo.dwSize.X - currentPos.X, currentPos, &count );
}

void GoRight ()
{
    DWORD count;
    WriteConsoleOutputCharacter ( hOutput, "R", 1, currentPos, &count );
    currentPos.X++;
}

void GoLeft ()
{
    DWORD count;
    currentPos.X--;
    WriteConsoleOutputCharacter ( hOutput, "L", 1, currentPos, &count );
    currentPos.X++;
}

void Backup ()
{
    currentPos.X--;
}

void ShowDone ()
{
    DWORD count;
    WriteConsoleOutputCharacter ( hOutput, "*", 1, currentPos, &count );
}

void ShowProgress ()
{
    DWORD count;
    WriteConsoleOutputCharacter ( hOutput, &progress [ progressIndex++ % SIZE ( progress )], 1, currentPos, &count );
}

void MoveUp ( int delta )
{
    GetConsoleScreenBufferInfo ( hOutput, &screenInfo );
    COORD pos;
    pos.X = ( SHORT ) 0;
    pos.Y = ( SHORT ) ( screenInfo.dwCursorPosition.Y - delta );
    SetConsoleCursorPosition ( hOutput, pos );
}

void MoveDown ( int delta )
{
    GetConsoleScreenBufferInfo ( hOutput, &screenInfo );
    COORD pos;
    pos.X = ( SHORT ) 0;
    pos.Y = ( SHORT ) ( screenInfo.dwCursorPosition.Y + delta );
    SetConsoleCursorPosition ( hOutput, pos );
}

#elif defined ( __GNUC__ )

static FILE *console;
static termios stored;
static termios term_getch;
static termios term_kbhit;
static int lastChar;
static int keyhit;
static bool cursor_visible = true;

extern char *strupr ( char *ptr );

int GetKey ()
{
    int retVal = lastChar;
    lastChar = 0;

    if ( keyhit == 0 ) {
        tcsetattr ( 0, TCSANOW, &term_getch );
        keyhit = read ( STDIN_FILENO, &retVal, sizeof ( retVal ));
    }

    keyhit = 0;

    return retVal;
}

bool KeyPressed ()
{
    if ( keyhit == 0 ) {
        tcsetattr ( 0, TCSANOW, &term_kbhit );
        keyhit = read ( STDIN_FILENO, &lastChar, sizeof ( lastChar ));
    }

    return ( keyhit != 0 ) ? true : false;
}

void cprintf ( char *fmt, ... )
{
    va_list args;
    va_start ( args, fmt );

    vfprintf ( console, fmt, args );

    va_end ( args );
}

void SignalHandler ( int signal )
{
    struct sigaction sa;
    memset ( &sa, 0, sizeof ( sa ));
    sa.sa_handler = SIG_DFL;
    switch ( signal ) {
        case SIGABRT :
            RestoreConsoleSettings ();
            break;
        case SIGINT :
            RestoreConsoleSettings ();
            printf ( "\r\n" );
            exit ( -1 );
            break;
        case SIGTSTP :
            RestoreConsoleSettings ();
            printf ( "\r\n" );
            sigaction ( SIGTSTP, &sa, NULL );
            kill ( getpid (), SIGTSTP );
            break;
        case SIGCONT :
            SaveConsoleSettings ();
            break;
    }
}

void SaveConsoleSettings ()
{
    tcgetattr ( 0, &stored );
    memcpy ( &term_getch, &stored, sizeof ( struct termios ));
    // Disable echo
    term_getch.c_lflag &= ~ECHO;
    // Disable canonical mode, and set buffer size to 1 byte
    term_getch.c_lflag &= ~ICANON;
    term_getch.c_cc[VMIN] = 1;

    memcpy ( &term_kbhit, &term_getch, sizeof ( struct termios ));
    term_kbhit.c_cc[VTIME] = 0;
    term_kbhit.c_cc[VMIN] = 0;

    // Create a 'console' device
    console = fopen ( "/dev/tty", "w" );
    if ( console == NULL ) {
        console = stdout;
    }

    HideCursor ();

    struct sigaction sa;
    memset ( &sa, 0, sizeof ( sa ));
    sa.sa_handler = SignalHandler;
    sigaction ( SIGABRT, &sa, NULL );
    sigaction ( SIGINT,  &sa, NULL );
    sigaction ( SIGTSTP, &sa, NULL );
    sigaction ( SIGCONT, &sa, NULL );

    atexit ( RestoreConsoleSettings );
}

void RestoreConsoleSettings ()
{
    struct sigaction sa;
    memset ( &sa, 0, sizeof ( sa ));
    sa.sa_handler = SIG_DFL;
    sigaction ( SIGABRT, &sa, NULL );
    sigaction ( SIGINT,  &sa, NULL );
    sigaction ( SIGTSTP, &sa, NULL );
    sigaction ( SIGCONT, &sa, NULL );

    tcsetattr ( 0, TCSANOW, &stored );

    ShowCursor ();
}

void ClearScreen ()
{
    printf ( "\033[2J" );
    fflush ( stdout );
}

void HideCursor ()
{
    if ( cursor_visible == true ) {
        printf ( "\033[?25l" );
        fflush ( stdout );
        cursor_visible = false;
    }
}

void ShowCursor ()
{
    if ( cursor_visible == false ) {
        printf ( "\033[?25h" );
        fflush ( stdout );
        cursor_visible = true;
    }
}

void GetXY ( ULONG *x, ULONG *y )
{
    *x = MAX_LUMP_NAME + 5;
    *y = 0;
}

void GotoXY ( ULONG x, ULONG y )
{
    fprintf ( console, "\033[%dG", x );
    fflush ( console );
}

ULONG CurrentTime ()
{
    timeval time;
    gettimeofday ( &time, NULL );
    return ( ULONG ) (( time.tv_sec * 1000 ) + ( time.tv_usec / 1000 ));
}

void Status ( char *message )
{
    fprintf ( console, "\033[%dG%s\033[K", startX, message );
    fflush ( console );
}

void GoRight ()
{
    fprintf ( console, "R" );
    fflush ( console );
}

void GoLeft ()
{
    fprintf ( console, "\033[DL" );
    fflush ( console );
}

void Backup ()
{
    fprintf ( console, "\033[D" );
    fflush ( console );
}

void ShowDone ()
{
    fprintf ( console, "*\033[D" );
    fflush ( console );
}

void ShowProgress ()
{
    fprintf ( console, "%c\033[D", progress [ progressIndex++ % SIZE ( progress )] );
    fflush ( console );
}

void MoveUp ( int delta )
{
    fprintf ( console, "\033[%dA", delta );
    fflush ( console );
}

void MoveDown ( int delta )
{
    fprintf ( console, "\033[%dB", delta );
    fflush ( console );
}

#endif
