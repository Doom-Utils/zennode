//----------------------------------------------------------------------------
//
// File:        level.hpp
// Date:        26-Oct-1994
// Programmer:  Marc Rousseau
//
// Description: Object classes for manipulating Doom Maps
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
//----------------------------------------------------------------------------

#ifndef _LEVEL_HPP_
#define _LEVEL_HPP_

#if ! defined ( _COMMON_HPP_ )
    #include "common.hpp"
#endif

#if ! defined ( _WAD_HPP_ )
    #include "wad.hpp"
#endif

#include "thing.hpp"
#include "lineDef.hpp"

#ifndef SHORT
    typedef short SHORT;
#endif

struct wThing1 {
    SHORT       xPos;                   // x position
    SHORT       yPos;                   // y position
    USHORT      angle;                  // direction
    USHORT      type;                   // thing type
    USHORT      attr;                   // attributes of thing
};

struct wThing2 {        // HEXEN
    USHORT      tid;                    // Thing ID - for scripts & specials
    SHORT       xPos;                   // x position
    SHORT       yPos;                   // y position
    USHORT      altitude;               // starting altitude
    USHORT      angle;                  // direction
    USHORT      type;                   // thing type
    USHORT      attr;                   // attributes of thing
    UCHAR       special;                // special type
    UCHAR       arg [5];                // special arguments
};      

struct wThing {
    SHORT       xPos;                   // x position
    SHORT       yPos;                   // y position
    USHORT      angle;                  // in degrees not BAM
    USHORT      type;                   // thing type
    USHORT      attr;                   // attributes of thing
    USHORT      tid;                    // Thing ID - for scripts & specials
    USHORT      altitude;               // starting altitude
    UCHAR       special;                // special type
    UCHAR       arg [5];                // special arguments
};

struct wLineDef1 {
    USHORT      start;                  // from this vertex ...
    USHORT      end;                    // ... to this vertex
    USHORT      flags;
    USHORT      type;
    USHORT      tag;                    // crossing this linedef activates the sector with the same tag
    USHORT      sideDef[2];             // sidedef
};

struct wLineDef2 {      // HEXEN
    USHORT      start;                  // from this vertex ...
    USHORT      end;                    // ... to this vertex
    USHORT      flags;
    UCHAR       special;                // special type
    UCHAR       arg [5];                // special arguments
    USHORT      sideDef[2];             // sidedef
};

struct wLineDef {
    USHORT      start;                  // from this vertex ...
    USHORT      end;                    // ... to this vertex
    USHORT      flags;
    USHORT      type;
    USHORT      tag;                    // crossing this linedef activates the sector with the same tag
    USHORT      sideDef[2];             // sidedef
    UCHAR       special;                // special type
    UCHAR       arg [5];                // special arguments
};

#define NO_SIDEDEF      (( USHORT ) -1 )
#define RIGHT_SIDEDEF   (( USHORT )  0 )
#define LEFT_SIDEDEF    (( USHORT )  1 )

#define EMPTY_TEXTURE   0x002D          // USHORT version of ASCII "-"

struct wSideDef {
    SHORT       xOff;                   // X offset for texture
    SHORT       yOff;                   // Y offset for texture
    char        text1[MAX_LUMP_NAME];   // texture name for the part above
    char        text2[MAX_LUMP_NAME];   // texture name for the part below
    char        text3[MAX_LUMP_NAME];   // texture name for the regular part
    USHORT      sector;                 // adjacent sector
};

struct wVertex {
    SHORT       x;                      // X coordinate
    SHORT       y;                      // Y coordinate
};

struct wSegs {
    USHORT      start;                  // from this vertex ...
    USHORT      end;                    // ... to this vertex
    USHORT      angle;                  // angle (0 = east, 16384 = north, ...)
    USHORT      lineDef;                // linedef that this seg goes along*/
    USHORT      flip;                   // true if not the same direction as linedef
    USHORT      offset;                 // distance from starting point
};

struct wSSector {
    USHORT      num;                    // number of Segs in this Sub-Sector
    USHORT      first;                  // first Seg
};

struct wBound {
    SHORT       maxy, miny;
    SHORT       minx, maxx;             // bounding rectangle
};

struct wNode {
    SHORT       x, y;                   // starting point
    SHORT       dx, dy;                 // offset to ending point
    wBound      side[2];
    USHORT      child[2];               // Node or SSector (if high bit is set)
};

struct wSector {
    SHORT       floorh;                 // floor height
    SHORT       ceilh;                  // ceiling height
    char        floorTexture[MAX_LUMP_NAME];    // floor texture
    char        ceilTexture[MAX_LUMP_NAME];     // ceiling texture
    USHORT      light;                  // light level (0-255)
    USHORT      special;                // special behaviour (0 = normal, 9 = secret, ...)
    USHORT      trigger;                // sector activated by a linedef with the same tag
};

struct wReject {
    USHORT      dummy;
};

struct wBlockMap {
    SHORT       xOrigin;
    SHORT       yOrigin;
    USHORT      noColumns;
    USHORT      noRows;
//    USHORT    data [];
};

class DoomLevel {

    WAD        *m_Wad;
    wLumpName   m_Name;
    bool        m_Modified;
    bool        m_Valid;
    bool        m_Checked;

