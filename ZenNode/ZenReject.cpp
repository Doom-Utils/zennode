//----------------------------------------------------------------------------
//
// File:        ZenReject.cpp
// Date:        15-Dec-1995
// Programmer:  Marc Rousseau
//
// Description: This module contains the logic for the REJECT builder.
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
//   06-12-99	Reordered functions & removed all function prototypes
//   06-14-99	Modified DrawBlockMapLine to elminate floating point & inlined calls to UpdateRow
//   07-19-99	Added code to track child sectors and active lines (36% faster!)
//
//----------------------------------------------------------------------------

#include <assert.h>
#include <math.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#if defined ( _MSC_VER )
    #include <string.h>
#endif
#include "common.hpp"
#include "level.hpp"
#include "ZenNode.hpp"
#include "geometry.hpp"

extern int CreateBLOCKMAP ( DoomLevel *level, bool compress );

enum eVisibility {
    visUnknown,
    visVisible,
    visHidden
};

struct sRejectRow {
    char          *sector;
};

struct sWADLine {
    int            index;
    const sPoint  *start;
    const sPoint  *end;
};

struct sSolidLine : sWADLine {
    bool           ignore;
};

struct sSeeThruLine : sWADLine {
    int            leftSector;
    int            rightSector;
    long           DY, DX;
    REAL           lo, hi;
    sPoint        *loPoint;
    sPoint        *hiPoint;
};

struct sPolyLine {
    int            noPoints;
    int            lastPoint;
    const sPoint **point;
};

struct sWorldInfo {
    sSeeThruLine  *src;
    sSeeThruLine  *tgt;
    sSolidLine   **testLines;
    int            loIndex;
    int            hiIndex;
    sPolyLine      upperPoly;
    sPolyLine      lowerPoly;
};

struct sBlockMapBounds {
    int            lo;
    int            hi;
} *blockMapBounds;

struct sBlockMapArrayEntry {
    bool          *available;
    sSolidLine    *line;
};

struct sSectorStuff {
    int            noActiveLines;
    int            noLines;
    sSeeThruLine **line;
    int            noNeighbors;
    sSectorStuff **neighbor;
    int            noChildren;
    sSectorStuff  *parent;
};

int            loRow;
int            hiRow;

wBlockMap             *blockMap;
sBlockMapArrayEntry ***blockMapArray;

sRejectRow    *rejectTable;

sPoint        *vertices;
int            noSolidLines;
sSolidLine    *solidLines;
int            noSeeThruLines;
sSeeThruLine  *seeThruLines;

sSectorStuff  *sector;
sSeeThruLine **sectorLines;
sSectorStuff **neighborList;
bool          *isChild;

int            checkLineSize;
bool          *checkLine;
sSolidLine   **indexToSolid;
sSolidLine   **testLines;
const sPoint **polyPoints;

static long    X, Y, DX, DY;

// ----- External Functions Required by ZenReject -----

extern void Status ( char * );

bool FeaturesDetected ( DoomLevel *level )
{
    int noSectors = level->SectorCount ();
    char *ptr = ( char * ) level->GetReject ();
    int bits = 8;
    int data = *ptr++;
    bool **table = new bool * [ noSectors ];
    for ( int i = 0; i < noSectors; i++ ) {
        table [i] = new bool [ noSectors ];
        for ( int j = 0; j < noSectors; j++ ) {
            table [i][j] = data & 1;
            data >>= 1;
            if ( --bits == 0 ) {
                bits = 8;
                data = *ptr++;
            }
        }
    }

    bool featureDetected = false;

    // Look for "special" features
    for ( int i = 0; i < noSectors; i++ ) {
        // Make sure each sector can see itself
        if ( table [i][i] != 0 ) {
            featureDetected = true;
            goto done;
        }
        for ( int j = i + 1; j < noSectors; j++ ) {
            // Make sure that if I can see J, then J can see I
            if ( table [i][j] != table [j][i] ) {
                featureDetected = true;
                goto done;
            }
        }
    }

done:
 
    for ( int i = 0; i < noSectors; i++ ) {
        delete [] table [i];
    }
    delete [] table;

    return featureDetected;
}

//
// Run through our rejectTable to create the actual REJECT resource
//
wReject *GetREJECT ( DoomLevel *level, bool empty, ULONG *efficiency )
{
    int noSectors = level->SectorCount ();
    int rejectSize = (( noSectors * noSectors ) + 7 ) / 8;

    char *reject = new char [ rejectSize ];
    memset ( reject, 0, rejectSize );

    if ( empty ) {
        *efficiency = 0;
        return ( wReject * ) reject;
    }

    int bits = 0, bitsToGo = 8, index = 0, hidden = 0;
    for ( int i = 0; i < noSectors; i++ ) {
        for ( int j = 0; j < noSectors; j++ ) {
            if ( rejectTable [i].sector [j] != visVisible ) {
                hidden++;
                bits = ( bits >> 1 ) | 0x80;
            } else {
                bits >>= 1;
            }

            if ( --bitsToGo == 0 ) {
                reject [ index++ ] = ( char ) bits;
                bitsToGo = 8;
            }
        }
    }
    if ( bitsToGo != 8 ) {
        reject [ index ] = ( char ) ( bits >> bitsToGo );
    }

    *efficiency = ( int ) ( 1000.0 * hidden / ( noSectors * noSectors ) + 0.5 );
    return ( wReject * ) reject;
}

void UpdateProgress ( double percentDone )
{
    char buffer [ 25 ];
    sprintf ( buffer, "REJECT: %5.1f%% done", percentDone );
    Status ( buffer );
}

void MarkVisibility ( int sector1, int sector2, eVisibility visibility )
{
    if ( rejectTable [ sector1 ].sector [ sector2 ] == visUnknown ) {
        rejectTable [ sector1 ].sector [ sector2 ] = ( char ) visibility;
    }
    if ( rejectTable [ sector2 ].sector [ sector1 ] == visUnknown ) {
        rejectTable [ sector2 ].sector [ sector1 ] = ( char ) visibility;
    }
}

void CopyVertices ( DoomLevel *level )
{
    int noVertices = level->VertexCount ();
    vertices = new sPoint [ noVertices ];
    const wVertex *vertex = level->GetVertices ();

    for ( int i = 0; i < noVertices; i++ ) {
        vertices [i].x = vertex [i].x;
        vertices [i].y = vertex [i].y;
    }
}

