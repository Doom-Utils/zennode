//----------------------------------------------------------------------------
//
// File:        compare.cpp
// Date:        16-Jan-1996
// Programmer:  Marc Rousseau
//
// Description: Compares two REJECT structures and report any differences
//
// Copyright (c) 1994-2000 Marc Rousseau, All Rights Reserved.
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
//----------------------------------------------------------------------------

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined ( __OS2__ )
    #define INCL_DOS
    #define INCL_SUB
    #include <conio.h>
    #include <dos.h>
    #include <os2.h>
#elif defined ( __WIN32__ )
    #include <conio.h>
    #include <dos.h>
    #include <windows.h>
    #include <wincon.h>
#elif defined ( __GNUC__ )
#else
    #error This program must be compiled as a 32-bit app.
#endif

#include "common.hpp"
#include "wad.hpp"
#include "level.hpp"

#define VERSION		"1.02"
#define MAX_LEVELS	50

#define UNSUPPORTED_FEATURE	-1
#define UNRECOGNIZED_PARAMETER	-2

#if defined ( __BORLANDC__ )
    #pragma option -x -xf
#endif

extern ULONG startX, startY;

void SaveConsoleSettings ();
void RestoreConsoleSettings ();

#if defined ( __OS2__ )

#define SEPERATOR	'\\'
#define DEFAULT_CHAR	'û'

#elif defined ( __WIN32__ )

#define SEPERATOR	'\\'
#define DEFAULT_CHAR	'û'

#elif defined ( __GNUC__ )

#define SEPERATOR	'/'
#define DEFAULT_CHAR	'*'

#define stricmp strcasecmp
#define cprintf printf

extern char *strupr ( char *ptr );
extern int getch ();
extern bool kbhit ();

#endif

void GetXY ( ULONG *x, ULONG *y );
void GotoXY ( ULONG x, ULONG y );
ULONG CurrentTime ();
void Status ( char *message );
void GoRight ();
void GoLeft ();
void Backup ();
void ShowDone ();
void ShowProgress ();
void MoveUp ( int delta );
void MoveDown ( int delta );

void printHelp ()
{
    fprintf ( stderr, "Usage: compare {/options} filename1[.wad] filename2[.wad] [level{+level}]\n" );
    fprintf ( stderr, "\n" );
    fprintf ( stderr, "     /x+ turn on option   /x- turn off option  û = default\n" );
    fprintf ( stderr, "\n" );
    fprintf ( stderr, "     level - ExMy for DOOM / Heretic\n" );
    fprintf ( stderr, "             MAPxx for DOOM II / HEXEN\n" );
}

int parseArgs ( int index, char *argv[] )
{
    bool errors = false;
    while ( argv [ index ] ) {

        if ( argv [index][0] != '/' ) break;
/*
        char *localCopy = strdup ( argv [ index ]);
        char *basePtr = localCopy + 1;
        strupr ( localCopy );

        basePtr = strtok ( basePtr, "/" );
        while ( basePtr ) {
            char *ptr;
            try {
                ptr = basePtr;
                while ( *ptr ) {
                    int option = *ptr++;
                    bool setting = true;
                    if (( *ptr == '+' ) || ( *ptr == '-' )) {
                        setting = ( *ptr++ == '-' ) ? false : true;
                    }
                    switch ( option ) {
                        default  : throw UNRECOGNIZED_PARAMETER;
                    }
                }
            }
            catch ( int x ) {
                errors = true;
                int offset = basePtr - localCopy - 1;
                int width = strlen ( basePtr ) + 1;
                switch ( x ) {
                    case UNSUPPORTED_FEATURE :
                    case UNRECOGNIZED_PARAMETER :
                        fprintf ( stderr, "Unrecognized parameter '%*.*s'\n", width, width, argv [index] + offset );
                        break;
                }
            }
            basePtr = strtok ( NULL, "/" );
        }
*/
        index++;
    }
    if ( errors ) fprintf ( stderr, "\n" );
    return index;
}

