//----------------------------------------------------------------------------
//
// File:        ZenReject-new-2.cpp
// Date:        15-Dec-1995
// Programmer:  Marc Rousseau
//
// Description: This module contains the logic for the REJECT builder.
//
// Copyright (c) 1995-2002 Marc Rousseau, All Rights Reserved.
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
//   04-01-01	Added code to use graphs to reduce LOS calculations (way faster!)
//
//----------------------------------------------------------------------------

#include <assert.h>
#include <math.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.hpp"
#include "level.hpp"
#include "ZenNode.hpp"
#include "geometry.hpp"

// ----- Local enum/structure definitions -----

enum eVisibility {
    visUnknown,
    visVisible,
    visHidden
};

struct sRejectRow {
    char          *sector;
};

struct sMapLine {
    int            index;
    const sPoint  *start;
    const sPoint  *end;
};

struct sSolidLine : sMapLine {
    bool           ignore;
};

struct sTransLine : sMapLine {
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

struct sLineSet {
    sSolidLine   **lines;
    int            loIndex;
    int            hiIndex;
};

struct sWorldInfo {
    sTransLine    *src;
    sTransLine    *tgt;
    sLineSet       solidSet;
    sPolyLine      upperPoly;
    sPolyLine      lowerPoly;
};

struct sBlockMapBounds {
    int            lo;
    int            hi;
};

struct sBlockMapArrayEntry {
    bool          *available;
    sSolidLine    *line;
};

struct sGraph;

struct sSectorStuff {
    int            index;
    int            noLines;
    int            noActiveLines;
    sTransLine   **line;
    int            noNeighbors;
    int            noActiveNeighbors;
    sSectorStuff **neighbor;
    int            noChildren;
    sSectorStuff  *parent;

    bool           isComplete;
    bool           isKey;
    int            metric;
    sGraph        *baseGraph;

