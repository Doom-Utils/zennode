//----------------------------------------------------------------------------
//
// File:        ZenMain.cpp
// Date:        26-Oct-1994
// Programmer:  Marc Rousseau
//
// Description: The application specific code for ZenNode
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
//   06-??-95	Added Win32 support
//   07-19-95	Updated command line & screen logic
//   11-19-95	Updated command line again
//   12-06-95	Add config & customization file support
//   11-??-98	Added Linux support
//
//----------------------------------------------------------------------------

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined ( __OS2__ )
    #include <conio.h>
    #include <dos.h>
    #include <io.h>
    #define INCL_DOS
    #define INCL_SUB
    #include <os2.h>
#elif defined ( __WIN32__ )
    #include <conio.h>
    #include <io.h>
#elif defined ( __GNUC__ )
    #include <unistd.h>
#else
    #error This program must be compiled as a 32-bit app.
#endif

#include "common.hpp"
#include "logger.hpp"
#include "wad.hpp"
#include "level.hpp"
#include "ZenNode.hpp"

#if defined ( __BORLANDC__ )
    #include <dir.h>
#endif

DBG_REGISTER ( __FILE__ );

#if defined ( DEBUG )
    LOG_FLAGS g_LogFlags;
#endif

#define VERSION		"1.0.8"
#define BANNER          "ZenNode Version " VERSION " (c) 1994-2001 Marc Rousseau"
#define CONFIG_FILENAME	"ZenNode.cfg"
#define MAX_LEVELS	99

#if defined ( __BORLANDC__ )
    #pragma option -x -xf
#endif

char HammingTable [ 256 ];

struct sOptions {
    sBlockMapOptions BlockMap;
    sNodeOptions     Nodes;
    sRejectOptions   Reject;
    bool             WriteWAD;
    bool             Extract;
} config;

extern ULONG startX, startY;

void SaveConsoleSettings ();
void RestoreConsoleSettings ();

#if defined ( __OS2__ )

#define SEPERATOR	'\\'
#define DEFAULT_CHAR	'û'

#elif defined ( __WIN32__ )

#define SEPERATOR	'\\'
#define DEFAULT_CHAR	'û'

#elif defined ( __GNUC__ )

#define SEPERATOR	'/'
#define DEFAULT_CHAR	'*'

#define stricmp strcasecmp

extern void cprintf ( char *, ... );
extern char *strupr ( char *ptr );
extern int getch ();
extern bool kbhit ();

#endif

void GetXY ( ULONG *x, ULONG *y );
void GotoXY ( ULONG x, ULONG y );
ULONG CurrentTime ();
void Status ( char *message );
void GoRight ();
void GoLeft ();
void Backup ();
void ShowDone ();
void ShowProgress ();
void MoveUp ( int delta );
void MoveDown ( int delta );

void printHelp ()
{
    FUNCTION_ENTRY ( NULL, "printHelp", true );

    fprintf ( stdout, "Usage: ZenNode {-options} filename[.wad] [level{+level}] {-o|x output[.wad]}\n" );
    fprintf ( stdout, "\n" );
    fprintf ( stdout, "     -x+ turn on option   -x- turn off option  %c = default\n", DEFAULT_CHAR );
    fprintf ( stdout, "\n" );
    fprintf ( stdout, "     -b[c]              %c - Rebuild BLOCKMAP\n", DEFAULT_CHAR );
    fprintf ( stdout, "        c               %c   - Compress BLOCKMAP\n", DEFAULT_CHAR );
    fprintf ( stdout, "     -n[m=1,2,3|q|u|i]  %c - Rebuild NODES\n", DEFAULT_CHAR );
    fprintf ( stdout, "        m                   - Partition Selection Algorithm\n" );
    fprintf ( stdout, "                        %c     1 = Minimize splits\n", DEFAULT_CHAR );
    fprintf ( stdout, "                              2 = Minimize BSP depth\n" );
    fprintf ( stdout, "                              3 = Minimize time\n" );
    fprintf ( stdout, "        q                   - Don't display progress bar\n" );
    fprintf ( stdout, "        u               %c   - Ensure all sub-sectors contain only 1 sector\n", DEFAULT_CHAR );
    fprintf ( stdout, "        i                   - Ignore non-visible lineDefs\n" );
    fprintf ( stdout, "     -r[zfcg]           %c - Rebuild REJECT resource\n", DEFAULT_CHAR );
    fprintf ( stdout, "        z                   - Insert empty REJECT resource\n" );
    fprintf ( stdout, "        f                   - Rebuild even if REJECT effects are detected\n" );
    fprintf ( stdout, "        c               %c   - Use child sector info to reduce LOS calculations\n", DEFAULT_CHAR );
    fprintf ( stdout, "        g               %c   - Use graphs to reduce LOS calculations\n", DEFAULT_CHAR );
    fprintf ( stdout, "     -t                   - Don't write output file (test mode)\n" );
    fprintf ( stdout, "\n" );
    fprintf ( stdout, "     level - ExMy for DOOM/Heretic or MAPxx for DOOM II/HEXEN\n" );
}

