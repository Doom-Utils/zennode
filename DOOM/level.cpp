//----------------------------------------------------------------------------
//
// File:        level.cpp
// Date:        26-Oct-1994
// Programmer:  Marc Rousseau
//
// Description: Object classes for manipulating Doom Maps
//
// Copyright (c) 1994-2002 Marc Rousseau, All Rights Reserved.
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

#define min(x,y)    ((( x ) < ( y )) ? x : y )

#if defined ( __GNUC__ )

char *strupr ( char *ptr )
{
    for ( int i = 0; ptr[i]; i++ ) {
        ptr[i] = toupper ( ptr[i] );
    }
    return ptr;
}

#endif

DoomLevel::DoomLevel ( const char *_name, WAD *_wad, bool bLoadData ) :
    m_Wad ( _wad ),
    m_Modified ( false ),
    m_Valid ( false ),
    m_Checked ( false ),
    m_NewFormat ( false ),
    m_Title ( NULL ),
    m_Music ( NULL ),
    m_Cluster ( 0 ),
    m_MapDataSize ( 0 ),
    m_NoThings ( 0 ),
    m_NoLineDefs ( 0 ),
    m_NoSideDefs ( 0 ),
    m_NoVertices ( 0 ),
    m_NoSectors ( 0 ),
    m_NoSegs ( 0 ),
    m_NoSubSectors ( 0 ),
    m_NoNodes ( 0 ),
    m_RejectSize ( 0 ),
    m_BlockMapSize ( 0 ),
    m_RawThing ( NULL ),
    m_RawLineDef ( NULL ),
    m_ThingsChanged ( false ),
    m_LineDefsChanged ( false ),
    m_SideDefsChanged ( false ),
    m_VerticesChanged ( false ),
    m_SectorsChanged ( false ),
    m_SegsChanged ( false ),
    m_SubSectorsChanged ( false ),
    m_NodesChanged ( false ),
    m_RejectChanged ( false ),
    m_BlockMapChanged ( false ),
    m_MapData ( NULL ),
    m_Thing ( NULL ),
    m_LineDef ( NULL ),
    m_SideDef ( NULL ),
    m_Vertex ( NULL ),
    m_Sector ( NULL ),
    m_Segs ( NULL ),
    m_SubSector ( NULL ),
    m_Node ( NULL ),
    m_Reject ( NULL ),
    m_BlockMap ( NULL )
{
    WipeOut ();

    m_Title     = NULL;
    m_Music     = NULL;
    m_Cluster   = -1;
    m_NewFormat = false;

    memset ( m_Name, 0, sizeof ( m_Name ));
    for ( int i = 0; i < 8; i++ ) {
        if ( _name[i] == '\0' ) break;
        m_Name[i] = ( char ) toupper ( _name[i] );
    }

    if ( bLoadData == true ) Load ();

    LoadHexenInfo ();
}

DoomLevel::~DoomLevel ()
{
    if ( m_Title ) free (( char * ) m_Title );
    if ( m_Music ) free (( char * ) m_Music );
    CleanUp ();
}

