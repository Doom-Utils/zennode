//----------------------------------------------------------------------------
//
// File:        wad.cpp
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

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.hpp"
#include "wad.hpp"
#include "level.hpp"

#if defined ( __GNUC__ )
    #include <unistd.h>
    #define stricmp strcasecmp
    #define TEMP_DIR		"TMPDIR="
#else
    #include <io.h>
    #if ! defined ( _MSC_VER )
        #include <dir.h>
    #endif
    #include <sys\stat.h>
    #define TEMP_DIR		"TMP="
#endif

int WAD::noFilters;
wadFilter **WAD::filter;

#if defined ( __GNUC__ )
void _fullpath ( char *full, const char *name, int max )
{
    strncpy ( full, name, max );
}
#endif

WAD::WAD ( const char *filename )
{
    wadName = ( filename ) ? strdup ( filename ) : strdup ( "" );
    wadFile = NULL;

    list = NULL;

    valid      = false;
    registered = false;
    dirChanged = false;

    memset ( &header, 0, sizeof ( header ));

    directory = NULL;
    dirInfo   = NULL;

    wadStatus = ws_UNKNOWN;
    wadType   = wt_UNKNOWN;
    wadStyle  = wst_UNKNOWN;

    mapStart = mapEnd = NULL;
    mapStart = mapEnd = NULL;
    spriteStart = spriteEnd = NULL;
    patchStart = patchEnd = NULL;
    flatStart = flatEnd = NULL;

    newData = NULL;

    if ( filename ) {
        OpenFile ();
    }
}

WAD::~WAD ()
{
    CloseFile ();

    free ( wadName );
}

void WAD::SetList ( wadList *_list )
{
    list = _list;
}

bool WAD::isMap ( const char *name )
{
    if ( name == NULL ) return false;
    if (( name[0] != 'M' ) && ( name[0] != 'E' )) return false;
    if ( strncmp ( name, "MAP", 3 ) == 0 ) {
        int level;
        if ( sscanf ( name+3, "%d", &level ) == 0 ) return false;
        return (( level >= 1 ) && ( level <= 99 )) ? true : false;
    }
    if (( name[0] == 'E' ) && ( name[2] == 'M' )) {
       int episode = name[1], mission = name[3];
       if (( episode < '1' ) || ( episode > '4' )) return false;
       if (( mission < '1' ) || ( mission > '9' )) return false;
       return true;
    }
    return false;
}

ULONG WAD::FileSize () const
{
    ULONG totalSize = sizeof ( wadHeader ) + header.dirSize * sizeof ( wadDirEntry );
    for ( ULONG i = 0; i < header.dirSize; i++ ) {
        totalSize += directory [i].size;
    }

    return totalSize;
}

bool WAD::AddFilter ( wadFilter *newFilter )
{
    wadFilter **newList = new wadFilter * [ noFilters + 1 ];
    if ( filter ) {
        memcpy ( newList, filter, sizeof ( wadFilter * ) * noFilters );
        delete filter;
    }
    filter = newList;
    filter [ noFilters++ ] = newFilter;

    return true;
}

ULONG WAD::indexOf ( const wadDirEntry *entry ) const
{
    return (( entry < directory ) || ( entry > directory + header.dirSize )) ? -1 : entry - directory;
}

bool WAD::HasChanged ( const wadDirEntry *entry ) const
{
    ULONG index = indexOf ( entry );
    return ( index == ( ULONG ) -1 ) ? false : dirInfo [ index ].newData ? true : false;
}									 

void WAD::Seek ( ULONG offset )
{
    wadStatus = ws_OK;
    if ( wadFile == NULL ) {
        wadStatus = ws_INVALID_FILE;
    } else if ( fseek ( wadFile, offset, SEEK_SET )) {
        wadStatus = ws_SEEK_ERROR;
    }
}

void WAD::ReadBytes ( void *ptr , ULONG size, ULONG count )
{
    wadStatus = ws_OK;
    if ( wadFile == NULL ) {
        wadStatus = ws_INVALID_FILE;
    } else if ( fread ( ptr, count, size, wadFile ) != size ) {
        wadStatus = ws_READ_ERROR;
    }
}

void *WAD::ReadEntry ( const char *name, ULONG *size, const wadDirEntry *start, const wadDirEntry *end, bool cache )
{
    return ReadEntry ( FindDir ( name, start, end ), size, cache );
}