bool parseBLOCKMAPArgs ( char *&ptr, bool setting )
{
    FUNCTION_ENTRY ( NULL, "parseBLOCKMAPArgs", true );

    config.BlockMap.Rebuild = setting;
    while ( *ptr ) {
        int option = *ptr++;
        bool setting = true;
        if (( *ptr == '+' ) || ( *ptr == '-' )) {
            setting = ( *ptr++ == '+' ) ? true : false;
        }
        switch ( option ) {
            case 'C' : config.BlockMap.Compress = setting;	break;
            default  : return true;
        }
        config.BlockMap.Rebuild = true;
    }
    return false;
}

bool parseNODESArgs ( char *&ptr, bool setting )
{
    FUNCTION_ENTRY ( NULL, "parseNODESArgs", true );

    config.Nodes.Rebuild = setting;
    while ( *ptr ) {
        int option = *ptr++;
        bool setting = true;
        if (( *ptr == '+' ) || ( *ptr == '-' )) {
            setting = ( *ptr++ == '+' ) ? true : false;
        }
        switch ( option ) {
            case '1' : config.Nodes.Method = 1;			break;
            case '2' : config.Nodes.Method = 2;			break;
            case '3' : config.Nodes.Method = 3;			break;
            case 'Q' : config.Nodes.Quiet = setting;		break;
            case 'U' : config.Nodes.Unique = setting;		break;
            case 'I' : config.Nodes.ReduceLineDefs = setting;	break;
            default  : return true;
        }
        config.Nodes.Rebuild = true;
    }
    return false;
}

bool parseREJECTArgs ( char *&ptr, bool setting )
{
    FUNCTION_ENTRY ( NULL, "parseREJECTArgs", true );

    config.Reject.Rebuild = setting;
    while ( *ptr ) {
        int option = *ptr++;
        bool setting = true;
        if (( *ptr == '+' ) || ( *ptr == '-' )) {
            setting = ( *ptr++ == '+' ) ? true : false;
        }
        switch ( option ) {
            case 'Z' : config.Reject.Empty = setting;		break;
            case 'F' : config.Reject.Force = setting;		break;
            case 'C' : config.Reject.FindChildren = setting;	break;
            case 'G' : config.Reject.UseGraphs = setting;	break;
            default  : return true;
        }
        config.Reject.Rebuild = true;
    }
    return false;
}

int parseArgs ( int index, char *argv [] )
{
    FUNCTION_ENTRY ( NULL, "parseArgs", true );

    bool errors = false;
    while ( argv [ index ] ) {

        if ( argv [index][0] != '-' ) break;

        char *localCopy = strdup ( argv [ index ]);
        char *ptr = localCopy + 1;
        strupr ( localCopy );

        bool localError = false;
        while ( *ptr && ( localError == false )) {
            int option = *ptr++;
            bool setting = true;
            if (( *ptr == '+' ) || ( *ptr == '-' )) {
                setting = ( *ptr == '-' ) ? false : true;
                ptr++;
            }
            switch ( option ) {
                case 'B' : localError = parseBLOCKMAPArgs ( ptr, setting );     break;
                case 'N' : localError = parseNODESArgs ( ptr, setting );        break;
                case 'R' : localError = parseREJECTArgs ( ptr, setting );       break;
                case 'T' : config.WriteWAD = ! setting;                         break;
                default  : localError = true;
            }
        }
        if ( localError ) {
            errors = true;
            int offset = ptr - localCopy - 1;
            int width = strlen ( ptr ) + 1;
            fprintf ( stderr, "Unrecognized parameter '%*.*s'\n", width, width, argv [index] + offset );
        }
        free ( localCopy );
        index++;
    }
    if ( errors ) fprintf ( stderr, "\n" );
    return index;
}

