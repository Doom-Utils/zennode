//----------------------------------------------------------------------------
//
// File:        wad.hpp
// Date:        26-October-1994
// Programmer:  Marc Rousseau
//
// Description: Object classes for manipulating Doom WAD files
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

#if ! defined ( _WAD_HPP_ )
#define _WAD_HPP_

#if ! defined ( _COMMON_HPP_ )
    #include "common.hpp"
#endif

#if ! defined ( __STDIO_H )
    #include <stdio.h>
#endif

#define IWAD_ID		0x44415749	// ASCII - 'IWAD'
#define PWAD_ID		0x44415750	// ASCII - 'PWAD'

#define IS_TYPE(x,y)	(( * ( ULONG * ) ( x )) == ( y ))

#define MAX_LUMP_NAME	8

extern void ProgError ( char *, ... );

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

class wadFilter {
public:
    virtual const char *getFileSpec () const = 0;
    virtual bool isRecognized ( ULONG, void * ) const = 0;
    virtual bool isRecognized ( const char * ) const = 0;
    virtual bool readData ( FILE *, ULONG *, void ** ) const = 0;
    virtual bool writeData ( FILE *, ULONG, void * ) const = 0;
};

class WAD {

    char        *wadName;
    FILE        *wadFile;

    bool      valid;
    bool      registered;
    bool      dirChanged;		// wadDirEntry added/deleted

    wadHeader    header;
    wadDirEntry *directory;
    wadDirInfo  *dirInfo;
    eWadStatus   wadStatus;
    eWadType     wadType;
    eWadStyle    wadStyle;
    
    const wadDirEntry *mapStart,	*mapEnd;
    const wadDirEntry *spriteStart,	*spriteEnd;
    const wadDirEntry *patchStart,	*patchEnd;
    const wadDirEntry *flatStart,	*flatEnd;

    void       **newData;

    static int          noFilters;
    static wadFilter  **filter;

    bool EnlargeDirectory ( int holePos, int entries );
    bool ReduceDirectory ( int holePos, int entries );

    void FindMarkers ();
		     
    void readMasterDir ();
    void writeMasterDir ();

    ULONG  indexOf ( const wadDirEntry * ) const;

public:

    WAD ( const char * );
   ~WAD ();

    static bool isMap ( const char * );
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

    int  InsertBefore ( const wLumpName *, const char *, bool, const wadDirEntry * = NULL );
    int  InsertAfter ( const wLumpName *, const char *, bool, const wadDirEntry * = NULL );
					  									
    int  InsertBefore ( const wLumpName *, ULONG, void *, bool, const wadDirEntry * = NULL );
    int  InsertAfter ( const wLumpName *, ULONG, void *, bool, const wadDirEntry * = NULL );
							 
    bool Remove ( const wLumpName *, const wadDirEntry * = NULL, const wadDirEntry * = NULL  );
};

inline void WAD::Format ( ULONG newFormat )	{ * ( ULONG * ) header.type = newFormat; }
inline void WAD::Type ( eWadType newType )	{ wadType = newType; }
inline void WAD::Style ( eWadStyle newStyle )	{ wadStyle = newStyle; }
		       		   	    		   	       
inline const char *WAD::Name () const		{ return wadName; }
inline ULONG       WAD::DirSize () const	{ return header.dirSize; }
inline ULONG       WAD::Format () const		{ return * ( ULONG * ) header.type; }
inline eWadStatus  WAD::Status () const		{ return wadStatus; }
inline eWadType    WAD::Type () const		{ return wadType; }
inline eWadStyle   WAD::Style () const		{ return wadStyle; }
inline bool     WAD::IsValid () const	{ return valid; }
inline bool     WAD::IsRegistered () const	{ return registered; }

struct wadListDirEntry {
    WAD               *wad;
    const wadDirEntry *entry;
};

class wadList {

    ULONG            dirSize;
    ULONG            maxSize;
    wadListDirEntry *directory;
    eWadType         wadType;
    eWadStyle        wadStyle;

    struct wadListEntry {
        WAD          *wad;
        wadListEntry *Next;
    } *List;

    ULONG  indexOf ( const wadListDirEntry * ) const;

    int    AddLevel ( ULONG, const wadDirEntry *&, WAD * );
    void   AddDirectory ( WAD *, bool = true );

public:

    wadList ();
    ~wadList ();

    bool Add ( WAD * );
    bool Remove ( WAD * );
    void Clear ();

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
    bool isEmpty () const;
    bool HasChanged () const;
};

inline ULONG     wadList::DirSize () const	{ return dirSize; }
inline bool      wadList::isEmpty () const	{ return ( List == NULL ) ? true : false; }
inline eWadType  wadList::Type () const		{ return wadType; }
inline eWadStyle wadList::Style () const	{ return wadStyle; }

#endif