// TBD
bool DoomLevel::isValid ( bool checkBSP )
{
    if ( m_Checked || ! m_Valid ) return m_Valid;

    int i;
    
    bool *used = new bool [ m_NoSideDefs ];
    memset ( used, false, sizeof ( bool ) * m_NoSideDefs );

    // Sanity check for LINEDEFS
    for ( i = 0; i < m_NoLineDefs; i++ ) {
        if ( m_LineDef [i].start >= m_NoVertices ) {
            fprintf ( stderr, "LINEDEFS[%d].%s m_Vertex is invalid (%d/%d)\n", i, "start", m_LineDef [i].start, m_NoVertices );
            m_Valid = false;
        }
        if ( m_LineDef [i].end >= m_NoVertices ) {
            fprintf ( stderr, "LINEDEFS[%d].%s m_Vertex is invalid (%d/%d)\n", i, "end", m_LineDef [i].end, m_NoVertices );
            m_Valid = false;
        }
        if ( m_LineDef [i].sideDef [ LEFT_SIDEDEF ] != NO_SIDEDEF ) {
            if ( m_LineDef [i].sideDef [ LEFT_SIDEDEF ] >= m_NoSideDefs ) {
                fprintf ( stderr, "LINEDEFS[%d].sideDef[%s] is invalid (%d/%d)\n", i, "left", m_LineDef [i].sideDef [LEFT_SIDEDEF], m_NoSideDefs );
                m_Valid = false;
            } else {
                used [ m_LineDef [i].sideDef [ LEFT_SIDEDEF ]] = true;
            }
        }
        if ( m_LineDef [i].sideDef [ RIGHT_SIDEDEF ] != NO_SIDEDEF ) {
            if ( m_LineDef [i].sideDef [ RIGHT_SIDEDEF ] >= m_NoSideDefs ) {
                fprintf ( stderr, "LINEDEFS[%d].sideDef[%s] is invalid (%d/%d)\n", i, "right", m_LineDef [i].sideDef [RIGHT_SIDEDEF], m_NoSideDefs );
                m_Valid = false;
            } else {
                used [ m_LineDef [i].sideDef [ RIGHT_SIDEDEF ]] = true;
            }
        }
    }

    // Sanity check for SIDEDEFS
    for ( i = 0; i < m_NoSideDefs; i++ ) {
        if (( m_SideDef [i].sector >= m_NoSectors ) && ( used [i] )) {
            fprintf ( stderr, "SIDEDEFS[%d].sector is invalid (%d/%d)\n", i, m_SideDef [i].sector, m_NoSectors );
            m_Valid = false;
        }
    }

    delete [] used;

    if ( checkBSP == true ) {

        // Sanity check for SEGS
        for ( i = 0; i < m_NoSegs; i++ ) {
            if ( m_Segs [i].start >= m_NoVertices ) {
                fprintf ( stderr, "SEGS[%d].%s m_Vertex is invalid (%d/%d)\n", i, "start", m_Segs [i].start, m_NoVertices );
                m_Valid = false;
            }
            if ( m_Segs [i].end >= m_NoVertices ) {
                fprintf ( stderr, "SEGS[%d].%s m_Vertex is invalid (%d/%d)\n", i, "end", m_Segs [i].end, m_NoVertices );
                m_Valid = false;
            }
            if ( m_Segs [i].lineDef >= m_NoLineDefs ) {
                fprintf ( stderr, "SEGS[%d].lineDef is invalid (%d/%d)\n", i, m_Segs [i].lineDef, m_NoLineDefs );
                m_Valid = false;
            }
        }

        // Sanity check for SSECTORS
        for ( i = 0; i < m_NoSubSectors; i++ ) {
            if ( m_SubSector [i].first >= m_NoSegs ) {
                fprintf ( stderr, "SSECTORS[%d].first is invalid (%d/%d)\n", i, m_SubSector [i].first, m_NoSegs );
                m_Valid = false;
            }
            if ( m_SubSector [i].first + m_SubSector [i].num > m_NoSegs ) {
                fprintf ( stderr, "SSECTORS[%d].num is invalid (%d/%d)\n", i, m_SubSector [i].num, m_NoSegs );
                m_Valid = false;
            }
        }

        // Sanity check for NODES
        for ( i = 0; i < m_NoNodes; i++ ) {
            USHORT child;
            child = m_Node [i].child [0];
            if ( child & 0x8000 ) {
                if (( child & 0x7FFF ) >= m_NoSubSectors ) {
                    fprintf ( stderr, "NODES[%d].child[%d] is invalid (0x8000 | %d/%d)\n", i, 0, child & 0x7FFF, m_NoSubSectors );
                    m_Valid = false;
                }
            } else {
                if ( child >= m_NoNodes ) {
                    fprintf ( stderr, "NODES[%d].child[%d] is invalid (%d/%d)\n", i, 0, child, m_NoNodes );
                    m_Valid = false;
                }
            }
            child = m_Node [i].child [1];
            if ( child & 0x8000 ) {
                if (( child & 0x7FFF ) >= m_NoSubSectors ) {
                    fprintf ( stderr, "NODES[%d].child[%d] is invalid (0x8000 | %d/%d)\n", i, 1, child & 0x7FFF, m_NoSubSectors );
                    m_Valid = false;
                }
            } else {
                if ( child >= m_NoNodes ) {
                    fprintf ( stderr, "NODES[%d].child[%d] is invalid (%d/%d)\n", i, 1, child, m_NoNodes );
                    m_Valid = false;
                }
            }
        }
    }

    m_Checked = true;
        
    return m_Valid;
}

bool DoomLevel::hasChanged () const	{ return m_Modified; }

void DoomLevel::DeleteTransients ()
{
    if ( m_Segs ) 	{ delete m_Segs;	m_Segs      = NULL; }
    if ( m_SubSector )	{ delete m_SubSector;	m_SubSector = NULL; }
    if ( m_Node )	{ delete m_Node;	m_Node      = NULL; }
    if ( m_Reject )	{ delete m_Reject;	m_Reject    = NULL; }
    if ( m_BlockMap )	{ delete m_BlockMap;	m_BlockMap  = NULL; }
}

void DoomLevel::WipeOut ()
{
    m_Modified = false;
    m_Checked  = false;
    m_Valid    = false;

    m_ThingsChanged     = false;
    m_LineDefsChanged   = false;
    m_SideDefsChanged   = false;
    m_VerticesChanged   = false;
    m_SectorsChanged    = false;
    m_SegsChanged       = false;
    m_SubSectorsChanged = false;
    m_NodesChanged      = false;
    m_RejectChanged     = false;
    m_BlockMapChanged   = false;

    m_NoThings     = 0;
    m_NoLineDefs   = 0;
    m_NoSideDefs   = 0;
    m_NoVertices   = 0;
    m_NoSectors    = 0;
 
    m_NoSegs       = 0;
    m_NoSubSectors = 0;
    m_NoNodes      = 0;

    m_RejectSize   = 0;
    m_BlockMapSize = 0;

    m_Segs       = NULL;
    m_SubSector  = NULL;
    m_Node       = NULL;
    m_Reject     = NULL;
    m_BlockMap   = NULL;

    m_RawThing   = NULL;
    m_RawLineDef = NULL;

    m_Thing      = NULL;
    m_LineDef    = NULL;
    m_SideDef    = NULL;
    m_Vertex     = NULL;
    m_Sector     = NULL;
}

