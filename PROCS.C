/**********************************************************************
 * MODULE NAME :  procs.c                AUTHOR:  Rick Fishman        *
 * DATE WRITTEN:  05-30-92                                            *
 *                                                                    *
 * DESCRIPTION:                                                       *
 *                                                                    *
 *  This program lists all running processes under OS/2 2.0. It uses  *
 *  the undocumented API DosQProcStatus to get a buffer filled with   *
 *  information related to the current state of the system. It then   *
 *  performs the following using the buffer:                          *
 *                                                                    *
 *  1. Get the relevant process information for all active pids into  *
 *     an array.                                                      *
 *  2. Go thru the module information table. For each module found,   *
 *     compare its module reference number against all module         *
 *     reference numbers associated with the active pids. If any      *
 *     match, add the module name to the active pid array.            *
 *  3. Print the process names that were found.                       *
 *                                                                    *
 * UPDATES:                                                           *
 *                                                                    *
 *   6/06/92 - Sort by process id within process name.                *
 *   6/06/92 - Add /f option for fully qualified process names.       *
 *   7/06/92 - Get DOS program title from tasklist and print it.      *
 *             Version 2.1 now.                                       *
 *   7/17/92 - Add /i option to sort by process ID, combine hex and   *
 *             decimal PID display. Up buffer size to 64k according   *
 *             to advice from IBM doc. Get tasklist title more        *
 *             efficiently. Version 2.2 now.                          *
 *   8/22/92 - Change all characters less than 0x10 to blanks in a    *
 *             DOS program's title. Version 2.21 now.                 *
 *                                                                    *
 **********************************************************************/


/*********************************************************************/
/*------- Include relevant sections of the OS/2 header files --------*/
/*********************************************************************/

#define INCL_DOSERRORS
#define INCL_DOSPROCESS
#define INCL_VIO
#define INCL_WINSWITCHLIST

/**********************************************************************/
/*----------------------------- INCLUDES -----------------------------*/
/**********************************************************************/

#include <os2.h>
#include <conio.h>
#include <ctype.h>
#include <process.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "procstat.h"

/*********************************************************************/
/*------------------- APPLICATION DEFINITIONS -----------------------*/
/*********************************************************************/

#define BUFFER_SIZE         0xFFFF

#define SCREEN_LINE_OVERHD  3
#define DEF_SCREEN_LINES    25

                                // COMMAND-LINE OPTIONS
#define SUPPRESSMORE        'S' // Suppress More [Y,N] messages
#define FULLNAMES           'F' // Fully qualify the process names
#define SORTBYPID           'I' // Sort process by process Id

#define DOS_PROGRAM_IDENT   "SYSINIT" // Identifies a DOS program

#define OUT_OF_MEMORY_MSG   "\nOut of memory!\n"

#define COPYRIGHT_INFO      "Procs.exe, 32-bit, Version 2.21\n"                \
                            "Copyright (c) Code Blazers, Inc. 1991-1992. "     \
                            "All rights reserved.\n"

#define USAGE_INFO          "\nusage: procs StartingPoint [ /f /i /s ]\n "     \
                            "\n    StartingPoint is a string that indicates "  \
                            "\n    a ProcessName or partial ProcessName after" \
                            "\n    which to start listing running processes"   \
                            "\n    (not applicable with /i)"                   \
                            "\n"                                               \
                            "\n    /f - Fully qualify the process names"       \
                            "\n    /i - Sort by process Id"                    \
                            "\n    /s - Suppress More [Y,N] displays"          \
                            "\n\n"

/**********************************************************************/
/*---------------------------- STRUCTURES ----------------------------*/
/**********************************************************************/

typedef struct _ACTIVEPID           // INFO ON AN ACTIVE PROCESS
{
    USHORT  hModRef;                // It's module reference handle
    PID     pid;                    // It's Process Id
    PSZ     szFullProcName;         // It's fully-qualified process name
    PSZ     szProcess;              // It's non-fully qualified name

} ACTIVEPID, *PACTIVEPID;

/**********************************************************************/
/*----------------------- FUNCTION PROTOTYPES ------------------------*/
/**********************************************************************/

INT   main               ( INT argc, PSZ szArg[] );
BOOL  Init               ( INT argc, PSZ szArg[] );
BOOL  BuildActivePidTbl  ( PPROCESSINFO ppi );
INT   CompareActivePids  ( const void *pActivePid1, const void *pActivePid2 );
VOID  Procs              ( PSZ szStartingPoint );
BOOL  StoreProcessName   ( PMODINFO pmi );
INT   CompareProcessNames( const void *pActivePid1, const void *pActivePid2 );
VOID  PrintReport        ( PSZ szStartingPoint );
VOID  PrintDosPgmName    ( PID pid );
VOID  Term               ( VOID );