void ReadConfigFile ( char *argv [] )
{
    FUNCTION_ENTRY ( NULL, "ReadConfigFile", true );

    FILE *configFile = fopen ( CONFIG_FILENAME, "rt" );
    if ( configFile == NULL ) {
        char fileName [ 256 ];
        strcpy ( fileName, argv [0] );
        char *ptr = &fileName [ strlen ( fileName )];
        while (( --ptr >= fileName ) && ( *ptr != SEPERATOR ));
        *++ptr = '\0';
        strcat ( ptr, CONFIG_FILENAME );
        configFile = fopen ( fileName, "rt" );
    }
    if ( configFile == NULL ) return;

    char ch = ( char ) fgetc ( configFile );
    bool errors = false;
    while ( ! feof ( configFile )) {
        ungetc ( ch, configFile );

        char lineBuffer [ 256 ];
        fgets ( lineBuffer, sizeof ( lineBuffer ), configFile );
        char *basePtr = strupr ( lineBuffer );
        while ( *basePtr == ' ' ) basePtr++;
        basePtr = strtok ( basePtr, "\n\x1A" );

        if ( basePtr ) {
            char *ptr = basePtr;
            bool localError = false;
            while ( *ptr && ( localError == false )) {
                int option = *ptr++;
                bool setting = true;
                if (( *ptr == '+' ) || ( *ptr == '-' )) {
                    setting = ( *ptr++ == '-' ) ? false : true;
                }
                switch ( option ) {
                    case 'B' : localError = parseBLOCKMAPArgs ( ptr, setting );     break;
                    case 'N' : localError = parseNODESArgs ( ptr, setting );        break;
                    case 'R' : localError = parseREJECTArgs ( ptr, setting );       break;
                    case 'T' : config.WriteWAD = ! setting;                         break;
                    default  : localError = true;
                }
            }
            if ( localError ) {
                errors = true;
                int offset = basePtr - lineBuffer - 1;
                int width = strlen ( basePtr ) + 1;
                fprintf ( stderr, "Unrecognized configuration option '%*.*s'\n", width, width, lineBuffer + offset );
            }
        }
        ch = ( char ) fgetc ( configFile );
    }
    fclose ( configFile );
    if ( errors ) fprintf ( stderr, "\n" );
}

int getLevels ( int argIndex, char *argv [], char names [][MAX_LUMP_NAME], wadList *list )
{
    FUNCTION_ENTRY ( NULL, "getLevels", true );

    int index = 0, errors = 0;

    char buffer [128];
    buffer [0] = '\0';
    if ( argv [argIndex] ) {
        strcpy ( buffer, argv [argIndex] );
        strupr ( buffer );
    }
    char *ptr = strtok ( buffer, "+" );

    // See if the user requested specific levels
    if ( WAD::IsMap ( ptr )) {
        argIndex++;
        while ( ptr ) {
            if ( WAD::IsMap ( ptr )) {
                if ( list->FindWAD ( ptr )) {
                    strcpy ( names [index++], ptr );
                } else {
                    fprintf ( stderr, "  Could not find %s\n", ptr, errors++ );
                }
            } else {
                fprintf ( stderr, "  %s is not a valid name for a level\n", ptr, errors++ );
            }
            ptr = strtok ( NULL, "+" );
        }
    } else {
        int size = list->DirSize ();
        const wadListDirEntry *dir = list->GetDir ( 0 );
        for ( int i = 0; i < size; i++ ) {
            if ( dir->wad->IsMap ( dir->entry->name )) {
                // Make sure it's really a level
                if ( strcmp ( dir[1].entry->name, "THINGS" ) == 0 ) {
                    if ( index == MAX_LEVELS ) {
                        fprintf ( stderr, "ERROR: Too many levels in WAD - ignoring %s!\n", dir->entry->name, errors++ );
                    } else {
                        memcpy ( names [index++], dir->entry->name, MAX_LUMP_NAME );
                    }
                }
            }
            dir++;
        }
    }
    memset ( names [index], 0, MAX_LUMP_NAME );

    if ( errors ) fprintf ( stderr, "\n" );
    return argIndex;
}

void EnsureExtension ( char *fileName, const char *ext )
{
    FUNCTION_ENTRY ( NULL, "EnsureExtension", true );

    int length = strlen ( fileName );
    if ( stricmp ( &fileName [length-4], ext ) != 0 ) {
        strcat ( fileName, ext );
    }
}