void DoomLevel::CleanUp ()
{
    DeleteTransients ();

    if ( m_RawThing )   { delete [] m_RawThing;    m_RawThing   = NULL; }
    if ( m_RawLineDef ) { delete [] m_RawLineDef;  m_RawLineDef = NULL; }

    if ( m_MapData )    { delete [] m_MapData;     m_MapData    = NULL; }
    if ( m_Thing )      { delete [] m_Thing;       m_Thing      = NULL; }
    if ( m_LineDef )    { delete [] m_LineDef;     m_LineDef    = NULL; }
    if ( m_SideDef )    { delete [] m_SideDef;     m_SideDef    = NULL; }
    if ( m_Vertex )     { delete [] m_Vertex;      m_Vertex     = NULL; }
    if ( m_Sector )     { delete [] m_Sector;      m_Sector     = NULL; }

    WipeOut ();
}

#if defined ( BIG_ENDIAN )
void DoomLevel::AdjustByteOrder ()
{
    /*
        char       *m_RawThing;		// Check Format
        char       *m_RawLineDef;		// Check Format

        wThing     *m_Thing;
        wLineDef   *m_LineDef;
        wSideDef   *m_SideDef;
        wVertex    *m_Vertex;
        wSector    *m_Sector;

        wSegs      *m_Segs;
        wSSector   *m_SubSector;
        wNode      *node;
        wReject    *m_Reject;		// May m_Not need to be swapped
        wBlockMap  *m_BlockMap;
    */
}
#endif

void DoomLevel::TrimVertices ()
{
    m_Modified = true;
    m_VerticesChanged = true;

    long highBit = 1L << ( 8 * sizeof ( long ) - 1 );
    long *Used = new long [ m_NoVertices ];
    memset ( Used, 0, sizeof ( long ) * m_NoVertices );
    int i;
    for ( i = 0; i < m_NoLineDefs; i++ ) {
        Used [ m_LineDef [i].start ] = highBit;
        Used [ m_LineDef [i].end ] = highBit;
    }
    if ( m_Segs ) for ( int i = 0; i < m_NoSegs; i++ ) {
        Used [ m_Segs [i].start ] = highBit;
        Used [ m_Segs [i].end ] = highBit;
    }

    int usedCount = 0;
    wVertex *newVertices = new wVertex [ m_NoVertices ];
    for ( i = 0; i < m_NoVertices; i++ ) {
        if ( Used [i] ) {
            newVertices [usedCount] = m_Vertex [i];
            Used [i] |= usedCount++;
        }
    }
    for ( i = 0; i < m_NoLineDefs; i++ ) {
        m_LineDef [i].start = ( USHORT ) ( Used [ m_LineDef [i].start ] & ~highBit );
        m_LineDef [i].end = ( USHORT ) ( Used [ m_LineDef [i].end ] & ~highBit );
    }
    if ( m_Segs ) for ( int i = 0; i < m_NoSegs; i++ ) {
        m_Segs [i].start = ( USHORT ) ( Used [ m_Segs [i].start ] & ~highBit );
        m_Segs [i].end = ( USHORT ) ( Used [ m_Segs [i].end ] & ~highBit );
    }

    if ( m_NoVertices != usedCount ) {
        m_LineDefsChanged = true;
        m_VerticesChanged = true;
        m_SegsChanged     = true;
    }
 
    m_NoVertices = usedCount;
    delete [] m_Vertex;
    delete [] Used;
    m_Vertex = newVertices;
}

