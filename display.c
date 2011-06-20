#include    <tchar.h>
#include    <windows.h>
#include    <wingdi.h>
#include    <winuser.h>
#include    <Wtsapi32.h>
#include    <userenv.h>
#include    <TlHelp32.h>
#include    <strsafe.h>

/* you have to change port number for your environment */
#define     COMPORT     _T("\\\\.\\COM4")

static VOID DebugMsg( LPCTSTR lpszFormat, ... );
static VOID WINAPI ServiceMain(DWORD , LPTSTR* );

static SERVICE_STATUS_HANDLE m_hStatus = NULL;
static HANDLE m_hStop = NULL;
#define SERVICE_NAME (TEXT("RotateDisplay"))
SERVICE_TABLE_ENTRY ServiceTable[] = {
          { SERVICE_NAME, ServiceMain },
          { NULL, NULL }
};

static VOID DebugMsg( LPCTSTR lpszFormat, ... )
{
    va_list arg;
    TCHAR buf[ 4096 ];
    LPTSTR lpszEnd;

    va_start( arg, lpszFormat );
    if( StringCchVPrintfEx( 
        buf, _countof( buf ) - 3, 
        &lpszEnd, NULL, STRSAFE_IGNORE_NULLS, lpszFormat, arg ) == S_OK )
    {
        *lpszEnd++ = '\r';
        *lpszEnd++ = '\n';
        *lpszEnd = '\0';

        OutputDebugString( buf );
    }else{
        OutputDebugString( lpszFormat );
    }
    va_end( arg );
}

static BOOL Rotate( int iDevNum,  SHORT dmOrientation )
{
    DISPLAY_DEVICE d;
    DEVMODE dm;
    int w;
    LONG r;

    ZeroMemory( &d, sizeof( d ) );
    d.cb = sizeof( d );

    if( !EnumDisplayDevices( NULL, iDevNum, &d, 0 ) ) {
        DebugMsg( _T("EnumDisplayDevices failed:%d\r\n" ), GetLastError() );
        return FALSE;
    }

    if( !EnumDisplaySettings( d.DeviceName, ENUM_CURRENT_SETTINGS, &dm ) ){
        DebugMsg( _T("EnumDisplaySettings(\"%s\") failed:%d\r\n" ), d.DeviceName, GetLastError() );
        return FALSE;
    }

	if( dm.dmDisplayOrientation == dmOrientation ) return TRUE;
    w = dm.dmPelsHeight;
    dm.dmPelsHeight = dm.dmPelsWidth;
    dm.dmPelsWidth = w;

    dm.dmDisplayOrientation = dmOrientation;

    r = ChangeDisplaySettingsEx( d.DeviceName, &dm, NULL, CDS_UPDATEREGISTRY, NULL );
    if( r != DISP_CHANGE_SUCCESSFUL ){
        DebugMsg( _T("ChangeDisplaySettingsEx failed:%d\r\n"), r );
        return FALSE;
    }
    return TRUE;
}

DWORD WINAPI HandlerEx ( DWORD dwControl, DWORD dwEventType, LPVOID lpEventData, LPVOID lpContext ) 
{

    SERVICE_STATUS ss;
    BOOL r;
    ss.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    ss.dwWin32ExitCode = NO_ERROR;
    ss.dwServiceSpecificExitCode = 0;
    ss.dwCheckPoint = 0;
    ss.dwWaitHint = 3000;
    ss.dwControlsAccepted = SERVICE_ACCEPT_STOP;

    UNREFERENCED_PARAMETER( dwControl );
    UNREFERENCED_PARAMETER( dwEventType );
    UNREFERENCED_PARAMETER( lpEventData );
    UNREFERENCED_PARAMETER( lpContext );

    switch( dwControl ){
    case SERVICE_CONTROL_STOP:
        ss.dwCurrentState = SERVICE_STOP_PENDING;
        r = SetServiceStatus( m_hStatus, &ss );
        if ( !r ) {
            DebugMsg( _T("SetServiceStatus:%d"), GetLastError() );
            break;
        }
        SetEvent( m_hStop );

        break;

    case SERVICE_CONTROL_SESSIONCHANGE:
    case SERVICE_CONTROL_HARDWAREPROFILECHANGE:
        break;
    default:
        return ERROR_CALL_NOT_IMPLEMENTED;

    }

    return NO_ERROR;
}