/**********************************************************************/
/*------------------------ GLOBAL VARIABLES --------------------------*/
/**********************************************************************/

BOOL        fFullNames,             // Fully qualify process names or not
            fSortByPid,             // Sort by process Id
            fSuppressMore;          // Suppress More [Y,N] messages or not

INT         iStartingPoint;         // Index of argv array of print start point

USHORT      usActiveProcesses,      // Number of active processes
            usScreenLines,          // Number of lines in current screen mode
            usProcsToPrint,         // Number of processes that will be printed
            usTaskItems;            // Number of items in tasklist

ACTIVEPID   *aActivePid;            // Array of active processes

PBUFFHEADER pbh;                    // Pointer to buffer header structure

/**********************************************************************/
/*------------------------------ MAIN --------------------------------*/
/*                                                                    */
/*  MAIN DRIVER FOR PROGRAM.                                          */
/*                                                                    */
/*  INPUT: number of command-line arguments,                          */
/*         command-line argument array                                */
/*                                                                    */
/*  1. Perform program initialization which will issue the            */
/*     DosQProcStatus call and obtain the buffer of information.      */
/*  2. If a starting point was given on the commandline, pass that    */
/*     to the Procs function that will list running processes. If not,*/
/*     pass a NULL address to the Procs function.                     */
/*  3. Perform program termination.                                   */
/*                                                                    */
/*  OUTPUT: nothing                                                   */
/*--------------------------------------------------------------------*/
/**********************************************************************/
INT main( INT argc, PSZ szArg[] )
{
    if( Init( argc, szArg ) )
        if( iStartingPoint )
            Procs( szArg[ iStartingPoint ] );
        else
            Procs( NULL );

    Term();

    return 0;
}

/**********************************************************************/
/*------------------------------ Init --------------------------------*/
/*                                                                    */
/*  PERFORM PROGRAM INITIALIZATION.                                   */
/*                                                                    */
/*  INPUT: number of command-line arguments,                          */
/*         command-line argument array                                */
/*                                                                    */
/*  1. Print copyright notice.                                        */
/*  2. If too many commandline parms, exit with usage info.           */
/*  3. Process commandline options:                                   */
/*     A. If FULLNAMES option is found, set the appropriate flag.     */
/*     B. If SORTBYPID option is found, set the appropriate flag.     */
/*     C. If SUPPRESSMORE option is found, set the appropriate flag.  */
/*     D. If an invalid option, exit with usage info.                 */
/*     E. If a starting point was specified, store the index into     */
/*        the argv array for later use.                               */
/*  4. Alocate memory for the output from DosQProcStatus.             */
/*  5. Make the DosQProcStatus call.                                  */
/*  6. Build an array of information related to active processes.     */
/*  7. Get the number of screen lines supported by the window we are  */
/*     running under.                                                 */
/*                                                                    */
/*  OUTPUT: TRUE or FALSE if successful or not                        */
/*                                                                    */
/*--------------------------------------------------------------------*/
/**********************************************************************/
BOOL Init( INT argc, PSZ szArg[] )
{
    SHORT   sIndex;
    USHORT  usRetCode;
    BOOL    fSuccess = TRUE;

    printf( COPYRIGHT_INFO );

    for( sIndex = 1; fSuccess && sIndex < argc; sIndex++ )
    {
        if( szArg[ sIndex ][ 0 ] == '/' || szArg[ sIndex ][ 0 ] == '-' )
        {
            switch( toupper( szArg[ sIndex ][ 1 ] ) )
            {
                case FULLNAMES:
                    fFullNames = TRUE;

                    break;

                case SORTBYPID:
                    fSortByPid = TRUE;

                    break;

                case SUPPRESSMORE:
                    fSuppressMore = TRUE;

                    break;

                default:
                    (void) printf( USAGE_INFO );

                    fSuccess = FALSE;
            }
        }
        else if( !iStartingPoint )
            iStartingPoint = sIndex;
        else
        {
            (void) printf( USAGE_INFO );

            fSuccess = FALSE;
        }
    }

    if( fSuccess && !(pbh = malloc( BUFFER_SIZE )) )
    {
        printf( OUT_OF_MEMORY_MSG );

        fSuccess = FALSE;
    }

    if( fSuccess )
    {
        usRetCode = DosQProcStatus( pbh, BUFFER_SIZE );

        if( usRetCode )
        {
            printf( "\nDosQProcStatus failed. RC: %u.", usRetCode );

            fSuccess = FALSE;
        }
        else
            fSuccess = BuildActivePidTbl( pbh->ppi );
    }

    if( fSuccess )
    {
        VIOMODEINFO vmi;

        vmi.cb = sizeof( VIOMODEINFO );

        usRetCode = VioGetMode( &vmi, 0 );

        if( usRetCode )
            usScreenLines = DEF_SCREEN_LINES - SCREEN_LINE_OVERHD;
        else
            usScreenLines = vmi.row - SCREEN_LINE_OVERHD;
    }

    return fSuccess;
}