int getLevels ( int argIndex, char *argv[], char names [][MAX_LUMP_NAME], wadList *list1, wadList *list2 )
{
    int index = 0, errors = 0;

    char buffer [128];
    buffer [0] = '\0';
    if ( argv [argIndex] ) {
        strcpy ( buffer, argv [argIndex] );
        strupr ( buffer );
    }
    char *ptr = strtok ( buffer, "+" );

    // See if the user requested specific levels
    if ( WAD::IsMap ( ptr )) {
        argIndex++;
        while ( ptr ) {
            if ( WAD::IsMap ( ptr )) {
                if ( list1->FindWAD ( ptr )) {
                    strcpy ( names [index++], ptr );
                } else {
                    fprintf ( stderr, "  Could not find %s\n", ptr, errors++ );
                }
            } else {
                fprintf ( stderr, "  %s is not a valid name for a level\n", ptr, errors++ );
            }
            ptr = strtok ( NULL, "+" );
        }
    } else {
        int size = list1->DirSize ();
        const wadListDirEntry *dir = list1->GetDir ( 0 );
        for ( int i = 0; i < size; i++ ) {
            if ( dir->wad->IsMap ( dir->entry->name )) {
                if ( index == MAX_LEVELS ) {
                    fprintf ( stderr, "ERROR: Too many levels in WAD - ignoring %s!\n", dir->entry->name, errors++ );
                } else {
                    memcpy ( names [index++], dir->entry->name, MAX_LUMP_NAME );
                }
            }
            dir++;
        }
    }
    memset ( names [index], 0, MAX_LUMP_NAME );

    // Remove any maps that aren't in both files
    for ( int i = 0; names [i][0]; i++ ) {
        if ( list2->FindWAD ( names [i] ) == NULL ) {
            memcpy ( names + i, names + i + 1, ( index - i ) * MAX_LUMP_NAME );
            i--;
        }
    }

    if ( errors ) fprintf ( stderr, "\n" );

    return argIndex;
}

void EnsureExtension ( char *fileName, const char *ext )
{
    int length = strlen ( fileName );
    char *ptr = strrchr ( fileName, '.' );
    if (( ptr && strchr ( ptr, '\\' )) ||
        ( ! ptr && stricmp ( &fileName[length-4], ext ))) {
        strcat ( fileName, ext );
    }
}

const char *TypeName ( eWadType type )
{
    switch ( type ) {
        case wt_DOOM    : return "DOOM";
        case wt_DOOM2   : return "DOOM2";
        case wt_HERETIC : return "Heretic";
        case wt_HEXEN   : return "Hexen";
        default         : break;
    }
    return "<Unknown>";
}

wadList *getInputFiles ( char *cmdLine, char *wadFileName )
{
    char *listNames = wadFileName;
    wadList *myList = new wadList;

    if ( cmdLine == NULL ) return myList;

    char temp [ 256 ];
    strcpy ( temp, cmdLine );
    char *ptr = strtok ( temp, "+" );

    int errors = 0;
    while ( ptr && *ptr ) {
        char wadName [ 256 ];
        strcpy ( wadName, ptr );
        EnsureExtension ( wadName, ".wad" );

        WAD *wad = new WAD ( wadName );
        if ( wad->Status () != ws_OK ) {
            const char *msg;
            switch ( wad->Status ()) {
                case ws_INVALID_FILE : msg = "The file %s does not exist\n";		break;
                case ws_CANT_READ    : msg = "Can't open the file %s for read access\n";	break;
                case ws_INVALID_WAD  : msg = "%s is not a valid WAD file\n";		break;
                default              : msg = "** Unexpected Error opening %s **\n";	break;
            }
            fprintf ( stderr, msg, wadName );
            delete wad;
        } else {
            if ( ! myList->IsEmpty ()) {
                cprintf ( "Merging: %s with %s\r\n", wadName, listNames );
                *wadFileName++ = '+';
            }
            if ( myList->Add ( wad ) == false ) {
                errors++;
                if ( myList->Type () != wt_UNKNOWN ) {
                    fprintf ( stderr, "ERROR: %s is not a %s PWAD.\n", wadName, TypeName ( myList->Type ()));
                } else {
                    fprintf ( stderr, "ERROR: %s is not the same type.\n", wadName );
                }
                delete wad;
            } else {
                char *end = wadName + strlen ( wadName ) - 1;
                while (( end > wadName ) && ( *end != '\\' )) end--;
                if ( *end == '\\' ) end++;
                wadFileName += sprintf ( wadFileName, "%s", end );
            }
        }
        ptr = strtok ( NULL, "+" );
    }
    if ( wadFileName [-1] == '+' ) wadFileName [-1] = '\0';
    if ( myList->wadCount () > 1 ) cprintf ( "\r\n" );
    if ( errors ) fprintf ( stderr, "\n" );

    return myList;
}

