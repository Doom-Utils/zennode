//----------------------------------------------------------------------------
//
// File:        ZenNode.hpp
// Date:        26-Oct-1994
// Programmer:  Marc Rousseau
//
// Description: Definitions of structures used by the ZenNode routines
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
//   06-??-95	Added LineDef alias list to speed up the process
//
//----------------------------------------------------------------------------

#ifndef _ZENNODE_HPP_
#define _ZENNODE_HPP_

#if ! defined ( _LEVEL_HPP_ )
  #include "level.hpp"
#endif

struct sBlockMapOptions {
    bool  Rebuild;
    bool  Compress;
};

struct sNodeOptions {
    bool  Rebuild;
    int   Method;
    bool  Quiet;
    bool  Unique;
    bool  ReduceLineDefs;
};

struct sRejectOptions {
    bool  Rebuild;
    bool  Empty;
    bool  Force;
    bool  FindChildren;
    bool  UseGraphs;
};

typedef unsigned short BAM;
typedef long double REAL;	// Must have at least 50 significant bits

struct sVertex {
    double x;
    double y;
};

struct SEG {
    wSegs           Data;
    const wLineDef *LineDef;
    int             Sector;
    int             Side;
    int             AliasFlip;
    bool            Split;
    bool            DontSplit;
    bool            final;
    sVertex         start;
    sVertex         end;
};

struct sBSPOptions {
    int       algorithm;
    bool      showProgress;
    bool      reduceLineDefs;		// global flag for invisible linedefs
    bool     *ignoreLineDef;		// linedefs that can be left out
    bool     *dontSplit;		// linedefs that can't be split
    bool     *keepUnique;		// unique sector requirements
};

struct sScoreInfo {
    int       index;
    int       metric1;
    int       metric2;
    int       invalid;
    int       total;
};

#define sgn(a)		((0<(a))-((a)<0))

#if defined ( _MSC_VER )
    #define hypot _hypot
#endif

#define BAM90		(( BAM ) 0x4000 )	// BAM:  90ψ ( «γ)
#define BAM180		(( BAM ) 0x8000 )	// BAM: 180ψ ( ργ)
#define BAM270		(( BAM ) 0xC000 )	// BAM: 270ψ (-«γ)
#define M_PI        	3.14159265358979323846

#define SIDE_UNKNOWN		-2
#define SIDE_LEFT		-1
#define SIDE_SPLIT		 0
#define SIDE_RIGHT		 1

#define SIDE_FLIPPED		0xFFFFFFFE
#define SIDE_NORMAL		0

#define LEFT			0
#define SPLIT			1
#define RIGHT			2

#define IS_LEFT_RIGHT(s)	( s & 1 )
#define FLIP(c,s)		( c ^ s )


// ----- C99 routines from <math.h> Required by ZenNode -----

#if defined ( __GNUC__ ) && ( __GNUC__ < 3 )

    __inline long lrint ( double flt )
    {
        int intgr;

        __asm__ __volatile__ ("fistpl %0" : "=m" (intgr) : "t" (flt) : "st");

        return intgr;
    }

#elif defined ( _MSC_VER )

    __inline long lrint ( double flt )
    {
        int intgr;

        _asm
        {
            fld   flt
            fistp intgr
        };

        return intgr;
    }

#endif


extern void CreateNODES ( DoomLevel *level, sBSPOptions *options );
extern int  CreateBLOCKMAP ( DoomLevel *level, const sBlockMapOptions &options );
extern bool CreateREJECT ( DoomLevel *level, const sRejectOptions &options, ULONG *efficiency );


// ----- External Functions Required by ZenNode -----

extern void Status ( char * );
extern void GoRight ();
extern void GoLeft ();
extern void Backup ();
extern void ShowProgress ();
extern void ShowDone ();

#endif
