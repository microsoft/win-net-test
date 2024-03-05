//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <windows.h>
#include <CppUnitTest.h>

#include "fntrace.h"
#include "testframeworkapi.h"
#include "tests.h"

#include "main.tmh"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

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
    Assert::IsTrue(TestSetup());
}

TEST_MODULE_CLEANUP(ModuleCleanup)
{
    Assert::IsTrue(TestCleanup());
}

TEST_CLASS(fnmpfunctionaltests)
{
public:
    TEST_METHOD(MpBasicRx) {
        ::MpBasicRx();
    }

    TEST_METHOD(MpBasicTx) {
        ::MpBasicTx();
    }

    TEST_METHOD(MpBasicRxOffload) {
        ::MpBasicRxOffload();
    }

    TEST_METHOD(MpBasicTxOffload) {
        ::MpBasicTxOffload();
    }
};

TEST_CLASS(fnlwffunctionaltests)
{
public:
    TEST_METHOD(LwfBasicRx) {
        ::LwfBasicRx();
    }

    TEST_METHOD(LwfBasicTx) {
        ::LwfBasicTx();
    }

    TEST_METHOD(LwfBasicOid) {
        ::LwfBasicOid();
    }
};
