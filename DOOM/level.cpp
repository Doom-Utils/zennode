//----------------------------------------------------------------------------
//
// File:        level.cpp
// Date:        26-October-1994
// Programmer:  Marc Rousseau
//
// Description: Object classes for manipulating Doom Maps
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
#include "common.hpp"
#include "level.hpp"
#include "wad.hpp"

#if defined ( __GNUC__ )

char *strupr ( char *ptr )
{
    for ( int i = 0; ptr[i]; i++ ) {
        ptr[i] = toupper ( ptr[i] );
    }
    return ptr;
}

#endif

DoomLevel::DoomLevel ( const char *_name, wadList *list, bool loadData ) :
    myList ( list )
{
    WipeOut ();

    title = NULL;
    music = NULL;
    cluster = -1;
    newFormat = false;
    memset ( name, 0, sizeof ( name ));
    strncpy ( name, _name, 8 );
    for ( int i = 0; i < 8; i++ ) {
        if ( name[i] ) name[i] = ( char ) toupper ( name[i] );
    }

    if ( loadData ) Load ();
    LoadHexenInfo ();
}

DoomLevel::~DoomLevel ()
{
    if ( title ) free (( char * ) title );
    if ( music ) free (( char * ) music );
    CleanUp ();
}

// TBD
bool DoomLevel::isValid ()
{
    if ( checked || ! valid ) return valid;

    int i;
    
    // Sanity check for LINEDEFS
    for ( i = 0; i < noLineDefs; i++ ) {
        if ( lineDef [i].start >= noVertices ) {
            fprintf ( stderr, "LINEDEFS[%d].%s vertex is invalid (%d/%d)\n", i, "start", lineDef [i].start, noVertices );
            valid = false;
        }
        if ( lineDef [i].end >= noVertices ) {
            fprintf ( stderr, "LINEDEFS[%d].%s vertex is invalid (%d/%d)\n", i, "end", lineDef [i].end, noVertices );
            valid = false;
        }
        if ( lineDef [i].sideDef [ LEFT_SIDEDEF ] != NO_SIDEDEF ) {
            if ( lineDef [i].sideDef [ LEFT_SIDEDEF ] >= noSideDefs ) {
                fprintf ( stderr, "LINEDEFS[%d].sideDef[%s] is invalid (%d/%d)\n", i, "left", lineDef [i].sideDef [LEFT_SIDEDEF], noSideDefs );
                valid = false;
            }
        }
        if ( lineDef [i].sideDef [ RIGHT_SIDEDEF ] != NO_SIDEDEF ) {
            if ( lineDef [i].sideDef [ RIGHT_SIDEDEF ] >= noSideDefs ) {
                fprintf ( stderr, "LINEDEFS[%d].sideDef[%s] is invalid (%d/%d)\n", i, "right", lineDef [i].sideDef [RIGHT_SIDEDEF], noSideDefs );
                valid = false;
            }
        }
    }

    // Sanity check for SIDEDEFS
    for ( i = 0; i < noSideDefs; i++ ) {
        if ( sideDef [i].sector >= noSectors ) {
            fprintf ( stderr, "SIDEDEFS[%d].sector is invalid (%d/%d)\n", i, "start", sideDef [i].sector, noSectors );
            valid = false;
        }
    }
    
    // Sanity check for SEGS
    for ( i = 0; i < noLineDefs; i++ ) {
        if ( segs [i].start >= noVertices ) {
            fprintf ( stderr, "SEGS[%d].%s vertex is invalid (%d/%d)\n", i, "start", segs [i].start, noVertices );
            valid = false;
        }
        if ( segs [i].end >= noVertices ) {
            fprintf ( stderr, "SEGS[%d].%s vertex is invalid (%d/%d)\n", i, "end", segs [i].end, noVertices );
            valid = false;
        }
        if ( segs [i].lineDef >= noLineDefs ) {
            fprintf ( stderr, "SEGS[%d].lineDef is invalid (%d/%d)\n", i, segs [i].lineDef, noLineDefs );
            valid = false;
        }
    }

    // Sanity check for SSECTORS
    for ( i = 0; i < noSubSectors; i++ ) {
        if ( subSector [i].first >= noSegs ) {
            fprintf ( stderr, "SSECTORS[%d].first is invalid (%d/%d)\n", i, subSector [i].first, noSegs );
            valid = false;
        }
        if ( subSector [i].first + subSector [i].num > noSegs ) {
            fprintf ( stderr, "SSECTORS[%d].num is invalid (%d/%d)\n", i, subSector [i].num, noSegs );
            valid = false;
        }
    }

    // Sanity check for NODES
    for ( i = 0; i < noNodes; i++ ) {
        USHORT child;
        child = node [i].child [0];
        if ( child & 0x8000 ) {
            if (( child & 0x7FFF ) >= noSubSectors ) {
                fprintf ( stderr, "NODES[%d].child[%d] is invalid (0x8000 | %d/%d)\n", i, 0, child & 0x7FFF, noSubSectors );
                valid = false;
            }
        } else {
            if ( child >= noNodes ) {
                fprintf ( stderr, "NODES[%d].child[%d] is invalid (%d/%d)\n", i, 0, child, noNodes );
                valid = false;
            }
        }
        child = node [i].child [1];
        if ( child & 0x8000 ) {
            if (( child & 0x7FFF ) >= noSubSectors ) {
                fprintf ( stderr, "NODES[%d].child[%d] is invalid (0x8000 | %d/%d)\n", i, 1, child & 0x7FFF, noSubSectors );
                valid = false;
            }
        } else {
            if ( child >= noNodes ) {
                fprintf ( stderr, "NODES[%d].child[%d] is invalid (%d/%d)\n", i, 1, child, noNodes );
                valid = false;
            }
        }
    }
    checked = true;
        
    return valid;
}