void *WAD::ReadEntry ( const wadDirEntry *entry, ULONG *size, bool cache )
{
    char *buffer = NULL;
    if ( size ) *size = 0;
    ULONG index = indexOf ( entry );
    if ( index != ( ULONG ) -1 ) {
        buffer = new char [ entry->size + 1 ];
        if ( size ) *size = entry->size;
        if ( dirInfo [ index ].newData ) {
            memcpy ( buffer, dirInfo [ index ].newData, entry->size );
        } else if ( dirInfo [ index ].cacheData ) {
            memcpy ( buffer, dirInfo [ index ].cacheData, entry->size );
        } else {
            Seek ( entry->offset );
            if ( wadStatus == ws_OK ) ReadBytes ( buffer, entry->size );
            if ( cache ) {
                dirInfo [ index ].cacheData = new UCHAR [ entry->size + 1 ];
                memcpy ( dirInfo [ index ].cacheData, buffer, entry->size );
                dirInfo [ index ].cacheData [ entry->size ] = '\0';
            }		 		    
        }
        buffer [ entry->size ] = '\0';
    }
    return ( void * ) buffer;
}

bool WAD::WriteEntry ( const char *name, ULONG newSize, void *newStuff, bool owner, const wadDirEntry *start, const wadDirEntry *end )
{
    const wadDirEntry *entry = FindDir ( name, start, end );
    return WriteEntry ( entry, newSize, newStuff, owner );
}

bool WAD::WriteEntry ( const wadDirEntry *entry, ULONG newSize, void *newStuff, bool owner )
{
    ULONG index = indexOf ( entry );
    if ( index == ( ULONG ) -1 ) return false;

    if ( newSize && ( newSize == entry->size )) {
        void *oldStuff = ReadEntry ( entry, NULL );
        if ( memcmp ( newStuff, oldStuff, newSize ) == 0 ) {
            delete oldStuff;
            if ( owner ) delete newStuff;
            return false;
        }
        delete oldStuff;
    }

    UCHAR *temp = ( UCHAR * ) newStuff;
    if ( ! owner ) {
        temp = new UCHAR [ newSize ];
        memcpy ( temp, newStuff, newSize );
    }
    if ( dirInfo [ index ].cacheData ) {
        delete dirInfo [ index ].cacheData;
        dirInfo [ index ].cacheData = NULL;
    }

    dirInfo [ index ].newData = temp;
    directory [ index ].size = newSize;
    directory [ index ].offset = ( ULONG ) -1;

    return true;
}

void WAD::OpenFile ()
{
    if ( wadFile ) fclose ( wadFile );
    wadFile = NULL;

    int handle = open ( wadName, O_RDONLY );
    if ( handle < 0 ) {
        wadStatus = ( errno == ENOENT ) ? ws_INVALID_FILE : ws_CANT_READ;
        return;
    } else {
        close ( handle );
    }

    if (( wadFile = fopen ( wadName, "rb" )) == NULL ) {
        wadStatus = ws_INVALID_FILE;
        return;
    }

    // read in the WAD's header
    ReadBytes ( &header, sizeof ( header ));

    if ( ! IS_TYPE ( header.type, IWAD_ID ) && ! IS_TYPE ( header.type, PWAD_ID )) {
        wadStatus = ws_INVALID_WAD;
        return;
    }
    wadStatus = ws_OK;

    // read in the WAD's directory info
    valid = true;
    readMasterDir ();
    if ( FindDir ( "TEXTURE2" )) registered = true;

    if ( FindDir ( "BEHAVIOR" )) wadType = wt_HEXEN;
    else if ( FindDir ( "M_HTIC" )) wadType = wt_HERETIC;
    else if ( FindDir ( "SHT2A0" )) wadType = wt_DOOM2;
    else if ( FindDir ( "S_START" )) wadType = wt_DOOM;

    switch ( wadType ) {
        case wt_DOOM    : wadStyle = wst_FORMAT_1;	break;
        case wt_DOOM2   : wadStyle = wst_FORMAT_2;	break;
        case wt_HERETIC : wadStyle = wst_FORMAT_1;	break;
        case wt_HEXEN   : wadStyle = wst_FORMAT_3;	break;
        default :
            if ( mapStart ) wadStyle = ( toupper ( mapStart->name[0] ) == 'E' ) ? wst_FORMAT_1 : wst_FORMAT_2;
    }	    						
}