//
// Create lists of all the solid and see-thru lines in the map
//
bool SetupLines ( DoomLevel *level )
{
    int noLineDefs = level->LineDefCount ();

    const wLineDef *lineDef = level->GetLineDefs ();
    const wSideDef *sideDef = level->GetSideDefs ();

    indexToSolid = new sSolidLine * [ noLineDefs ];
    memset ( indexToSolid, 0, sizeof ( sSolidLine * ) * noLineDefs );

    noSolidLines   = 0;
    noSeeThruLines = 0;
    solidLines   = new sSolidLine [ noLineDefs ];
    seeThruLines = new sSeeThruLine [ noLineDefs ];

    for ( int i = 0; i < noLineDefs; i++ ) {

        sWADLine *line;
        const sPoint *vertS = &vertices [ lineDef [i].start ];
        const sPoint *vertE = &vertices [ lineDef [i].end ];

        if ( lineDef[i].flags & LDF_TWO_SIDED ) {

            int rSide = lineDef[i].sideDef [ RIGHT_SIDEDEF ];
            int lSide = lineDef[i].sideDef [ LEFT_SIDEDEF ];
            if (( lSide == NO_SIDEDEF ) || ( rSide == NO_SIDEDEF )) continue;
            if ( sideDef [ lSide ].sector == sideDef [ rSide ].sector ) continue;
            sSeeThruLine *stLine = &seeThruLines [ noSeeThruLines++ ];
            line = ( sWADLine * ) stLine;
            stLine->leftSector = sideDef [ lSide ].sector;
            stLine->rightSector = sideDef [ rSide ].sector;
            stLine->DX = vertE->x - vertS->x;
            stLine->DY = vertE->y - vertS->y;

        } else {

            indexToSolid [i] = &solidLines [ noSolidLines++ ];
            line = ( sWADLine * ) indexToSolid [i];

        }

        line->index = i;
        line->start = vertS;
        line->end = vertE;
    }

    return ( noSeeThruLines > 0 ) ? true : false;
}

//
// Mark sectors sec1 & sec2 as neighbors of each other
//
void MakeNeighbors ( sSectorStuff *sec1, sSectorStuff *sec2 )
{
    for ( int i = 0; i < sec1->noNeighbors; i++ ) {
        if ( sec1->neighbor [i] == sec2 ) return;
    }

    sec1->neighbor [ sec1->noNeighbors++ ] = sec2;
    sec2->neighbor [ sec2->noNeighbors++ ] = sec1;
}

//
// Add sec2 to sec1's list of children
//
void AddChild ( sSectorStuff *sec1, sSectorStuff *sec2 )
{
}

//
// Mark sec2 and all it's children as children of sec1
//
void MakeChild ( sSectorStuff *sec1, sSectorStuff *sec2 )
{
    for ( int i = 0; i < sec1->noNeighbors; i++ ) {
        if ( sec1->neighbor [i] != sec2 ) continue;

        // Remove sec2 from the list of neighboring sectors
        memcpy ( sec1->neighbor + i, sec1->neighbor + i + 1, sizeof ( sSectorStuff * ) * ( sec1->noNeighbors-- - i - 1 ));

        // Remove any neighboring lines from sec1's list of active lines
        for ( int j = 0; j < sec2->noLines; j++ ) {
            if ( sec2->line [j] != NULL ) {
                for ( int k = 0; k < sec1->noLines; k++ ) {
                    if ( sec1->line [k] == sec2->line [j] ) {
                        sec1->line [k] = NULL;
                        sec1->noActiveLines--;
                        break;
                    }
                }
            }
        }

        sec2->parent = sec1;
        sec1->noChildren += sec2->noChildren + 1;

        return;
    }
}

//
// Find all sectors that are children of others and move them to the child list of the parent
//
void FindChildren ( DoomLevel *level )
{
    int noSectors = level->SectorCount ();

    // Do a quick check for obvious child sectors - singletons
    bool more;
    do {
        more = false;
        for ( int i = 0; i < noSectors; i++ ) {
            if (( sector [i].parent == NULL ) && ( sector [i].noNeighbors == 1 )) {
                MakeChild ( sector [i].neighbor [0], &sector [i] );
                more = true;
            }
        }
    } while ( more );

    // Any more ideas on finding groups of children?
}

//
// Create the sector table that records all the see-thru lines related to a
//   sector and all of it's neighboring and child sectors.
//
void CreateSectorInfo ( DoomLevel *level )
{
    int noSectors  = level->SectorCount ();

    sector = new sSectorStuff [ noSectors ];
    memset ( sector, 0, sizeof ( sSectorStuff ) * noSectors );

    isChild = new bool [ noSectors ];

    // Count the number of lines for each sector first
    for ( int i = 0; i < noSeeThruLines; i++ ) {
        sSeeThruLine *stLine = &seeThruLines [ i ];
        sector [ stLine->leftSector ].noLines++;
        sector [ stLine->rightSector ].noLines++;
    }

    // Set up the line & neighbor array for each sector
    sSeeThruLine **lines = sectorLines = new sSeeThruLine * [ noSeeThruLines * 2 ];
    sSectorStuff **neighbors = neighborList = new sSectorStuff * [ noSeeThruLines * 2 ];
    for ( int i = 0; i < noSectors; i++ ) {
        sector [i].line = lines;
        sector [i].neighbor = neighbors;
        lines += sector [i].noLines;
        neighbors += sector [i].noLines;
        sector [i].noLines = 0;
    }

    // Fill in line information & mark off neighbors
    for ( int i = 0; i < noSeeThruLines; i++ ) {
        sSeeThruLine *stLine = &seeThruLines [ i ];
        sSectorStuff *sec1 = &sector [ stLine->leftSector ];
        sSectorStuff *sec2 = &sector [ stLine->rightSector ];
        sec1->line [ sec1->noLines++ ] = stLine;
        sec2->line [ sec2->noLines++ ] = stLine;
        MakeNeighbors ( sec1, sec2 );
    }

    // Start with all lines as 'active'
    for ( int i = 0; i < noSectors; i++ ) {
        sector [i].noActiveLines = sector [i].noLines;
    }

    FindChildren ( level );
}

void EliminateTrivialCases ( DoomLevel *level )
{
    int noSectors = level->SectorCount ();

    // Mark all sectors with no see-thru lines as hidden
    for ( int i = 0; i < noSectors; i++ ) {
        if ( sector [i].noLines == 0 ) {
            for ( int j = 0; j < noSectors; j++ ) {
                MarkVisibility ( i, j, visHidden );
            }
        }
    }

    // Each sector can see itself
    for ( int i = 0; i < noSectors; i++ ) {
        rejectTable [i].sector [i] = visVisible;
    }

    // Each sector can see it's immediate neighbor(s)
    for ( int i = 0; i < noSeeThruLines; i++ ) {
        sSeeThruLine *line = &seeThruLines [i];
        MarkVisibility ( line->leftSector, line->rightSector, visVisible );
    }
}

bool DontBother ( const sSeeThruLine *srcLine, const sSeeThruLine *tgtLine )
{
    if (( rejectTable [ srcLine->leftSector ].sector [ tgtLine->leftSector ] != visUnknown ) &&
        ( rejectTable [ srcLine->leftSector ].sector [ tgtLine->rightSector ] != visUnknown ) &&
        ( rejectTable [ srcLine->rightSector ].sector [ tgtLine->leftSector ] != visUnknown ) &&
        ( rejectTable [ srcLine->rightSector ].sector [ tgtLine->rightSector ] != visUnknown )) return true;

    return false;
}

