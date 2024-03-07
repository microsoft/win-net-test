//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <windows.h>
#include <CppUnitTest.h>

#include "fntrace.h"
#include "testframeworkapi.h"
#include "tests.h"
#include "fntest.h"
#include "fnfunctionaltestdrvioctl.h"

#include "main.tmh"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

class DriverService {
    SC_HANDLE ScmHandle;
    SC_HANDLE ServiceHandle;
public:
    DriverService() :
        ScmHandle(nullptr),
        ServiceHandle(nullptr) {
    }
    bool Initialize(
        _In_z_ const char* DriverName,
        _In_opt_z_ const char* DependentFileNames,
        _In_opt_z_ const char* DriverPath
        ) {
        uint32_t Error;
        ScmHandle = OpenSCManager(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
        if (ScmHandle == nullptr) {
            Error = GetLastError();
            TraceError(
                "[ lib] ERROR, %u, %s.",
                Error,
                "GetFullPathName failed");
            return false;
        }
    QueryService:
        ServiceHandle =
            OpenServiceA(
                ScmHandle,
                DriverName,
                SERVICE_ALL_ACCESS);
        if (ServiceHandle == nullptr) {
            TraceError(
                "[ lib] ERROR, %u, %s.",
                 GetLastError(),
                "OpenService failed");
            char DriverFilePath[MAX_PATH] = {0};
            char* PathEnd;
            if (DriverPath != nullptr) {
                strcpy_s(DriverFilePath, RTL_NUMBER_OF(DriverFilePath), DriverPath);
            } else {
                if (GetModuleFileNameA(NULL, DriverFilePath, MAX_PATH) == 0) {
                    TraceError(
                        "[ lib] ERROR, %s.",
                        "Failed to get currently executing module path");
                    return false;
                }
            }
            PathEnd = strrchr(DriverFilePath, '\\');
            if (PathEnd == NULL) {
                TraceError(
                    "[ lib] ERROR, %s.",
                    "Failed parsing unexpected module path format");
                return false;
            }
            PathEnd++;
            size_t RemainingLength = sizeof(DriverFilePath) - (PathEnd - DriverFilePath);
            int PathResult =
                snprintf(
                    PathEnd,
                    RemainingLength,
                    "%s.sys",
                    DriverName);
            if (PathResult <= 0 || (size_t)PathResult > RemainingLength) {
                TraceError(
                    "[ lib] ERROR, %s.",
                    "Failed to create driver on disk file path");
                return false;
            }
            if (GetFileAttributesA(DriverFilePath) == INVALID_FILE_ATTRIBUTES) {
                TraceError(
                    "[ lib] ERROR, %s.",
                    "Failed to find driver on disk");
                return false;
            }
            ServiceHandle =
                CreateServiceA(
                    ScmHandle,
                    DriverName,
                    DriverName,
                    SC_MANAGER_ALL_ACCESS,
                    SERVICE_KERNEL_DRIVER,
                    SERVICE_DEMAND_START,
                    SERVICE_ERROR_NORMAL,
                    DriverFilePath,
                    nullptr,
                    nullptr,
                    DependentFileNames,
                    nullptr,
                    nullptr);
            if (ServiceHandle == nullptr) {
                Error = GetLastError();
                if (Error == ERROR_SERVICE_EXISTS) {
                    goto QueryService;
                }
                TraceError(
                    "[ lib] ERROR, %u, %s.",
                    Error,
                    "CreateService failed");
                return false;
            }
        }
        return true;
    }
    void Uninitialize() {
        if (ServiceHandle != nullptr) {
            CloseServiceHandle(ServiceHandle);
        }
        if (ScmHandle != nullptr) {
            CloseServiceHandle(ScmHandle);
        }
    }
    bool Start() {
        if (!StartServiceA(ServiceHandle, 0, nullptr)) {
            uint32_t Error = GetLastError();
            if (Error != ERROR_SERVICE_ALREADY_RUNNING) {
                TraceError(
                    "[ lib] ERROR, %u, %s.",
                    Error,
                    "StartService failed");
                return false;
            }
        }
        return true;
    }
};

class DriverClient {
    HANDLE DeviceHandle;
public:
    DriverClient() : DeviceHandle(INVALID_HANDLE_VALUE) { }
    ~DriverClient() { Uninitialize(); }
    bool Initialize(
        _In_z_ const char* DriverName
        ) {
        uint32_t Error;
        char IoctlPath[MAX_PATH];
        int PathResult =
            snprintf(
                IoctlPath,
                sizeof(IoctlPath),
                "\\\\.\\\\%s",
                DriverName);
        if (PathResult < 0 || PathResult >= sizeof(IoctlPath)) {
            TraceError(
                "[ lib] ERROR, %s.",
                "Creating Driver File Path failed");
            return false;
        }
        DeviceHandle =
            CreateFileA(
                IoctlPath,
                GENERIC_READ | GENERIC_WRITE,
                0,
                nullptr,                // no SECURITY_ATTRIBUTES structure
                OPEN_EXISTING,          // No special create flags
                FILE_FLAG_OVERLAPPED,   // Allow asynchronous requests
                nullptr);
        if (DeviceHandle == INVALID_HANDLE_VALUE) {
            Error = GetLastError();
            TraceError(
                "[ lib] ERROR, %u, %s.",
                Error,
                "CreateFile failed");
            return false;
        }
        return true;
    }
    void Uninitialize() {
        if (DeviceHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(DeviceHandle);
            DeviceHandle = INVALID_HANDLE_VALUE;
        }
    }
    bool Run(
        _In_ uint32_t IoControlCode,
        _In_reads_bytes_opt_(InBufferSize)
            void* InBuffer,
        _In_ uint32_t InBufferSize,
        _In_ uint32_t TimeoutMs = 30000
        ) {
        uint32_t Error;
        OVERLAPPED Overlapped = { 0 };
        Overlapped.hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (Overlapped.hEvent == nullptr) {
            Error = GetLastError();
            TraceError(
                "[ lib] ERROR, %u, %s.",
                Error,
                "CreateEvent failed");
            return false;
        }
        TraceVerbose(
            "[test] Sending Write IOCTL %u with %u bytes.",
            IoControlCode,
            InBufferSize);
        if (!DeviceIoControl(
                DeviceHandle,
                IoControlCode,
                InBuffer, InBufferSize,
                nullptr, 0,
                nullptr,
                &Overlapped)) {
            Error = GetLastError();
            if (Error != ERROR_IO_PENDING) {
                CloseHandle(Overlapped.hEvent);
                TraceError(
                    "[ lib] ERROR, %u, %s.",
                    Error,
                    "DeviceIoControl Write failed");
                return false;
            }
        }
        DWORD dwBytesReturned;
        if (!GetOverlappedResultEx(
                DeviceHandle,
                &Overlapped,
                &dwBytesReturned,
                TimeoutMs,
                FALSE)) {
            Error = GetLastError();
            if (Error == WAIT_TIMEOUT) {
                Error = ERROR_TIMEOUT;
                CancelIoEx(DeviceHandle, &Overlapped);
            }
            TraceError(
                "[ lib] ERROR, %u, %s.",
                Error,
                "GetOverlappedResultEx Write failed");
        } else {
            Error = ERROR_SUCCESS;
        }
        CloseHandle(Overlapped.hEvent);
        return Error == ERROR_SUCCESS;
    }
    bool Run(
        _In_ uint32_t IoControlCode,
        _In_ uint32_t TimeoutMs = 30000
        ) {
        return Run(IoControlCode, nullptr, 0, TimeoutMs);
    }
    template<class T>
    bool Run(
        _In_ uint32_t IoControlCode,
        _In_ const T& Data,
        _In_ uint32_t TimeoutMs = 30000
        ) {
        return Run(IoControlCode, (void*)&Data, sizeof(Data), TimeoutMs);
    }
    _Success_(return)
    bool Read(
        _In_ uint32_t IoControlCode,
        _Out_writes_bytes_opt_(OutBufferSize)
            void* OutBuffer,
        _In_ uint32_t OutBufferSize,
        _Out_opt_ uint32_t* OutBufferWritten,
        _In_ uint32_t TimeoutMs = 30000
        ) {
        uint32_t Error;
        OVERLAPPED Overlapped = { 0 };
        Overlapped.hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!Overlapped.hEvent) {
            Error = GetLastError();
            TraceError(
                "[ lib] ERROR, %u, %s.",
                Error,
                "CreateEvent failed");
            return false;
        }
        TraceVerbose(
            "[test] Sending Read IOCTL %u.",
            IoControlCode);
        if (!DeviceIoControl(
                DeviceHandle,
                IoControlCode,
                nullptr, 0,
                OutBuffer, OutBufferSize,
                nullptr,
                &Overlapped)) {
            Error = GetLastError();
            if (Error != ERROR_IO_PENDING) {
                CloseHandle(Overlapped.hEvent);
                TraceError(
                    "[ lib] ERROR, %u, %s.",
                    Error,
                    "DeviceIoControl Write failed");
                return false;
            }
        }
        DWORD dwBytesReturned;
        if (!GetOverlappedResultEx(
                DeviceHandle,
                &Overlapped,
                &dwBytesReturned,
                TimeoutMs,
                FALSE)) {
            Error = GetLastError();
            if (Error == WAIT_TIMEOUT) {
                Error = ERROR_TIMEOUT;
                if (CancelIoEx(DeviceHandle, &Overlapped)) {
                    GetOverlappedResult(DeviceHandle, &Overlapped, &dwBytesReturned, true);
                }
            } else {
                TraceError(
                    "[ lib] ERROR, %u, %s.",
                    Error,
                    "GetOverlappedResultEx Read failed");
            }
        } else {
            Error = ERROR_SUCCESS;
            if (OutBufferWritten != NULL) {
                *OutBufferWritten = dwBytesReturned;
            }
        }
        CloseHandle(Overlapped.hEvent);
        return Error == ERROR_SUCCESS;
    }
};