bool DoomLevel::hasChanged () const	{ return modified; }

void DoomLevel::DeleteTransients ()
{
    if ( segs ) 	{ delete segs;		segs = NULL; }
    if ( subSector )	{ delete subSector;	subSector = NULL; }
    if ( node )		{ delete node;		node = NULL; }
    if ( reject )	{ delete reject;	reject = NULL; }
    if ( blockMap )	{ delete blockMap;	blockMap = NULL; }
}

void DoomLevel::WipeOut ()
{
    modified = false;
    checked  = false;
    valid    = false;

    thingsChanged     = false;
    lineDefsChanged   = false;
    sideDefsChanged   = false;
    verticesChanged   = false;
    sectorsChanged    = false;
    segsChanged       = false;
    subSectorsChanged = false;
    nodesChanged      = false;
    rejectChanged     = false;
    blockMapChanged   = false;

    noThings     = 0;
    noLineDefs   = 0;
    noSideDefs   = 0;
    noVertices   = 0;
    noSectors    = 0;
 
    noSegs       = 0;
    noSubSectors = 0;
    noNodes      = 0;

    rejectSize   = 0;
    blockMapSize = 0;

    segs       = NULL;
    subSector  = NULL;
    node       = NULL;
    reject     = NULL;
    blockMap   = NULL;

    rawThing   = NULL;
    rawLineDef = NULL;

    thing      = NULL;
    lineDef    = NULL;
    sideDef    = NULL;
    vertex     = NULL;
    sector     = NULL;
}

void DoomLevel::CleanUp ()
{
    DeleteTransients ();

    if ( rawThing )   { delete rawThing;    rawThing   = NULL; }
    if ( rawLineDef ) { delete rawLineDef;  rawLineDef = NULL; }

    if ( thing )      { delete thing;       thing      = NULL; }
    if ( lineDef )    { delete lineDef;     lineDef    = NULL; }
    if ( sideDef )    { delete sideDef;     sideDef    = NULL; }
    if ( vertex )     { delete vertex;      vertex     = NULL; }
    if ( sector )     { delete sector;      sector     = NULL; }

    WipeOut ();
}

#if defined ( BIG_ENDIAN )
void DoomLevel::AdjustByteOrder ()
{
    /*
        void       *rawThing;		// Check Format
        void       *rawLineDef;		// Check Format

        wThing     *thing;
        wLineDef   *lineDef;
        wSideDef   *sideDef;
        wVertex    *vertex;
        wSector    *sector;

        wSegs      *segs;
        wSSector   *subSector;
        wNode      *node;
        wReject    *reject;		// May not need to be swapped
        wBlockMap  *blockMap;
    */
}
#endif

