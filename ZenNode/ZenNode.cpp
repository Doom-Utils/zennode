//----------------------------------------------------------------------------
//
// File:        ZenNode.cpp
// Date:        26-Oct-1994
// Programmer:  Marc Rousseau
//
// Description: This module contains the logic for the NODES builder.
//
// Copyright (c) 1994-2001 Marc Rousseau, All Rights Reserved.
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
//   06-??-95	Added LineDef alias list to speed up the process.
//   07-07-95	Added currentAlias/Side/Flipped to speed up WhichSide.
//   07-11-95	Initialized global variables in CreateNODES.
//              Changed logic for static variable last in CreateSSector.
//   10-05-95	Added convexList & extended the use of lineUsed.
//   10-25-95	Changed from doubly linked lists to an array of SEGs.
//   10-27-95	Added header to each function describing what it does.
//   11-14-95	Fixed sideInfo so that a SEG is always to it's own right.
//   12-06-95	Added code to support selective unique sectors & don't splits
//   05-09-96	Added nodePool to reduced calls to new/delete for NODEs
//   05-15-96	Reduced memory requirements for convexList & sectorInfo
//   05-23-96	Added FACTOR_XXX to reduced memory requirements
//   05-24-96	Removed all calls to new/delete during CreateNode
//   05-31-96	Made WhichSide inline & moved the bulk of code to _WhichSide
//   10-01-96	Reordered functions & removed all function prototypes
//   07-31-00   Increased max subsector factor from 15 to 256
//   10-29-00   Fixed _WhichSide & DivideSeg so that things finally(?) work!
//   02-21-01   Fixed _WhichSide & DivideSeg so that things finally(?) work!
//   02-22-01   Added vertSplitAlias to help _WhichSide's accuracy problem
//
//----------------------------------------------------------------------------

#include <assert.h>
#include <math.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.hpp"
#include "logger.hpp"

#include "level.hpp"
#include "ZenNode.hpp"

DBG_REGISTER ( __FILE__ );

// Emperical values derived from a test of all the .WAD files from id & Raven.
#define FACTOR_VERTEX		2.0		// 1.662791
#define FACTOR_SEGS		2.0		// 1.488095
#define FACTOR_NODE		2.2		// 1.030612
//define FACTOR_SSECTOR		7.6		// 7.518518
#define FACTOR_SSECTOR		50.0		// 7.518518

#define FACTOR_SUBSECTORS	256

static int       maxSegs;
static int       maxVertices;

static int       nodesLeft;
static NODE     *nodePool;
static NODE     *nodeStart = NULL;
static int       nodeCount;			// Number of NODES stored

static SEG      *tempSeg;
static SEG      *segStart = NULL;
static int       segCount;			// Number of SEGS stored

static int       ssectorsLeft;
static wSSector *ssectorPool;
static int       ssectorCount;			// Number of SSECTORS stored

static wVertex  *newVertices = NULL;
static sAlias  **vertSplitAlias;
static int       noVertices;

//
// Variables used by WhichSide to speed up side calculations
//
static char     *currentSide;
static int       currentFlipped;
static sAlias   *currentAlias;

static int      *convexList;
static int      *convexPtr;
static int       sectorCount;

static int       showProgress;
static UCHAR    *usedSector;
static bool     *keepUnique;
static bool      uniqueSubsectors;
static bool     *lineUsed;
static bool     *lineChecked;
static int       noAliases;
static sAlias   *lineDefAlias;
static char    **sideInfo;
static long      DY, DX, X, Y, ANGLE;

static sScoreInfo *score;
		  
// metric = S ? ( L * R ) / ( X1 ? X1 * S / X2 : 1 ) - ( X3 * S + X4 ) * S : ( L * R );
static long X1 = getenv ( "ZEN_X1" ) ? atol ( getenv ( "ZEN_X1" )) : 20;
static long X2 = getenv ( "ZEN_X2" ) ? atol ( getenv ( "ZEN_X2" )) : 10;
static long X3 = getenv ( "ZEN_X3" ) ? atol ( getenv ( "ZEN_X3" )) : 1;
static long X4 = getenv ( "ZEN_X4" ) ? atol ( getenv ( "ZEN_X4" )) : 25;

static long Y1 = getenv ( "ZEN_Y1" ) ? atol ( getenv ( "ZEN_Y1" )) : 1;
static long Y2 = getenv ( "ZEN_Y2" ) ? atol ( getenv ( "ZEN_Y2" )) : 7;
static long Y3 = getenv ( "ZEN_Y3" ) ? atol ( getenv ( "ZEN_Y3" )) : 1;
static long Y4 = getenv ( "ZEN_Y4" ) ? atol ( getenv ( "ZEN_Y4" )) : 0;

static SEG *(*PartitionFunction) ( SEG *, int );

int GCD ( int u, int v )
{
    if ( u < 0 ) u = -u;
    if ( v < 0 ) v = -v;

    if ( u > v ) {
        int temp = u;
        u = v;
        v = temp;
    }

    int retVal = 1;

    // Handle u and v both even
    while ((( u | v ) & 1 ) == 0 ) {
        retVal <<= 1;
        if (( u >>= 1 ) == 0 ) return retVal;
        if (( v >>= 1 ) == 0 ) return retVal;
    }

    // If the smaller of the two is still even we're done
    if (( u & 1 ) == 0 ) return retVal;

    // If the larger of the two is still even we're almost done
    if (( v & 1 ) == 0 ) {
        // See if the larger is evenly divisible by the smaller
        return ( v == v / u * u ) ? u * retVal : retVal;
    }

    // They're both odd
    while ( u != v ) {
        while ( u < v ) {
            v = v - u;
            while (( v & 1 ) == 0 ) {
                v >>= 1;
            }
        }
        int temp = u;
        u = v;
        v = temp;
    }

    return u * retVal;
}

//----------------------------------------------------------------------------
//  Create a list of SEGs from the *important* sidedefs.  A sidedef is
//    considered important if:
//     It has non-zero length
//     It's linedef has different sectors on each side
//     It has at least one visible texture
//----------------------------------------------------------------------------

