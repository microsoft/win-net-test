//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <ntddk.h>
#include <wsk.h>

#include "cxplat.h"
#include "trace.h"

#include "cxplat.tmh"

UINT64 CxPlatPerfFreq;

PAGEDX
_IRQL_requires_max_(PASSIVE_LEVEL)
CXPLAT_STATUS
CxPlatInitialize(
    VOID
    )
{
    PAGED_CODE();

    TraceInfo("CxPlatInitialize");

    (VOID)KeQueryPerformanceCounter((LARGE_INTEGER*)&CxPlatPerfFreq);

    return CXPLAT_STATUS_SUCCESS;
}

PAGEDX
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
CxPlatUninitialize(
    VOID
    )
{
    PAGED_CODE();
}

//
// Time Measurement Interfaces
//

UINT64
CxPlatTimePlat(
    void
    )
{
    return (UINT64)KeQueryPerformanceCounter(NULL).QuadPart;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
CxPlatSleep(
    _In_ UINT32 DurationMs
    )
{
    NT_ASSERT(DurationMs != (UINT32)-1);

    KTIMER SleepTimer;
    LARGE_INTEGER TimerValue;

    KeInitializeTimerEx(&SleepTimer, SynchronizationTimer);
    TimerValue.QuadPart = (UINT64)DurationMs * -10000;
    KeSetTimer(&SleepTimer, TimerValue, NULL);

    KeWaitForSingleObject(&SleepTimer, Executive, KernelMode, FALSE, NULL);
}

//
// Allocation/Memory Interfaces
//

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID*
CxPlatAllocNonPaged(
    _In_ SIZE_T Size,
    _In_ ULONG Tag
    )
{
    return ExAllocatePoolZero(NonPagedPoolNx, Size, Tag);
}

VOID
CxPlatFree(
    _In_ VOID* Mem,
    _In_ ULONG Tag
    )
{
    ExFreePoolWithTag(Mem, Tag);
}

VOID
CxPlatFreeNoTag(
    _In_opt_ VOID* Mem
    )
{
    if (Mem != NULL) {
        ExFreePool(Mem);
    }
}

//
// Create Thread Interfaces
//

CXPLAT_STATUS
CxPlatThreadCreate(
    _In_ CXPLAT_THREAD_CONFIG* Config,
    _Out_ CXPLAT_THREAD* Thread
    )
{
    CXPLAT_STATUS Status;
    _ETHREAD *EThread = NULL;
    HANDLE ThreadHandle;
    Status =
        PsCreateSystemThread(
            &ThreadHandle,
            THREAD_ALL_ACCESS,
            NULL,
            NULL,
            NULL,
            Config->Callback,
            Config->Context);
    NT_ASSERT(CXPLAT_SUCCEEDED(Status));
    if (CXPLAT_FAILED(Status)) {
        goto Error;
    }
    Status =
        ObReferenceObjectByHandle(
            ThreadHandle,
            THREAD_ALL_ACCESS,
            *PsThreadType,
            KernelMode,
            (void**)&EThread,
            NULL);
    NT_ASSERT(CXPLAT_SUCCEEDED(Status));
    if (CXPLAT_FAILED(Status)) {
        goto Cleanup;
    }
    PROCESSOR_NUMBER Processor, IdealProcessor;
    Status =
        KeGetProcessorNumberFromIndex(
            Config->IdealProcessor,
            &Processor);
    if (CXPLAT_FAILED(Status)) {
        Status = CXPLAT_STATUS_SUCCESS; // Currently we don't treat this as fatal
        goto SetPriority;             // TODO: Improve this logic.
    }
    IdealProcessor = Processor;
    if (Config->Flags & CXPLAT_THREAD_FLAG_SET_AFFINITIZE) {
        GROUP_AFFINITY Affinity;
        RtlZeroMemory(&Affinity, sizeof(Affinity));
        Affinity.Group = Processor.Group;
        Affinity.Mask = (1ull << Processor.Number);
        Status =
            ZwSetInformationThread(
                ThreadHandle,
                ThreadGroupInformation,
                &Affinity,
                sizeof(Affinity));
        NT_ASSERT(CXPLAT_SUCCEEDED(Status));
        if (CXPLAT_FAILED(Status)) {
            goto Cleanup;
        }
    } else { // NUMA Node Affinity
        SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX Info;
        ULONG InfoLength = sizeof(Info);
        Status =
            KeQueryLogicalProcessorRelationship(
                &Processor,
                RelationNumaNode,
                &Info,
                &InfoLength);
        NT_ASSERT(CXPLAT_SUCCEEDED(Status));
        if (CXPLAT_FAILED(Status)) {
            goto Cleanup;
        }
        Status =
            ZwSetInformationThread(
                ThreadHandle,
                ThreadGroupInformation,
                &Info.NumaNode.GroupMask,
                sizeof(GROUP_AFFINITY));
        NT_ASSERT(CXPLAT_SUCCEEDED(Status));
        if (CXPLAT_FAILED(Status)) {
            goto Cleanup;
        }
    }
    if (Config->Flags & CXPLAT_THREAD_FLAG_SET_IDEAL_PROC) {
        Status =
            ZwSetInformationThread(
                ThreadHandle,
                ThreadIdealProcessorEx,
                &IdealProcessor, // Don't pass in Processor because this overwrites on output.
                sizeof(IdealProcessor));
        NT_ASSERT(CXPLAT_SUCCEEDED(Status));
        if (CXPLAT_FAILED(Status)) {
            goto Cleanup;
        }
    }
SetPriority:
    if (Config->Flags & CXPLAT_THREAD_FLAG_HIGH_PRIORITY) {
        KeSetBasePriorityThread(
            (PKTHREAD)EThread,
            IO_NETWORK_INCREMENT + 1);
    }
    if (Config->Name) {
        DECLARE_UNICODE_STRING_SIZE(UnicodeName, 64);
        ULONG UnicodeNameLength = 0;
        Status =
            RtlUTF8ToUnicodeN(
                UnicodeName.Buffer,
                UnicodeName.MaximumLength,
                &UnicodeNameLength,
                Config->Name,
                (ULONG)strnlen(Config->Name, 64));
        NT_ASSERT(CXPLAT_SUCCEEDED(Status));
        UnicodeName.Length = (USHORT)UnicodeNameLength;
#define ThreadNameInformation ((THREADINFOCLASS)38)
        Status =
            ZwSetInformationThread(
                ThreadHandle,
                ThreadNameInformation,
                &UnicodeName,
                sizeof(UNICODE_STRING));
        NT_ASSERT(CXPLAT_SUCCEEDED(Status));
        Status = CXPLAT_STATUS_SUCCESS;
    }
    *Thread = (CXPLAT_THREAD)EThread;
Cleanup:
    ZwClose(ThreadHandle);
Error:
    return Status;
}

VOID
CxPlatThreadDelete(
    _In_ CXPLAT_THREAD Thread
    )
{
    _ETHREAD *EThread = (_ETHREAD *)Thread;

    ObDereferenceObject(EThread);
}

BOOLEAN
CxPlatThreadWait(
    _In_ CXPLAT_THREAD Thread,
    _In_ UINT32 TimeoutMs
    )
{
    _ETHREAD *EThread = (_ETHREAD *)Thread;

    LARGE_INTEGER Timeout100Ns;
    NT_ASSERT(TimeoutMs != MAXUINT32);
    Timeout100Ns.QuadPart = -1 * ((UINT64)TimeoutMs * 10000);
    return KeWaitForSingleObject(EThread, Executive, KernelMode, FALSE, &Timeout100Ns) != STATUS_TIMEOUT;
}