void DoomLevel::TrimVertices ()
{
    modified = true;
    verticesChanged = true;

    long highBit = 1L << ( 8 * sizeof ( long ) - 1 );
    long *Used = new long [ noVertices ];
    memset ( Used, 0, sizeof ( long ) * noVertices );
    int i;
    for ( i = 0; i < noLineDefs; i++ ) {
        Used [ lineDef [i].start ] = highBit;
        Used [ lineDef [i].end ] = highBit;
    }
    if ( segs ) for ( int i = 0; i < noSegs; i++ ) {
        Used [ segs [i].start ] = highBit;
        Used [ segs [i].end ] = highBit;
    }

    int usedCount = 0;
    wVertex *newVertices = new wVertex [ noVertices ];
    for ( i = 0; i < noVertices; i++ ) {
        if ( Used [i] ) {
            newVertices [usedCount] = vertex [i];
            Used [i] |= usedCount++;
        }
    }
    for ( i = 0; i < noLineDefs; i++ ) {
        lineDef [i].start = ( USHORT ) ( Used [ lineDef [i].start ] & ~highBit );
        lineDef [i].end = ( USHORT ) ( Used [ lineDef [i].end ] & ~highBit );
    }
    if ( segs ) for ( int i = 0; i < noSegs; i++ ) {
        segs [i].start = ( USHORT ) ( Used [ segs [i].start ] & ~highBit );
        segs [i].end = ( USHORT ) ( Used [ segs [i].end ] & ~highBit );
    }

    noVertices = usedCount;
    delete vertex;
    delete Used;
    vertex = newVertices;
}

void DoomLevel::PackVertices ()
{
    modified = true;
    verticesChanged = true;

#if defined ( __BORLANDC__ )
    #if sizeof ( wVertex ) != sizeof ( long )
        #error Sorry, PackVertices assumes sizeof ( wVertex ) == sizeof ( long )
	#endif
#endif
    long *vert = ( long * ) vertex;
    int *Used = new int [ noVertices ], newCount = 0;
    memset ( Used, 0, sizeof ( int ) * noVertices );
    int i, j;
    for ( i = 0; i < noVertices; i++ ) {
        long currentVert = vert [i];
        for ( j = 0; j < i; j++ ) {
            if ( vert [j] == currentVert ) break;
        }
        Used [i] = j;
        if ( i == j ) newCount++;
    }

    int usedCount = 0;
    wVertex *newVertices = new wVertex [ newCount ];
    for ( i = 0; i < noVertices; i++ ) {
        if ( Used [i] == i ) {
            newVertices [usedCount] = vertex [i];
            Used [i] = usedCount++;
        } else {
            Used [i] = Used [ Used [i]];
        }
    }

    for ( i = 0; i < noLineDefs; i++ ) {
        lineDef [i].start = ( USHORT ) ( Used [ lineDef [i].start ]);
        lineDef [i].end = ( USHORT ) ( Used [ lineDef [i].end ]);
    }

    if ( segs ) for ( i = 0; i < noSegs; i++ ) {
        segs [i].start = ( USHORT ) ( Used [ segs [i].start ]);
        segs [i].end = ( USHORT ) ( Used [ segs [i].end ]);
    }

    noVertices = newCount;
    delete vertex;
    delete Used;
    vertex = newVertices;
}

void DoomLevel::ConvertRaw1ToThing ( int max, wThing1 *src, wThing *dest )
{
    memset ( dest, 0, sizeof ( wThing ) * max );
    for ( int i = 0; i < max; i++ ) {
        memcpy ( &dest [i], &src [i], sizeof ( wThing1 ));
    }
}

void DoomLevel::ConvertRaw2ToThing ( int max, wThing2 *src, wThing *dest )
{
    memset ( dest, 0, sizeof ( wThing ) * max );
    for ( int i = 0; i < max; i++ ) {
        dest [i].xPos     = src [i].xPos;
        dest [i].yPos     = src [i].yPos;
        dest [i].angle    = src [i].angle;
        dest [i].type     = src [i].type;
        dest [i].attr     = src [i].attr;
        dest [i].tid      = src [i].tid;
        dest [i].altitude = src [i].altitude;
        dest [i].special  = src [i].special;
        memcpy ( dest [i].arg, src [i].arg, sizeof ( src[i].arg ));
    }
}

void DoomLevel::ConvertThingToRaw1 ( int max, wThing *src, wThing1 *dest )
{
    memset ( dest, 0, sizeof ( wThing1 ) * max );
    for ( int i = 0; i < max; i++ ) {
        memcpy ( &dest [i], &src [i], sizeof ( wThing1 ));
    }
}

void DoomLevel::ConvertThingToRaw2 ( int max, wThing *src, wThing2 *dest )
{
    memset ( dest, 0, sizeof ( wThing2 ) * max );
    for ( int i = 0; i < max; i++ ) {
        dest [i].xPos     = src [i].xPos;
        dest [i].yPos     = src [i].yPos;
        dest [i].angle    = src [i].angle;
        dest [i].type     = src [i].type;
        dest [i].attr     = src [i].attr;
        dest [i].tid      = src [i].tid;
        dest [i].altitude = src [i].altitude;
        dest [i].special  = src [i].special;
        memcpy ( dest [i].arg, src [i].arg, sizeof ( dest[i].arg ));
    }
}

