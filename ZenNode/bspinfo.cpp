//----------------------------------------------------------------------------
//
// File:        BSPInfo.cpp
// Date:        11-Oct-1995
// Programmer:  Marc Rousseau
//
// Description: An application to analyze the contents of a BSP
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

//#include <conio.h>
#include <ctype.h>
//#include <mem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.hpp"
#include "wad.hpp"
#include "level.hpp"

#define VERSION		"1.02"
#define MAX_LEVELS	50

#if defined ( __OS2__ ) || defined ( __WIN32__ )

#define SEPERATOR	'\\'

#else

#define SEPERATOR	'/'

#endif

struct {
    bool  Tree;
} flags;

void ProgError ( char *, ... )
{
}

void printHelp ()
{
    fprintf ( stderr, "Usage:\n\n" );
    fprintf ( stderr, "  bspInfo [-options] filename[.wad] [level[+level]]\n\n" );
    fprintf ( stderr, "        -x+ turn on option   -x- turn off option  * = default\n\n" );
    fprintf ( stderr, "        -t    - Display NODE tree\n" );
    fprintf ( stderr, "\n" );
    fprintf ( stderr, "        level - ExMy for DOOM / Heretic\n" );
    fprintf ( stderr, "                MAPxx for DOOM II / HEXEN\n" );
}

int parseArgs ( int index, char *argv[] )
{
    while ( argv [ index ] ) {
        char *ptr = argv [ index ];
        if ( *ptr != '-' ) break;
        index++;
        while ( *ptr ) {
            int ch = *ptr++;
            if ( ch != '-' ) return index;
            int option = toupper ( *ptr++ );
            int setting = true;
            if (( *ptr == '+' ) || ( *ptr == '-' )) {
                setting = ( *ptr++ == '+' ) ? true : false;
	    }
            switch ( option ) {
                case 'T' : flags.Tree = setting;	break;
                default  : fprintf ( stderr, "Unrecognized parameter '%s'\n", argv [ index-1 ] );
                           return -1;
            }
        }
    }
    return index;
}

int getLevels ( int argIndex, char *argv[], char names [][MAX_LUMP_NAME], wadList *list )
{
    int index = 0, errors = 0;

    char buffer [128];
    buffer [0] = '\0';
    if ( argv [argIndex] ) {
        strcpy ( buffer, argv [argIndex] );
    }

    // See if the user requested specific levels
    if ( WAD::isMap ( buffer )) {
        argIndex++;
        char *ptr = buffer;
        for ( EVER ) {
            char levelName [ 9 ];
            strncpy ( levelName, ptr, MAX_LUMP_NAME );
            int length = ( *ptr == 'M' ) ? 5 : 4;
            levelName [ length ] = '\0';
            if ( ! list->FindWAD ( levelName )) {
                fprintf ( stderr, "  Could not find %s\n", levelName );
                errors++;
            } else {
                strcpy ( names [index++], levelName );
            }
            ptr += length;
            if ( *ptr++ != '+' ) break;
            if ( ! WAD::isMap ( ptr )) {
                do {
                    char *end = ptr;
                    while ( *end && ( *end != '+' )) end++;
                    fprintf ( stderr, "  %*.*s is not a valid name for a level\n", end-ptr, end-ptr, ptr );
                    errors++;
                    ptr = end ? end+1 : NULL;
                } while ( ptr && ! WAD::isMap ( ptr ));
                if ( ptr == NULL ) break;
            }
        }
    } else {
        int size = list->DirSize ();
        const wadListDirEntry *dir = list->GetDir ( 0 );
        for ( int i = 0; i < size; i++ ) {
            if ( dir->wad->isMap ( dir->entry->name )) {
                if ( index == MAX_LEVELS ) {
                    fprintf ( stderr, "ERROR: Too many levels in WAD - ignoring %s!\n", dir->entry->name );
                    errors++;
                } else
                    memcpy ( names [index++], dir->entry->name, MAX_LUMP_NAME );
            }
            dir++;
        }
    }
    memset ( names [index], 0, MAX_LUMP_NAME );

    if ( errors ) fprintf ( stderr, "\n" );
    return argIndex;
}

void EnsureExtension ( char *fileName, const char *ext )
{
    int length = strlen ( fileName );
    char *ptr = strrchr ( fileName, '.' );
    if (( ptr && strchr ( ptr, SEPERATOR )) ||
        ( ! ptr && strcmp ( &fileName[length-4], ext ))) {
        strcat ( fileName, ext );
    }
}			       

wadList *getInputFiles ( char *cmdLine, char *wadFileName )
{
    char *listNames = wadFileName;
    wadList *myList = new wadList;

    if ( cmdLine == NULL ) return myList;

    char temp [ 256 ];
    strcpy ( temp, cmdLine );
    char *ptr = strtok ( temp, "+" );

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
            if ( ! myList->isEmpty ()) {
                printf ( "Merging: %s with %s\n", wadName, listNames );
                *wadFileName++ = '+';
            }
            myList->Add ( wad );
            char *end = wadName + strlen ( wadName ) - 1;
            while (( end > wadName ) && ( *end != '\\' )) end--;
            if ( *end == '\\' ) end++;
            wadFileName += sprintf ( wadFileName, "%s", end );
        }
        ptr = strtok ( NULL, "+" );
    }
    if ( myList->wadCount () > 1 ) printf ( "\n" );

    return myList;
}

const wNode *nodes;
int totalDepth;
int noLeafs;