static SEG *CreateSegs ( DoomLevel *level, sBSPOptions *options )
{
    FUNCTION_ENTRY ( NULL, "CreateSegs", true );

    // Get a rough count of how many SideDefs we're starting with
    segCount = maxSegs = 0;
    const wLineDef *lineDef = level->GetLineDefs ();
    const wSideDef *sideDef = level->GetSideDefs ();
    int i;
    for ( i = 0; i < level->LineDefCount (); i++ ) {
        if ( lineDef [i].sideDef [0] != NO_SIDEDEF ) maxSegs++;
        if ( lineDef [i].sideDef [1] != NO_SIDEDEF ) maxSegs++;
    }
    tempSeg  = new SEG [ maxSegs ];
    maxSegs  = ( int ) ( maxSegs * FACTOR_SEGS );
    segStart = new SEG [ maxSegs ];
    memset ( segStart, 0, sizeof ( SEG ) * maxSegs );

    SEG *seg = segStart;
    for ( i = 0; i < level->LineDefCount (); i++, lineDef++ ) {

        wVertex *vertS = &newVertices [ lineDef->start ];
        wVertex *vertE = &newVertices [ lineDef->end ];
        long dx = vertE->x - vertS->x;
        long dy = vertE->y - vertS->y;
        if (( dx == 0 ) && ( dy == 0 )) continue;

        int rSide = lineDef->sideDef [0];
        int lSide = lineDef->sideDef [1];
        const wSideDef *sideRight = ( rSide == NO_SIDEDEF ) ? ( const wSideDef * ) NULL : &sideDef [ rSide ];
        const wSideDef *sideLeft = ( lSide == NO_SIDEDEF ) ? ( const wSideDef * ) NULL : &sideDef [ lSide ];

        // Ignore line if both sides point to the same sector & neither side has any visible texture
        if ( options->reduceLineDefs && sideRight && sideLeft && ( sideRight->sector == sideLeft->sector )) {
            if ( * ( USHORT * ) sideLeft->text3 == EMPTY_TEXTURE ) {
                sideLeft = ( const wSideDef * ) NULL;
            }
            if ( * ( USHORT * ) sideRight->text3 == EMPTY_TEXTURE ) {
                sideRight = ( const wSideDef * ) NULL;
            }
            if ( ! sideLeft && ! sideRight ) continue;
        }

        if ( options->ignoreLineDef && options->ignoreLineDef [i] ) continue;

        BAM angle = ( dy == 0 ) ? ( BAM ) (( dx < 0 ) ? BAM180 : 0 ) :
                    ( dx == 0 ) ? ( BAM ) (( dy < 0 ) ? BAM270 : BAM90 ) :
                                  ( BAM ) ( atan2 ( dy, dx ) * BAM180 / M_PI + 0.5 * sgn ( dy ));

        bool split = options->dontSplit ? options->dontSplit [i] : false;

        if ( sideRight ) {
            seg->Data.start   = lineDef->start;
            seg->Data.end     = lineDef->end;
            seg->Data.angle   = angle;
            seg->Data.lineDef = ( USHORT ) i;
            seg->Data.flip    = 0;
            seg->LineDef      = lineDef;
            seg->Sector       = sideRight->sector;
            seg->DontSplit    = split;
            seg++;
        }

        if ( sideLeft ) {
            seg->Data.start   = lineDef->end;
            seg->Data.end     = lineDef->start;
            seg->Data.angle   = ( BAM ) ( angle + BAM180 );
            seg->Data.lineDef = ( USHORT ) i;
            seg->Data.flip    = 1;
            seg->LineDef      = lineDef;
            seg->Sector       = sideLeft->sector;
            seg->DontSplit    = split;
            seg++;
        }
    }
    segCount = seg - segStart;

    return segStart;
}

//----------------------------------------------------------------------------
//  Calculate the set of variables used frequently that are based on the
//    currently selected SEG to be used as a partition line.
//----------------------------------------------------------------------------

static void ComputeStaticVariables ( SEG *pSeg )
{
    FUNCTION_ENTRY ( NULL, "ComputeStaticVariables", true );

    currentAlias   = &lineDefAlias [ pSeg->Data.lineDef ];
    currentSide    = sideInfo ? sideInfo [ currentAlias->index ] : NULL;
    currentFlipped = ( pSeg->Data.flip ^ currentAlias->flip ) ? SIDE_FLIPPED : SIDE_NORMAL;
    
    const wLineDef *lineDef = pSeg->LineDef;
    wVertex *vertS = &newVertices [ pSeg->Data.flip ? lineDef->end : lineDef->start ];
    wVertex *vertE = &newVertices [ pSeg->Data.flip ? lineDef->start : lineDef->end ];

    ANGLE = pSeg->Data.angle;
    X     = vertS->x;
    Y     = vertS->y;
    DX    = vertE->x - vertS->x;
    DY    = vertE->y - vertS->y;
}

//----------------------------------------------------------------------------
//  Determine if the given SEG is co-linear (ie: they lie on the same line)
//    with the currently selected partition.
//----------------------------------------------------------------------------

static bool CoLinear ( SEG *seg )
{
    FUNCTION_ENTRY ( NULL, "CoLinear", true );

    // If they're not at the same angle ( ñ180ø ), bag it
    if (( ANGLE & 0x7FFF ) != ( seg->Data.angle & 0x7FFF )) return false;

    // Do the math stuff
    wVertex *vertS = &newVertices [ seg->Data.start ];
    if ( DX == 0 ) return ( vertS->x == X ) ? true : false;
    if ( DY == 0 ) return ( vertS->y == Y ) ? true : false;

    // Rotate vertS about (X,Y) by é degrees to get y offset
    //   Y = Hùsin(é)           ³  1  0  0 ³³ cos(é)  -sin(é)  0 ³
    //   X = Hùcos(é)    ³x y 1³³  0  1  0 ³³ sin(é)   cos(é)  0 ³
    //   H = (Xý+Yý)^«         ³ -X -Y  1 ³³   0         0    1 ³

    int y = DX * ( vertS->y - Y ) - DY * ( vertS->x - X );

    return ( y == 0 ) ? true : false;
}

//----------------------------------------------------------------------------
//  Given a list of SEGs, determine the bounding rectangle.
//----------------------------------------------------------------------------

static void FindBounds ( wBound *bound, SEG *seg, int noSegs )
{
    FUNCTION_ENTRY ( NULL, "FindBounds", true );

    wVertex *vert = &newVertices [ seg->Data.start ];
    bound->minx = bound->maxx = vert->x;
    bound->miny = bound->maxy = vert->y;
    for ( int i = 0; i < noSegs; i++ ) {
        wVertex *vertS = &newVertices [ seg->Data.start ];
        wVertex *vertE = &newVertices [ seg->Data.end ];

        int loX = vertS->x, hiX = vertS->x;
        if ( loX < vertE->x ) hiX = vertE->x; else loX = vertE->x;
        int loY = vertS->y, hiY = vertS->y;
        if ( loY < vertE->y ) hiY = vertE->y; else loY = vertE->y;

        if ( loX < bound->minx ) bound->minx = ( SHORT ) loX;
        if ( hiX > bound->maxx ) bound->maxx = ( SHORT ) hiX;
        if ( loY < bound->miny ) bound->miny = ( SHORT ) loY;
        if ( hiY > bound->maxy ) bound->maxy = ( SHORT ) hiY;

        seg++;
    }
}

//----------------------------------------------------------------------------
//  Determine which side of the partition line the given SEG lies.  A quick
//    check is made based on the sector containing the SEG.  If the sector
//    is split by the partition, a more detailed examination is made using
//    _WhichSide.
//
//    Returns:
//       -1 - SEG is on the left of the partition
//        0 - SEG is split by the partition
//       +1 - SEG is on the right of the partition
//----------------------------------------------------------------------------