const char *TypeName ( eWadType type )
{
    FUNCTION_ENTRY ( NULL, "TypeName", true );

    const char *name = NULL;
    switch ( type ) {
        case wt_DOOM    : name = "DOOM";	break;
        case wt_DOOM2   : name = "DOOM2";	break;
        case wt_HERETIC : name = "Heretic";	break;
        case wt_HEXEN   : name = "Hexen";	break;
        default         : name = "<Unknown>";	break;
    }
    return name;
}

wadList *getInputFiles ( char *cmdLine, char *wadFileName )
{
    FUNCTION_ENTRY ( NULL, "getInputFiles", true );

    char *listNames = wadFileName;
    wadList *myList = new wadList;

    if ( cmdLine == NULL ) return myList;

    char temp [ 256 ];
    strcpy ( temp, cmdLine );
    char *ptr = strtok ( temp, "+" );

    int errors = 0;

    while ( ptr && *ptr ) {
        char wadName [ 256 ];
        strcpy ( wadName, ptr );
        EnsureExtension ( wadName, ".wad" );

        WAD *wad = new WAD ( wadName );
        if ( wad->Status () != ws_OK ) {
            const char *msg;
            switch ( wad->Status ()) {
                case ws_INVALID_FILE : msg = "The file %s does not exist\n";		break;
                case ws_CANT_READ    : msg = "Can't open the file %s for read access\n";	break;
                case ws_INVALID_WAD  : msg = "%s is not a valid WAD file\n";		break;
                default              : msg = "** Unexpected Error opening %s **\n";	break;
            }
            fprintf ( stderr, msg, wadName );
            delete wad;
        } else {
            if ( ! myList->IsEmpty ()) {
                cprintf ( "Merging: %s with %s\r\n", wadName, listNames );
                *wadFileName++ = '+';
            }
            if ( myList->Add ( wad ) == false ) {
                errors++;
                if ( myList->Type () != wt_UNKNOWN ) {
                    fprintf ( stderr, "ERROR: %s is not a %s PWAD.\n", wadName, TypeName ( myList->Type ()));
                } else {
                    fprintf ( stderr, "ERROR: %s is not the same type.\n", wadName );
                }
                delete wad;
            } else {
                char *end = wadName + strlen ( wadName ) - 1;
                while (( end > wadName ) && ( *end != SEPERATOR )) end--;
                if ( *end == SEPERATOR ) end++;
                wadFileName += sprintf ( wadFileName, "%s", end );
            }
        }
        ptr = strtok ( NULL, "+" );
    }
    if ( wadFileName [-1] == '+' ) wadFileName [-1] = '\0';
    if ( myList->wadCount () > 1 ) cprintf ( "\r\n" );
    if ( errors ) fprintf ( stderr, "\n" );

    return myList;
}

void ReadSection ( FILE *file, int max, bool *array )
{
    FUNCTION_ENTRY ( NULL, "ReadSection", true );

    char ch = ( char ) fgetc ( file );
    while (( ch != '[' ) && ! feof ( file )) {
        ungetc ( ch, file );
        char lineBuffer [ 256 ];
        fgets ( lineBuffer, sizeof ( lineBuffer ), file );
        strtok ( lineBuffer, "\n\x1A" );
        char *ptr = lineBuffer;
        while ( *ptr == ' ' ) ptr++;

        bool value = true;
        if ( *ptr == '!' ) {
            value = false;
            ptr++;
        }
        ptr = strtok ( ptr, "," );
        while ( ptr ) {
            int low = -1, high = 0, count = 0;
            if ( stricmp ( ptr, "all" ) == 0 ) {
                memset ( array, value, sizeof ( bool ) * max );
            } else {
                count = sscanf ( ptr, "%d-%d", &low, &high );
            }
            ptr = strtok ( NULL, "," );
            if (( low < 0 ) || ( low >= max )) continue;
            switch ( count ) {
                case 1 : array [low] = value;
                         break;
                case 2 : if ( high >= max ) high = max - 1;
                         for ( int i = low; i <= high; i++ ) {
                             array [i] = value;
                         }
                         break;
            }
            if ( count == 0 ) break;
        }
        ch = ( char ) fgetc ( file );
    }
    ungetc ( ch, file );
}