void WAD::CloseFile ()
{
    valid = false;
    registered = false;
    dirChanged = false;

    if ( dirInfo ) {
        for ( ULONG i = 0; i < header.dirSize; i++ ) {
            if ( dirInfo [i].newData ) delete dirInfo [i].newData;
            dirInfo [i].newData = NULL;
            if ( dirInfo [i].cacheData ) delete dirInfo [i].cacheData;
            dirInfo [i].cacheData = NULL;
            dirInfo [i].type = wl_UNCHECKED;
        }
        delete dirInfo;
    }
    dirInfo = NULL;

    if ( directory ) delete [] directory;
    directory = NULL;

    mapStart = mapEnd = NULL;

    if ( wadFile ) fclose ( wadFile );
    wadFile = NULL;

    memset ( &header, 0, sizeof ( header ));
}

void WAD::FindMarkers ()
{
    mapStart = mapEnd = NULL;
    ULONG s;
    for ( s = 0; s < header.dirSize; s++ ) {
        if ( isMap ( directory [s].name )) {
            mapStart = &directory [s];
            break;
        }
    }
    for ( ULONG e = header.dirSize - 1; e >= s; e-- ) {
        if ( isMap ( directory [e].name )) {
            mapEnd = &directory [e];
            break;
        }
    }
    if ( mapEnd ) mapEnd += 10;

    spriteStart = FindDir ( "S_START" );
    spriteEnd = FindDir ( "S_END", spriteStart );

    patchStart = FindDir ( "P_START" );
    patchEnd = FindDir ( "P_END", patchStart );

    flatStart = FindDir ( "F_START" );
    flatEnd = FindDir ( "F_END", flatStart );
}

void WAD::readMasterDir ()
{
    if ( directory ) delete [] directory;
    directory = new wadDirEntry [ header.dirSize ];
    dirInfo = new wadDirInfo [ header.dirSize ];
    for ( ULONG i = 0; i < header.dirSize; i++ ) {
        dirInfo [i].newData = NULL;
        dirInfo [i].cacheData = NULL;
        dirInfo [i].type = wl_UNCHECKED;
    }
    Seek ( header.dirStart);		     
    ReadBytes ( directory, sizeof ( wadDirEntry ), header.dirSize );
    FindMarkers ();
}		

const wadDirEntry *WAD::GetDir ( ULONG index ) const
{
    return ( index >= header.dirSize ) ? ( const wadDirEntry * ) NULL : &directory [index];
}

const wadDirEntry *WAD::FindDir ( const char *name, const wadDirEntry *start, const wadDirEntry *end ) const
{
    ULONG i = 0, last = header.dirSize - 1;
    if ( start ) {
        i = indexOf ( start );
        if ( i == ( ULONG ) -1 ) return NULL;
    }
    if ( end ) {
        last = indexOf ( end );
        if ( last == ( ULONG ) -1 ) return NULL;
    }
    const wadDirEntry *dir = &directory [i];
    for ( ; i <= last; i++, dir++ ) {
        if ( dir->name[0] != name[0] ) continue;
        if ( strncmp ( dir->name, name, 8 ) == 0 ) return dir;
    }
    return NULL;
}

bool WAD::HasChanged () const
{
    if ( dirChanged ) return true;
    bool changed = false;
    for ( ULONG i = 0; ! changed && ( i < header.dirSize ); i++ ) {
        if ( dirInfo [i].newData ) changed = true;
    }
				 
    return changed;
}