int _WhichSide ( SEG *seg )
{
    FUNCTION_ENTRY ( NULL, "_WhichSide", true );

    wVertex *vertS = &newVertices [ seg->Data.start ];
    wVertex *vertE = &newVertices [ seg->Data.end ];
    long y1, y2;

    if ( DX == 0 ) {
        if ( DY > 0 ) {
            y1 = ( X - vertS->x ),    y2 = ( X - vertE->x );
        } else {
            y1 = ( vertS->x - X ),    y2 = ( vertE->x - X );
        }
    } else if ( DY == 0 ) {
        if ( DX > 0 ) {
            y1 = ( vertS->y - Y ),    y2 = ( vertE->y - Y );
        } else {
            y1 = ( Y - vertS->y ),    y2 = ( Y - vertE->y );
        }
    } else {

        y1 = DX * ( vertS->y - Y ) - DY * ( vertS->x - X );
        y2 = DX * ( vertE->y - Y ) - DY * ( vertE->x - X );

        // If we've already split this seg with this alias - we can bail out early
        if ( vertSplitAlias [ seg->Data.start ] == currentAlias ) {
            return ( y2 < 0 ) ? SIDE_RIGHT : SIDE_LEFT;
        } else if ( vertSplitAlias [ seg->Data.end ] == currentAlias ) {
            return ( y1 < 0 ) ? SIDE_RIGHT : SIDE_LEFT;
        } else {

            int dx = vertE->x - vertS->x;
            int dy = vertE->y - vertS->y;
            int det = dx * DY - dy * DX;

            if ( det != 0 ) {

                int num = ( vertS->y - Y ) * DX - ( vertS->x - X ) * DY;

                // If num is too big, try to reduce it
                if (( num > 0x00008000 ) || ( num < -0x00008000 )) {
                    // Divide num & det by their GCD to reduce the chance of overflow
                    int gcd = GCD ( num, det );
                    num /= gcd;
                    det /= gcd;
                }

                // Find the point of intersection
                int x = ( int ) ( vertS->x + num * dx / det );
                int y = ( int ) ( vertS->y + num * dy / det );

                int y3 = DX * ( y - Y ) - DY * ( x - X );

                // If either the start or end point is the intersecting point - pin it to 0
                // NOTE: We really should compare the translated/rotated Xs as well
                if ( y1 == y3 ) y1 = 0;
                if ( y2 == y3 ) y2 = 0;
            }
        }
    }

    // If its co-linear, decide based on direction
    if (( y1 == 0 ) && ( y2 == 0 )) {
        long x1 = DX * ( vertS->x - X ) + DY * ( vertS->y - Y );
        long x2 = DX * ( vertE->x - X ) + DY * ( vertE->y - Y );
        return ( x1 < x2 ) ? SIDE_RIGHT : SIDE_LEFT;
    }

    // Otherwise:
    //   Left   -1 : ( y1 >= 0 ) && ( y2 >= 0 )
    //   Both    0 : (( y1 < 0 ) && ( y2 > 0 )) || (( y1 > 0 ) && ( y2 < 0 ))
    //   Right   1 : ( y1 <= 0 ) && ( y2 <= 0 )

    return ( y1 <  0 ) ? (( y2 <= 0 ) ? SIDE_RIGHT : SIDE_SPLIT ) :
           ( y1 == 0 ) ? (( y2 <= 0 ) ? SIDE_RIGHT : SIDE_LEFT  ) :
                         (( y2 >= 0 ) ? SIDE_LEFT  : SIDE_SPLIT );
}

inline int WhichSide ( SEG *seg )
{
    FUNCTION_ENTRY ( NULL, "WhichSide", true );

    sAlias *alias = &lineDefAlias [ seg->Data.lineDef ];
    if ( alias->index == currentAlias->index ) {
        int isFlipped = ( seg->Data.flip ^ alias->flip ) ? SIDE_FLIPPED : SIDE_NORMAL;
        return ( currentFlipped == isFlipped ) ? SIDE_RIGHT : SIDE_LEFT;
    }

    int side = currentSide [ seg->Sector ];
    // NB: side & 1 implies either SIDE_LEFT or SIDE_RIGHT
    if ( IS_LEFT_RIGHT ( side )) return FLIP ( currentFlipped, side );

    return _WhichSide ( seg );
}

//----------------------------------------------------------------------------
//  Create a list of aliases vs sectors that indicates which side of a given
//    alias a sector is.  This requires:
//     Bounding rectangle information for each sector
//     List of line aliases (unique lines)
//----------------------------------------------------------------------------

static void CreateSideInfo ( DoomLevel *level, wBound *bound, sSectorInfo *sectInfo, SEG **aliasList )
{
    FUNCTION_ENTRY ( NULL, "CreateSideInfo", true );

    SEG testSeg, partSeg;
    memset ( &partSeg, 0, sizeof ( SEG ));
    memset ( &testSeg, 0, sizeof ( SEG ));

    int v = level->VertexCount ();
    testSeg.Data.lineDef = ( USHORT ) level->LineDefCount ();
    testSeg.Data.start   = ( USHORT ) v;
    testSeg.Data.end     = ( USHORT ) ( v + 1 );

    long size = ( sizeof ( char * ) + level->SectorCount ()) * ( long ) noAliases;
    char *temp = new char [ size ];
    sideInfo = ( char ** ) temp;
    memset ( temp, 0, sizeof ( char * ) * noAliases );
    temp += sizeof ( char * ) * noAliases;
    memset ( temp, SIDE_UNKNOWN, level->SectorCount () * noAliases );
    for ( int i = 0; i < noAliases; i++ ) {

        sideInfo [i] = ( char * ) temp;
        temp += level->SectorCount ();

        SEG *alias = aliasList [i];
        partSeg = *alias;
        ComputeStaticVariables ( &partSeg );
        for ( int j = 0; j < level->SectorCount (); j++ ) {
            int s = sectInfo [j].index;
            if ( sideInfo [i][s] != SIDE_UNKNOWN ) continue;
            testSeg.Sector = s;
            // Create a bounding box around the sector & check the lower edge 1st
            newVertices [v].x = bound [s].minx;
            newVertices [v].y = bound [s].miny;
            newVertices [v+1].x = bound [s].maxx;
            newVertices [v+1].y = bound [s].miny;
            int side1 = WhichSide ( &testSeg );
            if ( side1 != SIDE_SPLIT ) {
                // Now check the upper edge
                newVertices [v].y = bound [s].maxy;
                newVertices [v+1].y = bound [s].maxy;
                int side2 = WhichSide ( &testSeg );
                if ( side2 == side1 ) {
                    sSectorInfo *sect = &sectInfo [j];
                    int x = sect->noSubSectors;
                    while ( x ) sideInfo [i][ sect->subSector [--x]] = ( char ) side1;
                } else {
                    sideInfo [i][s] = SIDE_SPLIT;
                }
            } else {
                sideInfo [i][s] = SIDE_SPLIT;
            }
        }
    }
}

//----------------------------------------------------------------------------
//  Create a SSECTOR and record the index of the 1st SEG and the total number
//  of SEGs.
//----------------------------------------------------------------------------

static USHORT CreateSSector ( int noSegs, SEG *segs )
{
    FUNCTION_ENTRY ( NULL, "CreateSSector", true );

    if ( ssectorsLeft-- == 0 ) {
        fprintf ( stderr, "ERROR: ssectorPool exhausted\n" );
        exit ( -1 );
    }
    wSSector *ssec = ssectorPool++;
    ssec->num = ( USHORT ) noSegs;
    ssec->first = ( USHORT ) ( segs - segStart );

    return ( USHORT ) ssectorCount++;
}

//----------------------------------------------------------------------------
//  For each sector, create a bounding rectangle.
//----------------------------------------------------------------------------

static wBound *GetSectorBounds ( DoomLevel *level )
{
    FUNCTION_ENTRY ( NULL, "GetSectorBounds", true );

    // Calculate bounding rectangles for all sectors
    wBound *bound = new wBound [ level->SectorCount () ];
    int i;
    for ( i = 0; i < level->SectorCount (); i++ ) {
        bound [i].maxx = bound [i].maxy = ( SHORT ) 0x8000;
        bound [i].minx = bound [i].miny = ( SHORT ) 0x7FFF;
    }

    int index;
    const wLineDef *lineDef = level->GetLineDefs ();
    const wVertex  *vertex = level->GetVertices ();
    for ( i = 0; i < level->LineDefCount (); i++ ) {

        const wVertex *vertS = &vertex [ lineDef->start ];
        const wVertex *vertE = &vertex [ lineDef->end ];

        int loX = vertS->x, hiX = vertS->x;
        if ( loX < vertE->x ) hiX = vertE->x; else loX = vertE->x;
        int loY = vertS->y, hiY = vertS->y;
        if ( loY < vertE->y ) hiY = vertE->y; else loY = vertE->y;

        for ( int s = 0; s < 2; s++ ) {
            if (( index = lineDef->sideDef [s] ) != NO_SIDEDEF ) {
                int sec = level->GetSideDefs ()[index].sector;
                if ( loX < bound [sec].minx ) bound [sec].minx = ( SHORT ) loX;
                if ( hiX > bound [sec].maxx ) bound [sec].maxx = ( SHORT ) hiX;
                if ( loY < bound [sec].miny ) bound [sec].miny = ( SHORT ) loY;
                if ( hiY > bound [sec].maxy ) bound [sec].maxy = ( SHORT ) hiY;
            }
        }
        lineDef++;
    }

    return bound;
}