void DoomLevel::PackVertices ()
{
    m_Modified = true;
    m_VerticesChanged = true;

#if defined ( __BORLANDC__ )
    #if sizeof ( wVertex ) != sizeof ( long )
        #error Sorry, PackVertices assumes sizeof ( wVertex ) == sizeof ( long )
    #endif
#endif
    long *vert = ( long * ) m_Vertex;
    int *Used = new int [ m_NoVertices ], newCount = 0;
    memset ( Used, 0, sizeof ( int ) * m_NoVertices );
    int i, j;
    for ( i = 0; i < m_NoVertices; i++ ) {
        long currentVert = vert [i];
        for ( j = 0; j < i; j++ ) {
            if ( vert [j] == currentVert ) break;
        }
        Used [i] = j;
        if ( i == j ) newCount++;
    }

    int usedCount = 0;
    wVertex *newVertices = new wVertex [ newCount ];
    for ( i = 0; i < m_NoVertices; i++ ) {
        if ( Used [i] == i ) {
            newVertices [usedCount] = m_Vertex [i];
            Used [i] = usedCount++;
        } else {
            Used [i] = Used [ Used [i]];
        }
    }

    for ( i = 0; i < m_NoLineDefs; i++ ) {
        m_LineDef [i].start = ( USHORT ) ( Used [ m_LineDef [i].start ]);
        m_LineDef [i].end = ( USHORT ) ( Used [ m_LineDef [i].end ]);
    }

    if ( m_Segs ) for ( i = 0; i < m_NoSegs; i++ ) {
        m_Segs [i].start = ( USHORT ) ( Used [ m_Segs [i].start ]);
        m_Segs [i].end = ( USHORT ) ( Used [ m_Segs [i].end ]);
    }

    if ( m_NoVertices != usedCount ) {
        m_LineDefsChanged = true;
        m_VerticesChanged = true;
        m_SegsChanged     = true;
    }
 
    m_NoVertices = newCount;
    delete [] m_Vertex;
    delete [] Used;
    m_Vertex = newVertices;
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

void DoomLevel::ReadThings ( bool testFormat, const wadDirEntry *start, const wadDirEntry *end )
{
    ULONG temp;

    const wadDirEntry *dir = m_Wad->FindDir ( "THINGS", start, end );
    if ( dir == NULL ) return;

    // Make sure this isn't really a HEXEN m_Wad in disguise
    if ( testFormat && ( m_NewFormat == false )) {
        wThing1 *testThing = ( wThing1 * ) m_Wad->ReadEntry ( dir, &temp, true );
        int count = dir->size / sizeof ( wThing1 );
        // sizeof ( wThing2 ) is a multiple of sizeof ( wThing1 )
        if (( count & 0x0001 ) == 0 ) {
            int minX = 65536, m_NoType0s = 0, m_NoX0s = 0, m_NoBadAngles = 0;
            for ( int i = 0; i < count; i++ ) {
                if ( testThing[i].xPos < minX ) minX = testThing[i].xPos;
                if ( testThing[i].xPos == 0 ) m_NoX0s++;
                if ( testThing[i].type == 0 ) m_NoType0s++;
                if ( testThing[i].angle % 45 ) m_NoBadAngles++;
            }
            int bad = 0, threshold = ( count + 2 ) / 3;
            if ( minX >= 0 ) bad++;
            if ( m_NoX0s > threshold ) bad++;
            if ( m_NoType0s > threshold ) bad++;
            if ( m_NoBadAngles > threshold ) bad++;
            if ( bad > 1 ) m_NewFormat = true;
        }
        delete testThing;
    }

    m_RawThing = ( char * ) m_Wad->ReadEntry ( dir, &temp );

    if ( m_NewFormat ) {
        m_NoThings = temp / sizeof ( wThing2 );
        m_Thing = new wThing [ m_NoThings ];
        ConvertRaw2ToThing ( m_NoThings, ( wThing2 * ) m_RawThing, m_Thing );
    } else {
        m_NoThings = temp / sizeof ( wThing1 );
        m_Thing = new wThing [ m_NoThings ];
        ConvertRaw1ToThing ( m_NoThings, ( wThing1 * ) m_RawThing, m_Thing );
    }
}

bool DoomLevel::ReadLineDefs ( const wadDirEntry *start, const wadDirEntry *end )
{
    ULONG temp;
    const wadDirEntry *dir = m_Wad->FindDir ( "LINEDEFS", start, end );

    // Make sure we have the correct format
    if (( m_NewFormat == true ) && ( dir->size % sizeof ( wLineDef2 ))) return true;
    if (( m_NewFormat == false ) && ( dir->size % sizeof ( wLineDef1 ))) return true;

    m_RawLineDef = ( char * ) m_Wad->ReadEntry ( dir, &temp );

    if ( m_NewFormat ) {
        m_NoLineDefs = temp / sizeof ( wLineDef2 );
        m_LineDef = new wLineDef [ m_NoLineDefs ];
        ConvertRaw2ToLineDef ( m_NoLineDefs, ( wLineDef2 * ) m_RawLineDef, m_LineDef );
        for ( int i = 0; i < m_NoLineDefs; i++ ) {
            if ( m_LineDef [i].sideDef [ RIGHT_SIDEDEF ] == NO_SIDEDEF ) return true;
        }
    } else {
        m_NoLineDefs = temp / sizeof ( wLineDef1 );
        m_LineDef = new wLineDef [ m_NoLineDefs ];
        ConvertRaw1ToLineDef ( m_NoLineDefs, ( wLineDef1 * ) m_RawLineDef, m_LineDef );
    }
    return false;
}

int DoomLevel::Load ()
{
    if ( m_Wad == NULL ) return false;

    const wadDirEntry *dir;
    const wadDirEntry *start = m_Wad->FindDir ( Name ());
    const wadDirEntry *end = start + min ( 10, m_Wad->DirSize () - 1 );

    if ( start == NULL ) return false;

    ULONG temp;

    m_NewFormat = ( start->size > 0 ) ? true : false;

    m_MapData = ( char * ) m_Wad->ReadEntry ( start, &temp );
    m_MapDataSize = temp;

    start += 1;

    bool wrongFormat = true;
    for ( int i = 0; wrongFormat && ( i < 2 ); i++ ) {
        CleanUp ();
        ReadThings (( i == 0 ) ? true : false, start, end );
        wrongFormat = ReadLineDefs ( start, end );
        if ( wrongFormat ) m_NewFormat = ! m_NewFormat;
    }

    if ( wrongFormat ) {
        CleanUp ();
        return false;
    } else {
        m_Valid = true;
    }

    dir = m_Wad->FindDir ( "SIDEDEFS", start, end );
    if ( dir == NULL ) return false;
    m_SideDef = ( wSideDef * ) m_Wad->ReadEntry ( dir, &temp );
    m_NoSideDefs = temp / sizeof ( wSideDef );

    dir = m_Wad->FindDir ( "VERTEXES", start, end );
    if ( dir == NULL ) return false;
    m_Vertex = ( wVertex * ) m_Wad->ReadEntry ( dir, &temp );
    m_NoVertices = temp / sizeof ( wVertex );

    dir = m_Wad->FindDir ( "SECTORS", start, end );
    if ( dir == NULL ) return false;
    m_Sector = ( wSector * ) m_Wad->ReadEntry ( dir, &temp );
    m_NoSectors = temp / sizeof ( wSector );

    dir = m_Wad->FindDir ( "SEGS", start, end );
    if ( dir != NULL ) {
        m_Segs = ( wSegs * ) m_Wad->ReadEntry ( dir, &temp );
        m_NoSegs = temp / sizeof ( wSegs );
    }

    dir = m_Wad->FindDir ( "SSECTORS", start, end );
    if ( dir != NULL ) {
        m_SubSector = ( wSSector * ) m_Wad->ReadEntry ( dir, &temp );
        m_NoSubSectors = temp / sizeof ( wSSector );
    }

    dir = m_Wad->FindDir ( "NODES", start, end );
    if ( dir != NULL ) {
        m_Node = ( wNode * ) m_Wad->ReadEntry ( dir, &temp );
        m_NoNodes = temp / sizeof ( wNode );
    }

    dir = m_Wad->FindDir ( "REJECT", start, end );
    if ( dir != NULL ) {
        m_Reject = ( wReject * ) m_Wad->ReadEntry ( dir, &temp );
        m_RejectSize = temp;
    }

    dir = m_Wad->FindDir ( "BLOCKMAP", start, end );
    if ( dir != NULL ) {
        m_BlockMap = ( wBlockMap * ) m_Wad->ReadEntry ( dir, &temp );
        m_BlockMapSize = temp;
    }

#if defined ( BIG_ENDIAN )
    AdjustByteOrder ();
#endif

    return true;
}

void DoomLevel::LoadHexenInfo ()
{
    const wadDirEntry *dir = m_Wad->FindDir ( "MAPINFO" );
    if ( dir == NULL ) return;

    int level;
    sscanf ( m_Name, "MAP%02d", &level );

    ULONG Size;
    char *buffer = ( char * ) m_Wad->ReadEntry ( dir, &Size, true );
    char *ptr = buffer;

    if ( m_Title ) free (( char * ) m_Title );
    m_Title = NULL;

    do {
        if (( ptr = strstr ( ptr, "\nmap " )) == NULL ) break;
        if ( atoi ( &ptr[5] ) == level ) {
            while ( *ptr++ != '"' );
            strtok ( ptr, "\"" );
            m_Title = strdup ( ptr );
            ptr += strlen ( ptr ) + 1;
            char *clstr = strstr ( ptr, "\ncluster " );
            char *next = strstr ( ptr, "\n\r" );
            if ( clstr && ( clstr < next )) {
                m_Cluster = atoi ( clstr + 9 );
            }
            break;
        } else {
            ptr++;
        }
    } while ( ptr && *ptr );

    delete buffer;

    if ( m_Title ) {
        ptr = ( char * ) m_Title + 1;
        while ( *ptr ) {
            *ptr = ( char ) tolower ( *ptr );
            if ( *ptr == ' ' ) {
                while ( *ptr == ' ' ) ptr++;
                if ( strncmp ( ptr, "OF ", 3 ) == 0 ) ptr--;
            }
            if ( *ptr ) ptr++;
        }
    }

    dir = m_Wad->FindDir ( "SNDINFO" );
    if ( dir == NULL ) return;

    buffer = ( char * ) m_Wad->ReadEntry ( dir, &Size, true );
    ptr = buffer;

    do {
        if (( ptr = strstr ( ptr, "\n$MAP" )) == NULL ) break;
        if ( atoi ( &ptr[5] ) == level ) {
            ptr += 25;
            strtok ( ptr, "\r" );
            m_Music = strupr ( strdup ( ptr ));
            break;
        } else {
            ptr++;
        }
    } while ( ptr && *ptr );

    delete buffer;
}

void DoomLevel::AddToWAD ( WAD *m_Wad )
{
    m_Wad->InsertAfter (( const wLumpName * ) Name (), m_MapDataSize, m_MapData, false );

    ULONG size1, size2;
    if ( m_NewFormat ) {
        size1 = m_NoThings * sizeof ( wThing2 );
        size2 = m_NoLineDefs * sizeof ( wLineDef2 );
    } else {
        size1 = m_NoThings * sizeof ( wThing1 );
        size2 = m_NoLineDefs * sizeof ( wLineDef1 );
    }

#if defined ( BIG_ENDIAN )
    AdjustByteOrder ();
#endif

    m_Wad->InsertAfter (( const wLumpName * ) "THINGS",   size1, m_RawThing, false );
    m_Wad->InsertAfter (( const wLumpName * ) "LINEDEFS", size2, m_RawLineDef, false );
    m_Wad->InsertAfter (( const wLumpName * ) "SIDEDEFS", m_NoSideDefs * sizeof ( wSideDef ), m_SideDef, false );
    m_Wad->InsertAfter (( const wLumpName * ) "VERTEXES", m_NoVertices * sizeof ( wVertex ), m_Vertex, false );
    m_Wad->InsertAfter (( const wLumpName * ) "SEGS",     m_NoSegs * sizeof ( wSegs ), m_Segs, false );
    m_Wad->InsertAfter (( const wLumpName * ) "SSECTORS", m_NoSubSectors * sizeof ( wSSector ), m_SubSector, false );
    m_Wad->InsertAfter (( const wLumpName * ) "NODES",    m_NoNodes * sizeof ( wNode ), m_Node, false );
    m_Wad->InsertAfter (( const wLumpName * ) "SECTORS",  m_NoSectors * sizeof ( wSector ), m_Sector, false );
    m_Wad->InsertAfter (( const wLumpName * ) "REJECT",   m_RejectSize, m_Reject, false );
    m_Wad->InsertAfter (( const wLumpName * ) "BLOCKMAP", m_BlockMapSize, m_BlockMap, false );

#if defined ( BIG_ENDIAN )
    AdjustByteOrder ();
#endif

    if ( m_NewFormat ) {
        const wadDirEntry *dir;
        const wadDirEntry *start = m_Wad->FindDir ( Name ());
        const wadDirEntry *end = start + 11;
        dir = m_Wad->FindDir ( "BEHAVIOR", start, end );
        if ( dir != NULL ) {
            ULONG size;
            void *ptr = m_Wad->ReadEntry ( dir, &size );
            m_Wad->InsertAfter (( const wLumpName * ) "BEHAVIOR", size, ptr, true );
        }
    }
}

bool DoomLevel::SaveThings ( const wadDirEntry *start, const wadDirEntry *end )
{
    bool changed;
    const wadDirEntry *dir = m_Wad->FindDir ( "THINGS", start, end );
    if ( m_RawThing ) delete [] m_RawThing;
    if ( m_NewFormat ) {
        m_RawThing = new char [ sizeof ( wThing2 ) * m_NoThings ];
        ConvertThingToRaw2 ( m_NoThings, m_Thing, ( wThing2 * ) m_RawThing );
        changed = m_Wad->WriteEntry ( dir, m_NoThings * sizeof ( wThing2 ), m_RawThing, false );
    } else {
        m_RawThing = new char [ sizeof ( wThing1 ) * m_NoThings ];
        ConvertThingToRaw1 ( m_NoThings, m_Thing, ( wThing1 * ) m_RawThing );
        changed = m_Wad->WriteEntry ( dir, m_NoThings * sizeof ( wThing1 ), m_RawThing, false );
    }
    return changed;
}

bool DoomLevel::SaveLineDefs ( const wadDirEntry *start, const wadDirEntry *end )
{
    bool changed;
    const wadDirEntry *dir = m_Wad->FindDir ( "LINEDEFS", start, end );
    if ( m_RawLineDef ) delete [] m_RawLineDef;
    if ( m_NewFormat ) {
        m_RawLineDef = new char [ sizeof ( wLineDef2 ) * m_NoLineDefs ];
        ConvertLineDefToRaw2 ( m_NoLineDefs, m_LineDef, ( wLineDef2 * ) m_RawLineDef );
        changed = m_Wad->WriteEntry ( dir, m_NoLineDefs * sizeof ( wLineDef2 ), m_RawLineDef, false );
    } else {
        m_RawLineDef = new char [ sizeof ( wLineDef1 ) * m_NoLineDefs ];
        ConvertLineDefToRaw1 ( m_NoLineDefs, m_LineDef, ( wLineDef1 * ) m_RawLineDef );
        changed = m_Wad->WriteEntry ( dir, m_NoLineDefs * sizeof ( wLineDef1 ), m_RawLineDef, false );
    }
    return changed;
}

bool DoomLevel::UpdateWAD ()
{
    if (( m_Wad == NULL ) || ( ! m_Modified )) return false;
    
    const wadDirEntry *start = m_Wad->FindDir ( Name ());
    const wadDirEntry *end = start + min ( 10, m_Wad->DirSize () - 1 );

    if ( start == NULL ) return false;
    start += 1;

    const wadDirEntry *dir;

    bool changed = false;

    if ( m_ThingsChanged ) {
        m_ThingsChanged = false;
        changed |= SaveThings ( start, end );
    }
    if ( m_LineDefsChanged ) {
        m_LineDefsChanged = false;
        changed |= SaveLineDefs ( start, end );
    }
    if ( m_SideDefsChanged ) {
        m_SideDefsChanged = false;
        dir = m_Wad->FindDir ( "SIDEDEFS", start, end );
        if ( dir == NULL ) {
            fprintf ( stderr, "Invalid map - no SIDEDEFS!\n" );
        } else {
            changed |= m_Wad->WriteEntry ( dir, m_NoSideDefs * sizeof ( wSideDef ), m_SideDef, false );
        }
    }
    if ( m_VerticesChanged ) {
        m_VerticesChanged = false;
        dir = m_Wad->FindDir ( "VERTEXES", start, end );
        if ( dir == NULL ) {
            fprintf ( stderr, "Invalid map - no SIDEDEFS!\n" );
        } else {
            changed |= m_Wad->WriteEntry ( dir, m_NoVertices * sizeof ( wVertex ), m_Vertex, false );
        }
    }
    if ( m_SegsChanged ) {
        m_SegsChanged = false;
        dir = m_Wad->FindDir ( "SEGS", start, end );
        if ( dir == NULL ) {
            const wadDirEntry *last = m_Wad->FindDir ( "VERTEXES", start, end );
            changed |= m_Wad->InsertAfter (( const wLumpName * ) "SEGS", m_NoSegs * sizeof ( wSegs ), m_Segs, false, last );
            start = m_Wad->FindDir ( Name ());
            end = start + min ( 10, m_Wad->DirSize () - 1 );
        } else {
            changed |= m_Wad->WriteEntry ( dir, m_NoSegs * sizeof ( wSegs ), m_Segs, false );
        }
    }
    if ( m_SubSectorsChanged ) {
        m_SubSectorsChanged = false;
        dir = m_Wad->FindDir ( "SSECTORS", start, end );
        if ( dir == NULL ) {
            const wadDirEntry *last = m_Wad->FindDir ( "SEGS", start, end );
            changed |= m_Wad->InsertAfter (( const wLumpName * ) "SSECTORS", m_NoSubSectors * sizeof ( wSSector ), m_SubSector, false, last );
            start = m_Wad->FindDir ( Name ());
            end = start + min ( 10, m_Wad->DirSize () - 1 );
        } else {
            changed |= m_Wad->WriteEntry ( dir, m_NoSubSectors * sizeof ( wSSector ), m_SubSector, false );
        }
    }
    if ( m_NodesChanged ) {
        m_NodesChanged = false;
        dir = m_Wad->FindDir ( "NODES", start, end );
        if ( dir == NULL ) {
            const wadDirEntry *last = m_Wad->FindDir ( "SSECTORS", start, end );
            changed |= m_Wad->InsertAfter (( const wLumpName * ) "NODES", m_NoNodes * sizeof ( wNode ), m_Node, false, last );
            start = m_Wad->FindDir ( Name ());
            end = start + min ( 10, m_Wad->DirSize () - 1 );
        } else {
            changed |= m_Wad->WriteEntry ( dir, m_NoNodes * sizeof ( wNode ), m_Node, false );
        }
    }
    if ( m_SectorsChanged ) {
        m_SectorsChanged = false;
        dir = m_Wad->FindDir ( "SECTORS", start, end );
        if ( dir == NULL ) {
            const wadDirEntry *last = m_Wad->FindDir ( "NODES", start, end );
            changed |= m_Wad->InsertAfter (( const wLumpName * ) "SECTORS", m_NoSectors * sizeof ( wSector ), m_Sector, false, last );
            start = m_Wad->FindDir ( Name ());
            end = start + min ( 10, m_Wad->DirSize () - 1 );
        } else {
            changed |= m_Wad->WriteEntry ( dir, m_NoSectors * sizeof ( wSector ), m_Sector, false );
        }
    }

    if ( m_RejectChanged ) {
        m_RejectChanged = false;
        dir = m_Wad->FindDir ( "REJECT", start, end );
        if ( dir == NULL ) {
            const wadDirEntry *last = m_Wad->FindDir ( "SECTORS", start, end );
            changed |= m_Wad->InsertAfter (( const wLumpName * ) "REJECT", m_RejectSize, m_Reject, false, last );
            start = m_Wad->FindDir ( Name ());
            end = start + min ( 10, m_Wad->DirSize () - 1 );
        } else {
            changed |= m_Wad->WriteEntry ( dir, m_RejectSize, m_Reject, false );
        }
    }

    if ( m_BlockMapChanged ) {
        m_BlockMapChanged = false;
        dir = m_Wad->FindDir ( "BLOCKMAP", start, end );
        if ( dir == NULL ) {
            const wadDirEntry *last = m_Wad->FindDir ( "REJECT", start, end );
            changed |= m_Wad->InsertAfter (( const wLumpName * ) "BLOCKMAP", m_BlockMapSize, m_BlockMap, false, last );
        } else {
            changed |= m_Wad->WriteEntry ( dir, m_BlockMapSize, m_BlockMap, false );
        }
    }

    return changed;
}

void DoomLevel::NewThings ( int newCount, wThing *newData )
{
    if ( m_Thing && newData && ( newCount == m_NoThings ) &&
         ( memcmp ( newData, m_Thing, sizeof ( wThing ) * newCount ) == 0 )) {
        delete newData;
        return;
    }
    m_Modified = true;
    m_ThingsChanged = true;

    if ( m_RawThing ) delete m_RawThing;////FIX ME
    m_RawThing = NULL;

    if ( m_Thing ) delete m_Thing;
    m_Thing = newData;
    m_NoThings = newCount;
}

void DoomLevel::NewLineDefs ( int newCount, wLineDef *newData )
{
    if ( m_LineDef && newData && ( newCount == m_NoLineDefs ) &&
         ( memcmp ( newData, m_LineDef, sizeof ( wLineDef ) * newCount ) == 0 )) {
        delete newData;
        return;
    }
    m_Modified = true;
    m_LineDefsChanged = true;

    if ( m_RawLineDef ) delete m_RawLineDef;////FIX ME
    m_RawLineDef = NULL;

    if ( m_LineDef ) delete m_LineDef;
    m_LineDef = newData;
    m_NoLineDefs = newCount;
}

void DoomLevel::NewSideDefs ( int newCount, wSideDef *newData )
{
    if ( m_SideDef && newData && ( newCount == m_NoSideDefs ) &&
         ( memcmp ( newData, m_SideDef, sizeof ( wSideDef ) * newCount ) == 0 )) {
        delete newData;
        return;
    }
    m_Modified = true;
    m_SideDefsChanged = true;

    if ( m_SideDef ) delete m_SideDef;
    m_SideDef = newData;
    m_NoSideDefs = newCount;
}

void DoomLevel::NewVertices ( int newCount, wVertex *newData )
{
    if ( m_Vertex && newData && ( newCount == m_NoVertices ) &&
         ( memcmp ( newData, m_Vertex, sizeof ( wVertex ) * newCount ) == 0 )) {
        delete newData;
        return;
    }
    m_Modified = true;
    m_VerticesChanged = true;

    if ( m_Vertex ) delete m_Vertex;
    m_Vertex = newData;
    m_NoVertices = newCount;
}

void DoomLevel::NewSectors ( int newCount, wSector *newData )
{
    if ( m_Sector && newData && ( newCount == m_NoSectors ) &&
         ( memcmp ( newData, m_Sector, sizeof ( wSector ) * newCount ) == 0 )) {
        delete newData;
        return;
    }
    m_Modified = true;
    m_SectorsChanged = true;

    if ( m_Sector ) delete m_Sector;
    m_Sector = newData;
    m_NoSectors = newCount;
}

void DoomLevel::NewSegs ( int newCount, wSegs *newData )
{
    if ( m_Segs && newData && ( newCount == m_NoSegs ) &&
         ( memcmp ( newData, m_Segs, sizeof ( wSegs ) * newCount ) == 0 )) {
        delete newData;
        return;
    }
    m_Modified = true;
    m_SegsChanged = true;

    if ( m_Segs ) delete m_Segs;
    m_Segs = newData;
    m_NoSegs = newCount;
}

void DoomLevel::NewSubSectors ( int newCount, wSSector *newData )
{
    if ( m_SubSector && newData && ( newCount == m_NoSubSectors ) &&
         ( memcmp ( newData, m_SubSector, sizeof ( wSSector ) * newCount ) == 0 )) {
        delete newData;
        return;
    }
    m_Modified = true;
    m_SubSectorsChanged = true;

    if ( m_SubSector ) delete m_SubSector;
    m_SubSector = newData;
    m_NoSubSectors = newCount;
}

void DoomLevel::NewNodes ( int newCount, wNode *newData )
{
    if ( m_Node && newData && ( newCount == m_NoNodes ) &&
         ( memcmp ( newData, m_Node, sizeof ( wNode ) * newCount ) == 0 )) {
        delete newData;
        return;
    }
    m_Modified = true;
    m_NodesChanged = true;

    if ( m_Node ) delete m_Node;
    m_Node = newData;
    m_NoNodes = newCount;
}

void DoomLevel::NewReject ( int newSize, wReject *newData, bool saveBits )
{
    int mask = ( 0xFF00 >> ( m_RejectSize * 8 - m_NoSectors * m_NoSectors )) & 0xFF;
    int maskData = ( m_Reject != NULL ) ? (( UCHAR * ) m_Reject ) [ m_RejectSize -1 ] & mask : 0;

    if ( m_Reject && newData && ( newSize == m_RejectSize ) &&
         ( memcmp ( newData, m_Reject, newSize - 1 ) == 0 )) {
        UCHAR oldTail = (( UCHAR * ) m_Reject ) [ m_RejectSize - 1 ];
        UCHAR newTail = (( UCHAR * ) newData ) [ m_RejectSize - 1 ];
        if (( oldTail & ~mask ) == ( newTail & ~mask )) {
            if (( saveBits == true ) || ( oldTail == newTail )) {
                delete newData;
                return;
            }
        }
    }
    m_Modified = true;
    m_RejectChanged = true;

    if ( m_Reject ) delete m_Reject;
    m_Reject = newData;
    m_RejectSize = newSize;

    if ( saveBits ) {
        (( UCHAR * ) m_Reject ) [ m_RejectSize - 1 ] &= ( UCHAR ) ~mask;
        (( UCHAR * ) m_Reject ) [ m_RejectSize - 1 ] |= ( UCHAR ) maskData;
    }
}

void DoomLevel::NewBlockMap ( int newSize, wBlockMap *newData )
{
    if ( m_BlockMap && newData && ( newSize == m_BlockMapSize ) &&
         ( memcmp ( newData, m_BlockMap, newSize ) == 0 )) {
        delete newData;
        return;
    }
    m_Modified = true;
    m_BlockMapChanged = true;

    if ( m_BlockMap ) delete m_BlockMap;
    m_BlockMap = newData;
    m_BlockMapSize = newSize;
}