BOOL ExecMyself( LPCTSTR lpCommandLine )
{
    DWORD dwSession, r;
    HANDLE hToken;
    LPVOID lpEnv;
    STARTUPINFO startupInfo;
    PROCESS_INFORMATION processInformation;
    TCHAR szApplicationName[ 1024 ];
    TCHAR szCommandLine[ 1024 ];

    r = GetModuleFileName( NULL, szApplicationName, _countof( szApplicationName ) );
    if( r == 0 || r >= _countof( szApplicationName ) ){
        DebugMsg( _T("GetModuleFileName:%d"), GetLastError() );
        return FALSE;
    }

    if( StringCchPrintf( szCommandLine, _countof( szCommandLine ), _T("\"%s\" %s"), szApplicationName, lpCommandLine ) != S_OK ) return FALSE;
    dwSession = WTSGetActiveConsoleSessionId();
    if( dwSession == 0xFFFFFFFF ){
        DebugMsg( _T("WTSGetActiveConsoleSessionId:%d"), GetLastError() );
        return FALSE;
    }
    if( !WTSQueryUserToken( dwSession, &hToken ) ){
        DebugMsg( _T("WTSQueryUserToken:%d"), GetLastError() );
        return FALSE;
    }
    if( !CreateEnvironmentBlock( &lpEnv, hToken, FALSE ) ){
        DebugMsg( _T("CreateEnvironmentBlock:%d"), GetLastError() );
        return FALSE;
    }

    ZeroMemory( &startupInfo, sizeof(STARTUPINFO) );
    startupInfo.cb = sizeof(STARTUPINFO);
    startupInfo.lpDesktop = _T( "winsta0\\default" );
    if( !CreateProcessAsUser( hToken, szApplicationName, szCommandLine, NULL, NULL, 
        FALSE, CREATE_UNICODE_ENVIRONMENT, lpEnv, NULL, &startupInfo, &processInformation) )
    {
        DebugMsg( _T("CreateProcessAsUser:%d"), GetLastError() );
        return FALSE;
    }

    CloseHandle(processInformation.hThread);
    CloseHandle(processInformation.hProcess);

    return TRUE;
}

static VOID WINAPI ServiceMain(DWORD dwArgc, LPTSTR* pszArgv)
{
    DWORD r;
    SERVICE_STATUS ss;
    HANDLE hComm = INVALID_HANDLE_VALUE;
    DWORD dwCommState = 0;
    HANDLE hHandles[ 2 ];
    OVERLAPPED ov;

    UNREFERENCED_PARAMETER( dwArgc );
    UNREFERENCED_PARAMETER( pszArgv );
    ov.hEvent = NULL;

    __try{
        ss.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
        ss.dwWin32ExitCode = NO_ERROR;
        ss.dwServiceSpecificExitCode = 0;
        ss.dwCheckPoint = 1;
        ss.dwWaitHint = 1000;
        ss.dwControlsAccepted = SERVICE_ACCEPT_STOP |
            SERVICE_ACCEPT_SESSIONCHANGE ;
        m_hStatus = RegisterServiceCtrlHandlerEx(
            SERVICE_NAME, HandlerEx, NULL );
        if( m_hStatus == 0 ){
            DebugMsg( _T("RegisterServiceCtrlHandlerEx:%d" ), GetLastError() );
            __leave;
        }

        ss.dwCurrentState = SERVICE_START_PENDING;
        if( !SetServiceStatus( m_hStatus, &ss ) ){
            DebugMsg( _T("SetServiceStatus:%d" ), GetLastError() );
            __leave;
        }

        m_hStop = CreateEvent( NULL, TRUE, FALSE, NULL );
        if( m_hStop == NULL ){
            DebugMsg( _T("CreateEvent:%d"), GetLastError() );
            __leave;
        }

        ZeroMemory( &ov, sizeof( ov ) );
        ov.hEvent = CreateEvent( NULL, TRUE, TRUE, NULL );
        if( ov.hEvent == NULL ){
            DebugMsg( _T("CreateEvent:%d"), GetLastError() );
            __leave;
        }

        hComm = CreateFile( COMPORT, GENERIC_READ | GENERIC_WRITE,
            0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL );
        if( hComm == INVALID_HANDLE_VALUE ){
            DebugMsg( _T("CreateFile(\"%s\") failed:%d"), COMPORT, GetLastError() );
            __leave;
        }

        hHandles[ 0 ] = m_hStop;
        hHandles[ 1 ] = ov.hEvent;

        ss.dwCurrentState = SERVICE_RUNNING;
        ss.dwCheckPoint = 0;
        ss.dwControlsAccepted = SERVICE_ACCEPT_STOP;
        if( !SetServiceStatus( m_hStatus, &ss ) ){
            DebugMsg( _T("SetServiceStatus:%d"), GetLastError() );
            __leave;
        }

        EscapeCommFunction( hComm, SETDTR );
        for(;;){
            WaitCommEvent( hComm, &dwCommState, &ov );
            r = WaitForMultipleObjects( 2, hHandles, FALSE, 30000 );
            if( r == WAIT_OBJECT_0 ){
                __leave;
            }else if( r == WAIT_OBJECT_0 + 1 ){
                if( GetCommModemStatus( hComm, &dwCommState ) ){
                    if( dwCommState & MS_DSR_ON ){
                        ExecMyself( _T("0") );
                    }else{
                        ExecMyself( _T("90") );
                    }
                }
                Sleep( 1000 );
            }
            EscapeCommFunction( hComm, SETDTR );
            SetCommMask( hComm, EV_DSR );
            ResetEvent( ov.hEvent );
        }
    }
    __finally{
        if( m_hStop != NULL ) CloseHandle( m_hStop );
        if( hComm != INVALID_HANDLE_VALUE ) CloseHandle( hComm );
        if( ov.hEvent != NULL ) CloseHandle( ov.hEvent );
        ss.dwCurrentState = SERVICE_STOPPED;
        ss.dwCheckPoint = 0;
        ss.dwWaitHint = 0;
        SetServiceStatus( m_hStatus, &ss );
    }
}