//----------------------------------------------------------------------------
//  Sort sectors so the the largest (sector containing the most sectors) is
//    placed first in the list.
//----------------------------------------------------------------------------

int SectorSort ( const void *ptr1, const void *ptr2 )
{
    FUNCTION_ENTRY ( NULL, "SectorSort", true );

    int dif = (( sSectorInfo * ) ptr2)->noSubSectors - (( sSectorInfo * ) ptr1)->noSubSectors;
    if ( dif ) return dif;
    dif = (( sSectorInfo * ) ptr2)->index - (( sSectorInfo * ) ptr1)->index;
    return -dif;
}

//----------------------------------------------------------------------------
//  Determine which sectors contain which other sectors, then sort the list.
//----------------------------------------------------------------------------

sSectorInfo *GetSectorInfo ( int noSectors, wBound *bound )
{
    FUNCTION_ENTRY ( NULL, "GetSectorInfo", true );

    long max = noSectors * FACTOR_SUBSECTORS;
    long size = sizeof ( sSectorInfo ) * ( long ) noSectors + sizeof ( int ) * max;
    char *temp = new char [ size ];
    memset ( temp, 0, size );

    sSectorInfo *info = ( sSectorInfo * ) temp;
    temp += sizeof ( sSectorInfo ) * noSectors;

    for ( int i = 0; i < noSectors; i++ ) {
        info [i].index = i;
        info [i].noSubSectors = 0;
        info [i].subSector = ( int * ) temp;
        for ( int j = 0; j < noSectors; j++ ) {
            if (( bound [j].minx >= bound [i].minx ) &&
                ( bound [j].maxx <= bound [i].maxx ) &&
                ( bound [j].miny >= bound [i].miny ) &&
                ( bound [j].maxy <= bound [i].maxy )) {
                int index = info [i].noSubSectors++;
                if ( index >= max ) {
                    fprintf ( stderr, "Too many contained sectors in sector %d\n", i );
                    exit ( -1 );
                }
                info [i].subSector [index] = j;
            }
        }
        temp += sizeof ( int ) * info [i].noSubSectors;
        max -= info [i].noSubSectors;
    }

    qsort ( info, noSectors, sizeof ( sSectorInfo ), SectorSort );
    return info;
}

//----------------------------------------------------------------------------
//  Create a list of aliases.  These are all the unique lines within the map.
//    Each linedef is assigned an alias.  All subsequent calculations are
//    based on the aliases rather than the linedefs, since there are usually
//    significantly fewer aliases than linedefs.
//----------------------------------------------------------------------------

SEG **GetLineDefAliases ( DoomLevel *level )
{
    FUNCTION_ENTRY ( NULL, "GetLineDefAliases", true );

    noAliases = 0;
    lineDefAlias = new sAlias [ level->LineDefCount () + 1 ];
    memset ( lineDefAlias, 0, sizeof ( sAlias ) * ( level->LineDefCount () + 1 ));
    SEG **segAlias = new SEG * [ level->LineDefCount () ];

    SEG *testSeg = NULL, *refSeg = segStart;
    for ( int x, i = 0; i < level->LineDefCount (); i++ ) {

        // Skip lines that have been ignored
        if ( refSeg->Data.lineDef != i ) continue;

        ComputeStaticVariables ( refSeg );
        for ( x = noAliases - 1; x >= 0; x-- ) {
            testSeg = segAlias [x];
            if ( CoLinear ( testSeg )) break;
        }
        if ( x == -1 ) {
            lineDefAlias [i].flip = false;
            segAlias [ x = noAliases++ ] = refSeg;
        } else {
            lineDefAlias [i].flip = ( refSeg->Data.angle == testSeg->Data.angle ) ? false : true;
        }
        lineDefAlias [i].index = x;

        refSeg++;
        if ( refSeg->Data.lineDef == i ) refSeg++;
    }
    lineDefAlias [ level->LineDefCount () ].index = -1;

    return segAlias;
}

//----------------------------------------------------------------------------
//  Return an index for a vertex at (x,y).  If an existing vertex exists,
//    return it, otherwise, create a new one if room is left.
//----------------------------------------------------------------------------

static int AddVertex ( int x, int y )
{
    FUNCTION_ENTRY ( NULL, "AddVertex", true );

    for ( int i = 0; i < noVertices; i++ ) {
        if (( newVertices [i].x == x ) && ( newVertices [i].y == y )) return i;
    }

    if ( noVertices >= maxVertices ) {
        fprintf ( stderr, "\nError: maximum number of vertices exceeded.\n" );
        exit ( -1 );
    }

    newVertices [ noVertices ].x = ( USHORT ) x;
    newVertices [ noVertices ].y = ( USHORT ) y;

    vertSplitAlias [ noVertices ] = currentAlias;

    return noVertices++;
}

//----------------------------------------------------------------------------
//  Sort two SEGS so that the one with the lowest numbered LINEDEF is first.
//----------------------------------------------------------------------------

static int SortByLineDef ( const void *ptr1, const void *ptr2 )
{
    FUNCTION_ENTRY ( NULL, "SortByLineDef", true );

    int dif = (( SEG * ) ptr1)->Data.lineDef - (( SEG * ) ptr2)->Data.lineDef;
    if ( dif ) return dif;
    return (( SEG * ) ptr1)->Data.flip - (( SEG * ) ptr2)->Data.flip;
}

//----------------------------------------------------------------------------
//  If the given SEGs form a proper NODE but don't all belong to the same
//    sector, artificially break up the NODE by sector.  SEGs are arranged
//    so that SEGs belonging to sectors that should be kept unique are listed
//    first, followed by those that are not - and sorted by linedef for each
//    category.
//----------------------------------------------------------------------------

static int SortBySector ( const void *ptr1, const void *ptr2 )
{
    FUNCTION_ENTRY ( NULL, "SortBySector", true );

    int dif;
    int sector1 = (( SEG * ) ptr1)->Sector;
    int sector2 = (( SEG * ) ptr2)->Sector;
    dif = keepUnique [ sector2 ] - keepUnique [ sector1 ];
    if ( dif ) return dif;
    dif = sector1 - sector2;
    if ( dif ) return dif;
    return SortByLineDef ( ptr1, ptr2 );
}

static void SortSectors ( SEG *seg, int noSegs, int *noLeft, int *noRight )
{
    FUNCTION_ENTRY ( NULL, "SortSectors", true );

    qsort ( seg, noSegs, sizeof ( SEG ), SortBySector );

    // Seperate the 1st keep-unique sector - leave the rest
    int sector = seg->Sector;
    int i;
    for ( i = 0; seg [i].Sector == sector; i++ ) ;

    *noRight = i;
    *noLeft = noSegs - i;
}