/**********************************************************************/
/*------------------------ BuildActivePidTbl -------------------------*/
/*                                                                    */
/*  BUILD AN ARRAY OF ACTIVE PROCESSES USING THE PROCESS INFO SECTION */
/*  OF THE DosQProcStatus BUFFER.                                     */
/*                                                                    */
/*  INPUT: pointer to ProcessInfo section of buffer                   */
/*                                                                    */
/*  1. Get a count of active processes.                               */
/*  2. Allocate memory for the ActiveProcess table.                   */
/*  3. Store information about each active process in the table.      */
/*  4. Sort the table in ascending module number order.               */
/*                                                                    */
/*  OUTPUT: exit code                                                 */
/*                                                                    */
/*--------------------------------------------------------------------*/
/**********************************************************************/
BOOL BuildActivePidTbl( PPROCESSINFO ppi )
{
    PPROCESSINFO ppiLocal = ppi;
    BOOL         fSuccess = TRUE;

    // Count the number of processes in the process info section. The process
    // count in the summary record is not reliable (2/17/92 - version 6.177)

    usActiveProcesses = 0;

    while( ppiLocal->ulEndIndicator != PROCESS_END_INDICATOR )
    {
        usActiveProcesses++;

        // Next PROCESSINFO struct found by taking the address of the first
        // thread control block of the current PROCESSINFO structure and
        // adding the size of a THREADINFO structure times the number of
        // threads

        ppiLocal = (PPROCESSINFO) (ppiLocal->ptiFirst+ppiLocal->usThreadCount );
    }

    if( !(aActivePid = malloc( usActiveProcesses * sizeof( ACTIVEPID ) )) )
    {
        printf( OUT_OF_MEMORY_MSG );

        fSuccess = FALSE;
    }
    else
    {
        INT i;

        memset( aActivePid, 0, usActiveProcesses * sizeof( ACTIVEPID ) );

        for( i = 0; i < usActiveProcesses; i++ )
        {
            aActivePid[ i ].hModRef = ppi->hModRef;

            aActivePid[ i ].pid = (PID) ppi->pid;

            ppi = (PPROCESSINFO) (ppi->ptiFirst + ppi->usThreadCount);
        }

        qsort( aActivePid, usActiveProcesses, sizeof( ACTIVEPID ),
               CompareActivePids );
    }

    return fSuccess;
}

/**********************************************************************/
/*------------------------ CompareActivePids -------------------------*/
/*                                                                    */
/*  COMPARE FUNCTION FOR THE QSORT OF THE ACTIVE PID ARRAY. SORTS     */
/*  THE ARRAY IN MODULE HANDLE ORDER.                                 */
/*                                                                    */
/*  INPUT: pointer to first element for compare,                      */
/*         pointer to second element of compare                       */
/*                                                                    */
/*  1. Do the compare.                                                */
/*                                                                    */
/*  OUTPUT: < 0 means first < second                                  */
/*          = 0 means first = second                                  */
/*          > 0 means first > second                                  */
/*                                                                    */
/*--------------------------------------------------------------------*/
/**********************************************************************/
INT CompareActivePids( const void *pActivePid1, const void *pActivePid2 )
{
    if( ((PACTIVEPID)pActivePid1)->hModRef < ((PACTIVEPID)pActivePid2)->hModRef )
        return -1;
    else
    if( ((PACTIVEPID)pActivePid1)->hModRef > ((PACTIVEPID)pActivePid2)->hModRef )
        return +1;
    else
        return 0;
}