    sGraph        *graph;
    sSectorStuff  *graphParent;
    bool           isArticulation;
    int            loDFS;
    int            hiDFS;
    int            minReachable;
};

struct sGraph {
    int            noSectors;
    sSectorStuff **sector;
};

struct sGraphTable {
    int            noGraphs;
    sGraph        *graph;
    sSectorStuff **sectorStart;
    sSectorStuff **sectorPool;
};

static sGraphTable    graphTable;

static int            loRow;
static int            hiRow;

static wBlockMap             *blockMap;
static sBlockMapBounds       *blockMapBounds;
static sBlockMapArrayEntry ***blockMapArray;

static sRejectRow    *rejectTable;

static sPoint        *vertices;
static int            noSolidLines;
static sSolidLine    *solidLines;
static int            noTransLines;
static sTransLine    *transLines;

static sTransLine   **sectorLines;
static sSectorStuff **neighborList;
static bool          *isChild;

static int            checkLineSize;
static bool          *checkLine;
static sSolidLine   **indexToSolid;
static sSolidLine   **testLines;
static const sPoint **polyPoints;

static int          **distance;

static long    X, Y, DX, DY;

bool FeaturesDetected ( DoomLevel *level )
{
    char *ptr = ( char * ) level->GetReject ();
    if ( ptr == NULL ) return false;

    int noSectors = level->SectorCount ();

    // Make sure it's a valid REJECT structure before analyzing it
    int rejectSize = (( noSectors * noSectors ) + 7 ) / 8;
    if ( level->RejectSize () != rejectSize ) return false;

    int bits = 9;
    int data = *ptr++;
    bool **table = new bool * [ noSectors ];
    for ( int i = 0; i < noSectors; i++ ) {
        table [i] = new bool [ noSectors ];
        for ( int j = 0; j < noSectors; j++ ) {
            if ( --bits == 0 ) {
                bits = 8;
                data = *ptr++;
            }
            table [i][j] = data & 1;
            data >>= 1;
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
    sprintf ( buffer, "REJECT - %5.1f%% done", percentDone );
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

    checkLineSize = sizeof ( bool ) * noLineDefs;
    checkLine     = new bool [ noLineDefs ];

    indexToSolid = new sSolidLine * [ noLineDefs ];
    memset ( indexToSolid, 0, sizeof ( sSolidLine * ) * noLineDefs );

    noSolidLines   = 0;
    noTransLines = 0;
    solidLines   = new sSolidLine [ noLineDefs ];
    transLines = new sTransLine [ noLineDefs ];

    for ( int i = 0; i < noLineDefs; i++ ) {

        sMapLine *line;
        const sPoint *vertS = &vertices [ lineDef [i].start ];
        const sPoint *vertE = &vertices [ lineDef [i].end ];

        // We can't handle 0 length lineDefs!
        if ( vertS == vertE ) continue;

        if ( lineDef [i].flags & LDF_TWO_SIDED ) {

            int rSide = lineDef [i].sideDef [ RIGHT_SIDEDEF ];
            int lSide = lineDef [i].sideDef [ LEFT_SIDEDEF ];
            if (( lSide == NO_SIDEDEF ) || ( rSide == NO_SIDEDEF )) continue;
            if ( sideDef [ lSide ].sector == sideDef [ rSide ].sector ) continue;
            sTransLine *stLine = &transLines [ noTransLines++ ];
            line = ( sMapLine * ) stLine;
            stLine->leftSector  = sideDef [ lSide ].sector;
            stLine->rightSector = sideDef [ rSide ].sector;
            stLine->DX = vertE->x - vertS->x;
            stLine->DY = vertE->y - vertS->y;

        } else {

            indexToSolid [i] = &solidLines [ noSolidLines++ ];
            line = ( sMapLine * ) indexToSolid [i];

        }

        line->index = i;
        line->start = vertS;
        line->end   = vertE;
    }

    return ( noTransLines > 0 ) ? true : false;
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
// Mark sec2 and all it's children as children of sec1
//
bool MakeChild ( sSectorStuff *sec1, sSectorStuff *sec2 )
{
    for ( int i = 0; i < sec1->noActiveNeighbors; i++ ) {

        if ( sec1->neighbor [i] != sec2 ) continue;

        // Remove sec2 from the list of active neighboring sectors
        sec1->noActiveNeighbors--;
        sec1->neighbor [i] = sec1->neighbor [sec1->noActiveNeighbors];
        sec1->neighbor [sec1->noActiveNeighbors] = sec2;

        // Remove any neighboring lines from sec1's list of active lines
        for ( int j = 0; j < sec2->noActiveLines; j++ ) {
            if ( sec2->line [j] != NULL ) {
                for ( int k = 0; k < sec1->noActiveLines; k++ ) {
                    if ( sec1->line [k] == sec2->line [j] ) {
                        sec1->noActiveLines--;
                        sec1->line [k] = sec1->line [sec1->noActiveLines];
                        sec1->line [sec1->noActiveLines] = sec2->line [j];
                        break;
                    }
                }
            }
        }

        sec2->parent = sec1;
        sec1->noChildren += sec2->noChildren + 1;

        return true;
    }

    return false;
}

//
// Find all sectors that are children of others and move them to the child list of the parent
//
void FindChildren ( sSectorStuff *sector, int noSectors )
{
    // Do a quick check for obvious child sectors - singletons
    bool more;
    do {
        more = false;
        for ( int i = 0; i < noSectors; i++ ) {
            if (( sector [i].parent == NULL ) && ( sector [i].noActiveNeighbors == 1 )) {
                sSectorStuff *parent = sector [i].neighbor [0];
                if ( MakeChild ( parent, &sector [i] ) == false ) continue;
                if (( parent->noActiveNeighbors == 1 ) && ( parent < &sector [i] )) {
                    more = true;
                }
            }
        }
    } while ( more );

    // Any more ideas on finding groups of children?
}

//
// Create the sector table that records all the see-thru lines related to a
//   sector and all of it's neighboring and child sectors.
//
sSectorStuff *CreateSectorInfo ( DoomLevel *level )
{
    Status ( "Gathering sector information..." );

    int noSectors  = level->SectorCount ();

    sSectorStuff *sector = new sSectorStuff [ noSectors ];
    memset ( sector, 0, sizeof ( sSectorStuff ) * noSectors );

    isChild = new bool [ noSectors ];

    // Count the number of lines for each sector first
    for ( int i = 0; i < noTransLines; i++ ) {
        sTransLine *line = &transLines [ i ];
        sector [ line->leftSector ].noLines++;
        sector [ line->rightSector ].noLines++;
    }

    // Set up the line & neighbor array for each sector
    sTransLine **lines = sectorLines = new sTransLine * [ noTransLines * 2 ];
    sSectorStuff **neighbors = neighborList = new sSectorStuff * [ noTransLines * 2 ];
    for ( int i = 0; i < noSectors; i++ ) {
        sector [i].index    = i;
        sector [i].line     = lines;
        sector [i].neighbor = neighbors;
        lines += sector [i].noLines;
        neighbors += sector [i].noLines;
        sector [i].noLines  = 0;
    }

    // Fill in line information & mark off neighbors
    for ( int i = 0; i < noTransLines; i++ ) {
        sTransLine *line = &transLines [ i ];
        sSectorStuff *sec1 = &sector [ line->leftSector ];
        sSectorStuff *sec2 = &sector [ line->rightSector ];
        sec1->line [ sec1->noLines++ ] = line;
        sec2->line [ sec2->noLines++ ] = line;
        MakeNeighbors ( sec1, sec2 );
    }

    // Start with all lines & neighbors as 'active'
    for ( int i = 0; i < noSectors; i++ ) {
        sector [i].noActiveLines     = sector [i].noLines;
        sector [i].noActiveNeighbors = sector [i].noNeighbors;
    }

    return sector;
}

void CreateDistanceTable ( sSectorStuff *sector, int noSectors )
{
    Status ( "Calculating sector distances..." );

    char *distBuffer  = new char [ sizeof ( int ) * noSectors * noSectors + sizeof ( int * ) * noSectors ];
    distance = ( int ** ) distBuffer;
    distBuffer += sizeof ( int * ) * noSectors;

    int listRowSize = noSectors + 2;

    USHORT *listBuffer = new USHORT [ listRowSize * noSectors * 2 ];
    memset ( listBuffer, 0, sizeof ( USHORT ) * listRowSize * noSectors * 2 );

    USHORT *list [2] = { listBuffer, listBuffer + listRowSize * noSectors };

    for ( int i = 0; i < noSectors; i++ ) {
        distance [i] = ( int * ) distBuffer;
        distBuffer += sizeof ( int ) * noSectors;
        // Set up the initial distances
        for ( int j = 0; j < noSectors; j++ ) {
            distance [i][j] = 0x7FFFFFFF;
        }
        // Prime the first list
        list [0][i*listRowSize+0] = ( USHORT ) i;
        list [0][i*listRowSize+1] = ( USHORT ) i;
        list [0][i*listRowSize+2+i] = true;
    }

    int currIndex = 0, length = 0;
    int loRow = 0, hiRow = noSectors - 1;

    int count;

    // Find the # of sectors between each pair of sectors
    do {
        count = 0;

        int nextIndex = ( currIndex + 1 ) % 2;
        USHORT *currList = list [currIndex] + 2 + loRow * listRowSize;
        USHORT *nextList = list [nextIndex] + 2 + loRow * listRowSize;
        currIndex = nextIndex;

        int i = loRow, max = hiRow;
        loRow = noSectors;
        hiRow = 0;

        for ( ; i <= max; i++ ) {
            int loIndex = currList [-2];
            int hiIndex = currList [-1];
            int minIndex = noSectors, maxIndex = 0;
            // See if this row needs to be processed
            if ( loIndex <= hiIndex ) {
                int startCount = count;
                for ( int j = loIndex; j <= hiIndex; j++ ) {
                    if ( currList [j] == false ) continue;
                    if ( length < distance [i][j] ) {
                        distance [i][j] = length;
                        for ( int x = 0; x < sector [j].noNeighbors; x++ ) {
                            int index = sector [j].neighbor [x] - sector;
                            nextList [index] = true;
                            if ( index < minIndex ) minIndex = index;
                            if ( index > maxIndex ) maxIndex = index;
                        }
                        count++;
                    }
                    currList [j] = false;
                }
                // Should we process this row next time around?
                if ( startCount != count ) {
                    if ( i < loRow ) loRow = i;
                    if ( i > hiRow ) hiRow = i;
                }
            }
            nextList [-2] = ( USHORT ) minIndex;
            nextList [-1] = ( USHORT ) maxIndex;
            currList += listRowSize;
            nextList += listRowSize;
        }
        length++;
    } while ( count );

    // Now mark all sectors with no path to each other as hidden
    for ( int i = 0; i < noSectors; i++ ) {
        for ( int j = i + 1; j < noSectors; j++ ) {
            if ( distance [i][j] > length ) {
                MarkVisibility ( i, j, visHidden );
            }
        }
    }

    delete [] listBuffer;
}

int DFS ( sGraph *graph, sSectorStuff *sector )
{
    // Add this sector to the graph
    graph->sector [graph->noSectors++] = sector;

    // Initialize the sector
    sector->graph          = graph;
    sector->loDFS          = graph->noSectors;
    sector->minReachable   = graph->noSectors;
    sector->isArticulation = false;

    int noChildren = 0;

    for ( int i = 0; i < sector->noNeighbors; i++ ) {
        sSectorStuff *child = sector->neighbor [i];
        if ( child->graph != graph ) {
            noChildren++;
            child->graphParent = sector;
            DFS ( graph, child );
            if ( child->minReachable < sector->minReachable ) {
                sector->minReachable = child->minReachable;
            }
            if ( child->minReachable >= sector->loDFS ) {
                sector->isArticulation = true;
            }
        } else if ( child != sector->graphParent ) {
            if ( child->loDFS < sector->minReachable ) {
                sector->minReachable = child->loDFS;
            }
        }
    }

    if (( sector->graphParent != NULL ) && ( sector->minReachable <= sector->graphParent->loDFS )) {
        sector->graphParent->isArticulation = false;
    }

    sector->hiDFS = graph->noSectors;

    return noChildren;
}

sGraph *CreateGraph ( sSectorStuff *root )
{
    sGraph *graph = &graphTable.graph [ graphTable.noGraphs++ ];

    graph->sector    = graphTable.sectorStart;
    graph->noSectors = 0;

    root->graphParent    = NULL;
    root->isArticulation = ( DFS ( graph, root ) > 1 ) ? true : false;

    graphTable.sectorStart += graph->noSectors;

    return graph;
}

void HideComponents ( sGraph *oldGraph, sSectorStuff *key, sGraph *newGraph )
{
    // Special case used when creating the initial graphs - there is no articulation point
    if ( key == NULL ) {
        for ( int i = 0; i < oldGraph->noSectors; i++ ) {
            sSectorStuff *sec1 = oldGraph->sector [i];
            if ( sec1->graph == oldGraph ) {
                for ( int j = 0; j < newGraph->noSectors; j++ ) {
                    sSectorStuff *sec2 = newGraph->sector [j];
                    MarkVisibility ( sec1->index, sec2->index, visHidden );
                }
            }
        }
        return;
    }

    sRejectRow *keyRow = &rejectTable [ key->index ];

    // For each sector still in the original graph that can't see the articulation point, mark all
    //   the sectors in the new graph as hidden
    for ( int i = 0; i < oldGraph->noSectors; i++ ) {
        sSectorStuff *sec1 = oldGraph->sector [i];
        if ( sec1->graph == oldGraph ) {
            // If this sector can't see the articulation point, it can't see the disconnected component either
            if ( keyRow->sector [ sec1->index ] == visHidden ) {
                for ( int j = 0; j < newGraph->noSectors; j++ ) {
                    sSectorStuff *sec2 = newGraph->sector [j];
                    MarkVisibility ( sec1->index, sec2->index, visHidden );
                }
            } else {
                // Look for sectors in the new graph that can't see the articulation point
                for ( int j = 0; j < newGraph->noSectors; j++ ) {
                    sSectorStuff *sec2 = newGraph->sector [j];
                    if ( keyRow->sector [ sec2->index ] == visHidden ) {
                        MarkVisibility ( sec1->index, sec2->index, visHidden );
                    }
                }
            }
        }
    }
}

void SplitGraph ( sGraph *oldGraph, sSectorStuff *key )
{
    // Stop the key from including the entire graph again
    if ( key != NULL ) {
        key->noNeighbors = -key->noNeighbors;
    }

    // NOTE: Room for optimizations here by changing the -1 to a bigger number
    int remainingSectors = oldGraph->noSectors - 1;

    for ( int i = 0; i < oldGraph->noSectors; i++ ) {
        sSectorStuff *sec = oldGraph->sector [i];
        if (( sec->graph == oldGraph ) && ( sec != key )) {
            sGraph *newGraph = CreateGraph ( sec );
            if ( newGraph->noSectors < remainingSectors ) {
                HideComponents ( oldGraph, key, newGraph );
            }
            remainingSectors -= newGraph->noSectors - 1;
        }
    }

    // Restore key's neighbors
    if ( key != NULL ) {
        key->noNeighbors = -key->noNeighbors;
    }
}

bool UpdateGraphs ( sSectorStuff *key )
{
    sGraph *graph = key->baseGraph;

    // Reset everyone back to the original graph
    for ( int i = 0; i < graph->noSectors; i++ ) {
        graph->sector [i]->graph = graph;
    }

    // Store the starting values for graphTable
    sSectorStuff **sectorStart = graphTable.sectorStart;
    int noGraphs = graphTable.noGraphs;

    SplitGraph ( graph, key );

    // Restore the graphTable settings
    graphTable.sectorStart = sectorStart;
    graphTable.noGraphs    = noGraphs;

    return false;
}

void InitializeGraphs ( sSectorStuff *sector, int noSectors )
{
    Status ( "Creating sector graphs..." );

    graphTable.noGraphs    = 0;
    graphTable.graph       = new sGraph [ noSectors * 2 ];
    graphTable.sectorPool  = new sSectorStuff * [ noSectors * 4 ];
    graphTable.sectorStart = graphTable.sectorPool;
    memset ( graphTable.graph, 0, sizeof ( sGraph ) * noSectors * 2 );
    memset ( graphTable.sectorPool, 0, sizeof ( sSectorStuff * ) * noSectors * 4 );

    // Create the initial graph
    sGraph *graph = &graphTable.graph [0];
    graph->noSectors = noSectors;
    graph->sector    = graphTable.sectorStart;
    graphTable.sectorStart += noSectors;
    graphTable.noGraphs++;

    for ( int i = 0; i < noSectors; i++ ) {
        sector [i].graph = graph;
        graph->sector [i] = &sector [i];
    }

    SplitGraph ( graph, NULL );

    // Keep a permanent copy of the initial grap & isArticulation values
    for ( int i = 0; i < noSectors; i++ ) {
        sector [i].isKey     = sector [i].isArticulation;
        sector [i].baseGraph = sector [i].graph;
    }

    // Calculate the sector metrics
    for ( int i = 1; i < graphTable.noGraphs; i++ ) {
        sGraph *graph = &graphTable.graph [i];
        for ( int j = 0; j < graph->noSectors; j++ ) {
            sSectorStuff *sec = graph->sector [j];
            int sum = 0, left = graph->noSectors - 1;
            for ( int x = 0; x < sec->noNeighbors; x++ ) {
                sSectorStuff *child = sec->neighbor [x];
                if ( child->graphParent != sec ) continue;
                if (( child->loDFS > sec->loDFS ) && ( child->loDFS <= sec->hiDFS )) {
                    int num = child->hiDFS - child->loDFS + 1;
                    left -= num;
                    sum += num * left;
                }
            }
            sec->metric = sum;
        }
    }
}

void EliminateTrivialCases ( sSectorStuff *sector, int noSectors )
{
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
    for ( int i = 0; i < noTransLines; i++ ) {
        sTransLine *line = &transLines [i];
        MarkVisibility ( line->leftSector, line->rightSector, visVisible );
    }
}

bool DontBother ( const sTransLine *srcLine, const sTransLine *tgtLine )
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
        sBlockMapOptions options = { true, true };
        CreateBLOCKMAP ( level, options );
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
            int i = 1;
            while ( ptr [i] != ( USHORT ) -1 ) i++;
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
bool AdjustLinePair ( sTransLine *src, sTransLine *tgt, bool *swapped )
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

void MarkBlockMap ( sWorldInfo *world )
{
    loRow = blockMap->noRows;
    hiRow = -1;

    // Determine boundaries for the BLOCKMAP search
    DrawBlockMapLine ( world->src->start, world->src->end );
    DrawBlockMapLine ( world->tgt->start, world->tgt->end );
    DrawBlockMapLine ( world->src->start, world->tgt->end );
    DrawBlockMapLine ( world->tgt->start, world->src->end );
}

bool FindInterveningLines ( sLineSet *set )
{
    // Reset the checked flag for GetLines
    memset ( checkLine, true, checkLineSize );

    // Mark all lines that have been bounded
    int lineCount = 0;

    for ( int row = loRow; row <= hiRow; row++ ) {
        sBlockMapBounds *bound = &blockMapBounds [ row ];
        for ( int col = bound->lo; col <= bound->hi; col++ ) {
            sBlockMapArrayEntry *ptr = blockMapArray [ row ][ col ];
            if ( ptr ) do {
                set->lines [ lineCount ] = ptr->line;
                lineCount += *ptr->available;
                *ptr->available = false;
            } while ( (++ptr)->available );
        }
        bound->lo = blockMap->noColumns;
        bound->hi = -1;
    }

    set->loIndex = 0;
    set->hiIndex = lineCount - 1;
    set->lines [ lineCount ] = NULL;

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

void TrimSetBounds ( sLineSet *set )
{
    if ( set->loIndex >= set->hiIndex ) return;

    while ( set->lines [ set->loIndex ]->ignore == true ) {
        set->loIndex++;
        if ( set->loIndex >= set->hiIndex ) return;
    }
    while ( set->lines [ set->hiIndex ]->ignore == true ) {
        set->hiIndex--;
        if ( set->loIndex >= set->hiIndex ) return;
    }
}

void RotatePoint ( sPoint *p, int x, int y )
{
    p->x = DX * ( x - X ) + DY * ( y - Y );
    p->y = DX * ( y - Y ) - DY * ( x - X );
}

int TrimLines ( const sTransLine *src, const sTransLine *tgt, sLineSet *set )
{
    long loY, hiY, loX, hiX;
    GetBounds ( src->start, src->end, tgt->start, tgt->end, &loY, &hiY, &loX, &hiX );

    X = src->start->x;
    Y = src->start->y;
    DX = tgt->end->x - src->start->x;
    DY = tgt->end->y - src->start->y;

    // Variables for a rotated bounding box
    sPoint p1, p2, p3;
    RotatePoint ( &p1, src->end->x, src->end->y );
    RotatePoint ( &p2, tgt->start->x, tgt->start->y );
    RotatePoint ( &p3, tgt->end->x, tgt->end->y );
    long minX = ( p1.x < 0 ) ? 0 : p1.x;
    long maxX = ( p2.x < p3.x ) ? p2.x : p3.x;
    long minY = ( p1.y < p2.y ) ? ( p1.y < p3.y ) ? p1.y : p3.y : ( p2.y < p3.y ) ? p2.y : p3.y;

    int linesLeft = 0;

    for ( int i = set->loIndex; i <= set->hiIndex; i++ ) {

        sSolidLine *line = set->lines [i];

        line->ignore = true;

        // Eliminate any lines completely outside the axis aligned bounding box
        if ( line->start->y <= loY ) {
            if ( line->end->y <= loY ) continue;
        } else if ( line->start->y >= hiY ) {
            if ( line->end->y >= hiY ) continue;
        }
        if ( line->start->x >= hiX ) {
            if ( line->end->x >= hiX ) continue;
        } else if ( line->start->x <= loX ) {
            if ( line->end->x <= loX ) continue;
        }

        // Stop if we find a single line that obstructs the view completely
        if ( minX <= maxX ) {
            sPoint start, end;
            start.y = DX * ( line->start->y - Y ) - DY * ( line->start->x - X );
            if (( start.y >= 0 ) || ( start.y <= minY )) {
                end.y = DX * ( line->end->y - Y ) - DY * ( line->end->x - X );
                if ((( end.y <= minY ) && ( start.y >= 0 )) || (( end.y >= 0 ) && ( start.y <= minY ))) {
                    start.x = DX * ( line->start->x - X ) + DY * ( line->start->y - Y );
                    if (( start.x  >= minX ) && ( start.x <= maxX )) {
                        end.x = DX * ( line->end->x - X ) + DY * ( line->end->y - Y );
                        if (( end.x >= minX ) && ( end.x <= maxX )) {
                            return -1;
                        }
                    }
                // Use the new information and see if line is outside the bounding box
                } else if ((( end.y >= 0 ) && ( start.y >= 0 )) || (( end.y <= minY ) && ( start.y <= minY ))) {
                    continue;
                }
            }
        }

        line->ignore = false;
        linesLeft++;

    }

    if ( linesLeft == 0 ) return 0;

    // Eliminate lines that touch the src/tgt lines but are not in view
    int x1  = src->start->x;
    int y1  = src->start->y;
    int dx1 = src->end->x - src->start->x;
    int dy1 = src->end->y - src->start->y;

    int x2  = tgt->start->x;
    int y2  = tgt->start->y;
    int dx2 = tgt->end->x - tgt->start->x;
    int dy2 = tgt->end->y - tgt->start->y;

    for ( int i = set->loIndex; i <= set->hiIndex; i++ ) {
        sSolidLine *line = set->lines [i];
        if ( line->ignore == true ) continue;
        int y = 1;
        if (( line->start == src->start ) || ( line->start == src->end )) {
            y = dx1 * ( line->end->y - y1 ) - dy1 * ( line->end->x - x1 );
        } else if (( line->end == src->start ) || ( line->end == src->end )) {
            y = dx1 * ( line->start->y - y1 ) - dy1 * ( line->start->x - x1 );
        } else if (( line->start == tgt->start ) || ( line->start == tgt->end )) {
            y = dx2 * ( line->end->y - y2 ) - dy2 * ( line->end->x - x2 );
        } else if (( line->end == tgt->start ) || ( line->end == tgt->end )) {
            y = dx2 * ( line->start->y - y2 ) - dy2 * ( line->start->x - x2 );
        }
        if ( y < 0 ) {
            line->ignore = true;
            linesLeft--;
        }
    }

    TrimSetBounds ( set );

    return linesLeft;
}

//
// Find out which side of the poly-line the line is on
//
//  Return Values:
//      1 - above (not completely below) the poly-line
//      0 - intersects the poly-line
//     -1 - below the poly-line (one or both end-points may touch the poly-line)
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

    // Eliminate the 2 easy cases (t1 & t2 both above or below the x-axis)
    if (( y1 > 0 ) && ( y2 > 0 )) return 1;
    if (( y1 <= 0 ) && ( y2 <= 0 )) return -1;
    // t1->t2 crosses poly-Line segment (or one point touches it and the other is above it)

    // Rotate & translate using t1->t2 as the +X-axis
    DX = t2->x - t1->x;
    DY = t2->y - t1->y;

    y1 = DX * ( p1->y - t1->y ) - DY * ( p1->x - t1->x );
    y2 = DX * ( p2->y - t1->y ) - DY * ( p2->x - t1->x );

    // Eliminate the 2 easy cases (p1 & p2 both above or below the x-axis)
    if (( y1 > 0 ) && ( y2 > 0 )) return -2;
    if (( y1 < 0 ) && ( y2 < 0 )) return -2;

    return 0;
}

int FindSide ( sMapLine *line, sPolyLine *poly )
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

bool AdjustEndPoints ( sTransLine *left, sTransLine *right, sPolyLine *upper, sPolyLine *lower )
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

    sLineSet *set = &world->solidSet;

    for ( EVER ) {

        bool done = true;
        bool stray = false;

        for ( int i = set->loIndex; i <= set->hiIndex; i++ ) {

            sSolidLine *line = set->lines [ i ];
            if ( line->ignore == true ) continue;

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

        TrimSetBounds ( set );
    }

    return true;
}

bool FindObstacles ( sWorldInfo *world )
{
    if ( world->solidSet.hiIndex < world->solidSet.loIndex ) return false;

    // If we have an unbroken line between src & tgt there is a direct LOS
    if ( world->upperPoly.noPoints == 2 ) return false;
    if ( world->lowerPoly.noPoints == 2 ) return false;

    // To be absolutely correct, we should create a list of obstacles
    // (ie: connected lineDefs completely enclosed by upperPoly & lowerPoly)
    // and see if any of them completely block the LOS

    return false;
}

void InitializeWorld ( sWorldInfo *world, sTransLine *src, sTransLine *tgt )
{
    world->src = src;
    world->tgt = tgt;

    world->solidSet.lines = testLines;
    world->solidSet.loIndex = 0;
    world->solidSet.hiIndex = -1;

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

bool CheckLOS ( sTransLine *src, sTransLine *tgt )
{
    sWorldInfo myWorld;
    InitializeWorld ( &myWorld, src, tgt );

    MarkBlockMap ( &myWorld );

    // See if there are any solid lines in the blockmap region between src & tgt
    if ( FindInterveningLines ( &myWorld.solidSet ) == true ) {

        // Do a more refined check to see if there are any lines
        switch ( TrimLines ( myWorld.src, myWorld.tgt, &myWorld.solidSet )) {

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

bool DivideRegion ( const sTransLine *srcLine, const sTransLine *tgtLine, bool swapped, sTransLine *src, sTransLine *tgt )
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

    sPoint crossPoint ( src->start->x + ( long ) ( t * src->DX ), src->start->y + ( long ) ( t * src->DY ));
    sTransLine newSrc = *src;

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

bool TestLinePair ( const sTransLine *srcLine, const sTransLine *tgtLine )
{
    if ( DontBother ( srcLine, tgtLine )) {
        return false;
    }

    sTransLine src = *srcLine;
    sTransLine tgt = *tgtLine;

    bool swapped = false;
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
//  Sort sectors so the the most critical articulation points are placed first
//----------------------------------------------------------------------------
int SortSector ( const void *ptr1, const void *ptr2 )
{
    const sSectorStuff *sec1 = * ( const sSectorStuff ** ) ptr1;
    const sSectorStuff *sec2 = * ( const sSectorStuff ** ) ptr2;

    // Put complete sectors last
    if ( sec1->isComplete != sec2->isComplete ) {
        return sec1->isComplete ? 1 : -1;
    }

    if ( sec1->isComplete == false ) {
        // Put articulation points first
        if ( sec1->isKey != sec2->isKey ) {
            return sec1->isKey ? -1 : 1;
        }

        // Favor the sector with the best metric (higher is better)
        if ( sec1->metric != sec2->metric ) {
            return sec2->metric - sec1->metric;
        }

        // Favor number of children
        if ( sec1->noChildren != sec2->noChildren ) {
            return sec2->noChildren - sec1->noChildren;
        }

        // Favor the sector with the most active neighbors
        if ( sec1->noActiveNeighbors != sec2->noActiveNeighbors ) {
            return sec2->noActiveNeighbors - sec1->noActiveNeighbors;
        }

        // Favor the sector with the most visible lines
        if ( sec1->noActiveLines != sec2->noActiveLines ) {
            return sec2->noActiveLines - sec1->noActiveLines;
        }
    }

    // It's a tie - use the sector index - lower index favored
    return sec1->index - sec2->index;
}

//----------------------------------------------------------------------------
// Create a mapping for see-thru lines that tries to put lines that affect
//   the most sectors first.  As more sectors are marked visible/hidden, the
//   number of remaining line pairs that must be checked drops.  By ordering
//   the lines, we can speed things up quite a bit with just a little effort.
//----------------------------------------------------------------------------
int SetupLineMap ( sTransLine **lineMap, sSectorStuff **sectorList, int &maxSectors )
{
    // Try to order lines to maximize our chances of culling child sectors
    qsort ( sectorList, maxSectors, sizeof ( sSectorStuff * ), SortSector );

    // Don't bother looking at completed sectors
    while ( sectorList [ maxSectors - 1 ]->isComplete ) maxSectors--;

    int maxIndex = 0;
    for ( int i = 0; i < maxSectors; i++ ) {
        for ( int j = 0; j < sectorList [i]->noActiveLines; j++ ) {
            sTransLine *line = sectorList [i]->line [j];
            for ( int k = 0; k < maxIndex; k++ ) {
                if ( lineMap [k] == line ) goto next;
            }
            lineMap [ maxIndex++ ] = line;
        next:
            ; // Microsoft compiler is acting up
        }
    }

    return maxIndex;
}

void HideSector ( sSectorStuff *sec, sSectorStuff *sector, int noSectors )
{
    // Make a list of all children of this sector
    if ( sec->noChildren > 0 ) {
        for ( int j = 0; j < noSectors; j++ ) {
            sSectorStuff *parent = sector [j].parent;
            while (( parent != NULL ) && ( parent != sec )) parent = parent->parent;
            isChild [j] = ( parent != NULL ) ? true : false;
        }
    }

    for ( int j = 0; j < noSectors; j++ ) {
        // Don't mark sectors we already know are visible
        if ( rejectTable [ sec->index ].sector [ j ] != visUnknown ) continue;

        // Don't mark children as hidden to the parent
        if ( isChild [j] == true ) continue;

        // Mark this sector as invisible to the rest of the world
        MarkVisibility ( sec->index, j, visHidden );

        // Mark all of it's children as invisible to the rest of the world too
        if ( sec->noChildren > 0 ) {
            for ( int k = 0; k < noSectors; k++ ) {
                if ( isChild [k] == false ) continue;
                MarkVisibility ( k, j, visHidden );
            }
        }
    }
}

bool RemoveLine ( sSectorStuff *sec, sTransLine *line )
{
    // Remove line from the list of lines in this sector
    for ( int i = 0; i < sec->noActiveLines; i++ ) {
        if ( sec->line [i] == line ) {
            // If we've looked at all the active see-thru lines for a sector,
            //   then anything that isn't already visible is hidden!
            if ( --sec->noActiveLines == 0 ) {
                sec->isComplete = true;
                return true;
            }

            sec->line [i] = sec->line [sec->noActiveLines];
            sec->line [sec->noActiveLines] = line;

            break;
        }
    }

    return false;
}

bool LineComplete ( sTransLine *line, sSectorStuff *sector, int noSectors )
{
    bool recompute = false;

    sSectorStuff *left = &sector [ line->leftSector ]; 
    if ( RemoveLine ( left, line ) == true ) {
        HideSector ( left, sector, noSectors );
        if ( left->isKey == true ) {
            recompute = UpdateGraphs ( left );
        }
    }

    sSectorStuff *right = &sector [ line->rightSector ]; 
    if ( RemoveLine ( right, line ) == true ) {
        HideSector ( right, sector, noSectors );
        if ( right->isKey == true ) {
            recompute = UpdateGraphs ( right );
        }
    }

    return recompute;
}

void MarkPairVisible ( sTransLine *srcLine, sTransLine *tgtLine )
{
    // There is a direct LOS between the two lines - mark all affected sectors
    MarkVisibility ( srcLine->leftSector, tgtLine->leftSector, visVisible );
    MarkVisibility ( srcLine->leftSector, tgtLine->rightSector, visVisible );
    MarkVisibility ( srcLine->rightSector, tgtLine->leftSector, visVisible );
    MarkVisibility ( srcLine->rightSector, tgtLine->rightSector, visVisible );
}

void UpdateVisibleStatus ( sTransLine *srcLine, sTransLine *tgtLine )
{
    MarkPairVisible ( srcLine, tgtLine );
}

bool CreateREJECT ( DoomLevel *level, const sRejectOptions &options, ULONG *efficiency )
{
    if (( options.Force == false ) && ( FeaturesDetected ( level ) == true )) {
        return true;
    }

    int noSectors = level->SectorCount ();
    bool saveBits = level->hasChanged () ? false : true;
    if ( options.Empty ) {
        level->NewReject ((( noSectors * noSectors ) + 7 ) / 8, GetREJECT ( level, true, efficiency ), saveBits );
        return false;
    }

    PrepareREJECT ( noSectors );
    CopyVertices ( level );

    // Make sure we have something worth doing
    if ( SetupLines ( level )) {

        // Make a list of which sectors contain others and their boundary lines
        sSectorStuff *sector = CreateSectorInfo ( level );

        bool bFindChildren = options.FindChildren;
        bool bUseGraphs    = options.UseGraphs;

        if ( 0 ) {
            CreateDistanceTable ( sector, noSectors );
        }

        if ( bFindChildren == true ) {
            FindChildren ( sector, noSectors );
        }

        if ( bUseGraphs == true ) {
            InitializeGraphs ( sector, noSectors );
        }

        // Mark the easy ones visible to speed things up later
        EliminateTrivialCases ( sector, noSectors );

        testLines     = new sSolidLine * [ noSolidLines ];
        polyPoints    = new const sPoint * [ 2 * ( noSolidLines + 2 )];

        // Create a map to reorder lines more efficiently
        int noActiveSectors = noSectors;
        sSectorStuff **sectorList = new sSectorStuff * [ noSectors ];
        for ( int i = 0; i < noSectors; i++ ) sectorList [i] = &sector [i];

        sTransLine **lineMap = new sTransLine * [ noTransLines ];
        int lineMapSize = SetupLineMap ( lineMap, sectorList, noActiveSectors );

        // Set up a scaled BLOCKMAP type structure
        PrepareBLOCKMAP ( level );

        int done = 0, total = noTransLines * ( noTransLines - 1 ) / 2;
        double nextProgress = 0.0;

        Status ( "Working..." );

        // Now the tough part: check all lines against each other
        for ( int i = 0; i < lineMapSize; i++ ) {

            sTransLine *srcLine = lineMap [ i ];
            for ( int j = lineMapSize - 1; j > i; j-- ) {
                sTransLine *tgtLine = lineMap [ j ];
                if ( TestLinePair ( srcLine, tgtLine ) == true ) {
                    UpdateVisibleStatus ( srcLine, tgtLine );
                }
            }

            // Mark this line as complete for the surrounding sectors
            bool recompute = LineComplete ( srcLine, sector, noSectors );

            // See if we should re-order the lineMap 
            if ( recompute == true ) {
                lineMapSize = ( i + 1 ) + SetupLineMap ( lineMap + ( i + 1 ), sectorList, noActiveSectors );
                recompute = false;
            }

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
        delete [] sectorList;
        delete [] polyPoints;
        delete [] testLines;

        delete [] graphTable.graph;
        delete [] graphTable.sectorPool;

        delete [] distance;

        // Clean up allocations made by CreateSectorInfo
        delete [] neighborList;
        delete [] sectorLines;
        delete [] isChild;
        delete [] sector;

        // Clear these in case their not reset on the next level
        graphTable.graph      = NULL;
        graphTable.sectorPool = NULL;
        distance = NULL;
    }

    level->NewReject ((( noSectors * noSectors ) + 7 ) / 8, GetREJECT ( level, false, efficiency ), saveBits );

    // Clean up allocations made by SetupLines
    delete [] checkLine;
    delete [] solidLines;
    delete [] transLines;
    delete [] indexToSolid;

    // Delete our local copy of the vertices
    delete [] vertices;

    // Finally, release our reject table data
    CleanUpREJECT ( noSectors );

    return false;
}
