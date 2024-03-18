//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

#define ISR_DEVICE_NAME L"\\Device\\invokesystemrelaydrv"

#define ISR_OPEN_PACKET_NAME "IsrOpenPacket0"

//
// Type of object to create or open.
//
typedef enum _ISR_FILE_TYPE {
    ISR_FILE_TYPE_CLIENT,
    ISR_FILE_TYPE_SERVICE,
} ISR_FILE_TYPE;

//
// Open packet, the common header for NtCreateFile extended attributes.
//
typedef struct _ISR_OPEN_PACKET {
    ISR_FILE_TYPE ObjectType;
} ISR_OPEN_PACKET;

typedef struct _ISR_OPEN_CLIENT {
    UINT32 Reserved;
} ISR_OPEN_CLIENT;

typedef struct _ISR_OPEN_SERVICE {
    UINT32 Reserved;
} ISR_OPEN_SERVICE;

//
// Issued by kernel mode clients to enqueue an invoke system request.
// The request is processed asynchronously and the result is returned upon completion.
// Asynchronous.
//
// Input: char *Command
// Output: int Result
//
// TODO: add timeout so that these requests cannot pend forever?
//
#define ISR_IOCTL_INVOKE_SYSTEM_SUBMIT \
    CTL_CODE(FILE_DEVICE_NETWORK, 1, METHOD_BUFFERED, FILE_WRITE_ACCESS)

//
// Issued by invokesystemrelay user mode service to fetch an invoke system request.
// This will pend indefinitely until a request is ready to be processed.
// Asynchronous.
//
// Input: None
// Output: char *Command
//
#define ISR_IOCTL_INVOKE_SYSTEM_GET \
    CTL_CODE(FILE_DEVICE_NETWORK, 2, METHOD_BUFFERED, FILE_WRITE_ACCESS)

//
// Issued by invokesystemrelay user mode service to return the result of an invoke system request.
// Synchronous.
//
// Input: int Result
// Output: None
//
#define ISR_IOCTL_INVOKE_SYSTEM_POST \
    CTL_CODE(FILE_DEVICE_NETWORK, 3, METHOD_BUFFERED, FILE_WRITE_ACCESS)

EXTERN_C_END