/**********************************************************************/
/*------------------------------ Procs -------------------------------*/
/*                                                                    */
/*  LIST ALL RUNNING PROCESSES.                                       */
/*                                                                    */
/*  INPUT: starting point of report                                   */
/*                                                                    */
/*  1. Print the titles for the report.                               */
/*  2. Store the process names in the ActivePid array.                */
/*  3. Sort the ActivePid array by process name.                      */
/*  4. Print the report.                                              */
/*                                                                    */
/*  OUTPUT: nothing                                                   */
/*                                                                    */
/*--------------------------------------------------------------------*/
/**********************************************************************/
VOID Procs( PSZ szStartingPoint )
{
    PMODINFO pmi = pbh->pmi;
    BOOL     fSuccess = TRUE;

    while( pmi )
    {
        if( !StoreProcessName( pmi ) )
        {
            fSuccess = FALSE;

            break;
        }

        pmi = pmi->pNext;
    }

    if( fSuccess )
    {
        qsort( aActivePid, usActiveProcesses, sizeof( ACTIVEPID ),
               CompareProcessNames );

        PrintReport( szStartingPoint );
    }
}

/**********************************************************************/
/*------------------------- StoreProcessName -------------------------*/
/*                                                                    */
/*  STORE THE PROCESS NAME FOR LATER PRINTING.                        */
/*                                                                    */
/*  INPUT: pointer to MODINFO structure                               */
/*                                                                    */
/*  1. Go thru each entry in the ActivePid array:                     */
/*     A. If the module reference handle in the array is greater      */
/*        than the one passed here, we could not find a match         */
/*        because at this point the ActivePid array is sorted in      */
/*        module reference handle order.                              */
/*     B. If we find a match:                                         */
/*        1. Set a pointer to the beginning of the process name.      */
/*        2. If the user doesn't want fully qualified process names,  */
/*           set the pointer to the name minus the directory info.    */
/*        3. Allocate memory for the process name in the ActivePid    */
/*           array element.                                           */
/*        4. Copy the process name into the allocated memory.         */
/*                                                                    */
/*  OUTPUT: TRUE or FALSE if successful or not                        */
/*--------------------------------------------------------------------*/
/**********************************************************************/
BOOL StoreProcessName( PMODINFO pmi )
{
    INT  i;
    PSZ  szProcess;
    BOOL fSuccess = TRUE;

    for( i = 0; (fSuccess && i < usActiveProcesses); i++ )
    {
        if( aActivePid[ i ].hModRef > pmi->hMod )
            break;

        if( aActivePid[ i ].hModRef == pmi->hMod )
        {
            szProcess = pmi->szModName;

            aActivePid[ i ].szFullProcName = malloc( strlen( szProcess ) + 1 );

            if( aActivePid[ i ].szFullProcName )
            {
                strcpy( aActivePid[ i ].szFullProcName, szProcess );

                szProcess = strrchr( aActivePid[ i ].szFullProcName, '\\' );

                if( szProcess )
                    szProcess++;
                else
                    szProcess = aActivePid[ i ].szFullProcName;

                aActivePid[ i ].szProcess = szProcess;

                usProcsToPrint++;
            }
            else
            {
                (void) printf( OUT_OF_MEMORY_MSG );

                fSuccess = FALSE;
            }
        }
    }

    return fSuccess;
}

/**********************************************************************/
/*----------------------- CompareProcessNames ------------------------*/
/*                                                                    */
/*  COMPARE FUNCTION FOR THE QSORT OF THE ACTIVE PID ARRAY. SORTS     */
/*  THE ARRAY IN PROCESS NAME ORDER.                                  */
/*                                                                    */
/*  INPUT: pointer to first element for compare,                      */
/*         pointer to second element of compare                       */
/*                                                                    */
/*  1. Do the compare. If the process names are equal, sort within    */
/*     process name by pid. If we are to sort by pid, don't worry     */
/*     about process name.                                            */
/*                                                                    */
/*  OUTPUT: return from stricmp                                       */
/*                                                                    */
/*--------------------------------------------------------------------*/
/**********************************************************************/
INT CompareProcessNames( const void *pActivePid1, const void *pActivePid2 )
{
    PACTIVEPID pActPid1 = (PACTIVEPID) pActivePid1;
    PACTIVEPID pActPid2 = (PACTIVEPID) pActivePid2;
    INT        iResult;

    if( !fSortByPid )
        iResult = stricmp( pActPid1->szProcess, pActPid2->szProcess );

    if( fSortByPid || !iResult )
        if( pActPid1->pid < pActPid2->pid )
            iResult = -1;
        else
        if( pActPid1->pid > pActPid2->pid )
            iResult = +1;
        else
            iResult = 0;

    return iResult;
}