void PrepareREJECT ( int noSectors )
{
    rejectTable = new sRejectRow [ noSectors ];
    for ( int i = 0; i < noSectors; i++ ) {
        rejectTable [i].sector = new char [ noSectors ];
        memset ( rejectTable [i].sector, visUnknown, sizeof ( char ) * noSectors );
    }
}

void CleanUpREJECT ( int noSectors )
{
    for ( int i = 0; i < noSectors; i++ ) {
        delete [] rejectTable [i].sector;
    }
    delete [] rejectTable;
}

void PrepareBLOCKMAP ( DoomLevel *level )
{
    blockMap = ( wBlockMap * ) level->GetBlockMap ();
    if ( blockMap == NULL ) {
        CreateBLOCKMAP ( level, true );
        blockMap = ( wBlockMap * ) level->GetBlockMap ();
    }

    USHORT *offset = ( USHORT * ) ( blockMap + 1 );
    blockMapArray = new sBlockMapArrayEntry ** [ blockMap->noRows ];
    blockMapBounds = new sBlockMapBounds [ blockMap->noRows ];
    for ( int index = 0, row = 0; row < blockMap->noRows; row++ ) {
        blockMapArray [ row ] = new sBlockMapArrayEntry * [ blockMap->noColumns ];
        blockMapBounds [ row ].lo = blockMap->noColumns;
        blockMapBounds [ row ].hi = -1;
        for ( int col = 0; col < blockMap->noColumns; col++ ) {
            USHORT *ptr = ( USHORT * ) blockMap + offset [index++];
            int i;
            for ( i = 1; ptr [i] != ( USHORT ) -1; i++ );
            sBlockMapArrayEntry *newPtr = NULL;
            if ( i > 1 ) {
                int j = 0;
                newPtr = new sBlockMapArrayEntry [i];
                for ( i = 1; ptr [i] != ( USHORT ) -1; i++ ) {
                    int line = ptr [i];
                    if ( indexToSolid [ line ] != NULL ) {
                        newPtr [j].available = &checkLine [ line ];
                        newPtr [j].line = indexToSolid [ line ];
                        j++;
                    }
                }
                if ( j == 0 ) {
                    delete [] newPtr;
                    newPtr = NULL;
                } else {
                    newPtr [j].available = NULL;
                }
            }
            blockMapArray [ row ][ col ] = newPtr;
        }
    }
}

void CleanUpBLOCKMAP ()
{
    delete [] blockMapBounds;
    for ( int row = 0; row < blockMap->noRows; row++ ) {
        for ( int col = 0; col < blockMap->noColumns; col++ ) {
            if ( blockMapArray [ row ][ col ] ) delete [] blockMapArray [ row ][ col ];
        }
        delete [] blockMapArray [ row ];
    }
    delete [] blockMapArray;
}

//
// Adjust the two line so that:
//   1) If one line bisects the other:
//      a) The bisecting line is tgt
//      b) The point farthest from src is made both start & end
//   2) tgt is on the left side of src
//   3) src and tgt go in 'opposite' directions
//
bool AdjustLinePair ( sSeeThruLine *src, sSeeThruLine *tgt, bool *swapped )
{
    *swapped = false;

start:

    // Rotate & Translate so that src lies along the +X asix
    long y1 = src->DX * ( tgt->start->y - src->start->y ) - src->DY * ( tgt->start->x - src->start->x );
    long y2 = src->DX * (  tgt->end->y  - src->start->y ) - src->DY * (  tgt->end->x  - src->start->x );

    // The two lines are co-linear and should be ignored
    if (( y1 == 0 ) && ( y2 == 0 )) return false;

    // Make sure that src doesn't bi-sect tgt
    if ((( y1 > 0 ) && ( y2 < 0 )) || (( y1 < 0 ) && ( y2 > 0 ))) {
        Swap ( *src, *tgt );
        // See if these two lines actually intersect
        if ( *swapped == true ) {
            return false;
        }
        *swapped = true;
        goto start;
    }

    // Make sure that tgt will end up on the correct (left) side
    if (( y1 <= 0 ) && ( y2 <= 0 )) {
        // Flip src
        Swap ( src->start, src->end );
        // Adjust values y1 and y2 end reflect new src
        src->DX = -src->DX;
        src->DY = -src->DY;
        y1 = -y1;
        y2 = -y2;
    }

    // See if the lines are parallel
    if ( y2 == y1 ) {
        long x1 = src->DX * ( tgt->start->x - src->start->x ) + src->DY * ( tgt->start->y - src->start->y );
        long x2 = src->DX * (  tgt->end->x  - src->start->x ) + src->DY * (  tgt->end->y  - src->start->y );
        if ( x1 < x2 ) { Swap ( tgt->start, tgt->end ); tgt->DX = -tgt->DX; tgt->DY = -tgt->DY; }
        return true;
    }

    // Now look at src from tgt's point of view
    long x1 = tgt->DX * ( src->start->y - tgt->start->y ) - tgt->DY * ( src->start->x - tgt->start->x );
    long x2 = tgt->DX * (  src->end->y  - tgt->start->y ) - tgt->DY * (  src->end->x  - tgt->start->x );

    if ( y1 == 0 ) {
        if (( x1 >= 0 ) && ( x2 <= 0 )) tgt->start = tgt->end;
        else if ( x1 < 0 ) { Swap ( tgt->start, tgt->end ); tgt->DX = -tgt->DX; tgt->DY = -tgt->DY; }
        return true;
    }
    if ( y2 == 0 ) {
        if (( x1 <= 0 ) && ( x2 >= 0 )) tgt->end = tgt->start;
        else if ( x1 < 0 ) { Swap ( tgt->start, tgt->end ); tgt->DX = -tgt->DX; tgt->DY = -tgt->DY; }
        return true;
    }

    // See if a line along tgt intersects src
    if ((( x1 < 0 ) && ( x2 > 0 )) || (( x1 > 0 ) && ( x2 < 0 ))) {
        if ( y2 > y1 ) {
            tgt->start = tgt->end;
        } else {
            tgt->end = tgt->start;
        }
    } else if (( x1 <= 0 ) && ( x2 <= 0 )) {
        Swap ( tgt->start, tgt->end ); tgt->DX = -tgt->DX; tgt->DY = -tgt->DY;
    }

    return true;
}

void UpdateRow ( int column, int row )
{
    sBlockMapBounds *bound = &blockMapBounds [ row ];
    if ( column < bound->lo ) bound->lo = column;
    if ( column > bound->hi ) bound->hi = column;
}