#if defined ( DEBUG )
static void DumpSegs ( SEG *seg, int noSegs )
{
    FUNCTION_ENTRY ( NULL, "DumpSegs", true );

    for ( int i = 0; i < noSegs; i++ ) {
        wVertex *vertS = &newVertices [ seg->Data.start ];
        wVertex *vertE = &newVertices [ seg->Data.end ];
        int alias = lineDefAlias [ seg->Data.lineDef ].index;
        WARNING (( lineUsed [ alias ] ? '*' : ' ' ) <<
                  " lineDef: " << seg->Data.lineDef <<
                  " (" << vertS->x << "," << vertS->y << ") -" << 
                  " (" << vertE->x << "," << vertE->y << ")" );
        seg++;
    }
}
#endif

static void SortSegs ( SEG *pSeg, SEG *seg, int noSegs, int *noLeft, int *noRight, int *noSplits )
{
    FUNCTION_ENTRY ( NULL, "SortSegs", true );

    int count [3];

    if ( pSeg == NULL ) {
#if defined ( DEBUG )
        // Make sure that all SEGs are actually on the right side of each other
        ComputeStaticVariables ( seg );

        count [0] = count [1] = count [2] = 0;
        for ( int i = 0; i < noSegs; i++ ) {
            count [( seg [i].Side = WhichSide ( &seg [i] )) + 1 ]++;
        }

        if ( count [0] || count [1] ) {
            DumpSegs ( seg, noSegs );
            ERROR ( "Something weird is going on! (" << count [0] << "|" << count [1] << "|" << count [2] << ") " << noSegs );
        }
#endif
        *noRight  = noSegs;
        *noSplits = 0;
        *noLeft   = 0;
        return;
    }

    ComputeStaticVariables ( pSeg );

    count [0] = count [1] = count [2] = 0;
    int i;
    for ( i = 0; i < noSegs; i++ ) {
        count [( seg [i].Side = WhichSide ( &seg [i] )) + 1 ]++;
    }

    *noLeft = count [0], *noSplits = count [1], *noRight = count [2];

    ASSERT (( *noLeft != 0 ) || ( *noSplits != 0 ));

    SEG *rSeg = seg;
    for ( i = 0; seg [i].Side == SIDE_RIGHT; i++ ) rSeg++;

    if (( i < count [2] ) || count [1] ) {
        SEG *sSeg = tempSeg;
        SEG *lSeg = sSeg + *noSplits;
        for ( ; i < noSegs; i++ ) {
            switch ( seg [i].Side ) {
                case SIDE_LEFT  : *lSeg++ = seg [i];		break;
                case SIDE_SPLIT : *sSeg++ = seg [i];		break;
                case SIDE_RIGHT : *rSeg++ = seg [i];		break;
            }
        }
        memcpy ( rSeg, tempSeg, ( noSegs - count [2] ) * sizeof ( SEG ));
    }
    return;
}

//----------------------------------------------------------------------------
//  Use the requested algorithm to select a partition for the list of SEGs.
//    After a valid partition is selected, the SEGs are re-ordered.  All SEGs
//    to the right of the partition are placed first, then those that will
//    be split, followed by those that are to the left.
//----------------------------------------------------------------------------

static bool ChoosePartition ( SEG *seg, int noSegs, int *noLeft, int *noRight, int *noSplits )
{
    FUNCTION_ENTRY ( NULL, "ChoosePartition", true );

    memcpy ( lineChecked, lineUsed, sizeof ( bool ) * noAliases );

    SEG *pSeg = PartitionFunction ( seg, noSegs );

    SortSegs ( pSeg, seg, noSegs, noLeft, noRight, noSplits );

    return pSeg ? true : false;
}

//----------------------------------------------------------------------------
//  ALGORITHM 1: 'ZenNode Classic'
//    This is the original algorithm used by ZenNode.  It simply attempts
//    to minimize the number of SEGs that are split.  This actually yields
//    very small BSP trees, but usually results in trees that are not well
//    balanced and run deep.
//----------------------------------------------------------------------------

static SEG *Algorithm1 ( SEG *segs, int noSegs )
{
    FUNCTION_ENTRY ( NULL, "Algorithm1", true );

    SEG *pSeg = NULL, *testSeg = segs;
    int count [3];
    int &lCount = count [0], &sCount = count [1], &rCount = count [2];
    // Compute the maximum value maxMetric can possibly reach
    long bestMetric = ( noSegs / 2 ) * ( noSegs - noSegs / 2 );
    long maxMetric = 0x80000000, maxSplits = 0x7FFFFFFF;

    for ( int i = 0; i < noSegs; i++ ) {
        if ( showProgress && (( i & 15 ) == 0 )) ShowProgress ();
        int alias = lineDefAlias [ testSeg->Data.lineDef ].index;
        if ( ! lineChecked [ alias ]) {
            lineChecked [ alias ] = true;
            count [0] = count [1] = count [2] = 0;
            ComputeStaticVariables ( testSeg );
            if ( maxMetric < 0 ) for ( int j = 0; j < noSegs; j++ ) {
                count [ WhichSide ( &segs [j] ) + 1 ]++;
            } else for ( int j = 0; j < noSegs; j++ ) {
                count [ WhichSide ( &segs [j] ) + 1 ]++;
                if ( sCount > maxSplits ) goto next;
            }
            // Only consider SEG if it is not a boundary line
            if ( lCount + sCount ) {
                long metric = ( long ) lCount * ( long ) rCount;
                if ( sCount ) {
                    long temp = X1 * sCount;
                    if ( X2 < temp ) metric = X2 * metric / temp;
                    metric -= ( X3 * sCount + X4 ) * sCount;
                }
                if ( ANGLE & 0x3FFF ) metric--;
                if ( metric == bestMetric ) return testSeg;
                if ( metric > maxMetric ) {
                    pSeg = testSeg;
                    maxSplits = sCount + 2;
                    maxMetric = metric;
                }
            } else {
                // Eliminate outer edges of the map from here & down
                *convexPtr++ = alias;
            }
        }
next:
        testSeg++;
    }
    return pSeg;
}

//----------------------------------------------------------------------------
//  ALGORITHM 2: 'ZenNode Quality'
//    This is the 2nd algorithm used by ZenNode.  It attempts to keep the
//    resulting BSP tree balanced based on the number of sectors on each side of
//    the partition line in addition to the number of SEGs.  This seems more 
//    reasonable since a given SECTOR is usually made up of one or more SSECTORS.
//----------------------------------------------------------------------------

int sortTotalMetric ( const void *ptr1, const void *ptr2 )
{
    FUNCTION_ENTRY ( NULL, "sortTotalMetric", true );

    int dif;
    dif = (( sScoreInfo * ) ptr1)->invalid - (( sScoreInfo * ) ptr2)->invalid;
    if ( dif ) return dif;
    dif = (( sScoreInfo * ) ptr1)->total - (( sScoreInfo * ) ptr2)->total;
    if ( dif ) return dif;
    dif = (( sScoreInfo * ) ptr1)->index - (( sScoreInfo * ) ptr2)->index;
    return dif;
}

int sortMetric1 ( const void *ptr1, const void *ptr2 )
{
    FUNCTION_ENTRY ( NULL, "sortMetric1", true );

    if ((( sScoreInfo * ) ptr2)->metric1 < (( sScoreInfo * ) ptr1)->metric1 ) return -1;
    if ((( sScoreInfo * ) ptr2)->metric1 > (( sScoreInfo * ) ptr1)->metric1 ) return  1;
    if ((( sScoreInfo * ) ptr2)->metric2 < (( sScoreInfo * ) ptr1)->metric2 ) return -1;
    if ((( sScoreInfo * ) ptr2)->metric2 > (( sScoreInfo * ) ptr1)->metric2 ) return  1;
    return (( sScoreInfo * ) ptr1)->index - (( sScoreInfo * ) ptr2)->index;
}

