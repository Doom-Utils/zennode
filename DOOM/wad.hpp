//----------------------------------------------------------------------------
//
// File:        wad.hpp
// Date:        26-Oct-1994
// Programmer:  Marc Rousseau
//
// Description: Object classes for manipulating Doom WAD files
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
//   04-25-01	Added little/big endian conversions
//
//----------------------------------------------------------------------------

#if ! defined ( _WAD_HPP_ )
#define _WAD_HPP_

#if ! defined ( _COMMON_HPP_ )
    #include "common.hpp"
#endif

#if ! defined ( __STDIO_H )
    #include <stdio.h>
#endif

#if defined ( LITTLE_ENDIAN )
    #define IWAD_ID		0x44415749	// ASCII - 'IWAD'
    #define PWAD_ID		0x44415750	// ASCII - 'PWAD'
#else
    #define IWAD_ID		0x49574144	// ASCII - 'IWAD'
    #define PWAD_ID		0x50574144	// ASCII - 'PWAD'

    extern ULONG swap_ulong ( const unsigned char *ptr );
    extern ULONG swap_ushort ( const unsigned char *ptr );
#endif

#define IS_TYPE(x,y)	(( * ( ULONG * ) ( x )) == ( y ))

#define MAX_LUMP_NAME	8

enum eLumpType {
    wl_UNCHECKED,
    wl_UNKNOWN,
    wl_PALETTE,
    wl_COLORMAP,
    wl_DEMO,
    wl_TEXTURE_LIST,
    wl_PATCH_NAMES,
    wl_MIDI_MAPPING,
    wl_GRAVIS_PATCH,
    wl_MAP_NAME,
    wl_MAP_DATA,
    wl_PC_SPEAKER,
    wl_SOUND_EFFECT,
    wl_MUSIC,
    wl_FLAT,
    wl_PATCH,
    wl_SPRITE,
    wl_GRAPHIC,
    wl_SCREEN_SHOT,
    wl_TEXT_SCREEN,
    wl_SOUND_INFO,
    wl_SCRIPT,
    wl_SPECIAL
};

enum eWadType {
    wt_UNKNOWN,
    wt_DOOM,
    wt_DOOM2,
    wt_HERETIC,
    wt_HEXEN
};

enum eWadStyle {
    wst_UNKNOWN,
    wst_FORMAT_1,		// DOOM / Heretic
    wst_FORMAT_2,		// DOOM ][
    wst_FORMAT_3		// Hexen
};

enum eWadStatus {
    ws_UNKNOWN,
    ws_OK,
    ws_CANT_READ,
    ws_CANT_WRITE,
    ws_INVALID_WAD,
    ws_INVALID_FILE,
    ws_SEEK_ERROR,
    ws_READ_ERROR,
    ws_WRITE_ERROR
};

struct wadHeader {
    char   type [4];
    ULONG  dirSize;			// number of Lumps in WAD
    ULONG  dirStart;			// offset to start of directory
};

typedef char wLumpName [ MAX_LUMP_NAME ];

struct wadDirEntry {
    ULONG     offset;			// offset to start of data
    ULONG     size;			// byte size of data
    wLumpName name;			// name of data block
};

struct wadDirInfo {
    UCHAR    *cacheData;
    UCHAR    *newData;
    eLumpType type;
};

class wadList;

class wadFilter {
public:
    virtual const char *getFileSpec () const = 0;
    virtual bool isRecognized ( ULONG, void * ) const = 0;
    virtual bool isRecognized ( const char * ) const = 0;
    virtual bool readData ( FILE *, ULONG *, void ** ) const = 0;
    virtual bool writeData ( FILE *, ULONG, void * ) const = 0;
};

class WAD {

    char               *m_Name;
    FILE               *m_File;
    wadList            *m_List;

    bool                m_bValid;
    bool                m_bRegistered;
    bool                m_bDirChanged;		// wadDirEntry added/deleted

    wadHeader           m_Header;
    wadDirEntry        *m_Directory;
    wadDirInfo         *m_DirInfo;
    eWadStatus          m_Status;
    eWadType            m_Type;
    eWadStyle           m_Style;

    const wadDirEntry  *m_MapStart;
    const wadDirEntry  *m_MapEnd;
    const wadDirEntry  *m_SpriteStart;
    const wadDirEntry  *m_SpriteEnd;
    const wadDirEntry  *m_PatchStart;
    const wadDirEntry  *m_PatchEnd;
    const wadDirEntry  *m_FlatStart;
    const wadDirEntry  *m_FlatEnd;

    void              **m_NewData;

    static int          sm_NoFilters;
    static wadFilter  **sm_Filter;

    bool EnlargeDirectory ( int holePos, int entries );
    bool ReduceDirectory ( int holePos, int entries );

    void FindMarkers ();

    bool ReadHeader ( wadHeader * );
    bool WriteHeader ( FILE *, wadHeader * );

    bool ReadDirEntry ( wadDirEntry * );
    bool WriteDirEntry ( FILE *, wadDirEntry * );

    bool ReadDirectory ();
    bool WriteDirectory ( FILE * );