void DrawBlockMapLine ( const sPoint *p1, const sPoint *p2 )
{
    long x0 = p1->x - blockMap->xOrigin;
    long y0 = p1->y - blockMap->yOrigin;
    long x1 = p2->x - blockMap->xOrigin;
    long y1 = p2->y - blockMap->yOrigin;

    int startX = x0 / 128, startY = y0 / 128;
    int endX = x1 / 128, endY = y1 / 128;

    if ( startY < loRow ) loRow = startY;
    if ( startY > hiRow ) hiRow = startY;

    if ( endY < loRow ) loRow = endY;
    if ( endY > hiRow ) hiRow = endY;

    UpdateRow ( startX, startY );

    if ( startX == endX ) {
        if ( startY != endY ) { // vertical line
            int dy = (( endY - startY ) > 0 ) ? 1 : -1;
            do {
                startY += dy;
                UpdateRow ( startX, startY );
            } while ( startY != endY );
        }
    } else {
        if ( startY == endY ) { // horizontal line

            UpdateRow ( endX, startY );

        } else {                // diagonal line

            int dy = (( endY - startY ) > 0 ) ? 1 : -1;

            // Calculate the pre-scaled values to be used in the for loop
            int deltaX = ( x1 - x0 ) * 128 * dy;
            int deltaY = ( y1 - y0 ) * 128;
            int nextX = x0 * ( y1 - y0 );

            // Figure out where the 1st row ends
            switch ( dy ) {
                case -1 : nextX += ( startY * 128 - y0 ) * ( x1 - x0 );          break;
                case  1 : nextX += ( startY * 128 + 128 - y0 ) * ( x1 - x0 );    break;
            }

            int lastX = nextX / deltaY;
            UpdateRow ( lastX, startY );

            // Now do the rest using integer math - each row is a delta Y of 128
            sBlockMapBounds *bound = &blockMapBounds [ startY ];
            sBlockMapBounds *endBound = &blockMapBounds [ endY ];
            if ( x0 < x1 ) {
                for ( EVER ) {
                    // Do the next row
                    bound += dy;
                    if ( lastX < bound->lo ) bound->lo = lastX;
                    // Stop before we overshoot endX
                    if ( bound == endBound ) break;
                    nextX += deltaX;
                    lastX = nextX / deltaY;
                    if ( lastX > bound->hi ) bound->hi = lastX;
                }
            } else {
                for ( EVER ) {
                    // Do the next row
                    bound += dy;
                    if ( lastX > bound->hi ) bound->hi = lastX;
                    // Stop before we overshoot endX
                    if ( bound == endBound ) break;
                    nextX += deltaX;
                    lastX = nextX / deltaY;
                    if ( lastX < bound->lo ) bound->lo = lastX;
                }
            }

            UpdateRow ( endX, endY );
        }
    }
}

bool FindInterveningLines ( sWorldInfo *world )
{
    loRow = blockMap->noRows;
    hiRow = -1;

    // Determine boundaries for the BLOCKMAP search
    DrawBlockMapLine ( world->src->start, world->src->end );
    DrawBlockMapLine ( world->tgt->start, world->tgt->end );
    DrawBlockMapLine ( world->src->start, world->tgt->end );
    DrawBlockMapLine ( world->tgt->start, world->src->end );

    // Mark all lines that have been bounded
    int lineCount = 0;
    memset ( checkLine, true, checkLineSize );
    for ( int row = loRow; row <= hiRow; row++ ) {
        sBlockMapBounds *bound = &blockMapBounds [ row ];
        for ( int col = bound->lo; col <= bound->hi; col++ ) {
            sBlockMapArrayEntry *ptr = blockMapArray [ row ][ col ];
            if ( ptr ) do {
                world->testLines [ lineCount ] = ptr->line;
                lineCount += *ptr->available;
                *ptr->available = false;
            } while ( (++ptr)->available );
        }
        bound->lo = blockMap->noColumns;
        bound->hi = -1;
    }
    world->loIndex = 0;
    world->hiIndex = lineCount - 1;

    world->testLines [ lineCount ] = NULL;

    return ( lineCount > 0 ) ? true : false;
}

void GetBounds ( const sPoint *ss, const sPoint *se, const sPoint *ts, const sPoint *te,
                 long *loY, long *hiY, long *loX, long *hiX )
{
    if ( ss->y < se->y ) {
        if ( ts->y < te->y ) {
            *loY = ( ss->y < ts->y ) ? ss->y : ts->y;
            *hiY = ( se->y > te->y ) ? se->y : te->y;
        } else {
            *loY = ( ss->y < te->y ) ? ss->y : te->y;
            *hiY = ( se->y > ts->y ) ? se->y : ts->y;
        }
    } else {
        if ( ts->y < te->y ) {
            *loY = ( se->y < ts->y ) ? se->y : ts->y;
            *hiY = ( ss->y > te->y ) ? ss->y : te->y;
        } else {
            *loY = ( se->y < te->y ) ? se->y : te->y;
            *hiY = ( ss->y > ts->y ) ? ss->y : ts->y;
        }
    }
    if ( ss->x < se->x ) {
        if ( ts->x < te->x ) {
            *loX = ( ss->x < ts->x ) ? ss->x : ts->x;
            *hiX = ( se->x > te->x ) ? se->x : te->x;
        } else {
            *loX = ( ss->x < te->x ) ? ss->x : te->x;
            *hiX = ( se->x > ts->x ) ? se->x : ts->x;
        }
    } else {
        if ( ts->x < te->x ) {
            *loX = ( se->x < ts->x ) ? se->x : ts->x;
            *hiX = ( ss->x > te->x ) ? ss->x : te->x;
        } else {
            *loX = ( se->x < te->x ) ? se->x : te->x;
            *hiX = ( ss->x > ts->x ) ? ss->x : ts->x;
        }
    }
}

void RotatePoint ( sPoint *p, int x, int y )
{
    p->x = DX * ( x - X ) + DY * ( y - Y );
    p->y = DX * ( y - Y ) - DY * ( x - X );
}