int sortMetric2 ( const void *ptr1, const void *ptr2 )
{
    FUNCTION_ENTRY ( NULL, "sortMetric2", true );

    if ((( sScoreInfo * ) ptr2)->metric2 < (( sScoreInfo * ) ptr1)->metric2 ) return -1;
    if ((( sScoreInfo * ) ptr2)->metric2 > (( sScoreInfo * ) ptr1)->metric2 ) return  1;
    if ((( sScoreInfo * ) ptr2)->metric1 < (( sScoreInfo * ) ptr1)->metric1 ) return -1;
    if ((( sScoreInfo * ) ptr2)->metric1 > (( sScoreInfo * ) ptr1)->metric1 ) return  1;
    return (( sScoreInfo * ) ptr1)->index - (( sScoreInfo * ) ptr2)->index;
}

static SEG *Algorithm2 ( SEG *segs, int noSegs )
{
    FUNCTION_ENTRY ( NULL, "Algorithm2", true );

    SEG *testSeg = segs;
    int count [3], noScores = 0, rank, i;
    int &lCount = count [0], &sCount = count [1], &rCount = count [2];

    memset ( score, -1, sizeof ( sScoreInfo ) * noAliases );
    score [0].index = 0;

    for ( i = 0; i < noSegs; i++ ) {
        if ( showProgress && (( i & 15 ) == 0 )) ShowProgress ();
        int alias = lineDefAlias [ testSeg->Data.lineDef ].index;
        if ( ! lineChecked [ alias ]) {
            lineChecked [ alias ] = true;
            count [0] = count [1] = count [2] = 0;
            ComputeStaticVariables ( testSeg );

            sScoreInfo *curScore = &score [noScores];
            curScore->invalid = 0;
            memset ( usedSector, 0, sizeof ( UCHAR ) * sectorCount );
            SEG *destSeg = segs;
            for ( int j = 0; j < noSegs; j++, destSeg++ ) {
                switch ( WhichSide ( destSeg )) {
                    case SIDE_LEFT  : lCount++; usedSector [ destSeg->Sector ] |= 0xF0;	break;
                    case SIDE_SPLIT : if ( destSeg->DontSplit ) curScore->invalid++;
                                      sCount++; usedSector [ destSeg->Sector ] |= 0xFF;	break;
                    case SIDE_RIGHT : rCount++; usedSector [ destSeg->Sector ] |= 0x0F;	break;
                }
            }
            // Only consider SEG if it is not a boundary line
            if ( lCount + sCount ) {
                int lsCount = 0, rsCount = 0, ssCount = 0;
                for ( int j = 0; j < sectorCount; j++ ) {
                    switch ( usedSector [j] ) {
                        case 0xF0 : lsCount++;	break;
                        case 0xFF : ssCount++;	break;
                        case 0x0F : rsCount++;	break;
                    }
                }

                curScore->index = i;
                curScore->metric1 = ( long ) ( lCount + sCount ) * ( long ) ( rCount + sCount );
                curScore->metric2 = ( long ) ( lsCount + ssCount ) * ( long ) ( rsCount + ssCount );

                if ( sCount ) {
                    long temp = X1 * sCount;
                    if ( X2 < temp ) curScore->metric1 = X2 * curScore->metric1 / temp;
                    curScore->metric1 -= ( X3 * sCount + X4 ) * sCount;
                }
                if ( ssCount ) {
                    long temp = X1 * ssCount;
                    if ( X2 < temp ) curScore->metric2 = X2 * curScore->metric2 / temp;
                    curScore->metric2 -= ( X3 * ssCount + X4 ) * sCount;
                }

                noScores++;
            } else {
                // Eliminate outer edges of the map
                *convexPtr++ = alias;
            }
        }
        testSeg++;
    }

    if ( noScores > 1 ) {
        qsort ( score, noScores, sizeof ( sScoreInfo ), sortMetric1 );
        for ( rank = i = 0; i < noScores; i++ ) {
            score [i].total = rank;
            if ( score [i].metric1 != score [i+1].metric1 ) rank++;
        }
        qsort ( score, noScores, sizeof ( sScoreInfo ), sortMetric2 );
        for ( rank = i = 0; i < noScores; i++ ) {
            score [i].total += rank;
            if ( score [i].metric2 != score [i+1].metric2 ) rank++;
        }
        qsort ( score, noScores, sizeof ( sScoreInfo ), sortTotalMetric );
    }

    if ( noScores && score [0].invalid ) {
        int noBad = 0;
        for ( int i = 0; i < noScores; i++ ) if ( score [i].invalid ) noBad++;
        WARNING ( "Non-splittable linedefs have been split! ("<< noBad << "/" << noScores << ")" );
    }

    SEG *pSeg = noScores ? &segs [ score [0].index ] : NULL;
    return pSeg;
}

//----------------------------------------------------------------------------
//  ALGORITHM 3: 'ZenNode Lite'
//    This is a modified version of the original algorithm used by ZenNode.
//    It uses the same logic for picking the partition, but only looks at the
//    first 30 segs for a valid partition.  If none is found, the search is
//    continued until one is found or all segs have been searched.
//----------------------------------------------------------------------------

static SEG *Algorithm3 ( SEG *segs, int noSegs )
{
    FUNCTION_ENTRY ( NULL, "Algorithm3", true );

    SEG *pSeg = NULL, *testSeg = segs;
    int count [3];
    int &lCount = count [0], &sCount = count [1], &rCount = count [2];
    // Compute the maximum value maxMetric can possibly reach
    long bestMetric = ( long ) ( noSegs / 2 ) * ( long ) ( noSegs - noSegs / 2 );
    long maxMetric = 0x80000000, maxSplits = 0x7FFFFFFF;

    int i = 0, max = ( noSegs < 30 ) ? noSegs : 30;

retry:

    for ( ; i < max; i++ ) {
        if ( showProgress && (( i & 15 ) == 0 )) ShowProgress ();
        int alias = lineDefAlias [ testSeg->Data.lineDef ].index;
        if ( ! lineChecked [ alias ]) {
            lineChecked [ alias ] = true;
            count [0] = count [1] = count [2] = 0;
            ComputeStaticVariables ( testSeg );
            if ( maxMetric < 0 ) for ( int j = 0; j < noSegs; j++ ) {
                count [ WhichSide ( &segs [j] ) + 1 ]++;
            } else for ( int j = 0; j < noSegs; j++ ) {
                count [ WhichSide ( &segs [j] ) + 1 ]++;
                if ( sCount > maxSplits ) goto next;
            }
            if ( lCount + sCount ) {
                long metric = ( long ) lCount * ( long ) rCount;
                if ( sCount ) {
                    long temp = X1 * sCount;
                    if ( X2 < temp ) metric = X2 * metric / temp;
                    metric -= ( X3 * sCount + X4 ) * sCount;
                }
                if ( ANGLE & 0x3FFF ) metric--;
                if ( metric == bestMetric ) return testSeg;
                if ( metric > maxMetric ) {
                    pSeg = testSeg;
                    maxSplits = sCount;
                    maxMetric = metric;
                }
            } else {
                // Eliminate outer edges of the map from here & down
                *convexPtr++ = alias;
            }
        }
next:
        testSeg++;
    }
    if (( maxMetric == ( long ) 0x80000000 ) && ( max < noSegs )) {
        max += 5;
        if ( max > noSegs ) max = noSegs;
        goto retry;
    }

    return pSeg;
}

//----------------------------------------------------------------------------
//
//  Partition line:
//    DXùx - DYùy + C = 0               ³ DX  -DY ³ ³-C³
//  rSeg line:                          ³         ³=³  ³
//    dxùx - dyùy + c = 0               ³ dx  -dy ³ ³-c³
//
//----------------------------------------------------------------------------