void ReadCustomFile ( DoomLevel *curLevel, wadList *myList, sBSPOptions *options )
{
    FUNCTION_ENTRY ( NULL, "ReadCustomFile", true );

    char fileName [ 256 ];
    const wadListDirEntry *dir = myList->FindWAD ( curLevel->Name (), NULL, NULL );
    strcpy ( fileName, dir->wad->Name ());
    char *ptr = &fileName [ strlen ( fileName )];
    while ( *--ptr != '.' );
    *++ptr = '\0';
    strcat ( ptr, "zen" );
    while (( ptr > fileName ) && ( *ptr != SEPERATOR )) ptr--;
    if (( ptr < fileName ) || ( *ptr == SEPERATOR )) ptr++;
    FILE *optionFile = fopen ( ptr, "rt" );
    if ( optionFile == NULL ) {
        optionFile = fopen ( fileName, "rt" );
        if ( optionFile == NULL ) return;
    }

    char ch = ( char ) fgetc ( optionFile );
    bool foundMap = false;
    do {
        while ( ! feof ( optionFile ) && ( ch != '[' )) ch = ( char ) fgetc ( optionFile );
        char lineBuffer [ 256 ];
        fgets ( lineBuffer, sizeof ( lineBuffer ), optionFile );
        strtok ( lineBuffer, "\n\x1A]" );
        if ( WAD::IsMap ( lineBuffer )) {
            if ( strcmp ( lineBuffer, curLevel->Name ()) == 0 ) {
                foundMap = true;
            } else if ( foundMap ) {
                break;
            }
        }
        if ( ! foundMap ) {
            ch = ( char ) fgetc ( optionFile );
            continue;
        }

        int maxIndex = 0, isSectorSplit = false;
        bool *array = NULL;
        if ( stricmp ( lineBuffer, "ignore-linedefs" ) == 0 ) {
            maxIndex = curLevel->LineDefCount ();
            if ( options->ignoreLineDef == NULL ) {
                options->ignoreLineDef = new bool [ maxIndex ];
                memset ( options->ignoreLineDef, false, sizeof ( bool ) * maxIndex );
            }
            array = options->ignoreLineDef;
        } else if ( stricmp ( lineBuffer, "dont-split-linedefs" ) == 0 ) {
            maxIndex = curLevel->LineDefCount ();
            if ( options->dontSplit == NULL ) {
                options->dontSplit = new bool [ maxIndex ];
                memset ( options->dontSplit, false, sizeof ( bool ) * maxIndex );
            }
            array = options->dontSplit;
        } else if ( stricmp ( lineBuffer, "dont-split-sectors" ) == 0 ) {
            isSectorSplit = true;
            maxIndex = curLevel->LineDefCount ();
            if ( options->dontSplit == NULL ) {
                options->dontSplit = new bool [ maxIndex ];
                memset ( options->dontSplit, false, sizeof ( bool ) * maxIndex );
            }
            maxIndex = curLevel->SectorCount ();
            array = new bool [ maxIndex ];
            memset ( array, false, sizeof ( bool ) * maxIndex );
        } else if ( stricmp ( lineBuffer, "unique-sectors" ) == 0 ) {
            maxIndex = curLevel->SectorCount ();
            if ( options->keepUnique == NULL ) {
                options->keepUnique = new bool [ maxIndex ];
                memset ( options->keepUnique, false, sizeof ( bool ) * maxIndex );
            }
            array = options->keepUnique;
        }
        if ( array ) {
            ReadSection ( optionFile, maxIndex, array );
            if ( isSectorSplit ) {
                const wLineDef *lineDef = curLevel->GetLineDefs ();
                const wSideDef *sideDef = curLevel->GetSideDefs ();
                for ( int side, i = 0; i < curLevel->LineDefCount (); i++, lineDef++ ) {
                    side = lineDef->sideDef [0];
                    if (( side != NO_SIDEDEF ) && ( array [ sideDef [ side ].sector ])) {
                        options->dontSplit [i] = true;
                    }
                    side = lineDef->sideDef [1];
                    if (( side != NO_SIDEDEF ) && ( array [ sideDef [ side ].sector ])) {
                        options->dontSplit [i] = true;
                    }
                }
                delete array;
            }
        }

        ch = ( char ) fgetc ( optionFile );

    } while ( ! feof ( optionFile ));

    fclose ( optionFile );
}