int TrimLines ( sWorldInfo *world )
{
    long loY, hiY, loX, hiX;
    GetBounds ( world->src->start, world->src->end,
                world->tgt->start, world->tgt->end, &loY, &hiY, &loX, &hiX );

    X = world->src->start->x;
    Y = world->src->start->y;
    DX = world->tgt->end->x - world->src->start->x;
    DY = world->tgt->end->y - world->src->start->y;

    // Variables for a rotated bounding box
    sPoint p1, p2, p3;
    RotatePoint ( &p1, world->src->end->x, world->src->end->y );
    RotatePoint ( &p2, world->tgt->start->x, world->tgt->start->y );
    RotatePoint ( &p3, world->tgt->end->x, world->tgt->end->y );
    long minX = ( p1.x < 0 ) ? 0 : p1.x;
    long maxX = ( p2.x < p3.x ) ? p2.x : p3.x;
    long minY = ( p1.y < p2.y ) ? ( p1.y < p3.y ) ? p1.y : p3.y : ( p2.y < p3.y ) ? p2.y : p3.y;

    int linesLeft = 0;
    for ( int i = world->loIndex; i <= world->hiIndex; i++ ) {

        sSolidLine *line = world->testLines [i];

        // Eliminate any lines completely outside the axis aligned bounding box
        if ( line->start->y <= loY ) {
            if ( line->end->y <= loY ) {
                line->ignore = true;
                continue;
            }
        } else if ( line->start->y >= hiY ) {
            if ( line->end->y >= hiY ) {
                line->ignore = true;
                continue;
            }
        }
        if ( line->start->x >= hiX ) {
            if ( line->end->x >= hiX ) {
                line->ignore = true;
                continue;
            }
        } else if ( line->start->x <= loX ) {
            if ( line->end->x <= loX ) {
                line->ignore = true;
                continue;
            }
        }

        // Stop if we find a single line that obstructs the view completely
        if ( minX <= maxX ) {
            sPoint start, end;
            start.y = DX * ( line->start->y - Y ) - DY * ( line->start->x - X );
            if (( start.y >= 0 ) || ( start.y <= minY )) {
                end.y = DX * ( line->end->y - Y ) - DY * ( line->end->x - X );
                if ((( end.y <= minY ) && ( start.y >= 0 )) ||
                    (( end.y >= 0 ) && ( start.y <= minY ))) {
                    start.x = DX * ( line->start->x - X ) + DY * ( line->start->y - Y );
                    if (( start.x  >= minX ) && ( start.x <= maxX )) {
                        end.x = DX * ( line->end->x - X ) + DY * ( line->end->y - Y );
                        if (( end.x >= minX ) && ( end.x <= maxX )) {
                            return -1;
                        }
                    }
                // Use the new information and see if line is outside the bounding box
                } else if ((( end.y >= 0 ) && ( start.y >= 0 )) || (( end.y <= minY ) && ( start.y <= minY ))) {
                    line->ignore = true;
                    continue;
                }
            }
        }

        line->ignore = false;
        linesLeft++;

    }

    if ( linesLeft == 0 ) return 0;

    // Eliminate lines that touch the src/tgt lines but are not in view
    int x1  = world->src->start->x;
    int y1  = world->src->start->y;
    int dx1 = world->src->end->x - world->src->start->x;
    int dy1 = world->src->end->y - world->src->start->y;

    int x2  = world->tgt->start->x;
    int y2  = world->tgt->start->y;
    int dx2 = world->tgt->end->x - world->tgt->start->x;
    int dy2 = world->tgt->end->y - world->tgt->start->y;

    for ( int i = world->loIndex; i <= world->hiIndex; i++ ) {
        sSolidLine *line = world->testLines [i];
        if ( line->ignore ) continue;
        int y = 1;
        if (( line->start == world->src->start ) || ( line->start == world->src->end )) {
            y = dx1 * ( line->end->y - y1 ) - dy1 * ( line->end->x - x1 );
        } else if (( line->end == world->src->start ) || ( line->end == world->src->end )) {
            y = dx1 * ( line->start->y - y1 ) - dy1 * ( line->start->x - x1 );
        } else if (( line->start == world->tgt->start ) || ( line->start == world->tgt->end )) {
            y = dx2 * ( line->end->y - y2 ) - dy2 * ( line->end->x - x2 );
        } else if (( line->end == world->tgt->start ) || ( line->end == world->tgt->end )) {
            y = dx2 * ( line->start->y - y2 ) - dy2 * ( line->start->x - x2 );
        }
        if ( y < 0 ) {
            line->ignore = true;
            linesLeft--;
        }
    }

    if ( linesLeft > 0 ) {
        while ( world->testLines [ world->loIndex ]->ignore ) {
            world->loIndex++;
            if ( world->loIndex >= world->hiIndex ) break;
        }
        while ( world->testLines [ world->hiIndex ]->ignore ) {
            world->hiIndex--;
            if ( world->loIndex >= world->hiIndex ) break;
        }
    }

    return linesLeft;
}

//
// Find out which side of the poly-line the line is on
//
//  Return Values:
//      1 - above ( not completely below ) the poly-line
//      0 - intersects the poly-line
//     -1 - below the poly-line ( one or both end-points may touch the poly-line )
//     -2 - can't tell start this segment
//

int Intersects ( const sPoint *p1, const sPoint *p2, const sPoint *t1, const sPoint *t2 )
{
    long DX, DY, y1, y2;

    // Rotate & translate using p1->p2 as the +X-axis
    DX = p2->x - p1->x;
    DY = p2->y - p1->y;

    y1 = DX * ( t1->y - p1->y ) - DY * ( t1->x - p1->x );
    y2 = DX * ( t2->y - p1->y ) - DY * ( t2->x - p1->x );

    // Eliminate the 2 easy cases ( t1 & t2 both above or below the x-axis )
    if (( y1 > 0 ) && ( y2 > 0 )) return 1;
    if (( y1 <= 0 ) && ( y2 <= 0 )) return -1;
    // t1->t2 crosses poly-Line segment ( or one point touches it and the other is above it )

    // Rotate & translate using t1->t2 as the +X-axis
    DX = t2->x - t1->x;
    DY = t2->y - t1->y;

    y1 = DX * ( p1->y - t1->y ) - DY * ( p1->x - t1->x );
    y2 = DX * ( p2->y - t1->y ) - DY * ( p2->x - t1->x );

    // Eliminate the 2 easy cases ( p1 & p2 both above or below the x-axis )
    if (( y1 > 0 ) && ( y2 > 0 )) return -2;
    if (( y1 < 0 ) && ( y2 < 0 )) return -2;

    return 0;
}

int FindSide ( sWADLine *line, sPolyLine *poly )
{
    bool completelyBelow = true;
    for ( int i = 0; i < poly->noPoints - 1; i++ ) {
        const sPoint *p1 = poly->point [i];
        const sPoint *p2 = poly->point [i+1];
        switch ( Intersects ( p1, p2, line->start, line->end )) {
            case -1 : break;
            case  0 : return 0;
            case -2 :
            case  1 : completelyBelow = false;
        }
    }
    return completelyBelow ? -1 : 1;
}