bool WAD::EnlargeDirectory ( int holePos, int entries )
{
    int newSize = header.dirSize + entries;

    wadDirEntry *newDir = new wadDirEntry [ newSize ];
    wadDirInfo *newInfo = new wadDirInfo [ newSize ];

    if (( newDir == NULL ) || ( newInfo == NULL )) {
        if ( newDir ) delete newDir;
        if ( newInfo ) delete newInfo;
        return false;
    }

    int loCount = holePos;
    int hiCount = header.dirSize - holePos;

    memset ( newDir, 0, sizeof ( wadDirEntry ) * newSize );
    memset ( newInfo, 0, sizeof ( wadDirInfo ) * newSize );

    memcpy ( newDir, directory, sizeof ( wadDirEntry ) * loCount );
    memcpy ( newDir + loCount + entries, directory + loCount, sizeof ( wadDirEntry ) * hiCount );

    memcpy ( newInfo, dirInfo, sizeof ( wadDirInfo ) * loCount );
    memcpy ( newInfo + loCount + entries, dirInfo + loCount, sizeof ( wadDirInfo ) * hiCount );

    if ( directory ) delete [] directory;
    directory = newDir;

    if ( dirInfo ) delete dirInfo;
    dirInfo = newInfo;
	    
    header.dirSize = newSize;
    FindMarkers ();

    return true;
}

bool WAD::ReduceDirectory ( int holePos, int entries )
{
    if ( holePos + entries > ( int ) header.dirSize ) entries = header.dirSize - holePos;
    int hiCount = header.dirSize - ( holePos + entries );

    if ( hiCount > 0 ) {
        memcpy ( directory + holePos, directory + holePos + entries, sizeof ( wadDirEntry ) * hiCount );
        memcpy ( dirInfo + holePos, dirInfo + holePos + entries, sizeof ( wadDirInfo ) * hiCount );
    }
    header.dirSize -= entries;

    if ( list != NULL ) list->UpdateDirectory ();

    return true;
}

bool WAD::InsertBefore ( const wLumpName *name, ULONG newSize, void *newStuff, bool owner, const wadDirEntry *entry )
{
    ULONG index = indexOf ( entry );
    if ( entry && ( index == ( ULONG ) -1 )) return false;

    if ( entry == NULL ) index = 0;
    if ( ! EnlargeDirectory ( index, 1 )) return false;

    wadDirEntry *newDir = &directory [ index ];
    strncpy ( newDir->name, ( char * ) name, sizeof ( wLumpName ));

    bool retVal = WriteEntry ( newDir, newSize, newStuff, owner );

    if ( list != NULL ) list->UpdateDirectory ();

    return retVal;
}

bool WAD::InsertAfter ( const wLumpName *name, ULONG newSize, void *newStuff, bool owner, const wadDirEntry *entry )
{
    ULONG index = indexOf ( entry );
    if ( entry && ( index == ( ULONG ) -1 )) return false;

    if ( entry == NULL ) index = header.dirSize;
    else index += 1;

    if ( ! EnlargeDirectory ( index, 1 )) return false;

    wadDirEntry *newDir = &directory [ index ];
    strncpy ( newDir->name, ( char * ) name, sizeof ( wLumpName ));

    bool retVal = WriteEntry ( newDir, newSize, newStuff, owner );

    if ( list != NULL ) list->UpdateDirectory ();

    return retVal;
}

bool WAD::Remove ( const wLumpName *lump, const wadDirEntry *start, const wadDirEntry *end )
{
    const wadDirEntry *entry = FindDir ( *lump, start, end );
    ULONG index = indexOf ( entry );
    if ( index == ( ULONG ) -1 ) return false;

    return ReduceDirectory ( index, 1 );
}

/*
// TBD
int  InsertBefore ( const wLumpName *, ULONG, void *, bool, const wadDirEntry * = NULL );
int  InsertAfter ( const wLumpName *, ULONG, void *, bool, const wadDirEntry * = NULL );
// TBD
*/