int Traverse ( int index, int depth, int &diagonals, int &balance, int &lChildren, int &rChildren )
{
    const wNode *node = &nodes [ index ];
    if (( node->dx != 0 ) && ( node->dy != 0 )) diagonals++;
    if ( flags.Tree ) printf ( "(%5d,%5d)  [%5d,%5d]\n", node->x, node->y, node->dx, node->dy );

    depth++;
    int lDepth = 0, rDepth = 0;

    int lIndex = node->child [0];
    int rIndex = node->child [1];

    if (( lIndex & 0x8000 ) == ( rIndex & 0x8000 )) balance++;

    if (( lIndex & 0x8000 ) && ( rIndex & 0x8000 )) {
        totalDepth += depth;
        noLeafs++;
        return depth;
    }

    if ( flags.Tree ) printf ( "%5d %*.*sLeft - ", depth, depth*2, depth*2, "" );
    if (( lIndex & 0x8000 ) == 0 ) {
        int left = 0, right = 0;
        lDepth = Traverse ( lIndex, depth, diagonals, balance, left, right );
        lChildren = 1 + left + right;
    } else
        if ( flags.Tree ) printf ( "** NONE **\n" );

    if ( flags.Tree ) printf ( "%5d %*.*sRight - ", depth, depth*2, depth*2, "" );
    if (( rIndex & 0x8000 ) == 0 ) {
        int left = 0, right = 0;
        rDepth = Traverse ( rIndex, depth, diagonals, balance, left, right );
        rChildren = 1 + left + right;
    } else {
        if ( flags.Tree ) printf ( "** NONE **\n" );
    }

    return (( lDepth > rDepth ) ? lDepth : rDepth );
}

void AnalyzeBSP ( DoomLevel *curLevel )
{
    if ( curLevel->NodeCount () < 2 ) {
        printf ( "******** INVALID BSP TREE ********" );
        return;
    }

    totalDepth = 0;
    noLeafs = 0;

    nodes = curLevel->GetNodes ();
    int balance = 0, diagonals = 0;
    if ( flags.Tree ) printf ( "\n\nROOT: " );

    int left = 0;
    int right = 0;
    int depth = Traverse ( curLevel->NodeCount () - 1, 0, diagonals, balance, left, right );

    const wSegs *seg = curLevel->GetSegs ();
////    const wVertex *vertices = curLevel->GetVertices ();
    const wLineDef *lineDef = curLevel->GetLineDefs ();

    bool *lineUsed = new bool [ curLevel->LineDefCount ()];
    memset ( lineUsed, false, sizeof ( bool ) * curLevel->LineDefCount ());
    int i;
    for ( i = 0; i < curLevel->SegCount (); i++, seg++ ) {
        lineUsed [ seg->lineDef ] = true;
    }

////    int totalDiagonals = 0;
////    int lineDefs = 0;
    int sideDefs = 0;
    for ( i = 0; i < curLevel->LineDefCount (); i++ ) {
        if ( lineUsed [i] == false ) continue;
////        lineDefs++;
////        const wVertex *vertS = &vertices [ lineDef[i].start ];
////        const wVertex *vertE = &vertices [ lineDef[i].end ];
////        if (( vertS->x != vertE->x ) && ( vertS->y != vertE->y )) 
////            totalDiagonals++;
        if ( lineDef[i].sideDef[0] != NO_SIDEDEF ) sideDefs++;
        if ( lineDef[i].sideDef[1] != NO_SIDEDEF ) sideDefs++;
    }

    int splits = curLevel->SegCount () - sideDefs;

    if ( ! flags.Tree ) {
        float avgDepth = noLeafs ? ( float ) totalDepth / ( float ) noLeafs : 0;
        printf ( "%2d (%4.1f)  ", depth, avgDepth );
        printf ( "%5.3f ", ( float ) balance / curLevel->NodeCount ());
        printf ( "%5.1f/%-5.1f", 100.0 * left / ( curLevel->NodeCount () - 1 ),
                                 100.0 * right / ( curLevel->NodeCount () - 1 ));
        printf ( "%5d - %4.1f%%", splits, 100.0 * splits / sideDefs );
        printf ( "%5d - %4.1f%%", diagonals, 100.0 * diagonals / curLevel->NodeCount ());
////        printf ( "%5d - %4.1f%%", diagonals, 100.0 * diagonals / totalDiagonals );
        printf ( "%5d  ", curLevel->NodeCount ());
        printf ( "%5d", curLevel->SegCount ());
    }
}

int main ( int argc, char *argv[] )
{
    fprintf ( stderr, "BSPInfo Version %s (c) 1995 Marc Rousseau\n\n", VERSION );

    if ( argc == 1 ) {
        printHelp ();
        return 0;
    }

    flags.Tree = false;

    int argIndex = 1;
    do {

        argIndex = parseArgs ( argIndex, argv );
        if ( argIndex < 0 ) break;

        char wadFileName [ 256 ];
        wadList *myList = getInputFiles ( argv [argIndex++], wadFileName );
        if ( myList->isEmpty ()) break;
        printf ( "Analyzing: %s\n\n", wadFileName );

        char levelNames [MAX_LEVELS+1][MAX_LUMP_NAME];
        argIndex = getLevels ( argIndex, argv, levelNames, myList );
    
        if ( levelNames [0][0] == '\0' ) {
            fprintf ( stderr, "Unable to find any valid levels in %s\n", wadFileName );
            break;
        }

        if ( ! flags.Tree ) {
            printf ( "         Depth (Avg)   FOM    Balance      Splits      Diagonals  Nodes  Segs\n" );
	}

        int noLevels = 0;

        do {

            DoomLevel *curLevel = new DoomLevel ( levelNames [ noLevels ], myList );
            printf ( "%8.8s:  ", levelNames [ noLevels++ ]);
            AnalyzeBSP ( curLevel );
            printf ( "\n" );
            delete curLevel;

        } while ( levelNames [noLevels][0] );

        printf ( "\n" );

        delete myList;

    } while ( argv [argIndex] );

    return 0;
}