ULONG CheckREJECT ( DoomLevel *curLevel )
{
    FUNCTION_ENTRY ( NULL, "CheckREJECT", true );

    static bool initialized = false;
    if ( ! initialized ) {
        initialized = true;
        for ( int i = 0; i < 256; i++ ) {
            int val = i, count = 0;
            for ( int j = 0; j < 8; j++ ) {
                if ( val & 1 ) count++;
                val >>= 1;
            }
            HammingTable [i] = ( char ) count;
        }
    }

    int size = curLevel->RejectSize ();
    int noSectors = curLevel->SectorCount ();
    int mask = ( 0xFF00 >> ( size * 8 - noSectors * noSectors )) & 0xFF;
    int count = 0;
    if ( curLevel->GetReject () != 0 ) {
        UCHAR *ptr = ( UCHAR * ) curLevel->GetReject ();
        while ( size-- ) count += HammingTable [ *ptr++ ];
        count -= HammingTable [ ptr [-1] & mask ];
    }
    return ( ULONG ) ( 1000.0 * count / ( noSectors * noSectors ) + 0.5 );
}

void PrintTime ( ULONG time )
{
    FUNCTION_ENTRY ( NULL, "PrintTime", false );

    GotoXY ( 65, startY );
    cprintf ( "%3ld.%03ld sec%s", time / 1000, time % 1000, ( time == 1000 ) ? "" : "s" );
}

bool ProcessLevel ( char *name, wadList *myList, ULONG *ellapsed )
{
    FUNCTION_ENTRY ( NULL, "ProcessLevel", true );

    ULONG dummyX = 0;

    *ellapsed = 0;

    cprintf ( "\r  %-*.*s: ", MAX_LUMP_NAME, MAX_LUMP_NAME, name );
    GetXY ( &startX, &startY );

    const wadListDirEntry *dir = myList->FindWAD ( name );
    DoomLevel *curLevel = new DoomLevel ( name, dir->wad );
    if ( curLevel->isValid ( ! config.Nodes.Rebuild ) == false ) {
        cprintf ( "This level is not valid... " );
        cprintf ( "\r\n" );
        delete curLevel;
        return false;
    }

    int rows = 0;

    if ( config.BlockMap.Rebuild ) {

        rows++;

        int oldSize = curLevel->BlockMapSize ();
        ULONG blockTime = CurrentTime ();
        int saved = CreateBLOCKMAP ( curLevel, config.BlockMap );
        *ellapsed += blockTime = CurrentTime () - blockTime;
        int newSize = curLevel->BlockMapSize ();

        Status ( "" );
        GotoXY ( startX, startY );

        cprintf ( "BLOCKMAP - %5d/%-5d ", newSize, oldSize );
        if ( oldSize ) cprintf ( "(%3d%%)", ( int ) ( 100.0 * newSize / oldSize + 0.5 ));
        else cprintf ( "(****)" );
        cprintf ( "   Compressed: " );
        if ( newSize + saved ) cprintf ( "%3d%%", ( int ) ( 100.0 * newSize / ( newSize + saved ) + 0.5 ));
        else cprintf ( "(****)" );

        PrintTime ( blockTime );
        cprintf ( "\r\n" );
        GetXY ( &dummyX, &startY );
    }

    if ( config.Nodes.Rebuild ) {

        rows++;

        int oldNodeCount = curLevel->NodeCount ();
        int oldSegCount = curLevel->SegCount ();

        bool *keep = new bool [ curLevel->SectorCount ()];
        memset ( keep, config.Nodes.Unique, sizeof ( bool ) * curLevel->SectorCount ());

        sBSPOptions options;
        options.algorithm      = config.Nodes.Method;
        options.showProgress   = ! config.Nodes.Quiet;
        options.reduceLineDefs = config.Nodes.ReduceLineDefs;
        options.ignoreLineDef  = NULL;
        options.dontSplit      = NULL;
        options.keepUnique     = keep;

        ReadCustomFile ( curLevel, myList, &options );

        ULONG nodeTime = CurrentTime ();
        CreateNODES ( curLevel, &options );
        *ellapsed += nodeTime = CurrentTime () - nodeTime;

        if ( options.ignoreLineDef ) delete options.ignoreLineDef;
        if ( options.dontSplit ) delete options.dontSplit;
        if ( options.keepUnique ) delete options.keepUnique;

        Status ( "" );
        GotoXY ( startX, startY );

        cprintf ( "NODES - %4d/%-4d ", curLevel->NodeCount (), oldNodeCount );
        if ( oldNodeCount ) cprintf ( "(%3d%%)", ( int ) ( 100.0 * curLevel->NodeCount () / oldNodeCount + 0.5 ));
        else cprintf ( "(****)" );
        cprintf ( "  " );
        cprintf ( "SEGS - %5d/%-5d ", curLevel->SegCount (), oldSegCount );
        if ( oldSegCount ) cprintf ( "(%3d%%)", ( int ) ( 100.0 * curLevel->SegCount () / oldSegCount + 0.5 ));
        else cprintf ( "(****)" );

        PrintTime ( nodeTime );
        cprintf ( "\r\n" );
        GetXY ( &dummyX, &startY );
    }

    if ( config.Reject.Rebuild ) {

        rows++;

        ULONG oldEfficiency = CheckREJECT ( curLevel );

        ULONG rejectTime = CurrentTime (), efficiency;
        bool special = CreateREJECT ( curLevel, config.Reject, &efficiency );
        *ellapsed += rejectTime = CurrentTime () - rejectTime;

        if ( special == false ) {
            Status ( "" );
            GotoXY ( startX, startY );
            cprintf ( "REJECT - Efficiency: %3ld.%1ld%%/%2ld.%1ld%%  Sectors: %5d", efficiency / 10, efficiency % 10,
                                                                 oldEfficiency / 10, oldEfficiency % 10, curLevel->SectorCount ());
            PrintTime ( rejectTime );
        } else {
            cprintf ( "REJECT - Special effects detected - use -rf to force an update" );
        }

        cprintf ( "\r\n" );
        GetXY ( &dummyX, &startY );
    }

    bool changed = false;
    if ( rows != 0 ) {
        Status ( "Updating Level ... " );
        changed = curLevel->UpdateWAD ();
        Status ( "" );
        if ( changed ) {
            MoveUp ( rows );
            cprintf ( "\r *" );
            MoveDown ( rows );
        }
    } else {
        cprintf ( "Nothing to do here ... " );
        cprintf ( "\r\n" );
    }

    int noSectors = curLevel->SectorCount ();
    int rejectSize = (( noSectors * noSectors ) + 7 ) / 8;
    if (( curLevel->RejectSize () != rejectSize ) && ( config.Reject.Rebuild == false )) {
        fprintf ( stderr, "WARNING: The REJECT structure for %s is the wrong size - try using -r\n", name );
    }

    delete curLevel;

    return changed;
}