bool WAD::SaveFile ( const char *newName )
{
    if ( newName == NULL ) newName = wadName;
    const char *tempName = newName;

    char wadPath [MAXPATH], newPath [MAXPATH];
    _fullpath ( wadPath, wadName, MAXPATH );
    _fullpath ( newPath, newName, MAXPATH );

    if ( stricmp ( wadPath, newPath ) == 0 ) {
        if ( HasChanged () == false ) return true;
        putenv ( TEMP_DIR );
        tempName = tempnam ( ".", "wad" );
    }

    FILE *tmpFile = fopen ( tempName, "wb" );
    if ( tmpFile == NULL ) return false;

    bool errors = false;
    if ( fwrite ( &header, sizeof ( header ), 1, tmpFile ) != 1 ) {
        errors = true;
//      fprintf ( stderr, "ERROR: WAD::SaveFile - Error writing dummy header.\n" );
    }

    wadDirEntry *dir = directory;
    ULONG i;
    for ( i = 0; i < header.dirSize; i++ ) {
        ULONG offset = ftell ( tmpFile );
        if ( dir->size ) {
            if ( dirInfo [i].newData ) {
                if ( fwrite ( dirInfo [i].newData, dir->size, 1, tmpFile ) != 1 ) {
                    errors = true;
//                  fprintf ( stderr, "ERROR: WAD::SaveFile - Error writing entry %8.8s. (newData)\n", dir->name );
                }
            } else if ( dirInfo [i].cacheData ) {
                if ( fwrite ( dirInfo [i].cacheData, dir->size, 1, tmpFile ) != 1 ) {
                    errors = true;
//                  fprintf ( stderr, "ERROR: WAD::SaveFile - Error writing entry %8.8s. (cached)\n", dir->name );
                }
            } else {
                void *ptr = ReadEntry ( dir, NULL );
                if ( wadStatus != ws_OK ) {
                    errors = true;
//                  fprintf ( stderr, "ERROR: WAD::SaveFile - Error reading entry %8.8s. (%04x)\n", dir->name, wadStatus );
                }
                if ( fwrite ( ptr, dir->size, 1, tmpFile ) != 1 ) {
                    errors = true;
//                  fprintf ( stderr, "ERROR: WAD::SaveFile - Error writing entry %8.8s. (file copy)\n", dir->name );
                }
                delete ptr;
            }
	}
        dir->offset = offset;
        dir++;
    }

    header.dirStart = ftell ( tmpFile );
    if ( fwrite ( directory, sizeof ( wadDirEntry ), header.dirSize, tmpFile ) != header.dirSize ) {
        errors = true;
//      fprintf ( stderr, "\nERROR: WAD::SaveFile - Error writing directory." );
    }
    fseek ( tmpFile, 0, SEEK_SET );
    if ( fwrite ( &header,  sizeof ( header ), 1, tmpFile ) != 1 ) {
        errors = true;
//      fprintf ( stderr, "\nERROR: WAD::SaveFile - Error writing header." );
    }
    fclose ( tmpFile );

    if ( errors ) {
        remove ( tempName );
        return false;
    }

    if ( stricmp ( wadPath, newPath ) == 0 ) {
        if ( wadFile ) fclose ( wadFile );
        if ( remove ( wadName ) != 0 ) {
//          fprintf ( stderr, "\nERROR: WAD::SaveFile - Unable to remove %s.", wadName );
            return false;
        }
        if ( rename ( tempName, wadName ) != 0 ) {
//          fprintf ( stderr, "\nERROR: WAD::SaveFile - Unable to rename %s to %s.", tempName, wadName );
            return false;
        }
        free (( char * ) tempName );
        wadFile = fopen ( wadName, "rb" );
    }

    for ( i = 0; i < header.dirSize; i++ ) {
        if ( dirInfo [i].newData ) {
            if ( dirInfo [i].cacheData ) delete dirInfo [i].cacheData;
            dirInfo [i].cacheData = dirInfo [i].newData;
            dirInfo [i].newData = NULL;
        }
    }

    return true;
}

wadList::wadList ()
{
    dirSize = 0;
    maxSize = 0;
    directory = NULL;
    List = NULL;
    wadType = wt_UNKNOWN;
    wadStyle = wst_UNKNOWN;
}

wadList::~wadList ()
{
    while ( List ) {
        wadListEntry *temp = List->Next;
        delete List->wad;
        delete List;
        List = temp;
    }
    if ( directory ) delete [] directory;
}

int wadList::wadCount () const
{
    int size = 0;
    wadListEntry *ptr = List;
    while ( ptr ) {
        size++;
        ptr = ptr->Next;
    }
    return size;
}

ULONG wadList::FileSize () const
{
    ULONG totalSize = sizeof ( wadHeader ) + dirSize * sizeof ( wadDirEntry );
    for ( ULONG i = 0; i < dirSize; i++ ) {
        totalSize += directory [i].entry->size;
    }

    return totalSize;
}

WAD *wadList::GetWAD ( int index ) const
{
    wadListEntry *ptr = List;
    while ( ptr && index-- ) ptr = ptr->Next;
    return ptr ? ptr->wad : NULL;
}