void DoomLevel::ConvertRaw1ToLineDef ( int max, wLineDef1 *src, wLineDef *dest )
{
    memset ( dest, 0, sizeof ( wLineDef ) * max );
    for ( int i = 0; i < max; i++ ) {
        memcpy ( &dest [i], &src [i], sizeof ( wLineDef1 ));
    }
}

void DoomLevel::ConvertRaw2ToLineDef ( int max, wLineDef2 *src, wLineDef *dest )
{
    memset ( dest, 0, sizeof ( wLineDef ) * max );
    for ( int i = 0; i < max; i++ ) {
        dest [i].start      = src [i].start;
        dest [i].end        = src [i].end;
        dest [i].flags      = src [i].flags;
        dest [i].type       = 0;
        dest [i].tag        = 0;
        dest [i].sideDef[0] = src [i].sideDef[0];
        dest [i].sideDef[1] = src [i].sideDef[1];
        dest [i].special    = src [i].special;
        memcpy ( dest [i].arg, src [i].arg, sizeof ( src[i].arg ));
    }
}

void DoomLevel::ConvertLineDefToRaw1 ( int max, wLineDef *src, wLineDef1 *dest )
{
    memset ( dest, 0, sizeof ( wLineDef1 ) * max );
    for ( int i = 0; i < max; i++ ) {
        memcpy ( &dest [i], &src [i], sizeof ( wLineDef1 ));
    }
}

void DoomLevel::ConvertLineDefToRaw2 ( int max, wLineDef *src, wLineDef2 *dest )
{
    memset ( dest, 0, sizeof ( wLineDef2 ) * max );
    for ( int i = 0; i < max; i++ ) {
        dest [i].start      = src [i].start;
        dest [i].end        = src [i].end;
        dest [i].flags      = src [i].flags;
        dest [i].sideDef[0] = src [i].sideDef[0];
        dest [i].sideDef[1] = src [i].sideDef[1];
        dest [i].special    = src [i].special;
        memcpy ( dest [i].arg, src [i].arg, sizeof ( dest[i].arg ));
    }
}

void DoomLevel::ReadThings ( bool testFormat, const wadListDirEntry *&start, const wadListDirEntry *end )
{
    ULONG temp;
    const wadListDirEntry *dir = myList->FindWAD ( "THINGS", start, end );
    // Make sure this isn't really a HEXEN wad in disguise
    if ( testFormat && ( newFormat == false )) {
        wThing1 *testThing = ( wThing1 * ) dir->wad->ReadEntry ( dir->entry, &temp, true );
        int count = dir->entry->size / sizeof ( wThing1 );
        // sizeof ( wThing2 ) is a multiple of sizeof ( wThing1 )
        if (( count & 0x0001 ) == 0 ) {
            int minX = 65536, noType0s = 0, noX0s = 0, noBadAngles = 0;
            for ( int i = 0; i < count; i++ ) {
                if ( testThing[i].xPos < minX ) minX = testThing[i].xPos;
                if ( testThing[i].xPos == 0 ) noX0s++;
                if ( testThing[i].type == 0 ) noType0s++;
                if ( testThing[i].angle % 45 ) noBadAngles++;
            }
            int bad = 0, threshold = ( count + 2 ) / 3;
            if ( minX >= 0 ) bad++;
            if ( noX0s > threshold ) bad++;
            if ( noType0s > threshold ) bad++;
            if ( noBadAngles > threshold ) bad++;
            if ( bad > 1 ) newFormat = true;
        }
        delete testThing;
    }
    rawThing = dir->wad->ReadEntry ( dir->entry, &temp );
    if ( newFormat ) {
        noThings = temp / sizeof ( wThing2 );
        thing = new wThing [ noThings ];
        ConvertRaw2ToThing ( noThings, ( wThing2 * ) rawThing, thing );
    } else {
        noThings = temp / sizeof ( wThing1 );
        thing = new wThing [ noThings ];
        ConvertRaw1ToThing ( noThings, ( wThing1 * ) rawThing, thing );
    }
}

