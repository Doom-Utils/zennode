//----------------------------------------------------------------------------
//
// File:        ZenNode.cpp
// Date:        26-Oct-1994
// Programmer:  Marc Rousseau
//
// Description: This module contains the logic for the NODES builder.
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
//   02-26-01   Removed vertSplitAlias - switched to 64-bit ints to solve overflow problem
//   03-05-01   Fixed _WhichSide & DivideSeg so that things finally(?) work!
//   03-05-01   Switched to 64-bit ints to solve overflow problem
//   04-13-02   Changed vertex & ssector allocation to reallocate the free pool
//   04-14-02   Fixed _WhichSide so that things finally(?) work! :-)
//   04-16-02   Added more consistancy checks in DEBUG mode
//   04-20-02   Switched to floating point math
//   04-24-02   Fixed bug in _WhichSide for zero-length SEGs
//   05-02-02   Simplified CreateNode & eliminated NODES structure
//   05-04-02   Simplified use of aliases and flipping in WhichSide 
//   05-05-02   Fixed double-int conversions to round correctly
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

#define ANGLE_MASK              0x7FFF

#define EPSILON                 0.0001

// Emperical values derived from a test of numerous .WAD files
#define FACTOR_VERTEX		1.0		//  1.662791 - ???
#define FACTOR_SEGS		2.0		//  1.488095 - ???
#define FACTOR_NODE		0.6		//  0.590830 - MAP01 - csweeper.wad

static int       maxSegs;
static int       maxVertices;

static int       nodesLeft;
static wNode    *nodePool;
static int       nodeCount;			// Number of NODES stored

static SEG      *tempSeg;
static SEG      *segStart;
static int       segCount;			// Number of SEGS stored

static int       ssectorsLeft;
static wSSector *ssectorPool;
static int       ssectorCount;			// Number of SSECTORS stored

static wVertex  *newVertices;
static int       noVertices;

//
// Variables used by WhichSide to speed up side calculations
//
static char     *currentSide;
static int       currentAlias;

static int      *convexList;
static int      *convexPtr;
static int       sectorCount;

static int       showProgress;
static UCHAR    *usedSector;
static bool     *keepUnique;
static bool      uniqueSubsectors;
static char     *lineUsed;
static char     *lineChecked;
static int       noAliases;
static int      *lineDefAlias;
static char    **sideInfo;
static double    DY, DX, X, Y;
static long      ANGLE;

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

//----------------------------------------------------------------------------
//  Create a list of SEGs from the *important* sidedefs.  A sidedef is
//    considered important if:
//     - It has non-zero length
//     - It's linedef has different sectors on each side
//     - It has at least one visible texture
//----------------------------------------------------------------------------