void wadList::Clear ()
{
    wadListEntry *ptr = List;
    while ( ptr ) {
        wadListEntry *next = ptr->Next;
        delete ptr->wad;
        delete ptr;
        ptr = next;
    }
    if ( directory ) delete [] directory;

    dirSize = 0;
    maxSize = 0;
    directory = NULL;
    List = NULL;
    wadType = wt_UNKNOWN;
    wadStyle = wst_UNKNOWN;
}

void wadList::UpdateDirectory ()
{
    dirSize = 0;
    wadListEntry *ptr = List;
    while ( ptr ) {
        AddDirectory ( ptr->wad );
        ptr = ptr->Next;
    }
}

bool wadList::Add ( WAD *wad )
{
    if (( wadType == wt_UNKNOWN ) && ( wadStyle == wst_UNKNOWN )) {
        wadType = wad->Type ();
        wadStyle = wad->Style ();
    }

    if (( wadType != wt_UNKNOWN ) && ( wad->Type () == wt_UNKNOWN ) 
                                  && ( wad->Format () == PWAD_ID )) {
        const wadDirEntry *dir = wad->FindDir ( "SECTORS" );
        if ( dir ) {
            ULONG temp;
            wSector *sector = ( wSector * ) wad->ReadEntry ( dir, &temp, true );
            int noSectors = temp / sizeof ( wSector );
            char tempName [ MAX_LUMP_NAME + 1 ]; 
            tempName [ MAX_LUMP_NAME ] = '\0';
            int i;
            for ( i = 0; i < noSectors; i++ ) {
                strncpy ( tempName, sector[i].floorTexture, MAX_LUMP_NAME );
                if ( FindWAD ( tempName ) == NULL ) break;
                strncpy ( tempName, sector[i].ceilTexture, MAX_LUMP_NAME );
                if ( FindWAD ( tempName ) == NULL ) break;
            }
            if ( i == noSectors ) wad->Type ( wadType );
            delete sector;
        }
    }

    if ( wadType != wad->Type ()) return false;
    if ( wadStyle != wad->Style ()) return false;
    
    wadListEntry *newNode = new wadListEntry;
    newNode->wad = wad;
    newNode->Next = NULL;
    
    if ( List == NULL ) {
        List = newNode;
    } else {
        wadListEntry *ptr = List;
        while ( ptr->Next ) {
            ptr = ptr->Next;
        }
        ptr->Next = newNode;
    }

    AddDirectory ( wad, List->Next ? true : false );

    wad->SetList ( this );

    return true;
}

bool wadList::Remove ( WAD *wad )
{	
    bool found = false;
    wadListEntry *ptr = List;

    if ( List->wad == wad ) {
        found = true;
        List = List->Next;
        delete ptr;
    } else {
        while ( ptr->Next ) {
            if ( ptr->Next->wad == wad ) {
                found = true;
                wadListEntry *next = ptr->Next->Next;
                delete ptr->Next;
                ptr->Next = next;
                break;
            }
            ptr = ptr->Next;
        }
    }

    if ( found ) {
        wad->SetList ( NULL );
        dirSize = 0;
        ptr = List;
        while ( ptr ) {
            AddDirectory ( ptr->wad );
            ptr = ptr->Next;
        }
    }

    if ( dirSize == 0 ) wadType = wt_UNKNOWN;

    return found;
}

ULONG wadList::indexOf ( const wadListDirEntry *entry ) const
{
    return (( entry < directory ) || ( entry > directory + dirSize )) ? -1 : entry - directory;
}

int wadList::AddLevel ( ULONG index, const wadDirEntry *&entry, WAD *wad )
{
    int size = 0;
    const wadDirEntry *start = entry + 1;
    const wadDirEntry *end = entry + 11;

    if ( wad->FindDir ( "THINGS",    start, end )) size++;
    if ( wad->FindDir ( "LINEDEFS",  start, end )) size++;
    if ( wad->FindDir ( "SIDEDEFS",  start, end )) size++;
    if ( wad->FindDir ( "VERTEXES",  start, end )) size++;
    if ( wad->FindDir ( "SEGS",      start, end )) size++;
    if ( wad->FindDir ( "SSECTORS",  start, end )) size++;
    if ( wad->FindDir ( "NODES",     start, end )) size++;
    if ( wad->FindDir ( "SECTORS",   start, end )) size++;
    if ( wad->FindDir ( "REJECT",    start, end )) size++;
    if ( wad->FindDir ( "BLOCKMAP",  start, end )) size++;
    if ( wad->FindDir ( "BEHAVIOR",  start, end )) size++;

    if ( index == dirSize ) {
        dirSize += size;
        for ( int i = 0; i < size; i++ ) {
            directory [ index ].wad = wad;
            directory [ index ].entry = ++entry;
            index++;
        }
    } else {
        for ( int i = 0; i < size; i++ ) {
/* TBD proper replacement of level lumps
        const wadListDirEntry *entry = FindWAD ( entry[1].name, index, index + 10 );
        ULONG index = indexOf ( entry );
*/
            directory [ index ].wad = wad;
            directory [ index ].entry = ++entry;
            index++;
        }
    }

    return size;
}