/**********************************************************************/
/*--------------------------- PrintReport ----------------------------*/
/*                                                                    */
/*  PRINT INFO ABOUT EACH PROCESS.                                    */
/*                                                                    */
/*  INPUT: starting point to begin report                             */
/*                                                                    */
/*  1. For each element in the ActivePid array:                       */
/*     A. If we are passed the starting point specified on            */
/*        the commandline, get the next element (unless we are sorting*/
/*        by PID in which case starting point does not apply).        */
/*     B. If we have exceeeded the screen lines for the window        */
/*        that we are running under and the user has not specified    */
/*        to suppress the More [Y,N] messages, display that message   */
/*        and wait on a key. If the user wants more displayed,        */
/*        continue, else exit.                                        */
/*     C. Print information about the process.                        */
/*                                                                    */
/*  OUTPUT: nothing                                                   */
/*--------------------------------------------------------------------*/
/**********************************************************************/
VOID PrintReport( PSZ szStartingPoint )
{
    INT     i, KbdChar;
    USHORT  usLines = 0;
    CHAR    szProcessNameDesc[ 64 ];
    PSZ     szProcessName;

    strcpy( szProcessNameDesc, "Process Name " );

    if( !fFullNames )
        strcat( szProcessNameDesc, "(use /f for fully qualified names)" );

    printf( "\n%-12.12s %-63.63s",
            "PID(hex/dec)", szProcessNameDesc );

    printf( "\n%-12.12s %-63.63s",
            "컴컴컴컴컴컴",
            "컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴�" );

    for( i = 0; i < usActiveProcesses; i++ )
    {
        if( !fSortByPid &&
            szStartingPoint &&
            stricmp( szStartingPoint, aActivePid[ i ].szProcess ) > 0 )
            continue;

        if( !fSuppressMore && ++usLines > usScreenLines )
        {
            printf( "\nMore [Y,N]?" );

            fflush( stdout );

            KbdChar = getch();

            printf( "\r           \r" );

            fflush( stdout );

            if( toupper( KbdChar ) == 'N' )
                return;

            usLines = 0;
        }
        else
            printf( "\n" );

        if( fFullNames )
            szProcessName = aActivePid[ i ].szFullProcName;
        else
            szProcessName = aActivePid[ i ].szProcess;

        printf( "%3x     %3u  %s", aActivePid[ i ].pid, aActivePid[ i ].pid,
                szProcessName );

        if( !stricmp( szProcessName, DOS_PROGRAM_IDENT ) )
            PrintDosPgmName( aActivePid[ i ].pid );
    }
}

/*~********************************************************************/
/*------------------------- PrintDosPgmName --------------------------*/
/*                                                                    */
/*  FIND THE DOS PROGRAM NAME IN THE TASKLIST.                        */
/*                                                                    */
/*  INPUT: process id of dos program                                  */
/*                                                                    */
/*  1. Get the switch list entry for the DOS program using its PID.   */
/*  2. Get rid of any carraige return/line feeds (or any other char   */
/*     less than 0x10).                                               */
/*  3. Print the title.                                               */
/*                                                                    */
/*  OUTPUT: nothing                                                   */
/*                                                                    */
/*--------------------------------------------------------------------*/
/********************************************************************~*/
VOID PrintDosPgmName( PID pid )
{
    HSWITCH hs;
    SWCNTRL swctl;
    PCH     pch;

    hs = WinQuerySwitchHandle( 0, pid );

    if( hs )
    {
        WinQuerySwitchEntry( hs, &swctl );

        pch = swctl.szSwtitle;

        while( *pch )
        {
            if( *pch < 0x10 )
                if( pch != swctl.szSwtitle && *(pch - 1) == 0x20 )
                    memmove( pch, pch + 1, strlen( pch ) );
                else
                {
                    *pch = 0x20;

                    pch++;
                }
            else
                pch++;
        }

        printf( "( %s )", swctl.szSwtitle );
    }
}

/**********************************************************************/
/*------------------------------ Term --------------------------------*/
/*                                                                    */
/*  PERFORM PROGRAM TERMINATION                                       */
/*                                                                    */
/*  INPUT: nothing                                                    */
/*                                                                    */
/*  1. Free the ActiveProcess array. First free the memory for the    */
/*     process name associated with each element of the array.        */
/*  2. Free the buffer allocated for the DosQProcStatus output.       */
/*  3. Return to the operating system.                                */
/*                                                                    */
/*  OUTPUT: nothing                                                   */
/*                                                                    */
/*--------------------------------------------------------------------*/
/**********************************************************************/
VOID Term()
{
    if( aActivePid )
    {
        INT i;

        for( i = 0; i < usActiveProcesses; i++ )
            if( aActivePid[ i ].szFullProcName )
                free( aActivePid[ i ].szFullProcName );

        free( aActivePid );
    }

    if( pbh )
        free( pbh );

    DosExit( EXIT_PROCESS, 0 );
}

/**********************************************************************
 *                       END OF SOURCE CODE                           *
 **********************************************************************/