static SEG *CreateSegs ( DoomLevel *level, sBSPOptions *options )
{
    FUNCTION_ENTRY ( NULL, "CreateSegs", true );

    // Get a rough count of how many SideDefs we're starting with
    segCount = maxSegs = 0;
    const wLineDef *lineDef = level->GetLineDefs ();
    const wSideDef *sideDef = level->GetSideDefs ();

    for ( int i = 0; i < level->LineDefCount (); i++ ) {
        if ( lineDef [i].sideDef [0] != NO_SIDEDEF ) maxSegs++;
        if ( lineDef [i].sideDef [1] != NO_SIDEDEF ) maxSegs++;
    }
    tempSeg  = new SEG [ maxSegs ];
    maxSegs  = ( int ) ( maxSegs * FACTOR_SEGS );
    segStart = new SEG [ maxSegs ];
    memset ( segStart, 0, sizeof ( SEG ) * maxSegs );

    SEG *seg = segStart;
    for ( int i = 0; i < level->LineDefCount (); i++, lineDef++ ) {

        wVertex *vertS = &newVertices [ lineDef->start ];
        wVertex *vertE = &newVertices [ lineDef->end ];
        long dx = vertE->x - vertS->x;
        long dy = vertE->y - vertS->y;
        if (( dx == 0 ) && ( dy == 0 )) continue;

        int rSide = lineDef->sideDef [0];
        int lSide = lineDef->sideDef [1];
        const wSideDef *sideRight = ( rSide == NO_SIDEDEF ) ? ( const wSideDef * ) NULL : &sideDef [ rSide ];
        const wSideDef *sideLeft  = ( lSide == NO_SIDEDEF ) ? ( const wSideDef * ) NULL : &sideDef [ lSide ];

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
            seg->start.x      = vertS->x;
            seg->start.y      = vertS->y;
            seg->end.x        = vertE->x;
            seg->end.y        = vertE->y;
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
            seg->start.x      = vertE->x;
            seg->start.y      = vertE->y;
            seg->end.x        = vertS->x;
            seg->end.y        = vertS->y;
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

    if ( pSeg->final == false ) {

        currentAlias   = lineDefAlias [ pSeg->Data.lineDef ];
        currentSide    = sideInfo ? sideInfo [ currentAlias ] : NULL;

        wVertex *vertS = &newVertices [ pSeg->AliasFlip ? pSeg->Data.end : pSeg->Data.start ];
        wVertex *vertE = &newVertices [ pSeg->AliasFlip ? pSeg->Data.start : pSeg->Data.end ];
        X     = vertS->x;
        Y     = vertS->y;
        DX    = vertE->x - vertS->x;
        DY    = vertE->y - vertS->y;

    } else {

        currentAlias = 0;

        X     = pSeg->start.x;
        Y     = pSeg->start.y;
        DX    = pSeg->end.x - pSeg->start.x;
        DY    = pSeg->end.y - pSeg->start.y;

    }

    ANGLE = pSeg->Data.angle;
}

//----------------------------------------------------------------------------
//  Determine if the given SEG is co-linear (ie: they lie on the same line)
//    with the currently selected partition.
//----------------------------------------------------------------------------

static bool CoLinear ( SEG *seg )
{
    FUNCTION_ENTRY ( NULL, "CoLinear", true );

    // If they're not at the same angle ( ñ180ø ), bag it
    if (( ANGLE & ANGLE_MASK ) != ( seg->Data.angle & ANGLE_MASK )) return false;

    // Do the math stuff
    if ( DX == 0.0 ) return ( seg->start.x == X ) ? true : false;
    if ( DY == 0.0 ) return ( seg->start.y == Y ) ? true : false;

    // Rotate vertS about (X,Y) by é degrees to get y offset
    //   Y = Hùsin(é)           ³  1  0  0 ³³ cos(é)  -sin(é)  0 ³
    //   X = Hùcos(é)    ³x y 1³³  0  1  0 ³³ sin(é)   cos(é)  0 ³
    //   H = (Xý+Yý)^«         ³ -X -Y  1 ³³   0         0    1 ³

    double y = DX * ( seg->start.y - Y ) - DY * ( seg->start.x - X );

    return ( y == 0.0 ) ? true : false;
}

//----------------------------------------------------------------------------
//  Given a list of SEGs, determine the bounding rectangle.
//----------------------------------------------------------------------------

static void FindBounds ( wBound *bound, SEG *seg, int noSegs )
{
    FUNCTION_ENTRY ( NULL, "FindBounds", true );

    bound->minx = bound->maxx = ( SHORT ) seg->start.x;
    bound->miny = bound->maxy = ( SHORT ) seg->start.y;

    for ( int i = 0; i < noSegs; i++ ) {

        int startX = lrint ( seg [i].start.x );
        int endX   = lrint ( seg [i].end.x );
        int startY = lrint ( seg [i].start.y );
        int endY   = lrint ( seg [i].end.y );

        int loX = startX, hiX = startX;
        if ( loX < endX ) hiX = endX; else loX = endX;
        int loY = startY, hiY = startY;
        if ( loY < endY ) hiY = endY; else loY = endY;

        if ( loX < bound->minx ) bound->minx = ( SHORT ) loX;
        if ( hiX > bound->maxx ) bound->maxx = ( SHORT ) hiX;
        if ( loY < bound->miny ) bound->miny = ( SHORT ) loY;
        if ( hiY > bound->maxy ) bound->maxy = ( SHORT ) hiY;
    }
}

//----------------------------------------------------------------------------
//  Determine which side of the partition line the given SEG lies.  A quick
//    check is made based on the sector containing the SEG.  If the sector
//    is split by the partition, a more detailed examination is made using
//    _WhichSide.
//
//    Returns:
//       -1 - SEG is on the left of the partition
//        0 - SEG is split by the partition
//       +1 - SEG is on the right of the partition
//----------------------------------------------------------------------------

static int _WhichSide ( SEG *seg )
{
    FUNCTION_ENTRY ( NULL, "_WhichSide", true );

    sVertex *vertS = &seg->start;
    sVertex *vertE = &seg->end;
    double y1, y2;

    if ( DX == 0.0 ) {
        if ( DY > 0.0 ) {
            y1 = ( X - vertS->x ),    y2 = ( X - vertE->x );
        } else {
            y1 = ( vertS->x - X ),    y2 = ( vertE->x - X );
        }
    } else if ( DY == 0.0 ) {
        if ( DX > 0.0 ) {
            y1 = ( vertS->y - Y ),    y2 = ( vertE->y - Y );
        } else {
            y1 = ( Y - vertS->y ),    y2 = ( Y - vertE->y );
        }
    } else {
        y1 = DX * ( vertS->y - Y ) - DY * ( vertS->x - X );
        y2 = DX * ( vertE->y - Y ) - DY * ( vertE->x - X );

        if ( seg->final == true ) {
            const wLineDef *lineDef = seg->LineDef;
            wVertex *vertS = &newVertices [ lineDef->start ];
            wVertex *vertE = &newVertices [ lineDef->end ];

            double dx = vertE->x - vertS->x;
            double dy = vertE->y - vertS->y;
            double det = dx * DY - dy * DX;
            if ( det != 0.0 ) {
                double num = DX * ( vertS->y - Y ) - DY * ( vertS->x - X );
                double x = lrint ( vertS->x + num * dx / det );
                double y = lrint ( vertS->y + num * dy / det );
                if (( seg->start.x == x ) && ( seg->start.y == y )) y1 = 0.0;
                if (( seg->end.x == x ) && ( seg->end.y == y )) y2 = 0.0;
            }
        }
    }

    if ( fabs ( y1 ) < EPSILON ) y1 = 0.0;
    if ( fabs ( y2 ) < EPSILON ) y2 = 0.0;

    // If its co-linear, decide based on direction
    if (( y1 == 0.0 ) && ( y2 == 0.0 )) {
        double x1 = DX * ( vertS->x - X ) + DY * ( vertS->y - Y );
        double x2 = DX * ( vertE->x - X ) + DY * ( vertE->y - Y );
        return ( x1 <= x2 ) ? SIDE_RIGHT : SIDE_LEFT;
    }

    // Otherwise:
    //   Left   -1 : ( y1 >= 0 ) && ( y2 >= 0 )
    //   Both    0 : (( y1 < 0 ) && ( y2 > 0 )) || (( y1 > 0 ) && ( y2 < 0 ))
    //   Right   1 : ( y1 <= 0 ) && ( y2 <= 0 )

    return ( y1 <  0.0 ) ? (( y2 <= 0.0 ) ? SIDE_RIGHT : SIDE_SPLIT ) :
           ( y1 == 0.0 ) ? (( y2 <= 0.0 ) ? SIDE_RIGHT : SIDE_LEFT  ) :
                           (( y2 >= 0.0 ) ? SIDE_LEFT  : SIDE_SPLIT );
}

static inline int WhichSide ( SEG *seg )
{
    FUNCTION_ENTRY ( NULL, "WhichSide", true );

    // Treat split partition/seg differently
    if (( seg->Split == true ) || ( currentAlias == 0 )) {
        return _WhichSide ( seg );
    }

    // See if partition & seg lie on the same line
    int alias = lineDefAlias [ seg->Data.lineDef ];
    if ( alias == currentAlias ) {
        return seg->AliasFlip ^ SIDE_RIGHT;
    }

    // See if we've already categorized the LINEDEF for this SEG
    int side = currentSide [ seg->Data.lineDef ];
    if ( IS_LEFT_RIGHT ( side )) return side;

    side = _WhichSide ( seg );

    if ( seg->Split == false ) {
        currentSide [ seg->Data.lineDef ] = ( char ) side;
    }

    return side;
}

#if defined ( DEBUG )

    static int dbgWhichSide ( SEG *seg )
    {
        FUNCTION_ENTRY ( NULL, "dbgWhichSide", true );

        int side = WhichSide ( seg );
        if ( side != _WhichSide ( seg )) {
            ERROR ( "WhichSide is wigging out!" );
        }
        return side;
    }

#define WhichSide dbgWhichSide

#endif

//----------------------------------------------------------------------------
//  Create a list of aliases vs LINEDEFs that indicates which side of a given
//    alias a LINEDEF is on.
//----------------------------------------------------------------------------

static void CreateSideInfo ( DoomLevel *level )
{
    FUNCTION_ENTRY ( NULL, "CreateSideInfo", true );

    long size = ( sizeof ( char * ) + level->LineDefCount ()) * ( long ) noAliases;
    char *temp = new char [ size ];

    sideInfo = ( char ** ) temp;
    memset ( temp, 0, sizeof ( char * ) * noAliases );

    temp += sizeof ( char * ) * noAliases;
    memset ( temp, SIDE_UNKNOWN, level->LineDefCount () * noAliases );

    for ( int i = 0; i < noAliases; i++ ) {
        sideInfo [i] = ( char * ) temp;
        temp += level->LineDefCount ();
    }
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

    if ( noVertices == maxVertices ) {
        maxVertices = ( 110 * maxVertices ) / 100;
        newVertices = ( wVertex * ) realloc ( newVertices, sizeof ( wVertex ) * maxVertices );
    }

    newVertices [ noVertices ].x = ( USHORT ) x;
    newVertices [ noVertices ].y = ( USHORT ) y;

    return noVertices++;
}

//----------------------------------------------------------------------------
//  Create a SSECTOR and record the index of the 1st SEG and the total number
//  of SEGs.
//----------------------------------------------------------------------------

static USHORT CreateSSector ( SEG *segs, int noSegs )
{
    FUNCTION_ENTRY ( NULL, "CreateSSector", true );

    if ( ssectorsLeft-- == 0 ) {
        int delta     = ( 10 * ssectorCount ) / 100;
        ssectorPool   = ( wSSector * ) realloc ( ssectorPool, sizeof ( wSSector ) * ( ssectorCount + delta ));
        ssectorsLeft += delta;
    }

    int count = noSegs;
    int first = segs - segStart;

    // Eliminate zero length SEGs and assign vertices
    for ( int i = 0; i < noSegs; i++ ) {
        if (( segs->start.x == segs->end.x ) && ( segs->start.y == segs->end.y )) {
            memcpy ( segs, segs + 1, sizeof ( SEG ) * ( noSegs - i - 1 ));
            count--;
        } else {
            segs->Data.start = ( USHORT ) AddVertex ( lrint ( segs->start.x ), lrint ( segs->start.y ));
            segs->Data.end   = ( USHORT ) AddVertex ( lrint ( segs->end.x ),   lrint ( segs->end.y ));
            segs++;
        }
    }
    if ( count == 0 ) {
        WARNING ( "No valid SEGS left in list!" );
    }

    wSSector *ssec = &ssectorPool [ssectorCount];
    ssec->num = ( USHORT ) count;
    ssec->first = ( USHORT ) first;

    return ( USHORT ) ssectorCount++;
}

static int SortByAngle ( const void *ptr1, const void *ptr2 )
{
    FUNCTION_ENTRY ( NULL, "SortByAngle", true );

    int dif = ( ANGLE_MASK & (( SEG * ) ptr1)->Data.angle ) - ( ANGLE_MASK & (( SEG * ) ptr2)->Data.angle );
    if ( dif ) return dif;
    dif = (( SEG * ) ptr1)->Data.lineDef - (( SEG * ) ptr2)->Data.lineDef;
    if ( dif ) return dif;
    return (( SEG * ) ptr1)->Data.flip - (( SEG * ) ptr2)->Data.flip;
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
//  Create a list of aliases.  These are all the unique lines within the map.
//    Each linedef is assigned an alias.  All subsequent calculations are
//    based on the aliases rather than the linedefs, since there are usually
//    significantly fewer aliases than linedefs.
//----------------------------------------------------------------------------

static int GetLineDefAliases ( DoomLevel *level, SEG *segs, int noSegs )
{
    FUNCTION_ENTRY ( NULL, "GetLineDefAliases", true );

    int noAliases = 1;

    lineDefAlias = new int [ level->LineDefCount () ];
    memset ( lineDefAlias, -1, sizeof ( int ) * ( level->LineDefCount ()));

    SEG **segAlias = new SEG * [ level->LineDefCount () ];

    qsort ( segs, noSegs, sizeof ( SEG ), SortByAngle );

    int lowIndex  = 1;
    int lastAngle = -1;

    for ( int i = 0; i < noSegs; i++ ) {

        // If the LINEDEF has been covered, skip this SEG
        int *alias = &lineDefAlias [ segs [i].Data.lineDef ];

        if ( *alias == -1 ) {

            ComputeStaticVariables ( &segs [i] );

            // Compare against existing aliases with the same angle
            int x = lowIndex;
            while ( x < noAliases ) {
                if ( CoLinear ( segAlias [x] )) break;
                x++;
            }

            if ( x >= noAliases ) {
                segAlias [ x = noAliases++ ] = &segs [i];
                if ( lastAngle != ( ANGLE & ANGLE_MASK )) {
                    lowIndex = x;
                    lastAngle = ANGLE & ANGLE_MASK;
                }
            }

            *alias = x;
        }

        segs [i].AliasFlip = ( segs [i].Data.angle == segAlias [*alias]->Data.angle ) ? 0 : SIDE_FLIPPED;
    }

    delete [] segAlias;

    qsort ( segs, noSegs, sizeof ( SEG ), SortByLineDef );

    return noAliases;
}

//----------------------------------------------------------------------------
//  If the given SEGs form a proper node but don't all belong to the same
//    sector, artificially break up the node by sector.  SEGs are arranged
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
            sVertex *vertS = &seg->start;
            sVertex *vertE = &seg->end;
            int alias = lineDefAlias [ seg->Data.lineDef ];
            WARNING (( lineUsed [ alias ] ? "*" : " " ) <<
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
        for ( int x = 0; x < noSegs; x++ ) {
            // Make sure that all SEGs are actually on the right side of each other
            ComputeStaticVariables ( &seg [x] );
            if (( fabs ( DX ) < EPSILON ) && ( fabs ( DY ) < EPSILON )) continue;

            count [0] = count [1] = count [2] = 0;
            for ( int i = 0; i < noSegs; i++ ) {
                count [( seg [i].Side = _WhichSide ( &seg [i] )) + 1 ]++;
            }

            if (( count [0] * count [2] ) || count [1] ) {
                DumpSegs ( seg, noSegs );
                ERROR ( "Something weird is going on! (" << count [0] << "|" << count [1] << "|" << count [2] << ") " << noSegs );
                break;
            }
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

    ASSERT (( count [0] * count [2] != 0 ) || ( count [1] != 0 ));

    *noLeft = count [0], *noSplits = count [1], *noRight = count [2];

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

    bool check = true;

retry:

    if ( seg->final == false ) {
        memcpy ( lineChecked, lineUsed, sizeof ( char ) * noAliases );
    } else {
        memset ( lineChecked, 0, sizeof ( char ) * noAliases );
    }

    SEG *pSeg = PartitionFunction ( seg, noSegs );

    SortSegs ( pSeg, seg, noSegs, noLeft, noRight, noSplits );

    // Make sure the set of SEGs is still convex after we convert to integer coordinates
    if (( pSeg == NULL ) && ( check == true )) {
        check = false;
        double error = 0.0;
        for ( int i = 0; i < noSegs; i++ ) {

            int startX = lrint ( seg [i].start.x );
            int startY = lrint ( seg [i].start.y );
            int endX   = lrint ( seg [i].end.x );
            int endY   = lrint ( seg [i].end.y );
                          
            error += fabs ( seg [i].start.x - startX );
            error += fabs ( seg [i].start.y - startY );
            error += fabs ( seg [i].end.x - endX );
            error += fabs ( seg [i].end.y - endY );

            seg [i].start.x = startX;
            seg [i].start.y = startY;
            seg [i].end.x   = endX;
            seg [i].end.y   = endY;
            seg [i].final   = true;
        }
        if ( error > EPSILON ) {
            // Force a check of each line
            goto retry;
        }
    }

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
    long maxMetric = ( noSegs / 2 ) * ( noSegs - noSegs / 2 );
    long bestMetric = 0x80000000, bestSplits = 0x7FFFFFFF;

    for ( int i = 0; i < noSegs; i++ ) {
        if ( showProgress && (( i & 15 ) == 0 )) ShowProgress ();
        int alias = testSeg->Split ? 0 : lineDefAlias [ testSeg->Data.lineDef ];
        if (( alias == 0 ) || ( lineChecked [ alias ] == false )) {
            lineChecked [ alias ] = -1;
            count [0] = count [1] = count [2] = 0;
            ComputeStaticVariables ( testSeg );
            if (( fabs ( DX ) < EPSILON ) && ( fabs ( DY ) < EPSILON )) goto next;
            if ( bestMetric < 0 ) for ( int j = 0; j < noSegs; j++ ) {
                count [ WhichSide ( &segs [j] ) + 1 ]++;
            } else for ( int j = 0; j < noSegs; j++ ) {
                count [ WhichSide ( &segs [j] ) + 1 ]++;
                if ( sCount > bestSplits ) goto next;
            }
            // Only consider SEG if it is not a boundary line
            if ( lCount * rCount + sCount ) {
                long metric = ( long ) lCount * ( long ) rCount;
                if ( sCount ) {
                    long temp = X1 * sCount;
                    if ( X2 < temp ) metric = X2 * metric / temp;
                    metric -= ( X3 * sCount + X4 ) * sCount;
                }
                if ( ANGLE & 0x3FFF ) metric--;
                if ( metric == maxMetric ) return testSeg;
                if ( metric > bestMetric ) {
                    pSeg = testSeg;
                    bestSplits = sCount + 2;
                    bestMetric = metric;
                }
            } else if ( alias != 0 ) {
                // Eliminate outer edges of the map from here & down
                *convexPtr++ = alias;
            }
        }
#if defined ( DEBUG )
        else if ( lineChecked [alias] > 0 ) {
            count [0] = count [1] = count [2] = 0;
            ComputeStaticVariables ( testSeg );
            int side = WhichSide ( testSeg );
            if (( fabs ( DX ) < EPSILON ) && ( fabs ( DY ) < EPSILON )) continue;
            for ( int j = 0; j < noSegs; j++ ) {
                switch ( WhichSide ( &segs [j] )) {
                    case SIDE_LEFT :
                        if ( side == SIDE_RIGHT ) {
                            WARNING ( "lineDef " << segs [j].Data.lineDef << " should not to the left of lineDef " << testSeg->Data.lineDef );
                        }
                        break;
                    case SIDE_SPLIT :
                        WARNING ( "lineDef " << segs [j].Data.lineDef << " should not be split by lineDef " << testSeg->Data.lineDef );
                        break;
                    case SIDE_RIGHT :
                        if ( side == SIDE_LEFT ) {
                            WARNING ( "lineDef " << segs [j].Data.lineDef << " should not to the right of lineDef " << testSeg->Data.lineDef );
                        }
                        break;
                    default :
                        break;
                }
            }
        }
#endif

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
        int alias = testSeg->Split ? 0 : lineDefAlias [ testSeg->Data.lineDef ];
        if (( alias == 0 ) || ( lineChecked [ alias ] == false )) {
            lineChecked [ alias ] = -1;
            count [0] = count [1] = count [2] = 0;
            ComputeStaticVariables ( testSeg );
            if (( fabs ( DX ) < EPSILON ) && ( fabs ( DY ) < EPSILON )) goto next;
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
            if ( lCount * rCount + sCount ) {
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
            } else if ( alias != 0 ) {
                // Eliminate outer edges of the map
                *convexPtr++ = alias;
            }
        }
#if defined ( DEBUG )
        else if ( lineChecked [alias] > 0 ) {
            count [0] = count [1] = count [2] = 0;
            ComputeStaticVariables ( testSeg );
            int side = WhichSide ( testSeg );
            if (( fabs ( DX ) < EPSILON ) && ( fabs ( DY ) < EPSILON )) continue;
            for ( int j = 0; j < noSegs; j++ ) {
                switch ( WhichSide ( &segs [j] )) {
                    case SIDE_LEFT :
                        if ( side == SIDE_RIGHT ) {
                            WARNING ( "lineDef " << segs [j].Data.lineDef << " should not to the left of lineDef " << testSeg->Data.lineDef );
                        }
                        break;
                    case SIDE_SPLIT :
                        WARNING ( "lineDef " << segs [j].Data.lineDef << " should not be split by lineDef " << testSeg->Data.lineDef );
                        break;
                    case SIDE_RIGHT :
                        if ( side == SIDE_LEFT ) {
                            WARNING ( "lineDef " << segs [j].Data.lineDef << " should not to the right of lineDef " << testSeg->Data.lineDef );
                        }
                        break;
                    default :
                        break;
                }
            }
        }
#endif

next:
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
    long maxMetric = ( long ) ( noSegs / 2 ) * ( long ) ( noSegs - noSegs / 2 );
    long bestMetric = 0x80000000, bestSplits = 0x7FFFFFFF;

    int i = 0, max = ( noSegs < 30 ) ? noSegs : 30;

retry:

    for ( ; i < max; i++ ) {
        if ( showProgress && (( i & 15 ) == 0 )) ShowProgress ();
        int alias = testSeg->Split ? 0 : lineDefAlias [ testSeg->Data.lineDef ];
        if (( alias == 0 ) || ( lineChecked [ alias ] == false )) {
            lineChecked [ alias ] = -1;
            count [0] = count [1] = count [2] = 0;
            ComputeStaticVariables ( testSeg );
            if (( fabs ( DX ) < EPSILON ) && ( fabs ( DY ) < EPSILON )) goto next;
            if ( bestMetric < 0 ) for ( int j = 0; j < noSegs; j++ ) {
                count [ WhichSide ( &segs [j] ) + 1 ]++;
            } else for ( int j = 0; j < noSegs; j++ ) {
                count [ WhichSide ( &segs [j] ) + 1 ]++;
                if ( sCount > bestSplits ) goto next;
            }
            if ( lCount * rCount + sCount ) {
                long metric = ( long ) lCount * ( long ) rCount;
                if ( sCount ) {
                    long temp = X1 * sCount;
                    if ( X2 < temp ) metric = X2 * metric / temp;
                    metric -= ( X3 * sCount + X4 ) * sCount;
                }
                if ( ANGLE & 0x3FFF ) metric--;
                if ( metric == maxMetric ) return testSeg;
                if ( metric > bestMetric ) {
                    pSeg = testSeg;
                    bestSplits = sCount;
                    bestMetric = metric;
                }
            } else if ( alias != 0 ) {
                // Eliminate outer edges of the map from here & down
                *convexPtr++ = alias;
            }
        }
#if defined ( DEBUG )
        else if ( lineChecked [alias] > 0 ) {
            count [0] = count [1] = count [2] = 0;
            ComputeStaticVariables ( testSeg );
            int side = WhichSide ( testSeg );
            if (( fabs ( DX ) < EPSILON ) && ( fabs ( DY ) < EPSILON )) continue;
            for ( int j = 0; j < noSegs; j++ ) {
                switch ( WhichSide ( &segs [j] )) {
                    case SIDE_LEFT :
                        if ( side == SIDE_RIGHT ) {
                            WARNING ( "lineDef " << segs [j].Data.lineDef << " should not to the left of lineDef " << testSeg->Data.lineDef );
                        }
                        break;
                    case SIDE_SPLIT :
                        WARNING ( "lineDef " << segs [j].Data.lineDef << " should not be split by lineDef " << testSeg->Data.lineDef );
                        break;
                    case SIDE_RIGHT :
                        if ( side == SIDE_LEFT ) {
                            WARNING ( "lineDef " << segs [j].Data.lineDef << " should not to the right of lineDef " << testSeg->Data.lineDef );
                        }
                        break;
                    default :
                        break;
                }
            }
        }
#endif

next:
        testSeg++;
    }
    if (( pSeg == NULL ) && ( max < noSegs )) {
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

    const wLineDef *lineDef = rSeg->LineDef;
    wVertex *vertS = &newVertices [ lineDef->start ];
    wVertex *vertE = &newVertices [ lineDef->end ];

    // Minimum precision required to avoid overflow/underflow:
    //   dx, dy  - 16 bits required
    //   c       - 33 bits required
    //   det     - 32 bits required
    //   x, y    - 50 bits required

    double dx  = vertE->x - vertS->x;
    double dy  = vertE->y - vertS->y;
    double num = DX * ( vertS->y - Y ) - DY * ( vertS->x - X );
    double det = dx * DY - dy * DX;

    if ( det == 0.0 ) {
        ERROR ( "SEG is parallel to partition line" );
    }

    double x = vertS->x + num * dx / det;
    double y = vertS->y + num * dy / det;

    if ( rSeg->final == true ) {
        x = lrint ( x );
        y = lrint ( y );
#if defined ( DEBUG )
        if ((( rSeg->start.x == x ) && ( rSeg->start.y == y )) ||
            (( lSeg->start.x == x ) && ( lSeg->start.y == y ))) {
            fprintf ( stderr, "\nNODES: End point duplicated in DivideSeg: LineDef #%d", rSeg->Data.lineDef );
            fprintf ( stderr, "\n       Partition: from (%f,%f) to (%f,%f)", X, Y, X + DX, Y + DY );
            fprintf ( stderr, "\n       LineDef: from (%d,%d) to (%d,%d) split at (%f,%f)", vertS->x, vertS->y, vertE->x, vertE->y, x, y );
            fprintf ( stderr, "\n       SEG: from (%f,%f) to (%f,%f)", rSeg->start.x, rSeg->start.y, rSeg->end.x, rSeg->end.y );
            fprintf ( stderr, "\n       dif: (%f,%f)  (%f,%f)", rSeg->start.x - x, rSeg->start.y - y, rSeg->end.x - x, rSeg->end.y - y );
        }
#endif
    }

    // Determine which sided of the partition line the start point is on
    double sideS = DX * ( rSeg->start.y - Y ) - DY * ( rSeg->start.x - X );

    // Get the correct endpoint of the base LINEDEF for the offset calculation
    if ( rSeg->Data.flip ) vertS = vertE;

    rSeg->Split = true;
    lSeg->Split = true;

    // Fill in the parts of lSeg & rSeg that have changed
    if ( sideS < 0.0 ) {
        rSeg->end.x       = x;
        rSeg->end.y       = y;
        lSeg->start.x     = x;
        lSeg->start.y     = y;
        lSeg->Data.offset = ( USHORT ) ( hypot (( double ) ( x - vertS->x ), ( double ) ( y - vertS->y )) + 0.5 );
    } else {
        lSeg->end.x       = x;
        lSeg->end.y       = y;
        rSeg->start.x     = x;
        rSeg->start.y     = y;
        rSeg->Data.offset = ( USHORT ) ( hypot (( double ) ( x - vertS->x ), ( double ) ( y - vertS->y )) + 0.5 );
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
//    selected is valid and calculate the necessary data for the node.  If
//    no valid partition could be found, return NULL to indicate that the
//    list of SEGs forms a valid SSECTOR.
//----------------------------------------------------------------------------

static bool PartitionNode ( wNode *node, SEG *segs, int noSegs, int *noLeft, int *noRight )
{
    FUNCTION_ENTRY ( NULL, "PartitionNode", true );

    int noSplits;

    if ( ChoosePartition ( segs, noSegs, noLeft, noRight, &noSplits ) == false ) {

        if ( uniqueSubsectors ) {
            memset ( usedSector, false, sizeof ( UCHAR ) * sectorCount );
            for ( int i = 0; i < noSegs; i++ ) {
                usedSector [ segs [i].Sector ] = true;
            }
            int noSectors = 0;
            for ( int i = 0; i < sectorCount; i++ ) {
                if ( usedSector [i] ) noSectors++;
            }
            if ( noSectors > 1 ) for ( int i = 0; noSectors && ( i < sectorCount ); i++ ) {
                if ( usedSector [i] ) {
                    if ( keepUnique [i] ) goto NonUnique;
                    noSectors--;
                }
            }
        }

        // Splits may have 'upset' the lineDef ordering - some special effects
        //   assume the SEGS appear in the same order as the LINEDEFS
        if ( noSegs > 1 ) {
            qsort ( segs, noSegs, sizeof ( SEG ), SortByLineDef );
        }

        return false;

NonUnique:

        ComputeStaticVariables ( segs );
        SortSectors ( segs, noSegs, noLeft, noRight );

    } else if ( noSplits ) {

        SplitSegs ( &segs [ *noRight ], noSplits );
        *noLeft  += noSplits;
        *noRight += noSplits;

    }

    node->x  = ( SHORT ) X;
    node->y  = ( SHORT ) Y;
    node->dx = ( SHORT ) DX;
    node->dy = ( SHORT ) DY;

    FindBounds ( &node->side [0], segs, *noRight );
    FindBounds ( &node->side [1], segs + *noRight, *noLeft );

    return true;
}

//----------------------------------------------------------------------------
//  Recursively create the actual NODEs.  The given list of SEGs is analyzed
//    and a partition is chosen.  If no partition can be found, a leaf node
//    is created.  Otherwise, the right and left SEGs are analyzed.  Features:
//    - A list of 'convex' aliases is maintained.  These are lines that border
//      the list of SEGs and can never be partitions.  A line is marked as
//      convex for this and all children, and unmarked before returing.
//    - Similarly, the alias chosen as the partition is marked as convex
//      since it will be convex for all children.
//----------------------------------------------------------------------------
static USHORT CreateNode ( SEG *segs, int *noSegs )
{
    FUNCTION_ENTRY ( NULL, "CreateNode", true );

    wNode tempNode;

    int noLeft, noRight;
    int *cptr = convexPtr;

    if (( *noSegs <= 1 ) || ( PartitionNode ( &tempNode, segs, *noSegs, &noLeft, &noRight ) == false )) {
        convexPtr = cptr;
        if ( showProgress ) ShowDone ();
        return ( USHORT ) ( 0x8000 | CreateSSector ( segs, *noSegs ));
    }

    int alias = currentAlias;

    lineUsed [ alias ] = 1;
    for ( int *tempPtr = cptr; tempPtr != convexPtr; tempPtr++ ) {
        lineUsed [ *tempPtr ] = 2;
    }

    if ( showProgress ) GoRight ();

    USHORT rNode = CreateNode ( segs, &noRight );

    if ( showProgress ) GoLeft ();

    USHORT lNode = CreateNode ( segs + noRight, &noLeft );

    if ( showProgress ) Backup ();

    *noSegs = noLeft + noRight;

    while ( convexPtr != cptr ) lineUsed [ *--convexPtr ] = false;
    lineUsed [ alias ] = false;

    if ( nodesLeft-- == 0 ) {
        int delta  = ( 10 * nodeCount ) / 100;
        nodePool   = ( wNode * ) realloc ( nodePool, sizeof ( wNode ) * ( nodeCount + delta ));
        nodesLeft += delta;
    }

    wNode *node = &nodePool [nodeCount];
    *node           = tempNode;
    node->child [0] = rNode;
    node->child [1] = lNode;

    if ( showProgress ) ShowDone ();

    return ( USHORT ) nodeCount++;
}

wVertex  *GetVertices ()
{
    FUNCTION_ENTRY ( NULL, "GetVertices", true );

    wVertex *vert = new wVertex [ noVertices ];
    memcpy ( vert, newVertices, sizeof ( wVertex ) * noVertices );

    return vert;
}

wNode *GetNodes ( wNode *nodeList, int noNodes )
{
    FUNCTION_ENTRY ( NULL, "GetNodes", true );

    wNode *nodes = new wNode [ noNodes ];

    for ( int i = 0; i < noNodes; i++ ) {
        nodes [i] = nodeList [i];
    }

    return nodes;
}

static wSegs *finalSegs;

wSSector *GetSSectors ( wSSector *ssectorList, int noSSectors )
{
    FUNCTION_ENTRY ( NULL, "GetSSectors", true );

    wSegs *segs = finalSegs = new wSegs [ segCount ];

    for ( int i = 0; i < noSSectors; i++ ) {

        int start = ssectorList[i].first;
        ssectorList[i].first = ( USHORT ) ( segs - finalSegs );

        // Copy the used SEGs to the final SEGs list
        for ( int x = 0; x < ssectorList [i].num; x++ ) {
            segs [x] = segStart [start+x].Data;
        }

        segs += ssectorList [i].num;
    }

    delete [] segStart;
    segCount = segs - finalSegs;

    wSSector *ssector = new wSSector [ noSSectors ];
    memcpy ( ssector, ssectorList, sizeof ( wSSector ) * noSSectors );

    return ssector;
}

wSegs *GetSegs ()
{
    FUNCTION_ENTRY ( NULL, "GetSegs", true );
/*
    wSegs *segs = new wSegs [ segCount ];

    for ( int i = 0; i < segCount; i++ ) {
        segs [i] = segStart [i].Data;
    }

    delete [] segStart;

    return segs;
*/
    return finalSegs;
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

    // Sanity check on environment variables
    if ( X2 <= 0 ) X2 = 1;
    if ( Y2 <= 0 ) Y2 = 1;

    // Copy options to global variables
    showProgress     = options->showProgress;
    uniqueSubsectors = options->keepUnique ? true : false;

    PartitionFunction = Algorithm1;
    if ( options->algorithm == 2 ) PartitionFunction = Algorithm2;
    if ( options->algorithm == 3 ) PartitionFunction = Algorithm3;

    nodeCount    = 0;
    ssectorCount = 0;

    // Get rid of old SEGS and associated vertices
    level->NewSegs ( 0, NULL );
    level->TrimVertices ();
    level->PackVertices ();

    noVertices  = level->VertexCount ();
    sectorCount = level->SectorCount ();
    usedSector = new UCHAR [ sectorCount ];
    keepUnique = new bool [ sectorCount ];
    if ( options->keepUnique ) {
        memcpy ( keepUnique, options->keepUnique, sizeof ( bool ) * sectorCount );
    } else {
        memset ( keepUnique, true, sizeof ( bool ) * sectorCount );
    }
    maxVertices = ( int ) ( noVertices * FACTOR_VERTEX );
    newVertices = ( wVertex * ) malloc ( sizeof ( wVertex ) * maxVertices );
    memcpy ( newVertices, level->GetVertices (), sizeof ( wVertex ) * noVertices );

    Status ( "Creating SEGS ... " );
    segStart = CreateSegs ( level, options );

    Status ( "Getting LineDef Aliases ... " );
    noAliases = GetLineDefAliases ( level, segStart, segCount );

    lineChecked = new char [ noAliases ];
    lineUsed    = new char [ noAliases ];
    memset ( lineUsed, false, sizeof ( char ) * noAliases );

    Status ( "Creating Side Info ... " );
    CreateSideInfo ( level );

    score = ( options->algorithm == 2 ) ? new sScoreInfo [ noAliases ] : NULL;

    convexList = new int [ noAliases ];
    convexPtr  = convexList;

    Status ( "Creating NODES ... " );
    int noSegs = segCount;
    nodesLeft = ( int ) ( FACTOR_NODE * level->SideDefCount ());
    nodePool  = ( wNode * ) malloc( sizeof ( wNode ) * nodesLeft );
    ssectorsLeft = ( int ) ( FACTOR_NODE * level->SideDefCount ());
    ssectorPool = ( wSSector * ) malloc ( sizeof ( wSSector ) * ssectorsLeft );
    CreateNode ( segStart, &noSegs );

    delete [] convexList;
    if ( score ) delete [] score;

    // Clean up temporary buffers
    Status ( "Cleaning up ... " );
    delete [] sideInfo;
    delete [] lineDefAlias;
    delete [] lineChecked;
    delete [] lineUsed;
    delete [] keepUnique;
    delete [] usedSector;

    sideInfo = NULL;

    level->NewVertices ( noVertices, GetVertices ());
    level->NewNodes ( nodeCount, GetNodes ( nodePool, nodeCount ));
    level->NewSubSectors ( ssectorCount, GetSSectors ( ssectorPool, ssectorCount ));
    level->NewSegs ( segCount, GetSegs ());

    free ( newVertices );
    free ( ssectorPool );
    free ( nodePool );
    delete [] tempSeg;
}