    ULONG IndexOf ( const wadDirEntry * ) const;

public:

    WAD ( const char * );
   ~WAD ();

    void SetList ( wadList * );

    static bool IsMap ( const char * );
    static bool AddFilter ( wadFilter * );

    // Called by wadList::Save
    void OpenFile ();
    void CloseFile ();

    bool SaveFile ( const char * = NULL );

    void Type ( eWadType );
    void Style ( eWadStyle );
    void Format ( ULONG );

    const char *Name () const;
    eWadStatus  Status () const;
    eWadType    Type () const;
    eWadStyle   Style () const;
    ULONG       Format () const;
    ULONG       FileSize () const;

    bool     IsValid () const;
    bool     IsRegistered () const;
    bool     HasChanged () const;
    bool     HasChanged ( const wadDirEntry * ) const;

    eLumpType   GetLumpType ( const wadDirEntry * );
    const char *GetLumpDescription ( eLumpType ) const;
    const char *GetLumpDescription ( const wadDirEntry * );

    void Seek ( ULONG );
    void ReadBytes ( void *, ULONG, ULONG = 1 );
    void WriteBytes ( void *, ULONG, ULONG = 1 );
    void CopyBytes ( WAD *, ULONG, ULONG = 1 );

    ULONG DirSize () const;
    const wadDirEntry *GetDir ( ULONG ) const;
    const wadDirEntry *FindDir ( const char *, const wadDirEntry * = NULL, const wadDirEntry * = NULL ) const;

    void *ReadEntry ( const char *, ULONG *, const wadDirEntry * = NULL, const wadDirEntry * = NULL, bool = false );
    void *ReadEntry ( const wadDirEntry *, ULONG *, bool = false );

    bool WriteEntry ( const char *, ULONG, void *, bool, const wadDirEntry * = NULL, const wadDirEntry * = NULL );
    bool WriteEntry ( const wadDirEntry *, ULONG, void *, bool );

    bool Extract ( const wLumpName *, const char * = NULL ) const;

    bool InsertBefore ( const wLumpName *, const char *, bool, const wadDirEntry * = NULL );
    bool InsertAfter ( const wLumpName *, const char *, bool, const wadDirEntry * = NULL );

    bool InsertBefore ( const wLumpName *, ULONG, void *, bool, const wadDirEntry * = NULL );
    bool InsertAfter ( const wLumpName *, ULONG, void *, bool, const wadDirEntry * = NULL );

    bool Remove ( const wLumpName *, const wadDirEntry * = NULL, const wadDirEntry * = NULL  );
};

inline void WAD::Format ( ULONG newFormat )	{ * ( ULONG * ) m_Header.type = newFormat; }
inline void WAD::Type ( eWadType newType )	{ m_Type = newType; }
inline void WAD::Style ( eWadStyle newStyle )	{ m_Style = newStyle; }

inline const char *WAD::Name () const		{ return m_Name; }
inline ULONG       WAD::DirSize () const	{ return m_Header.dirSize; }
inline ULONG       WAD::Format () const		{ return * ( ULONG * ) m_Header.type; }
inline eWadStatus  WAD::Status () const		{ return m_Status; }
inline eWadType    WAD::Type () const		{ return m_Type; }
inline eWadStyle   WAD::Style () const		{ return m_Style; }
inline bool        WAD::IsValid () const	{ return m_bValid; }
inline bool        WAD::IsRegistered () const	{ return m_bRegistered; }

struct wadListDirEntry {
    WAD               *wad;
    const wadDirEntry *entry;
};

class wadList {

    struct wadListEntry {
        WAD          *wad;
        wadListEntry *Next;
    };

    ULONG            m_DirSize;
    ULONG            m_MaxSize;
    wadListDirEntry *m_Directory;
    eWadType         m_Type;
    eWadStyle        m_Style;
    wadListEntry    *m_List;

    ULONG  IndexOf ( const wadListDirEntry * ) const;

    int    AddLevel ( ULONG, const wadDirEntry *&, WAD * );
    void   AddDirectory ( WAD *, bool = true );

public:

    wadList ();
    ~wadList ();

    bool Add ( WAD * );
    bool Remove ( WAD * );
    void Clear ();
    void UpdateDirectory ();

    int   wadCount () const;
    ULONG FileSize () const;
    WAD *GetWAD ( int ) const;
    eWadType Type () const;
    eWadStyle Style () const;

    bool Save ( const char * = NULL );
    bool Extract ( const wLumpName *, const char *file = NULL );

    ULONG DirSize () const;
    const wadListDirEntry *GetDir ( ULONG ) const;
    const wadListDirEntry *FindWAD ( const char *, const wadListDirEntry * = NULL, const wadListDirEntry * = NULL ) const;

    bool Contains ( WAD * ) const;
    bool IsEmpty () const;
    bool HasChanged () const;
};

inline ULONG     wadList::DirSize () const	{ return m_DirSize; }
inline bool      wadList::IsEmpty () const	{ return ( m_List == NULL ) ? true : false; }
inline eWadType  wadList::Type () const		{ return m_Type; }
inline eWadStyle wadList::Style () const	{ return m_Style; }

#endif