    bool        m_NewFormat;
    const char *m_Title;
    const char *m_Music;
    int         m_Cluster;

    int         m_NoThings;
    int         m_NoLineDefs;
    int         m_NoSideDefs;
    int         m_NoVertices;
    int         m_NoSectors;
    int         m_NoSegs;
    int         m_NoSubSectors;
    int         m_NoNodes;
    int         m_RejectSize;
    int         m_BlockMapSize;

    char       *m_RawThing;
    char       *m_RawLineDef;

    bool        m_ThingsChanged;
    bool        m_LineDefsChanged;
    bool        m_SideDefsChanged;
    bool        m_VerticesChanged;
    bool        m_SectorsChanged;
    bool        m_SegsChanged;
    bool        m_SubSectorsChanged;
    bool        m_NodesChanged;
    bool        m_RejectChanged;
    bool        m_BlockMapChanged;

    wThing     *m_Thing;
    wLineDef   *m_LineDef;
    wSideDef   *m_SideDef;
    wVertex    *m_Vertex;
    wSector    *m_Sector;
    wSegs      *m_Segs;
    wSSector   *m_SubSector;
    wNode      *m_Node;
    wReject    *m_Reject;               // optional
    wBlockMap  *m_BlockMap;

    static void ConvertRaw1ToThing ( int, wThing1 *, wThing * );
    static void ConvertRaw2ToThing ( int, wThing2 *, wThing * );
    static void ConvertThingToRaw1 ( int, wThing *, wThing1 * );
    static void ConvertThingToRaw2 ( int, wThing *, wThing2 * );

    static void ConvertRaw1ToLineDef ( int, wLineDef1 *, wLineDef * );
    static void ConvertRaw2ToLineDef ( int, wLineDef2 *, wLineDef * );
    static void ConvertLineDefToRaw1 ( int, wLineDef *, wLineDef1 * );
    static void ConvertLineDefToRaw2 ( int, wLineDef *, wLineDef2 * );

    int  Load ();
    void LoadHexenInfo ();

    void ReadThings ( bool, const wadDirEntry *, const wadDirEntry * );
    bool ReadLineDefs ( const wadDirEntry *, const wadDirEntry * );

    bool SaveThings ( const wadDirEntry *, const wadDirEntry * );
    bool SaveLineDefs ( const wadDirEntry *, const wadDirEntry * );

#if defined ( BIG_ENDIAN )
    void AdjustByteOrder ();
#endif

    void DeleteTransients ();
    void CleanUp ();
    void WipeOut ();

public:

    DoomLevel ( const char *, WAD *, bool = true );
    ~DoomLevel ();

    const WAD *GetWAD () const                  { return m_Wad; }

    const char *Name () const                   { return m_Name; }
    const char *Title () const                  { return m_Title ? m_Title : m_Name; }
    const char *Music () const                  { return m_Music ? m_Music : NULL; }
    int MapCluster () const                     { return m_Cluster; }

    int ThingCount () const                     { return m_NoThings; }
    int LineDefCount () const                   { return m_NoLineDefs; }
    int SideDefCount () const                   { return m_NoSideDefs; }
    int VertexCount () const                    { return m_NoVertices; }
    int SectorCount () const                    { return m_NoSectors; }
    int SegCount () const                       { return m_NoSegs; }
    int SubSectorCount () const                 { return m_NoSubSectors; }
    int NodeCount () const                      { return m_NoNodes; }
    int RejectSize () const                     { return m_RejectSize; }
    int BlockMapSize () const                   { return m_BlockMapSize; }

    const wThing        *GetThings () const     { return m_Thing; }
    const wLineDef      *GetLineDefs () const   { return m_LineDef; }
    const wSideDef      *GetSideDefs () const   { return m_SideDef; }
    const wVertex       *GetVertices () const   { return m_Vertex; }
    const wSector       *GetSectors () const    { return m_Sector; }
    const wSegs         *GetSegs () const       { return m_Segs; }
    const wSSector      *GetSubSectors () const { return m_SubSector; }
    const wNode         *GetNodes () const      { return m_Node; }
    const wReject       *GetReject () const     { return m_Reject; }
    const wBlockMap     *GetBlockMap () const   { return m_BlockMap; }

    void NewThings ( int, wThing * );
    void NewLineDefs ( int, wLineDef * );
    void NewSideDefs ( int, wSideDef * );
    void NewVertices ( int, wVertex * );
    void NewSectors ( int, wSector * );
    void NewSegs ( int, wSegs * );
    void NewSubSectors ( int, wSSector * );
    void NewNodes ( int, wNode * );
    void NewReject ( int, wReject *, bool );
    void NewBlockMap ( int, wBlockMap * );

    bool isValid ( bool );
    bool hasChanged () const;

    void TrimVertices ();
    void PackVertices ();
    void UnPackVertices ();

    void PackSideDefs ();
    void UnPackSideDefs ();

    bool UpdateWAD ();
    void AddToWAD ( WAD *wad );

    sThingDesc   *FindThing ( int type );
    static sLineDefDesc *FindLineDef ( int type );

    sThingDesc   *GetThing ( int index );
    static sLineDefDesc *GetLineDef ( int index );
};

#endif