typedef struct MYPROCESSENTRY32_tag{
    PROCESSENTRY32 pe;
    struct MYPROCESSENTRY32_tag *next;
} MYPROCESSENTRY32, *LPMYPROCESS32;

// Retrieve tha name of parent process. return length of the name in chars.
static DWORD GetParentProcessName( LPTSTR lpszBuf, DWORD cchBufSize )
{
    HANDLE hSnap;
    LPMYPROCESS32 ppe, ppe0;
    DWORD dwPid, dwPPid;
    size_t w;
    DWORD r = 0;

    ppe = (LPMYPROCESS32)LocalAlloc( LMEM_FIXED, sizeof( MYPROCESSENTRY32 ) );
    ppe0 = ppe;
    dwPid = GetCurrentProcessId(), dwPPid = 0;

    hSnap = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
    if( hSnap == INVALID_HANDLE_VALUE ){
        DebugMsg( _T("CreateToolhelp32Snapshot:%d"), GetLastError() );
        return 0;
    }
    ZeroMemory( ppe, sizeof( MYPROCESSENTRY32 ) );
    ppe->next = NULL;
    ppe->pe.dwSize = sizeof( PROCESSENTRY32 );
    if( Process32First( hSnap, &(ppe->pe) ) ){
        for(;;){
            LPMYPROCESS32 ppe2 = (LPMYPROCESS32)LocalAlloc( LMEM_FIXED, sizeof( MYPROCESSENTRY32 ) );
            ZeroMemory( ppe2, sizeof( MYPROCESSENTRY32 ) );
            ppe2->pe.dwSize = sizeof( PROCESSENTRY32 );
            ppe2->next = NULL;
            if( !Process32Next( hSnap, &(ppe->pe) ) ){
                LocalFree( ppe2 );
                break;
            }
            ppe->next = ppe2;
            if( ppe->pe.th32ProcessID == dwPid ){
                dwPPid = ppe->pe.th32ParentProcessID;
            }
            ppe = ppe2;
        }
    }
    ppe = ppe0;

    while( ppe != NULL ){
        if( ppe->pe.th32ProcessID == dwPPid ){
            StringCchCopy( lpszBuf, cchBufSize, ppe->pe.szExeFile );
            if( StringCchLength( ppe->pe.szExeFile, MAX_PATH + 2, &w ) == NO_ERROR ){
                r = w;
            }
            break;
        }
        ppe = ppe->next;
    }
    ppe = ppe0;
    while( ppe != NULL ){
        ppe0 = ppe->next;
        LocalFree( ppe );
        ppe = ppe0;;
    }
    CloseHandle( hSnap );
    return r;
}


int WINAPI WinMain(
  HINSTANCE hInstance,
  HINSTANCE hPrevInstance,
  LPSTR lpCmdLine,
  int nCmdShow
)
{
    TCHAR buf[ MAX_PATH ];
    SHORT dmOrientation = DMDO_DEFAULT;

    UNREFERENCED_PARAMETER( hInstance );
    UNREFERENCED_PARAMETER( hPrevInstance );
    UNREFERENCED_PARAMETER( lpCmdLine );
    UNREFERENCED_PARAMETER( nCmdShow );

    if( !GetParentProcessName( buf, _countof( buf ) ) ){
        return FALSE;
    }

    if( lstrcmpi( _T("services.exe"), buf ) == 0 ){
        // run as a service
        if( !StartServiceCtrlDispatcher( ServiceTable )){
            DebugMsg( _T("StartServiceCtrlDispatcher:%d"), GetLastError() );
            return -1;
        }
    }else{
        // run as a rotate command
        if( lstrcmpA( lpCmdLine, "90" ) == 0 ){
            dmOrientation = DMDO_90;
        }else{
            dmOrientation = DMDO_DEFAULT;
        }
        if( !GetSystemMetrics( SM_REMOTESESSION ) ){
            Rotate( 1, dmOrientation );
        }
    }
    if( !StartServiceCtrlDispatcher( ServiceTable )){
        DebugMsg( _T("StartServiceCtrlDispatcher:%d"), GetLastError() );
    }

    return 0;
}