bool DoomLevel::ReadLineDefs ( const wadListDirEntry *&start, const wadListDirEntry *end )
{
    ULONG temp;
    const wadListDirEntry *dir = myList->FindWAD ( "LINEDEFS", start, end );

    // Make sure we have the correct format
    if (( newFormat == true ) && ( dir->entry->size % sizeof ( wLineDef2 ))) return true;
    if (( newFormat == false ) && ( dir->entry->size % sizeof ( wLineDef1 ))) return true;

    rawLineDef = dir->wad->ReadEntry ( dir->entry, &temp );
    if ( newFormat ) {
        noLineDefs = temp / sizeof ( wLineDef2 );
        lineDef = new wLineDef [ noLineDefs ];
        ConvertRaw2ToLineDef ( noLineDefs, ( wLineDef2 * ) rawLineDef, lineDef );
        for ( int i = 0; i < noLineDefs; i++ ) {
            if ( lineDef [i].sideDef [ RIGHT_SIDEDEF ] == NO_SIDEDEF ) return true;
        }
    } else {
        noLineDefs = temp / sizeof ( wLineDef1 );
        lineDef = new wLineDef [ noLineDefs ];
        ConvertRaw1ToLineDef ( noLineDefs, ( wLineDef1 * ) rawLineDef, lineDef );
    }
    return false;
}

int DoomLevel::Load ()
{
    if ( myList == NULL ) return false;

    const wadListDirEntry *dir;
    const wadListDirEntry *start = myList->FindWAD ( Name ());
    const wadListDirEntry *end = start + 11;

    if ( start == NULL ) return false;

    ULONG temp;

    newFormat = ( start->entry->size > 0 ) ? true : false;

    start += 1;

    bool wrongFormat = true;
    for ( int i = 0; wrongFormat && ( i < 2 ); i++ ) {
        CleanUp ();
        ReadThings (( i == 0 ) ? true : false, start, end );
        wrongFormat = ReadLineDefs ( start, end );
        if ( wrongFormat ) newFormat = ! newFormat;
    }

    if ( wrongFormat ) {
        CleanUp ();
        return false;
    } else {
        valid = true;
    }

    dir = myList->FindWAD ( "SIDEDEFS", start, end );
    sideDef = ( wSideDef * ) dir->wad->ReadEntry ( dir->entry, &temp );
    noSideDefs = temp / sizeof ( wSideDef );

    dir = myList->FindWAD ( "VERTEXES", start, end );
    vertex = ( wVertex * ) dir->wad->ReadEntry ( dir->entry, &temp );
    noVertices = temp / sizeof ( wVertex );

    dir = myList->FindWAD ( "SEGS", start, end );
    segs = ( wSegs * ) dir->wad->ReadEntry ( dir->entry, &temp );
    noSegs = temp / sizeof ( wSegs );

    dir = myList->FindWAD ( "SSECTORS", start, end );
    subSector = ( wSSector * ) dir->wad->ReadEntry ( dir->entry, &temp );
    noSubSectors = temp / sizeof ( wSSector );

    dir = myList->FindWAD ( "NODES", start, end );
    node = ( wNode * ) dir->wad->ReadEntry ( dir->entry, &temp );
    noNodes = temp / sizeof ( wNode );

    dir = myList->FindWAD ( "SECTORS", start, end );
    sector = ( wSector * ) dir->wad->ReadEntry ( dir->entry, &temp );
    noSectors = temp / sizeof ( wSector );

    dir = myList->FindWAD ( "REJECT", start, end );
    reject = ( wReject * ) dir->wad->ReadEntry ( dir->entry, &temp );
    rejectSize = temp;

    dir = myList->FindWAD ( "BLOCKMAP", start, end );
    blockMap = ( wBlockMap * ) dir->wad->ReadEntry ( dir->entry, &temp );
    blockMapSize = temp;

#if defined ( BIG_ENDIAN )
    AdjustByteOrder ();
#endif

    return true;
}