/*
long CompareREJECT ( DoomLevel *curLevel, UCHAR *oldPtr, long &vis, long &hid )
{
    int size = curLevel->RejectSize ();
    int noSectors = curLevel->SectorCount ();
    int mask = ( 0xFF00 >> ( size * 8 - noSectors * noSectors )) & 0xFF;
    UCHAR *newPtr = ( UCHAR * ) curLevel->GetReject ();
    long count = 0, newVal, tempVal;
    hid = 0;
    while ( size-- ) {
        newVal = *newPtr++;
        tempVal = newVal ^ *oldPtr++;
        count += HammingTable [ tempVal ];
        hid   += HammingTable [ newVal &= tempVal ];
    }
    count -= HammingTable [ tempVal & mask ];
    hid   -= HammingTable [ newVal & mask ];
    vis = count - hid;
    return count;
}
*/

int CompareREJECT ( DoomLevel *srcLevel, DoomLevel *tgtLevel )
{
    bool match = true;
    int noSectors = srcLevel->SectorCount ();
    char *srcPtr = ( char * ) srcLevel->GetReject ();
    char *tgtPtr = ( char * ) tgtLevel->GetReject ();

    int **vis2hid = new int * [ noSectors ];
    int **hid2vis = new int * [ noSectors ];

    int *v2hCount = new int [ noSectors ];
    int *h2vCount = new int [ noSectors ];

    int bits = 8;
    int srcVal = *srcPtr++;
    int tgtVal = *tgtPtr++;
    int dif = srcVal ^ tgtVal;
    for ( int i = 0; i < noSectors; i++ ) {
        vis2hid [i] = new int [ noSectors ];
        hid2vis [i] = new int [ noSectors ];
        memset ( vis2hid [i], 0, noSectors * sizeof ( int ));
        memset ( hid2vis [i], 0, noSectors * sizeof ( int ));
        v2hCount [i] = 0;
        h2vCount [i] = 0;
        for ( int j = 0; j < noSectors; j++ ) {
            if ( dif & 1 ) {
                if ( srcVal & 1 ) {
                    hid2vis [i][h2vCount [i]++] = j;
                } else {
                    vis2hid [i][v2hCount [i]++] = j;
                }
                match = false;
            }
            if ( --bits == 0 ) {
                bits = 8;
                srcVal = *srcPtr++;
                tgtVal = *tgtPtr++;
                dif = srcVal ^ tgtVal;
            } else {
                srcVal >>= 1;
                tgtVal >>= 1;
                dif >>= 1;
            }
        }
    }

    bool first = true;
    for ( int i = 0; i < noSectors; i++ ) {
        if (( v2hCount [i] == 0 ) && ( h2vCount [i] == 0 )) continue;
        bool v2h = false;
        for ( int j = 0; j < v2hCount [i]; j++ ) {
            int index = vis2hid [i][j];
	    if (( v2hCount [i] > v2hCount [index] ) ||
	        (( v2hCount [i] == v2hCount [index] ) && ( i > index ))) {
	        v2h = true;
	        break;
	    }
	}
        bool h2v = false;
        for ( int j = 0; j < h2vCount [i]; j++ ) {
            int index = hid2vis [i][j];
	    if (( h2vCount [i] > h2vCount [index] ) ||
	        (( h2vCount [i] == h2vCount [index] ) && ( i > index ))) {
	        h2v = true;
	        break;
	    }
	}
        if ( v2h == true ) {
            if ( first == false ) printf ( "            " );
            printf ( "vis->hid %5d:", i );
            for ( int j = 0; j < v2hCount [i]; j++ ) {
                printf ( " %d", vis2hid [i][j] );
            }
            printf ( "\n" );
            first = false;
        }
        if ( h2v == true ) {
            if ( first == false ) printf ( "            " );
            printf ( "hid->vis %5d:", i );
            for ( int j = 0; j < h2vCount [i]; j++ ) {
                printf ( " %d", hid2vis [i][j] );
            }
            printf ( "\n" );
            first = false;
        }
    }

    if ( match == true ) printf ( "Perfect Match\n" );

    delete [] vis2hid;
    delete [] hid2vis;

    return match ? 0 : 1;
}