void AddToPolyLine ( sPolyLine *poly, sSolidLine *line )
{
    long DX, DY, y1, y2;

    y1 = 0;

    // Find new index start from the 'left'
    int i;
    for ( i = 0; i < poly->noPoints - 1; i++ ) {
        const sPoint *p1 = poly->point [i];
        const sPoint *p2 = poly->point [i+1];
        DX = p2->x - p1->x;
        DY = p2->y - p1->y;

        y1 = DX * ( line->start->y - p1->y ) - DY * ( line->start->x - p1->x );
        y2 = DX * ( line->end->y - p1->y ) - DY * ( line->end->x - p1->x );
        if (( y1 > 0 ) != ( y2 > 0 )) break;
    }
    i += 1;

    // Find new index start from the 'right'
    int j;
    for ( j = poly->noPoints - 1; j > i; j-- ) {
        const sPoint *p1 = poly->point [j-1];
        const sPoint *p2 = poly->point [j];
        DX = p2->x - p1->x;
        DY = p2->y - p1->y;

        long y1 = DX * ( line->start->y - p1->y ) - DY * ( line->start->x - p1->x );
        long y2 = DX * ( line->end->y - p1->y ) - DY * ( line->end->x - p1->x );
        if (( y1 > 0 ) != ( y2 > 0 )) break;
    }

    int ptsRemoved = j - i;
    int toCopy = poly->noPoints - j;
    if ( toCopy ) memmove ( &poly->point [i+1], &poly->point [j], sizeof ( sPoint * ) * toCopy );
    poly->noPoints += 1 - ptsRemoved;

    poly->point [i] = ( y1 > 0 ) ? line->start : line->end;
    poly->lastPoint = i;
}

bool PolyLinesCross ( sPolyLine *upper, sPolyLine *lower )
{
    bool foundAbove = false, ambiguous = false;
    int last = 0, max = upper->noPoints - 1;
    if ( upper->lastPoint != -1 ) {
        max = 2;
        last = upper->lastPoint - 1;
    }
    for ( int i = 0; i < max; i++ ) {
        const sPoint *p1 = upper->point [ last + i ];
        const sPoint *p2 = upper->point [ last + i + 1 ];
        for ( int j = 0; j < lower->noPoints - 1; j++ ) {
            const sPoint *p3 = lower->point [j];
            const sPoint *p4 = lower->point [j+1];
            switch ( Intersects ( p1, p2, p3, p4 )) {
                case  1 : foundAbove = true;
                          break;
                case  0 : return true;
                case -2 : ambiguous = true;
                          break;
            }
        }
    }

    if ( foundAbove ) return false;

    if ( ambiguous ) {
        const sPoint *p1 = upper->point [0];
        const sPoint *p2 = upper->point [ upper->noPoints - 1 ];
        long DX = p2->x - p1->x;
        long DY = p2->y - p1->y;
        for ( int i = 1; i < lower->noPoints - 1; i++ ) {
            const sPoint *testPoint = lower->point [i];
            if ( DX * ( testPoint->y - p1->y ) - DY * ( testPoint->x - p1->x ) < 0 ) return true;
        }
    }

    return false;
}

bool CorrectForNewStart ( sPolyLine *poly )
{
    const sPoint *p0 = poly->point [0];
    for ( int i = poly->noPoints - 1; i > 1; i-- ) {
        const sPoint *p1 = poly->point [i];
        const sPoint *p2 = poly->point [i-1];
        long dx = p1->x - p0->x;
        long dy = p1->y - p0->y;
        long y = dx * ( p2->y - p0->y ) - dy * ( p2->x - p0->x );
        if ( y < 0 ) {
            poly->point [i-1] = p0;
            poly->point += i - 1;
            poly->noPoints -= i - 1;
            poly->lastPoint -= i - 1;
            return true;
        }
    }
    return false;
}

bool CorrectForNewEnd ( sPolyLine *poly )
{
    const sPoint *p0 = poly->point [ poly->noPoints - 1 ];
    for ( int i = 0; i < poly->noPoints - 2; i++ ) {
        const sPoint *p1 = poly->point [i];
        const sPoint *p2 = poly->point [i+1];
        long dx = p0->x - p1->x;
        long dy = p0->y - p1->y;
        long y = dx * ( p2->y - p1->y ) - dy * ( p2->x - p1->x );
        if ( y < 0 ) {
            poly->point [i+1] = p0;
            poly->noPoints -= poly->noPoints - i - 2;
            return true;
        }
    }
    return false;
}

bool AdjustEndPoints ( sSeeThruLine *left, sSeeThruLine *right, sPolyLine *upper, sPolyLine *lower )
{
    if ( upper->lastPoint == -1 ) return true;
    const sPoint *test = upper->point [ upper->lastPoint ];

    long dx, dy, y;
    bool changed = false;

    dx = test->x - left->hiPoint->x;
    dy = test->y - left->hiPoint->y;
    y = dx * ( right->hiPoint->y - left->hiPoint->y ) -
        dy * ( right->hiPoint->x - left->hiPoint->x );
    if ( y > 0 ) {
        long num = ( right->start->y - left->hiPoint->y ) * dx -
                   ( right->start->x - left->hiPoint->x ) * dy;
        long det = right->DX * dy - right->DY * dx;
        REAL t = ( REAL ) num / ( REAL ) det;
        if ( t <= right->lo ) return false;
        if ( t < right->hi ) {
            right->hi = t;
            right->hiPoint->x = right->start->x + ( long ) ( t * right->DX );
            right->hiPoint->y = right->start->y + ( long ) ( t * right->DY );
            changed |= CorrectForNewStart ( upper );
        }
    }

    dx = test->x - right->loPoint->x;
    dy = test->y - right->loPoint->y;
    y = dx * ( left->loPoint->y - right->loPoint->y ) -
        dy * ( left->loPoint->x - right->loPoint->x );
    if ( y < 0 ) {
        long num = ( left->start->y - right->loPoint->y ) * dx -
                   ( left->start->x - right->loPoint->x ) * dy;
        long det = left->DX * dy - left->DY * dx;
        REAL t = ( REAL ) num / ( REAL ) det;
        if ( t >= left->hi ) return false;
        if ( t > left->lo ) {
            left->lo = t;
            left->loPoint->x = left->start->x + ( long ) ( t * left->DX );
            left->loPoint->y = left->start->y + ( long ) ( t * left->DY );
            changed |= CorrectForNewEnd ( upper );
        }
    }

    return ( changed && PolyLinesCross ( upper, lower )) ? false : true;
}

bool FindPolyLines ( sWorldInfo *world )
{
    sPolyLine *upperPoly = &world->upperPoly;
    sPolyLine *lowerPoly = &world->lowerPoly;

    for ( EVER ) {

        bool done = true;
        bool stray = false;

        for ( int i = world->loIndex; i <= world->hiIndex; i++ ) {

            sSolidLine *line = world->testLines [ i ];
            if ( line->ignore ) continue;

            switch ( FindSide ( line, lowerPoly )) {

                case  1 : // Completely above the lower/right poly-Line
                    switch ( FindSide ( line, upperPoly )) {

                        case  1 : // Line is between the two poly-lines
                            stray = true;
                            break;

                        case  0 : // Intersects the upper/left poly-Line
                            if ( stray ) done = false;
                            AddToPolyLine ( upperPoly, line );
                            if (( lowerPoly->noPoints > 2 ) && ( PolyLinesCross ( upperPoly, lowerPoly ))) {
                                return false;
                            }
                            if ( AdjustEndPoints ( world->src, world->tgt, upperPoly, lowerPoly ) == false ) {
                                return false;
                            }
                        case -1 : // Completely above the upper/left poly-line
                            line->ignore = true;
                            break;

                    }
                    break;

                case  0 : // Intersects the lower/right poly-Line
                    if ( stray ) done = false;
                    AddToPolyLine ( lowerPoly, line );
                    if ( PolyLinesCross ( lowerPoly, upperPoly )) {
                        return false;
                    }
                    if ( AdjustEndPoints ( world->tgt, world->src, lowerPoly, upperPoly ) == false ) {
                        return false;
                    }
                case -1 : // Completely below the lower/right poly-Line
                    line->ignore = true;
                    break;

            }
        }

        if ( done ) break;

        while ( world->testLines [ world->loIndex ]->ignore ) {
            world->loIndex++;
            if ( world->loIndex >= world->hiIndex ) break;
        }
        while ( world->testLines [ world->hiIndex ]->ignore ) {
            world->hiIndex--;
            if ( world->loIndex >= world->hiIndex ) break;
        }
    }

    return true;
}

