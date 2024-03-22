//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <windows.h>
#include <strsafe.h>
#include <stdlib.h>

#include <fnioctl_um.h>
#include "invokesystemrelayioctl.h"

#include "trace.h"

#include "main.tmh"

#define SVCNAME "isrsvc"

static SERVICE_STATUS SvcStatus;
static SERVICE_STATUS_HANDLE SvcStatusHandle;
static BOOLEAN Stopping = FALSE;
static FNIOCTL_HANDLE IsrHandle = NULL;

//
// Called to update SCM of the service status.
//
static
VOID
ReportSvcStatus(
    _In_ DWORD CurrentState,
    _In_ DWORD Win32ExitCode,
    _In_ DWORD WaitHint
    )
{
    TraceInfo(TRACE_CONTROL, "CurrentState=%d", CurrentState);

    SvcStatus.dwCurrentState = CurrentState;
    SvcStatus.dwWin32ExitCode = Win32ExitCode;
    SvcStatus.dwWaitHint = WaitHint;

    if (CurrentState == SERVICE_START_PENDING ||
        CurrentState == SERVICE_STOP_PENDING) {
        SvcStatus.dwControlsAccepted = 0;
    } else {
        SvcStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    }

    //
    // Report the status of the service to the SCM.
    //
    SetServiceStatus(SvcStatusHandle, &SvcStatus);
}

//
// Called by SCM when service should handle a service control request.
//
static
VOID
WINAPI
SvcCtrlHandler(
    _In_ DWORD ControlCode
    )
{
    TraceInfo(TRACE_CONTROL, "ControlCode=%d", ControlCode);

    switch(ControlCode) {
    case SERVICE_CONTROL_STOP:
        ReportSvcStatus(SERVICE_STOP_PENDING, NO_ERROR, 0);

        //
        // Mark the service as stopping.
        //
        Stopping = TRUE;

        //
        // Cancel any pending IO (most likely the get IOCTL is pending).
        //
        if (!CancelIoEx(IsrHandle, NULL)) {
            TraceError(TRACE_CONTROL, "CancelIoEx failed with %d", GetLastError());
        }

        break;

    default:
        break;
   }
}

static
VOID
DoServiceWork(
    VOID
    )
{
    HRESULT Result;
    INT CommandResult;
    ISR_GET_OUTPUT Get;
    ISR_POST_INPUT Post;

    while (!Stopping) {
        //
        // Get a request from the driver.
        //
        TraceInfo(TRACE_CONTROL, "Getting request...");
        Result =
            FnIoctl(
                IsrHandle, ISR_IOCTL_INVOKE_SYSTEM_GET, NULL, 0,
                &Get, sizeof(Get), NULL, NULL);
        if (FAILED(Result)) {
            TraceError(TRACE_CONTROL, "ISR_IOCTL_INVOKE_SYSTEM_GET failed with %d", Result);
            //
            // Mitigation to avoid burning CPU in a tight loop.
            //
            Sleep(1000);
            continue;
        }

        //
        // Process the request.
        //
        TraceInfo(TRACE_CONTROL, "Running request Id=%llu, Command=%s", Get.Id, Get.Command);
        CommandResult = system(Get.Command);

        RtlZeroMemory(&Post, sizeof(Post));
        Post.Id = Get.Id;
        Post.Result = CommandResult;

        //
        // Post the status to the driver.
        //
        TraceInfo(TRACE_CONTROL, "Posting request Id=%llu, Result=%d", Post.Id, Post.Result);
        Result =
            FnIoctl(
                IsrHandle, ISR_IOCTL_INVOKE_SYSTEM_POST, &Post, sizeof(Post),
                NULL, 0, NULL, NULL);
        if (FAILED(Result)) {
            TraceError(TRACE_CONTROL, "ISR_IOCTL_INVOKE_SYSTEM_POST failed with %d", Result);
            continue;
        }
    }
}

//
// Called by SCM via StartServiceCtrlDispatcher.
//
static
VOID
WINAPI
SvcMain(
    _In_ DWORD Argc,
    _In_ LPCSTR *Argv
    )
{
    DWORD ExitCode;
    HRESULT Result;
    ISR_OPEN_SERVICE *OpenService;
    CHAR EaBuffer[ISR_OPEN_EA_LENGTH + sizeof(*OpenService)];

    UNREFERENCED_PARAMETER(Argc);
    UNREFERENCED_PARAMETER(Argv);

    TraceInfo(TRACE_CONTROL, "start");

    //
    // Register service control handler.
    //
    SvcStatusHandle = RegisterServiceCtrlHandler(SVCNAME, SvcCtrlHandler);
    if (SvcStatusHandle == NULL) {
        ExitCode = GetLastError();
        TraceError(TRACE_CONTROL, "RegisterServiceCtrlHandler failed with %d", ExitCode);
        goto Exit;
    }

    SvcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    SvcStatus.dwServiceSpecificExitCode = 0;

    //
    // Initialization begin.
    //

    ReportSvcStatus(SERVICE_START_PENDING, NO_ERROR, 3000);

    OpenService =
        (ISR_OPEN_SERVICE *)
            IsrInitializeEa(ISR_FILE_TYPE_SERVICE, EaBuffer, sizeof(EaBuffer));

    Result =
        FnIoctlOpen(
            ISR_DEVICE_NAME,
            FILE_CREATE,
            EaBuffer,
            sizeof(EaBuffer),
            &IsrHandle);
    if (FAILED(Result)) {
        ExitCode = ERROR_GEN_FAILURE;
        TraceError(TRACE_CONTROL, "FnIoctlOpen failed with %d", Result);
        goto Exit;
    }

    //
    // Initialization complete.
    //

    ReportSvcStatus(SERVICE_RUNNING, NO_ERROR, 0);

    DoServiceWork();
    ExitCode = NO_ERROR;

Exit:

    if (IsrHandle != NULL) {
        FnIoctlClose(IsrHandle);
    }

    ReportSvcStatus(SERVICE_STOPPED, ExitCode, 0);

    return;
}

//
// Called by SCM when service is started.
//
INT
__cdecl
main(
    VOID
    )
{
    SERVICE_TABLE_ENTRY DispatchTable[] = {
        { SVCNAME, (LPSERVICE_MAIN_FUNCTION) SvcMain },
        { NULL, NULL }
    };

    WPP_INIT_TRACING(NULL);

    TraceInfo(TRACE_CONTROL, "start");

    //
    // This call returns when the service has stopped.
    // The process should simply terminate when the call returns.
    //
    if (!StartServiceCtrlDispatcher(DispatchTable)) {
        TraceError(TRACE_CONTROL, "StartServiceCtrlDispatcher failed with %d", GetLastError());
    }

    WPP_CLEANUP();
}