static bool TestingKernelMode = false;
static DriverService TestDriverService;
static DriverClient TestDriverClient;

//
// Test framework specific.
//

VOID
StopTest()
{
    Assert::Fail(L"Stop test execution.");
}

VOID
LogTestFailure(
    _In_z_ PCWSTR File,
    _In_z_ PCWSTR Function,
    INT Line,
    _Printf_format_string_ PCWSTR Format,
    ...
    )
{
    static const INT Size = 128;
    WCHAR Buffer[Size];

    UNREFERENCED_PARAMETER(File);
    UNREFERENCED_PARAMETER(Function);
    UNREFERENCED_PARAMETER(Line);

    va_list Args;
    va_start(Args, Format);
    _vsnwprintf_s(Buffer, Size, _TRUNCATE, Format, Args);
    va_end(Args);

    TraceError("%S", Buffer);
    Logger::WriteMessage(Buffer);
}

VOID
LogTestWarning(
    _In_z_ PCWSTR File,
    _In_z_ PCWSTR Function,
    INT Line,
    _Printf_format_string_ PCWSTR Format,
    ...
    )
{
    static const INT Size = 128;
    WCHAR Buffer[Size];

    UNREFERENCED_PARAMETER(File);
    UNREFERENCED_PARAMETER(Function);
    UNREFERENCED_PARAMETER(Line);

    va_list Args;
    va_start(Args, Format);
    _vsnwprintf_s(Buffer, Size, _TRUNCATE, Format, Args);
    va_end(Args);

    TraceWarn("%S", Buffer);
    Logger::WriteMessage(Buffer);
}