int ProcessLevel ( char *name, wadList *myList1, wadList *myList2 )
{
    int change;
    cprintf ( "  %-*.*s: ", MAX_LUMP_NAME, MAX_LUMP_NAME, name );
    GetXY ( &startX, &startY );

    DoomLevel *tgtLevel = NULL;
    const wadListDirEntry *dir = myList1->FindWAD ( name );
    DoomLevel *srcLevel = new DoomLevel ( name, dir->wad );
    if ( srcLevel->isValid ( true ) == false ) {
        change = -1000;
        Status ( "This level is not valid... " );
        goto done;
    }

    dir = myList2->FindWAD ( name );
    tgtLevel = new DoomLevel ( name, dir->wad );
    if ( tgtLevel->isValid ( true ) == false ) {
        change = -1000;
        Status ( "This level is not valid... " );
        goto done;
    }
    if ( srcLevel->RejectSize () != tgtLevel->RejectSize ()) {
        change = -1000;
        Status ( "The reject maps aren't the same size" );
        goto done;
    }

    change = CompareREJECT ( srcLevel, tgtLevel );

done:

    delete tgtLevel;
    delete srcLevel;

    return change;
}

#if defined ( __BORLANDC__ )
    #include <dir.h>
#endif

int main ( int argc, char *argv[] )
{
    fprintf ( stderr, "Compare Version %s (c) 1996 Marc Rousseau\n\n", VERSION );

    if ( argc == 1 ) {
        printHelp ();
        return -1;
    }

    SaveConsoleSettings ();

    int argIndex = 1, changes = 0;

    while ( kbhit ()) getch ();
    do {

        argIndex = parseArgs ( argIndex, argv );
        if ( argIndex < 0 ) break;

        char wadFileName1 [ 256 ];
        wadList *myList1 = getInputFiles ( argv [argIndex++], wadFileName1 );
        if ( myList1->IsEmpty ()) { changes = -1000;  break; }

        char wadFileName2 [ 256 ];
        wadList *myList2 = getInputFiles ( argv [argIndex++], wadFileName2 );
        if ( myList2->IsEmpty ()) { changes = -1000;  break; }

        cprintf ( "Comparing: %s and %s\r\n\n", wadFileName1, wadFileName2 );

        char levelNames [MAX_LEVELS+1][MAX_LUMP_NAME];
        argIndex = getLevels ( argIndex, argv, levelNames, myList1, myList2 );

        if ( levelNames [0][0] == '\0' ) {
            fprintf ( stderr, "Unable to find any valid levels in %s\n", wadFileName1 );
            break;
        }

        int noLevels = 0;

        do {

            changes += ProcessLevel ( levelNames [noLevels++], myList1, myList2 );
            if ( kbhit () && ( getch () == 0x1B )) break;

        } while ( levelNames [noLevels][0] );

        delete myList1;
        delete myList2;

    } while ( argv [argIndex] );

    RestoreConsoleSettings ();

    return changes;
}