void wadList::AddDirectory ( WAD *wad, bool check )
{
    // Make sure AddDirectory has enough room to work
    if ( dirSize + wad->DirSize () > maxSize ) {
        maxSize = dirSize + wad->DirSize ();
        wadListDirEntry *temp = new wadListDirEntry [ maxSize ];
        if ( directory ) {
            memcpy ( temp, directory, sizeof ( wadListDirEntry ) * dirSize );
            delete [] directory;
        }   
        directory = temp;
    }

    const wadDirEntry *newDir = wad->GetDir ( 0 );
    ULONG count = wad->DirSize ();
    while ( count ) {
        const wadListDirEntry *entry = check ? FindWAD ( newDir->name ) : NULL;
        if ( entry ) {
            ULONG index = indexOf ( entry );
            directory [ index ].wad = wad;
            directory [ index ].entry = newDir;
            if ( WAD::isMap ( newDir->name )) {
                count -= AddLevel ( index + 1, newDir, wad );
            }
        } else {
            ULONG index = dirSize++;
            directory [ index ].wad = wad;
            directory [ index ].entry = newDir;
            if ( WAD::isMap ( newDir->name )) {
                count -= AddLevel ( index + 1, newDir, wad );
            }
        }
        newDir++;
        count--;
    }
}

const wadListDirEntry *wadList::GetDir ( ULONG index ) const
{
    return ( index >= dirSize ) ? ( const wadListDirEntry * ) NULL : &directory [index];
}

const wadListDirEntry *wadList::FindWAD ( const char *name, const wadListDirEntry *start, const wadListDirEntry *end ) const
{
    int i = 0, last = dirSize;

    if ( start ) i = indexOf ( start );
    if ( end ) last = indexOf ( end );


    for ( ; i < last; i++ ) {
        const wadListDirEntry *dir = &directory [i];
        if ( dir->entry->name[0] != name[0] ) continue;
        if ( strncmp ( dir->entry->name, name, 8 ) == 0 ) return dir;
    }

    return NULL;
}

bool wadList::HasChanged () const
{
    wadListEntry *ptr = List;
    while ( ptr ) {
        if ( ptr->wad->HasChanged ()) return true;
        ptr = ptr->Next;
    }
    return false;
}

bool wadList::Contains ( WAD *wad ) const
{
    wadListEntry *ptr = List;
    while ( ptr ) {
        if ( ptr->wad == wad ) return true;
        ptr = ptr->Next;
    }
    return false;
}