TEST_MODULE_INITIALIZE(ModuleSetup)
{
    size_t RequiredSize;

    getenv_s(&RequiredSize, NULL, 0, "fnfunctionaltests::KernelModeEnabled");
    TestingKernelMode = (RequiredSize != 0);

    TraceInfo(
        "[ lib] INFO, KernelMode=%d",
        TestingKernelMode);
    if (TestingKernelMode) {
        char DriverPath[MAX_PATH];

        TEST_EQUAL(
            0,
            getenv_s(
                &RequiredSize, DriverPath, RTL_NUMBER_OF(DriverPath),
                "fnfunctionaltests::KernelModeDriverPath"));
        TEST_TRUE(TestDriverService.Initialize(FUNCTIONAL_TEST_DRIVER_NAME, nullptr, DriverPath));
        TEST_TRUE(TestDriverService.Start());

        TEST_TRUE(TestDriverClient.Initialize(FUNCTIONAL_TEST_DRIVER_NAME));
    }
    Assert::IsTrue(TestSetup());
}

TEST_MODULE_CLEANUP(ModuleCleanup)
{
    Assert::IsTrue(TestCleanup());

    if (TestingKernelMode) {
        TestDriverClient.Uninitialize();
        TestDriverService.Uninitialize();
    }
}

TEST_CLASS(fnmpfunctionaltests)
{
public:
    TEST_METHOD(MpBasicRx) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_MP_BASIC_RX));
        } else {
            ::MpBasicRx();
        }
    }

    TEST_METHOD(MpBasicTx) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_MP_BASIC_TX));
        } else {
            ::MpBasicTx();
        }
    }

    TEST_METHOD(MpBasicRxOffload) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_MP_BASIC_RX_OFFLOAD));
        } else {
            ::MpBasicRxOffload();
        }
    }

    TEST_METHOD(MpBasicTxOffload) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_MP_BASIC_TX_OFFLOAD));
        } else {
            ::MpBasicTxOffload();
        }
    }
};

TEST_CLASS(fnlwffunctionaltests)
{
public:
    TEST_METHOD(LwfBasicRx) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_LWF_BASIC_RX));
        } else {
            ::LwfBasicRx();
        }
    }

    TEST_METHOD(LwfBasicTx) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_LWF_BASIC_TX));
        } else {
            ::LwfBasicTx();
        }
    }

    TEST_METHOD(LwfBasicOid) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_LWF_BASIC_OID));
        } else {
            ::LwfBasicOid();
        }
    }
};
