//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#ifndef FNMP_RTL_H
#define FNMP_RTL_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef RTL_PTR_ADD
#define RTL_PTR_ADD(Pointer, Value) \
    ((VOID *)((ULONG_PTR)(Pointer) + (ULONG_PTR)(Value)))
#endif

#ifndef RTL_PTR_SUBTRACT
#define RTL_PTR_SUBTRACT(Pointer, Value) \
    ((PVOID)((ULONG_PTR)(Pointer) - (ULONG_PTR)(Value)))
#endif

#ifndef RTL_IS_POWER_OF_TWO
#define RTL_IS_POWER_OF_TWO(Value) \
    ((Value != 0) && !((Value) & ((Value) - 1)))
#endif

#ifndef RTL_IS_CLEAR_OR_SINGLE_FLAG
#define RTL_IS_CLEAR_OR_SINGLE_FLAG(Flags, Mask) \
    (((Flags) & (Mask)) == 0 || !(((Flags) & (Mask)) & (((Flags) & (Mask)) - 1)))
#endif

#ifndef RTL_NUM_ALIGN_DOWN
#define RTL_NUM_ALIGN_DOWN(Number, Alignment) \
    ((Number) - ((Number) & ((Alignment) - 1)))
#endif

#ifndef RTL_NUM_ALIGN_UP
#define RTL_NUM_ALIGN_UP(Number, Alignment) \
    RTL_NUM_ALIGN_DOWN((Number) + (Alignment) - 1, (Alignment))
#endif

#ifndef RTL_PTR_SUBTRACT
#define RTL_PTR_SUBTRACT(Pointer, Value) \
    ((PVOID)((ULONG_PTR)(Pointer) - (ULONG_PTR)(Value)))
#endif

#ifndef ALIGN_DOWN_BY
#define ALIGN_DOWN_BY(Length, Alignment) \
    ((ULONG_PTR)(Length)& ~(Alignment - 1))
#endif

#ifndef ALIGN_UP_BY
#define ALIGN_UP_BY(Length, Alignment) \
    (ALIGN_DOWN_BY(((ULONG_PTR)(Length) + Alignment - 1), Alignment))
#endif

#define RTL_MILLISEC_TO_100NANOSEC(m) ((m) * 10000ui64)
#define RTL_SEC_TO_100NANOSEC(s) ((s) * 10000000ui64)
#define RTL_SEC_TO_MILLISEC(s) ((s) * 1000ui64)

#ifndef ReadUInt64NoFence
#define ReadUInt64NoFence ReadULong64NoFence
#endif

#ifndef htons
#define htons _byteswap_ushort
#endif

#ifndef ntohs
#define ntohs _byteswap_ushort
#endif

#ifndef htonl
#define htonl _byteswap_ulong
#endif

#ifndef ntohl
#define ntohl _byteswap_ulong
#endif

#if (!defined(NTDDI_WIN10_CO) || (WDK_NTDDI_VERSION < NTDDI_WIN10_CO)) && \
    !defined(UINT32_VOLATILE_ACCESSORS)
#define UINT32_VOLATILE_ACCESSORS

FORCEINLINE
UINT32
ReadUInt32Acquire(
    _In_ _Interlocked_operand_ UINT32 const volatile *Source
    )
{
    return (UINT32)ReadULongAcquire((PULONG)Source);
}

FORCEINLINE
UINT32
ReadUInt32NoFence(
    _In_ _Interlocked_operand_ UINT32 const volatile *Source
    )
{
    return (UINT32)ReadULongNoFence((PULONG)Source);
}

FORCEINLINE
VOID
WriteUInt32Release(
    _Out_ _Interlocked_operand_ UINT32 volatile *Destination,
    _In_ UINT32 Value
    )
{
    WriteULongRelease((PULONG)Destination, (ULONG)Value);
}

FORCEINLINE
VOID
WriteUInt32NoFence(
    _Out_ _Interlocked_operand_ UINT32 volatile *Destination,
    _In_ UINT32 Value
    )
{
    WriteULongNoFence((PULONG)Destination, (ULONG)Value);
}

#endif

#ifdef _KERNEL_MODE

FORCEINLINE
_IRQL_requires_max_(APC_LEVEL)
_Acquires_exclusive_lock_(Lock)
VOID
RtlAcquirePushLockExclusive(
    _Inout_ EX_PUSH_LOCK *Lock
    )
{
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(Lock);
}

FORCEINLINE
_IRQL_requires_max_(APC_LEVEL)
_Releases_exclusive_lock_(Lock)
VOID
RtlReleasePushLockExclusive(
    _Inout_ EX_PUSH_LOCK *Lock
    )
{
    ExReleasePushLockExclusive(Lock);
    KeLeaveCriticalRegion();
}

FORCEINLINE
_IRQL_requires_max_(APC_LEVEL)
_Acquires_shared_lock_(Lock)
VOID
RtlAcquirePushLockShared(
    _Inout_ EX_PUSH_LOCK *Lock
    )
{
    KeEnterCriticalRegion();
    ExAcquirePushLockShared(Lock);
}

FORCEINLINE
_IRQL_requires_max_(APC_LEVEL)
_Releases_shared_lock_(Lock)
VOID
RtlReleasePushLockShared(
    _Inout_ EX_PUSH_LOCK *Lock
    )
{
    ExReleasePushLockShared(Lock);
    KeLeaveCriticalRegion();
}

FORCEINLINE
VOID
RtlCopyVolatileMemory(
    _Out_writes_bytes_(Size) VOID *Destination,
    _In_reads_bytes_(Size) volatile const VOID *Source,
    _In_ SIZE_T Size
    )
{
    RtlCopyMemory(Destination, (const VOID *)Source, Size);
    _ReadWriteBarrier();
}

FORCEINLINE
HANDLE
ReadHandleNoFence(
    _In_reads_bytes_(sizeof(HANDLE)) volatile CONST HANDLE *Address
    )
{
    return (HANDLE)ReadPointerNoFence((PVOID *)Address);
}

#endif

#ifdef __cplusplus
} // extern "C"
#endif

#endif