bool FindObstacles ( sWorldInfo *world )
{
    if ( world->hiIndex < world->loIndex ) return false;

    // If we have an unbroken line between src & tgt there is a direct LOS
    if ( world->upperPoly.noPoints == 2 ) return false;
    if ( world->lowerPoly.noPoints == 2 ) return false;

    // To be absolutely correct, we should create a list of obstacles
    // (ie: connected lineDefs completely enclosed by upperPoly & lowerPoly)
    // and see if any of them completely block the LOS
 
    return false;
}

void InitializeWorld ( sWorldInfo *world, sSeeThruLine *src, sSeeThruLine *tgt )
{
    world->src = src;
    world->tgt = tgt;
    world->testLines = testLines;

    static sPoint p1, p2, p3, p4;
    p1 = *src->start;
    p2 = *src->end;
    p3 = *tgt->start;
    p4 = *tgt->end;

    src->loPoint = &p1;    src->lo = 0.0;
    src->hiPoint = &p2;    src->hi = 1.0;
    tgt->loPoint = &p3;    tgt->lo = 0.0;
    tgt->hiPoint = &p4;    tgt->hi = 1.0;

    sPolyLine *lowerPoly = &world->lowerPoly;
    lowerPoly->point     = polyPoints;
    lowerPoly->noPoints  = 2;
    lowerPoly->lastPoint = -1;
    lowerPoly->point [0] = src->hiPoint;
    lowerPoly->point [1] = tgt->loPoint;

    sPolyLine *upperPoly = &world->upperPoly;
    upperPoly->point     = &polyPoints [ noSolidLines + 2 ];
    upperPoly->noPoints  = 2;
    upperPoly->lastPoint = -1;
    upperPoly->point [0] = tgt->hiPoint;
    upperPoly->point [1] = src->loPoint;
}

bool CheckLOS ( sSeeThruLine *src, sSeeThruLine *tgt )
{
    sWorldInfo myWorld;
    InitializeWorld ( &myWorld, src, tgt );

    // See if there are any solid lines in the blockmap region between src & tgt
    if ( FindInterveningLines ( &myWorld ) == true ) {

       // Do a more refined check to see if there are any lines
        switch ( TrimLines ( &myWorld )) {

            case -1 :		// A single line completely blocks the view
                return false;

            case 0 :		// No intervening lines left - end check
                break;

            default :
                // Do an even more refined check
                if ( FindPolyLines ( &myWorld ) == false ) return false;
                // Now see if there are any obstacles that may block the LOS
                if ( FindObstacles ( &myWorld ) == true ) return false;
        }
    }

    return true;
}

bool DivideRegion ( const sSeeThruLine *srcLine, const sSeeThruLine *tgtLine, bool swapped, sSeeThruLine *src, sSeeThruLine *tgt )
{
    // Find the two end-points for tgt
    const sPoint *nearPoint;
    const sPoint *farPoint = tgt->end;
    if ( swapped ) {
        if ( tgt->end != srcLine->end ) {
            tgt->DX = -tgt->DX;
            tgt->DY = -tgt->DY;
            nearPoint = srcLine->end;
        } else {
            nearPoint = srcLine->start;
        }
    } else {
        if ( tgt->end != tgtLine->end ) {
            tgt->DX = -tgt->DX;
            tgt->DY = -tgt->DY;
            nearPoint = tgtLine->end;
        } else {
            nearPoint = tgtLine->start;
        }
    }

    // Find the point of intersection on src
    long dx = nearPoint->x - farPoint->x;
    long dy = nearPoint->y - farPoint->y;
    long num = ( src->start->y - farPoint->y ) * dx - ( src->start->x - farPoint->x ) * dy;
    long det = src->DX * dy - src->DY * dx;
    REAL t = ( REAL ) num / ( REAL ) det;

    sPoint crossPoint (( long ) ( src->start->x + t * src->DX ), ( long ) ( src->start->y + t * src->DY ));
    sSeeThruLine newSrc = *src;

    newSrc.end = &crossPoint;
    tgt->start = nearPoint;
    tgt->end = farPoint;

    bool isVisible = CheckLOS ( &newSrc, tgt );
    if ( isVisible == false ) {
        newSrc.start = &crossPoint;
        newSrc.end = src->end;
        tgt->start = farPoint;
        tgt->end = nearPoint;
        tgt->DX = -tgt->DX;
        tgt->DY = -tgt->DY;
        isVisible = CheckLOS ( &newSrc, tgt );
    }

    return isVisible;
}

bool TestLinePair ( const sSeeThruLine *srcLine, const sSeeThruLine *tgtLine )
{
    if ( DontBother ( srcLine, tgtLine )) {
        return false;
    }

    bool swapped;
    sSeeThruLine src = *srcLine;
    sSeeThruLine tgt = *tgtLine;

    if ( AdjustLinePair ( &src, &tgt, &swapped ) == false ) {
        return false;
    }

    // See if one line bisects the other
    if ( tgt.start == tgt.end ) {
        return DivideRegion ( srcLine, tgtLine, swapped, &src, &tgt );
    }

    return CheckLOS ( &src, &tgt );
}

//----------------------------------------------------------------------------
//  Sort sectors so the the largest (sector containing the most sectors) is
//    placed first in the list.
//----------------------------------------------------------------------------
int SortSector ( const void *ptr1, const void *ptr2 )
{
    const sSectorStuff *sec1 = ( const sSectorStuff * ) ptr1;
    const sSectorStuff *sec2 = ( const sSectorStuff * ) ptr2;

    // Favor number of children
    if ( sec1->noChildren != sec2->noChildren ) {
        return sec2->noChildren - sec1->noChildren;
    }
    // Favor sectors without a parent next
    if (( sec1->parent != NULL ) != ( sec2->parent != NULL )) {
        return ( sec1->parent != NULL ) ? 1 : -1;
    }
    // Favor the sector with the most visible lines
    if ( sec2->noActiveLines != sec1->noActiveLines ) {
        return sec2->noActiveLines - sec1->noActiveLines;
    }

    // It's a tie - use the sector index - lower index favored
    return sec1 - sec2;
}