static void DivideSeg ( SEG *rSeg, SEG *lSeg )
{
    FUNCTION_ENTRY ( NULL, "DivideSeg", true );

    wVertex *vertS = &newVertices [ rSeg->Data.start ];
    wVertex *vertE = &newVertices [ rSeg->Data.end ];

    // Determine which sided of the partition line the start point is on
    long sideS = ( long ) (( REAL ) ( DX * ( vertS->y - Y )) -
                           ( REAL ) ( DY * ( vertS->x - X )));

    // Minimum precision required to avoid overflow/underflow:
    //   dx, dy  - 16 bits required
    //   c       - 33 bits required
    //   det     - 32 bits required
    //   x, y    - 50 bits required

    int dx = vertE->x - vertS->x;
    int dy = vertE->y - vertS->y;
    int num = ( vertS->y - Y ) * DX - ( vertS->x - X ) * DY;
    int det = dx * DY - dy * DX;

    // If num is too big, try to reduce it
    if (( num > 0x00008000 ) || ( num < -0x00008000 )) {
        // Divide num & det by their GCD to reduce the chance of overflow
        int gcd = GCD ( num, det );
        num /= gcd;
        det /= gcd;
    }

    int x = ( int ) ( vertS->x + num * dx / det );
    int y = ( int ) ( vertS->y + num * dy / det );

    int newIndex = AddVertex ( x, y );

#if defined ( DEBUG )
    if (( rSeg->Data.start == newIndex ) || ( rSeg->Data.end == newIndex )) {
        wVertex *vertN = &newVertices [ newIndex ];
        fprintf ( stderr, "\nNODES: End point duplicated in DivideSeg: LineDef #%d", rSeg->Data.lineDef );
        fprintf ( stderr, "\n       Partition: from (%d,%d) to (%d,%d)", X, Y, X + DX, Y + DY );
        fprintf ( stderr, "\n       LineDef: from (%d,%d) to (%d,%d) split at (%d,%d)", vertS->x, vertS->y, vertE->x, vertE->y, vertN->x, vertN->y );
    }
#endif

    // Fill in the parts of lSeg & rSeg that have changed
    if ( sideS < 0 ) {
        rSeg->Data.end    = ( USHORT ) newIndex;
        lSeg->Data.start  = ( USHORT ) newIndex;
        lSeg->Data.offset += ( USHORT ) hypot (( double ) ( x - vertS->x ), ( double ) ( y - vertS->y ));
    } else {
        rSeg->Data.start  = ( USHORT ) newIndex;
        lSeg->Data.end    = ( USHORT ) newIndex;
        rSeg->Data.offset += ( USHORT ) hypot (( double ) ( x - vertS->x ), ( double ) ( y - vertS->y ));
    }

#if defined ( DEBUG )
    if ( _WhichSide ( rSeg ) != SIDE_RIGHT ) {
        fprintf ( stderr, "DivideSeg: %s split invalid\n", "right" );
    }
    if ( _WhichSide ( lSeg ) != SIDE_LEFT ) {
        fprintf ( stderr, "DivideSeg: %s split invalid\n", "left" );
    }
#endif
}

//----------------------------------------------------------------------------
//  Split the list of SEGs in two and adjust each copy to the appropriate
//    values.
//----------------------------------------------------------------------------

static void SplitSegs ( SEG *segs, int noSplits )
{
    FUNCTION_ENTRY ( NULL, "SplitSegs", true );

    segCount += noSplits;
    if ( segCount > maxSegs ) {
        fprintf ( stderr, "\nError: Too many SEGs have been split!\n" );
        exit ( -1 );
    }

    int count = segCount - ( segs - segStart ) - noSplits;
    memmove ( segs + noSplits, segs, count * sizeof ( SEG ));

    for ( int i = 0; i < noSplits; i++ ) {
        DivideSeg ( segs, segs + noSplits );
        segs++;
    }
}

//----------------------------------------------------------------------------
//  Choose a SEG and partition the list of SEGs.  Verify that the partition
//    selected is valid and calculate the necessary data for the NODE.  If
//    no valid partition could be found, return NULL to indicate that the
//    list of SEGs forms a valid SSECTOR.
//----------------------------------------------------------------------------

static bool PartitionNode ( NODE *node, SEG *rSegs, int noSegs, int *noLeft, int *noRight )
{
    FUNCTION_ENTRY ( NULL, "PartitionNode", true );

    int noSplits;

    if ( ChoosePartition ( rSegs, noSegs, noLeft, noRight, &noSplits ) == false ) {

        if ( uniqueSubsectors ) {
            memset ( usedSector, false, sizeof ( UCHAR ) * sectorCount );
            int i;
            for ( i = 0; i < noSegs; i++ ) {
                usedSector [ rSegs [i].Sector ] = true;
            }
            int noSectors = 0;
            for ( i = 0; i < sectorCount; i++ ) {
                if ( usedSector [i] ) noSectors++;
            }
            if ( noSectors > 1 ) for ( i = 0; noSectors && ( i < sectorCount ); i++ ) {
                if ( usedSector [i] ) {
                    if ( keepUnique [i] ) goto NonUnique;
                    noSectors--;
                }
            }
        }

        // Splits may have 'upset' the lineDef ordering - some special effects
        //   assume the SEGS appear in the same order as the lineDefs
        if ( noSegs > 1 ) {
            qsort ( rSegs, noSegs, sizeof ( SEG ), SortByLineDef );
        }

        return false;

NonUnique:

        ComputeStaticVariables ( rSegs );
        SortSectors ( rSegs, noSegs, noLeft, noRight );

    } else if ( noSplits ) {

        SplitSegs ( &rSegs [ *noRight ], noSplits );
        *noLeft  += noSplits;
        *noRight += noSplits;

    }

    node->Data.x  = ( SHORT ) X;
    node->Data.y  = ( SHORT ) Y;
    node->Data.dx = ( SHORT ) DX;
    node->Data.dy = ( SHORT ) DY;

    SEG *lSegs = &rSegs [ *noRight ];
    FindBounds ( &node->Data.side [0], rSegs, *noRight );
    FindBounds ( &node->Data.side [1], lSegs, *noLeft );

    return true;
}

//----------------------------------------------------------------------------
//  Recursively create the actual NODEs.  The given list of SEGs is analyzed
//    and a partition is chosen.  If no partition can be found, a leaf NODE
//    is created.  Otherwise, the right and left SEGs are analyzed.  Features:
//     A list of 'convex' aliases is maintained.  These are lines that border
//      the list of SEGs and can never be partitions.  A line is marked as
//      convex for this and all children, and unmarked before returing.
//     Similarly, the alias chosen as the partition is marked as convex
//      since it will be convex for all children.
//----------------------------------------------------------------------------
static NODE *CreateNode ( NODE *prev, SEG *rSegs, int &noSegs, SEG *&nextSeg )
{
    FUNCTION_ENTRY ( NULL, "CreateNode", true );

    if ( nodesLeft-- == 0 ) {
        fprintf ( stderr, "ERROR: nodePool exhausted\n" );
        exit ( -1 );
    }

    NODE *node = nodePool++;
    node->Next = NULL;
    if ( prev ) prev->Next = node;

    int noLeft, noRight;
    int *cptr = convexPtr;

    if (( noSegs <= 1 ) || ( PartitionNode ( node, rSegs, noSegs, &noLeft, &noRight ) == false )) {

        convexPtr = cptr;
        if ( nodeStart == NULL ) nodeStart = node;
        node->id = ( USHORT ) ( 0x8000 | CreateSSector ( noSegs, rSegs ));
        if ( showProgress ) ShowDone ();
        nextSeg = &rSegs [ noSegs ];
        return node;
    }

    int alias = currentAlias->index;

    lineUsed [ alias ] = true;
    for ( int *tempPtr = cptr; tempPtr != convexPtr; tempPtr++ ) {
        lineUsed [ *tempPtr ] = true;
    }

    SEG *lSegs;

    if ( showProgress ) GoRight ();
    NODE *rNode = CreateNode ( prev, rSegs, noRight, lSegs );
    node->Data.child [0] = rNode->id;

    if ( showProgress ) GoLeft ();
    NODE *lNode = CreateNode ( rNode, lSegs, noLeft, lSegs );
    node->Data.child [1] = lNode->id;

    while ( convexPtr != cptr ) lineUsed [ *--convexPtr ] = false;
    lineUsed [ alias ] = false;

    if ( showProgress ) Backup ();

    lNode->Next = node;
    node->id = ( USHORT ) nodeCount++;

    if ( showProgress ) ShowDone ();

    noSegs = noLeft + noRight;
    nextSeg = &rSegs [ noSegs ];

    return node;
}