void DoomLevel::LoadHexenInfo ()
{
    int level;
    sscanf ( name, "MAP%02d", &level );

    ULONG Size;
    const wadListDirEntry *wadEntry;
    if (( wadEntry = myList->FindWAD ( "MAPINFO" )) == NULL ) return;
    char *buffer = ( char * ) wadEntry->wad->ReadEntry ( wadEntry->entry, &Size, true );
    char *ptr = buffer;

    if ( title ) free (( char * ) title );
    title = NULL;

    do {
        if (( ptr = strstr ( ptr, "\nmap " )) == NULL ) break;
        if ( atoi ( &ptr[5] ) == level ) {
            while ( *ptr++ != '"' );
            strtok ( ptr, "\"" );
            title = strdup ( ptr );
            ptr += strlen ( ptr ) + 1;
            char *clstr = strstr ( ptr, "\ncluster " );
            char *next = strstr ( ptr, "\n\r" );
            if ( clstr && ( clstr < next )) {
                cluster = atoi ( clstr + 9 );
            }
            break;
        } else {
            ptr++;
        }
    } while ( ptr && *ptr );

    delete buffer;

    if ( title ) {
        ptr = ( char * ) title + 1;
        while ( *ptr ) {
            *ptr = ( char ) tolower ( *ptr );
            if ( *ptr == ' ' ) {
                while ( *ptr == ' ' ) ptr++;
                if ( strncmp ( ptr, "OF ", 3 ) == 0 ) ptr--;
            }
            if ( *ptr ) ptr++;
        }
    }

    if (( wadEntry = myList->FindWAD ( "SNDINFO" )) == NULL ) return;
    buffer = ( char * ) wadEntry->wad->ReadEntry ( wadEntry->entry, &Size, true );
    ptr = buffer;

    do {
        if (( ptr = strstr ( ptr, "\n$MAP" )) == NULL ) break;
        if ( atoi ( &ptr[5] ) == level ) {
            ptr += 25;
            strtok ( ptr, "\r" );
            music = strupr ( strdup ( ptr ));
            break;
        } else {
            ptr++;
        }
    } while ( ptr && *ptr );

    delete buffer;
}

void DoomLevel::AddToWAD ( WAD *wad )
{
    wad->InsertAfter (( const wLumpName * ) Name (), 0L, ( void * ) "", false );

    ULONG size1, size2;
    if ( newFormat ) {
        size1 = noThings * sizeof ( wThing2 );
        size2 = noLineDefs * sizeof ( wLineDef2 );
    } else {
        size1 = noThings * sizeof ( wThing1 );
        size2 = noLineDefs * sizeof ( wLineDef1 );
    }

#if defined ( BIG_ENDIAN )
    AdjustByteOrder ();
#endif

    wad->InsertAfter (( const wLumpName * ) "THINGS",   size1, rawThing, false );
    wad->InsertAfter (( const wLumpName * ) "LINEDEFS", size2, rawLineDef, false );
    wad->InsertAfter (( const wLumpName * ) "SIDEDEFS", noSideDefs * sizeof ( wSideDef ), sideDef, false );
    wad->InsertAfter (( const wLumpName * ) "VERTEXES", noVertices * sizeof ( wVertex ), vertex, false );
    wad->InsertAfter (( const wLumpName * ) "SEGS",     noSegs * sizeof ( wSegs ), segs, false );
    wad->InsertAfter (( const wLumpName * ) "SSECTORS", noSubSectors * sizeof ( wSSector ), subSector, false );
    wad->InsertAfter (( const wLumpName * ) "NODES",    noNodes * sizeof ( wNode ), node, false );
    wad->InsertAfter (( const wLumpName * ) "SECTORS",  noSectors * sizeof ( wSector ), sector, false );
    wad->InsertAfter (( const wLumpName * ) "REJECT",   rejectSize, reject, false );
    wad->InsertAfter (( const wLumpName * ) "BLOCKMAP", blockMapSize, blockMap, false );

#if defined ( BIG_ENDIAN )
    AdjustByteOrder ();
#endif

    if ( newFormat ) {
        const wadListDirEntry *dir;
        const wadListDirEntry *start = myList->FindWAD ( Name ());
        const wadListDirEntry *end = start + 11;
        dir = myList->FindWAD ( "BEHAVIOR", start, end );
        ULONG size;
        void *ptr = dir->wad->ReadEntry ( dir->entry, &size );
        wad->InsertAfter (( const wLumpName * ) "BEHAVIOR", size, ptr, true );
    }
}

bool DoomLevel::SaveThings ( const wadListDirEntry *&start, const wadListDirEntry *end )
{
    bool changed;
    const wadListDirEntry *dir = myList->FindWAD ( "THINGS", start, end );
    if ( rawThing ) delete rawThing;
    if ( newFormat ) {
        rawThing = new wThing2 [ noThings ];
        ConvertThingToRaw2 ( noThings, thing, ( wThing2 * ) rawThing );
        changed = dir->wad->WriteEntry ( dir->entry, noThings * sizeof ( wThing2 ), rawThing, false );
    } else {
        rawThing = new wThing1 [ noThings ];
        ConvertThingToRaw1 ( noThings, thing, ( wThing1 * ) rawThing );
        changed = dir->wad->WriteEntry ( dir->entry, noThings * sizeof ( wThing1 ), rawThing, false );
    }
    return changed;
}