bool wadList::Save ( const char *newName )
{
    if ( isEmpty ()) return false;

    if ( List->Next ) {

        wadListEntry *ptr = List;
        const char *wadName = NULL;
        char wadPath [MAXPATH], newPath [MAXPATH];
        _fullpath ( newPath, newName, MAXPATH );
        while ( ptr->Next ) {
            wadName = ptr->wad->Name ();
            _fullpath ( wadPath, wadName, MAXPATH );
            if ( stricmp ( wadPath, newPath ) == 0 ) break;
            ptr = ptr->Next;
        }    
        WAD *wad = ptr->wad;
        if ( newName == NULL ) newName = wadName;
        const char *tempName = newName;
        if ( stricmp ( wadPath, newPath ) == 0 ) {
            tempName = tmpnam ( NULL );
        }
    
        FILE *tmpFile = fopen ( tempName, "wb" );
        if ( tmpFile == NULL ) return false;

        bool errors = false;
        wadHeader header;
        if ( fwrite ( &header,  sizeof ( header ), 1, tmpFile ) != 1 ) {
            errors = true;
            fprintf ( stderr, "\nERROR: wadList::Save - Error writing dummy header." );
        }

        wadListDirEntry *srcDir = directory;
        wadDirEntry *dir = new wadDirEntry [ dirSize ];
        for ( ULONG i = 0; i < dirSize; i++ ) {
            dir[i] = *srcDir->entry;
            long offset = ftell ( tmpFile );
            void *ptr = srcDir->wad->ReadEntry ( srcDir->entry, NULL );
            if ( srcDir->wad->Status () != ws_OK ) {
                errors = true;
//              fprintf ( stderr, "\nERROR: wadList::Save - Error reading entry %8.8s. (%04X)", srcDir->entry->name, srcDir->wad->Status ());
            }
            if (( dir[i].size > 0 ) && ( fwrite ( ptr, dir[i].size, 1, tmpFile ) != 1 )) {
                errors = true;
//                fprintf ( stderr, "\nERROR: wadList::Save - Error writing entry %8.8s.", srcDir->entry->name );
            }
            delete ptr;
            dir[i].offset = offset;
            srcDir++;
        }

        * ( ULONG * ) header.type = wad->Format ();
        header.dirSize = dirSize;
        header.dirStart = ftell ( tmpFile );

        if ( fwrite ( dir, sizeof ( wadDirEntry ), dirSize, tmpFile ) != dirSize ) {
            errors = true;
//            fprintf ( stderr, "\nERROR: wadList::Save - Error writing directory." );
        }
        delete dir;

        fseek ( tmpFile, 0, SEEK_SET );
        if ( fwrite ( &header,  sizeof ( header ), 1, tmpFile ) != 1 ) {
            errors = true;
//            fprintf ( stderr, "\nERROR: wadList::Save - Error writing header." );
        }

        fclose ( tmpFile );

        if ( errors ) {
            remove ( tempName );
            return false;
        }

        if ( stricmp ( wadPath, newPath ) == 0 ) {
            wad->CloseFile ();
            if ( remove ( wadName ) != 0 ) {
//              fprintf ( stderr, "\nERROR: wadList::Save - Unable to remove %s.", wadName );
                return false;
            }
            if ( rename ( tempName, wadName ) != 0 ) {
//              fprintf ( stderr, "\nERROR: wadList::Save - Unable to rename %s to %s.", tempName, wadName );
                return false;
            }
            wad->OpenFile ();
        }
    } else {
        return List->wad->SaveFile ( newName );
    }

    return true;
}

bool wadList::Extract ( const wLumpName *res, const char *wadName )
{
    ULONG size;
    const wadListDirEntry *dir;

    WAD *newWad = new WAD ( NULL );

    bool hasMaps = false;
    for ( int i = 0; res [i][0]; i++ ) {
        if ( WAD::isMap ( res[i] )) {
            hasMaps = true;
            const wadListDirEntry *dir = FindWAD ( res[i] );
            DoomLevel *level = new DoomLevel ( res[i], dir->wad, true );
            level->AddToWAD ( newWad );
            delete level;
        } else {
            if (( dir = FindWAD ( res[i], NULL, NULL )) != NULL ) {
                void *ptr = dir->wad->ReadEntry ( dir->entry, &size, false );
                newWad->InsertAfter (( const wLumpName * ) res[i], size, ptr, true );
            }
        }
    }

    if ( hasMaps ) {
        if (( dir = FindWAD ( "MAPINFO" )) != NULL ) {
            void *ptr = dir->wad->ReadEntry ( dir->entry, &size, false );
            newWad->InsertAfter (( const wLumpName * ) "MAPINFO", size, ptr, true );
        }
        if (( dir = FindWAD ( "SNDINFO" )) != NULL ) {
            void *ptr = dir->wad->ReadEntry ( dir->entry, &size, false );
            newWad->InsertAfter (( const wLumpName * ) "SNDINFO", size, ptr, true );
        }
    }

    newWad->Format ( PWAD_ID );

    char filename [ 256 ];
    if ( wadName ) strcpy ( filename, wadName );
    else sprintf ( filename, "%s.WAD", res [0] );
    bool retVal = newWad->SaveFile ( filename );
    delete newWad;

    return retVal;
}