void PrintStats ( int totalLevels, ULONG totalTime, int totalUpdates )
{
    FUNCTION_ENTRY ( NULL, "PrintStats", true );

    if ( totalLevels != 0 ) {

        cprintf ( "%d Level%s processed in ", totalLevels, totalLevels > 1 ? "s" : "" );
        if ( totalTime > 60000 ) {
            ULONG minutes = totalTime / 60000;
            ULONG tempTime = totalTime - minutes * 60000;
            cprintf ( "%ld minute%s %ld.%03ld second%s - ", minutes, minutes > 1 ? "s" : "", tempTime / 1000, tempTime % 1000, ( tempTime == 1000 ) ? "" : "s" );
        } else {
            cprintf ( "%ld.%03ld second%s - ", totalTime / 1000, totalTime % 1000, ( totalTime == 1000 ) ? "" : "s"  );
        }

        if ( totalUpdates ) {
            cprintf ( "%d Level%s need%s updating.\r\n", totalUpdates, totalUpdates > 1 ? "s" : "", config.WriteWAD ? "ed" : "" );
        } else {
            cprintf ( "No Levels need%s updating.\r\n", config.WriteWAD ? "ed" : "" );
        }

        if ( totalTime == 0 ) {
            cprintf ( "WOW! Whole bunches of levels/sec!\r\n" );
        } else if ( totalTime < 1000 ) {
            cprintf ( "%f levels/sec\r\n", 1000.0 * totalLevels / totalTime );
        } else if ( totalLevels > 1 ) {
            cprintf ( "%f secs/level\r\n", totalTime / ( totalLevels * 1000.0 ));
        }
    }
}

int getOutputFile ( int index, char *argv [], char *wadFileName )
{
    FUNCTION_ENTRY ( NULL, "getOutputFile", true );

    strtok ( wadFileName, "+" );

    char *ptr = argv [ index ];
    if ( ptr && ( *ptr == '-' )) {
        char ch = ( char ) toupper ( *++ptr );
        if (( ch == 'O' ) || ( ch == 'X' )) {
            index++;
            if ( *++ptr ) {
                if ( *ptr++ != ':' ) {
                    fprintf ( stderr, "\nUnrecognized argument '%s'\n", argv [ index ] );
                    config.Extract = false;
                    return index + 1;
                }
            } else {
                ptr = argv [ index++ ];
            }
            if ( ptr ) strcpy ( wadFileName, ptr );
            if ( ch == 'X' ) config.Extract = true;
        }
    }

    EnsureExtension ( wadFileName, ".wad" );

    return index;
}