//----------------------------------------------------------------------------
// Create a mapping for see-thru lines that tries to put lines that affect
//   the most sectors first.  As more sectors are marked visible/hidden, the
//   number of remaining line pairs that must be checked drops.  By ordering
//   the lines, we can speed things up quite a bit for just a little effort.
//----------------------------------------------------------------------------
int SetupLineMap ( int *lineMap, int noSectors )
{
    // Try to order lines to maximize our chances of culling child sectors
    sSectorStuff *sectorCopy = new sSectorStuff [ noSectors ];
    memcpy ( sectorCopy, sector, sizeof ( sSectorStuff ) * noSectors );

    qsort ( sectorCopy, noSectors, sizeof ( sSectorStuff ), SortSector );

    int maxIndex = 0;
    for ( int i = 0; i < noSectors; i++ ) {
        for ( int j = 0; j < sectorCopy [i].noLines; j++ ) {
            if ( sectorCopy [i].line [j] == NULL ) continue;
            int lineIndex = sectorCopy [i].line [j] - seeThruLines;
            for ( int k = 0; k < maxIndex; k++ ) {
                if ( lineMap [k] == lineIndex ) goto next;
            }
            lineMap [ maxIndex++ ] = lineIndex;
        next:
            ; // Microsoft compiler is acting up
        }
    }

    delete [] sectorCopy;

    return maxIndex;
}

void LineComplete ( int sectorIndex, sSeeThruLine *line, int noSectors )
{
    sSectorStuff *sec = &sector [ sectorIndex ];

    // Don't waste time if we've already marked this sector as hidden
    if (( sec->noActiveLines == 0 ) || ( sec->noChildren == 0 )) return;

    for ( int i = 0; i < sec->noLines; i++ ) {
        if ( sec->line [i] == line ) {
            sec->line [i] = NULL;
            // If we've looked at all the active see-thru lines for a sector,
            //   then anything that isn't already visible is hidden!
            if ( --sec->noActiveLines == 0 ) {

                // Make a list of all children of this sector
                if ( sec->noChildren > 0 ) {
                    for ( int j = 0; j < noSectors; j++ ) {
                        sSectorStuff  *parent = sector [j].parent;
                        while (( parent != NULL ) && ( parent != sec )) parent = parent->parent;
                        isChild [j] = ( parent != NULL ) ? true : false;
                    }
                }

                for ( int j = 0; j < noSectors; j++ ) {
                    // Don't mark sectors we already know are visible
                    if ( rejectTable [ sectorIndex ].sector [ j ] != visUnknown ) continue;

                    // Don't mark children as hidden to the parent
                    if ( isChild [j] ) continue;

                    // Mark this sector as invisible to the rest of the world
                    MarkVisibility ( sectorIndex, j, visHidden );

                    // Mark all of it's children as invisible to the rest of the world too
                    if ( sec->noChildren > 0 ) {
                        for ( int k = 0; k < noSectors; k++ ) {
                            if ( isChild [k] == false ) continue;
                            MarkVisibility ( k, j, visHidden );
                        }
                    }
                }
            }
            return;
        }
    }
}

bool CreateREJECT ( DoomLevel *level, bool empty, bool force, ULONG *efficiency )
{
    if (( force == false ) && ( FeaturesDetected ( level ) == true )) {
        return true;
    }

    int noSectors = level->SectorCount ();
    bool saveBits = level->hasChanged () ? false : true;
    if ( empty ) {
        level->NewReject ((( noSectors * noSectors ) + 7 ) / 8, GetREJECT ( level, true, efficiency ), saveBits );
        return false;
    }

    PrepareREJECT ( noSectors );
    CopyVertices ( level );

    // Make sure we have something worth doing
    if ( SetupLines ( level )) {

        // Make a list of which sectors contain others and their boundary lines
        CreateSectorInfo ( level );

        // Mark the easy ones visible to speed things up later
        EliminateTrivialCases ( level );

        int noLineDefs = level->LineDefCount ();

        checkLineSize = sizeof ( bool ) * noLineDefs;
        checkLine     = new bool [ noLineDefs ];
        testLines     = new sSolidLine * [ noSolidLines ];
        polyPoints    = new const sPoint * [ 2 * ( noSolidLines + 2 )];

        // Create a map to reorder lines more efficiently
        int *lineMap = new int [ noSeeThruLines ];
        int lineMapSize = SetupLineMap ( lineMap, noSectors );

        // Set up a scaled BLOCKMAP type structure
        PrepareBLOCKMAP ( level );

        int done = 0, total = noSeeThruLines * ( noSeeThruLines - 1 ) / 2;
        double nextProgress = 0.0;

        // Now the tough part: check all lines against each other
        for ( int i = 0; i < lineMapSize; i++ ) {
            sSeeThruLine *srcLine = &seeThruLines [ lineMap [ i ]];
            for ( int j = lineMapSize - 1; j > i; j-- ) {
                sSeeThruLine *tgtLine = &seeThruLines [ lineMap [ j ]];
                if ( TestLinePair ( srcLine, tgtLine ) == true ) {
                    // There is a direct LOS between the two lines - mark all affected sectors
                    MarkVisibility ( srcLine->leftSector, tgtLine->leftSector, visVisible );
                    MarkVisibility ( srcLine->leftSector, tgtLine->rightSector, visVisible );
                    MarkVisibility ( srcLine->rightSector, tgtLine->leftSector, visVisible );
                    MarkVisibility ( srcLine->rightSector, tgtLine->rightSector, visVisible );
                }
            }

            // Mark this line as complete for the surrounding sectors
            LineComplete ( srcLine->leftSector, srcLine, noSectors );
            LineComplete ( srcLine->rightSector, srcLine, noSectors );

            // Update the progress indicator to let the user know we're not hung
            done += lineMapSize - ( i + 1 );
            double progress = ( 100.0 * done ) / total;
            if ( progress >= nextProgress ) {
                UpdateProgress ( progress );
                nextProgress = progress + 0.1;
            }
        }

        CleanUpBLOCKMAP ();

        // Clean up allocations we made
        delete [] lineMap;
        delete [] polyPoints;
        delete [] testLines;
        delete [] checkLine;

        // Clean up allocations made by CreateSectorInfo
        delete [] neighborList;
        delete [] sectorLines;
        delete [] isChild;
        delete [] sector;

    }

    level->NewReject ((( noSectors * noSectors ) + 7 ) / 8, GetREJECT ( level, false, efficiency ), saveBits );

    // Clean up allocations made by SetupLines
    delete [] solidLines;
    delete [] seeThruLines;
    delete [] indexToSolid;

    // Delete our local copy of the vertices
    delete [] vertices;

    // Finally, release our reject table data
    CleanUpREJECT ( noSectors );

    return false;
}