bool DoomLevel::SaveLineDefs ( const wadListDirEntry *&start, const wadListDirEntry *end )
{
    bool changed;
    const wadListDirEntry *dir = myList->FindWAD ( "LINEDEFS", start, end );
    if ( rawLineDef ) delete rawLineDef;
    if ( newFormat ) {
        rawLineDef = new wLineDef2 [ noLineDefs ];
        ConvertLineDefToRaw2 ( noLineDefs, lineDef, ( wLineDef2 * ) rawLineDef );
        changed = dir->wad->WriteEntry ( dir->entry, noLineDefs * sizeof ( wLineDef2 ), rawLineDef, false );
    } else {
        rawLineDef = new wLineDef1 [ noLineDefs ];
        ConvertLineDefToRaw1 ( noLineDefs, lineDef, ( wLineDef1 * ) rawLineDef );
        changed = dir->wad->WriteEntry ( dir->entry, noLineDefs * sizeof ( wLineDef1 ), rawLineDef, false );
    }
    return changed;
}

bool DoomLevel::UpdateWAD ()
{
    if (( myList == NULL ) || ( ! modified )) return false;

    const wadListDirEntry *start = myList->FindWAD ( Name ());
    const wadListDirEntry *end = start + 11;

    if ( start == NULL ) return false;
    start += 1;

    const wadListDirEntry *dir;

    bool changed = false;

    if ( thingsChanged ) {
        thingsChanged = false;
        changed |= SaveThings ( start, end );
    }
    if ( lineDefsChanged ) {
        lineDefsChanged = false;
        changed |= SaveLineDefs ( start, end );
    }
    if ( sideDefsChanged ) {
        sideDefsChanged = false;
        dir = myList->FindWAD ( "SIDEDEFS", start, end );
        changed |= dir->wad->WriteEntry ( dir->entry, noSideDefs * sizeof ( wSideDef ), sideDef, false );
    }
    if ( verticesChanged ) {
        verticesChanged = false;
        dir = myList->FindWAD ( "VERTEXES", start, end );
        changed |= dir->wad->WriteEntry ( dir->entry, noVertices * sizeof ( wVertex ), vertex, false );
    }
    if ( segsChanged ) {
        segsChanged = false;
        dir = myList->FindWAD ( "SEGS", start, end );
        changed |= dir->wad->WriteEntry ( dir->entry, noSegs * sizeof ( wSegs ), segs, false );
    }
    if ( subSectorsChanged ) {
        subSectorsChanged = false;
        dir = myList->FindWAD ( "SSECTORS", start, end );
        changed |= dir->wad->WriteEntry ( dir->entry, noSubSectors * sizeof ( wSSector ), subSector, false );
    }
    if ( nodesChanged ) {
        nodesChanged = false;
        dir = myList->FindWAD ( "NODES", start, end );
        changed |= dir->wad->WriteEntry ( dir->entry, noNodes * sizeof ( wNode ), node, false );
    }
    if ( sectorsChanged ) {
        sectorsChanged = false;
        dir = myList->FindWAD ( "SECTORS", start, end );
        changed |= dir->wad->WriteEntry ( dir->entry, noSectors * sizeof ( wSector ), sector, false );
    }
    if ( rejectChanged ) {
        rejectChanged = false;
        dir = myList->FindWAD ( "REJECT", start, end );
        changed |= dir->wad->WriteEntry ( dir->entry, rejectSize, reject, false );
    }
    if ( blockMapChanged ) {
        blockMapChanged = false;
        dir = myList->FindWAD ( "BLOCKMAP", start, end );
        changed |= dir->wad->WriteEntry ( dir->entry, blockMapSize, blockMap, false );
    }

    return changed;
}

void DoomLevel::NewThings ( int newCount, wThing *newData )
{
    if ( thing && newData && ( newCount == noThings ) &&
         ( memcmp ( newData, thing, sizeof ( wThing ) * newCount ) == 0 )) {
        delete newData;
        return;
    }
    modified = true;
    thingsChanged = true;

    if ( rawThing ) delete rawThing;
    rawThing = NULL;

    if ( thing ) delete thing;
    thing = newData;
    noThings = newCount;
}