char *ConvertNumber ( ULONG value )
{
    FUNCTION_ENTRY ( NULL, "ConvertNumber", true );

    static char buffer [ 25 ];
    char *ptr = &buffer [ 20 ];

    while ( value ) {
        if ( value < 1000 ) sprintf ( ptr, "%4d", value );
        else sprintf ( ptr, ",%03d", value % 1000 );
        if ( ptr < &buffer [ 20 ] ) ptr [4] = ',';
        value /= 1000;
        if ( value ) ptr -= 4;
    }
    while ( *ptr == ' ' ) ptr++;
    return ptr;
}

int main ( int argc, char *argv [] )
{
    FUNCTION_ENTRY ( NULL, "main", true );
 
    SaveConsoleSettings ();

    cprintf ( "%s\r\n\r\n", BANNER );
    if ( ! isatty ( fileno ( stdout ))) fprintf ( stdout, "%s\n\n", BANNER );
    if ( ! isatty ( fileno ( stderr ))) fprintf ( stderr, "%s\n\n", BANNER );

    if ( argc == 1 ) {
        printHelp ();
        return -1;
    }

    config.BlockMap.Rebuild     = true;
    config.BlockMap.Compress    = true;

    config.Nodes.Rebuild        = true;
    config.Nodes.Method         = 1;
    config.Nodes.Quiet          = isatty ( fileno ( stdout )) ? false : true;
    config.Nodes.Unique         = true;
    config.Nodes.ReduceLineDefs = false;

    config.Reject.Rebuild       = true;
    config.Reject.Empty         = false;
    config.Reject.Force         = false;
    config.Reject.FindChildren  = true;
    config.Reject.UseGraphs     = true;

    config.WriteWAD             = true;

    ReadConfigFile ( argv );

    int argIndex = 1;
    int totalLevels = 0, totalTime = 0, totalUpdates = 0;

    while ( kbhit ()) getch ();

    do {

        config.Extract = false;
        argIndex = parseArgs ( argIndex, argv );
        if ( argIndex < 0 ) break;

        char wadFileName [ 256 ];
        wadList *myList = getInputFiles ( argv [argIndex++], wadFileName );
        if ( myList->IsEmpty ()) break;
        cprintf ( "Working on: %s\r\n\n", wadFileName );

        TRACE ( "Processing " << wadFileName );

        char levelNames [MAX_LEVELS+1][MAX_LUMP_NAME];
        argIndex = getLevels ( argIndex, argv, levelNames, myList );

        if ( levelNames [0][0] == '\0' ) {
            fprintf ( stderr, "Unable to find any valid levels in %s\n", wadFileName );
            break;
        }

        int noLevels = 0, updateCount = 0;

        do {

            ULONG ellapsedTime;
            if ( ProcessLevel ( levelNames [noLevels++], myList, &ellapsedTime )) updateCount++;
            totalTime += ellapsedTime;
            if ( kbhit () && ( getch () == 0x1B )) break;

        } while ( levelNames [noLevels][0] );

        config.Extract = false;
        argIndex = getOutputFile ( argIndex, argv, wadFileName );

        if ( updateCount || config.Extract ) {
            if ( config.WriteWAD ) {
                cprintf ( "\r\n%s to %s...", config.Extract ? "Extracting" : "Saving", wadFileName );
                if ( config.Extract ) {
                    if ( myList->Extract ( levelNames, wadFileName ) == false ) {
                        fprintf ( stderr," Error writing to file!\n" );
                    }
                } else {
                    if ( myList->Save ( wadFileName ) == false ) {
                        fprintf ( stderr," Error writing to file!\n" );
                    }
                }
                cprintf ( "\r\n" );
            } else {
                cprintf ( "\r\nChanges would have been written to %s ( %s bytes )\n", wadFileName, ConvertNumber ( myList->FileSize ()));
            }
        }
        cprintf ( "\r\n" );

        totalLevels += noLevels;
        totalUpdates += updateCount;

        delete myList;

    } while ( argv [argIndex] );

    PrintStats ( totalLevels, totalTime, totalUpdates );
    RestoreConsoleSettings ();

    return 0;
}