wVertex  *GetVertices ()
{
    FUNCTION_ENTRY ( NULL, "GetVertices", true );

    wVertex *vert = new wVertex [ noVertices ];
    memcpy ( vert, newVertices, sizeof ( wVertex ) * noVertices );
    return vert;
}

wSSector *GetSSectors ( wSSector *first )
{
    FUNCTION_ENTRY ( NULL, "GetSSectors", true );

    wSSector *ssector = new wSSector [ ssectorCount ];
    memcpy ( ssector, first, sizeof ( wSSector ) * ssectorCount );
    return ssector;
}

wSegs *GetSegs ()
{
    FUNCTION_ENTRY ( NULL, "GetSegs", true );

    wSegs *segs = new wSegs [ segCount ];
    for ( int i = 0; i < segCount; i++ ) {
        segs [i] = segStart [i].Data;
    }
    delete [] segStart;
    return segs;
}

wNode *GetNodes ()
{
    FUNCTION_ENTRY ( NULL, "GetNodes", true );

    wNode *nodes = new wNode [ nodeCount ];
    for ( int i = 0; i < nodeCount; i++ ) {
        while ( nodeStart->id & 0x8000 ) {
            nodeStart = nodeStart->Next;
        }
        nodes [i] = nodeStart->Data;
        nodeStart = nodeStart->Next;
    }
    return nodes;
}

//----------------------------------------------------------------------------
//  Wrapper function that calls all the necessary functions to prepare the
//    BSP tree and insert the new data into the level.  All screen I/O is
//    done in this routine (with the exception of progress indication).
//----------------------------------------------------------------------------

void CreateNODES ( DoomLevel *level, sBSPOptions *options )
{
    FUNCTION_ENTRY ( NULL, "CreateNODES", true );

    TRACE ( "Processing " << level->Name ());

    if ( X2 == 0 ) X2 = 1;
    if ( Y2 == 0 ) Y2 = 1;

    showProgress = options->showProgress;
    uniqueSubsectors = options->keepUnique ? true : false;
    PartitionFunction = Algorithm1;
    if ( options->algorithm == 2 ) PartitionFunction = Algorithm2;
    if ( options->algorithm == 3 ) PartitionFunction = Algorithm3;
    	    				  
    nodeStart = NULL;
    segStart = NULL;
    nodeCount = 0;
    ssectorCount = 0;

    level->NewSegs ( 0, NULL );
    level->TrimVertices ();
    level->PackVertices ();

    noVertices = level->VertexCount ();
    sectorCount = level->SectorCount ();
    usedSector = new UCHAR [ sectorCount ];
    keepUnique = new bool [ sectorCount ];
    if ( options->keepUnique ) {
        memcpy ( keepUnique, options->keepUnique, sizeof ( bool ) * sectorCount );
    } else {
        memset ( keepUnique, true, sizeof ( bool ) * sectorCount );
    }
    maxVertices = ( int ) ( noVertices * FACTOR_VERTEX );
    newVertices = new wVertex [ maxVertices ];
    memcpy ( newVertices, level->GetVertices (), sizeof ( wVertex ) * noVertices );
    vertSplitAlias = new sAlias * [ maxVertices ];
    memset ( vertSplitAlias, -1, sizeof ( sAlias * ) * maxVertices );

    Status ( "Creating SEGS ... " );
    segStart = CreateSegs ( level, options );

    if ( options->algorithm != 3 ) {

        Status ( "Getting LineDef Aliases ... " );
        SEG **aliasList = GetLineDefAliases ( level );

        lineChecked = new bool [ noAliases ];
        lineUsed = new bool [ noAliases ];
        memset ( lineUsed, false, sizeof ( bool ) * noAliases );

        Status ( "Getting Sector Bounds ... " );
        wBound *bound = GetSectorBounds ( level );
        sSectorInfo *sectInfo = GetSectorInfo ( sectorCount, bound );

        Status ( "Creating Side Info ... " );
        CreateSideInfo ( level, bound, sectInfo, aliasList );

        delete [] ( char * ) sectInfo;
        delete [] bound;
        delete [] aliasList;

    } else {

        noAliases = level->LineDefCount ();
        lineDefAlias = new sAlias [ noAliases ];
        memset ( lineDefAlias, 0, sizeof ( sAlias ) * noAliases );
        int i;
        for ( i = 0; i < noAliases; i++ ) {
            lineDefAlias [i].index = i;
        }

        // Set all sideInfo entries to SIDE_SPLIT
        sideInfo = new char * [ noAliases ];
        sideInfo [0] = new char [ segCount ];
        for ( i = 1; i < noAliases; i++ ) sideInfo [i] = sideInfo [0];
        for ( i = 0; i < segCount; i++ ) sideInfo [0][i] = SIDE_SPLIT;

        lineChecked = new bool [ noAliases ];
        lineUsed = new bool [ noAliases ];
        memset ( lineUsed, false, sizeof ( bool ) * noAliases );

    }

    score = ( options->algorithm == 2 ) ? new sScoreInfo [ noAliases ] : NULL; 
    convexList = new int [ noAliases ];	  
    convexPtr = convexList;

    Status ( "Creating NODES ... " );
    int noSegs = segCount;
    SEG *endSeg;
    NODE *firstNode = nodePool = new NODE [ nodesLeft = ( int ) ( FACTOR_NODE * level->LineDefCount ()) ];
    wSSector *firstSSector = ssectorPool = new wSSector [ ssectorsLeft = ( int ) ( FACTOR_SSECTOR * level->SectorCount ()) ];
    CreateNode ( NULL, segStart, noSegs, endSeg );    		 		    

    delete [] convexList;
    if ( score ) delete [] score;

    // Clean Up temporary buffers
    Status ( "Cleaning up ... " );
    delete [] sideInfo;
    delete [] lineDefAlias;
    delete [] lineChecked;
    delete [] lineUsed;
    delete [] keepUnique;
    delete [] usedSector;

    sideInfo = NULL;

    level->NewVertices ( noVertices, GetVertices ());
    level->NewSegs ( segCount, GetSegs ());
    level->NewSubSectors ( ssectorCount, GetSSectors ( firstSSector ));
    level->NewNodes ( nodeCount, GetNodes ());

    delete [] vertSplitAlias;
    delete [] newVertices;
    delete [] firstSSector;
    delete [] firstNode;
    delete [] tempSeg;
}