void DoomLevel::NewLineDefs ( int newCount, wLineDef *newData )
{
    if ( lineDef && newData && ( newCount == noLineDefs ) &&
         ( memcmp ( newData, lineDef, sizeof ( wLineDef ) * newCount ) == 0 )) {
        delete newData;
        return;
    }
    modified = true;
    lineDefsChanged = true;

    if ( rawLineDef ) delete rawLineDef;
    rawLineDef = NULL;

    if ( lineDef ) delete lineDef;
    lineDef = newData;
    noLineDefs = newCount;
}

void DoomLevel::NewSideDefs ( int newCount, wSideDef *newData )
{
    if ( sideDef && newData && ( newCount == noSideDefs ) &&
         ( memcmp ( newData, sideDef, sizeof ( wSideDef ) * newCount ) == 0 )) {
        delete newData;
        return;
    }
    modified = true;
    sideDefsChanged = true;

    if ( sideDef ) delete sideDef;
    sideDef = newData;
    noSideDefs = newCount;
}

void DoomLevel::NewVertices ( int newCount, wVertex *newData )
{
    if ( vertex && newData && ( newCount == noVertices ) &&
         ( memcmp ( newData, vertex, sizeof ( wVertex ) * newCount ) == 0 )) {
        delete newData;
        return;
    }
    modified = true;
    verticesChanged = true;

    if ( vertex ) delete vertex;
    vertex = newData;
    noVertices = newCount;
}

void DoomLevel::NewSectors ( int newCount, wSector *newData )
{
    if ( sector && newData && ( newCount == noSectors ) &&
         ( memcmp ( newData, sector, sizeof ( wSector ) * newCount ) == 0 )) {
        delete newData;
        return;
    }
    modified = true;
    sectorsChanged = true;

    if ( sector ) delete sector;
    sector = newData;
    noSectors = newCount;
}

void DoomLevel::NewSegs ( int newCount, wSegs *newData )
{
    if ( segs && newData && ( newCount == noSegs ) &&
         ( memcmp ( newData, segs, sizeof ( wSegs ) * newCount ) == 0 )) {
        delete newData;
        return;
    }
    modified = true;
    segsChanged = true;

    if ( segs ) delete segs;
    segs = newData;
    noSegs = newCount;
}

void DoomLevel::NewSubSectors ( int newCount, wSSector *newData )
{
    if ( subSector && newData && ( newCount == noSubSectors ) &&
         ( memcmp ( newData, subSector, sizeof ( wSSector ) * newCount ) == 0 )) {
        delete newData;
        return;
    }
    modified = true;
    subSectorsChanged = true;

    if ( subSector ) delete subSector;
    subSector = newData;
    noSubSectors = newCount;
}

void DoomLevel::NewNodes ( int newCount, wNode *newData )
{
    if ( node && newData && ( newCount == noNodes ) &&
         ( memcmp ( newData, node, sizeof ( wNode ) * newCount ) == 0 )) {
        delete newData;
        return;
    }
    modified = true;
    nodesChanged = true;

    if ( node ) delete node;
    node = newData;
    noNodes = newCount;
}

void DoomLevel::NewReject ( int newSize, wReject *newData, bool saveBits )
{
    int mask = ( 0xFF00 >> ( rejectSize * 8 - noSectors * noSectors )) & 0xFF;
    int maskData = (( UCHAR * ) reject ) [ rejectSize -1 ] & mask;

    if ( reject && newData && ( newSize == rejectSize ) &&
         ( memcmp ( newData, blockMap, newSize - 1 ) == 0 )) {
        UCHAR oldTail = (( UCHAR * ) reject ) [ rejectSize - 1 ];
        UCHAR newTail = (( UCHAR * ) newData ) [ rejectSize - 1 ];
        if ((( saveBits ) && ( oldTail == newTail )) ||
            (( oldTail & mask ) == ( newTail & mask ))) {
            delete newData;
            return;
        }
    }
    modified = true;
    rejectChanged = true;

    if ( reject ) delete reject;
    reject = newData;
    rejectSize = newSize;

    if ( saveBits ) {
        (( UCHAR * ) reject ) [ rejectSize - 1 ] &= ( UCHAR ) ~mask;
        (( UCHAR * ) reject ) [ rejectSize - 1 ] |= ( UCHAR ) maskData;
    }
}

void DoomLevel::NewBlockMap ( int newSize, wBlockMap *newData )
{
    if ( blockMap && newData && ( newSize == blockMapSize ) &&
         ( memcmp ( newData, blockMap, newSize ) == 0 )) {
        delete newData;
        return;
    }
    modified = true;
    blockMapChanged = true;

    if ( blockMap ) delete blockMap;
    blockMap = newData;
    blockMapSize = newSize;
}
