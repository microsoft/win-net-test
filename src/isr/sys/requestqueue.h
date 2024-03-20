//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

//
// Client API.
//

//
// Notifies the client when a specific request has been completed.
//
typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
CLIENT_REQUEST_COMPLETE_FN(
    _In_ UINT64 Id,
    _In_ VOID *Context,
    _In_ INT Result
    );

//
// Registers a request queue client. The client adds requests and waits for
// their completion.
//
// Only a single client may be registered at a time.
//
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
RqClientRegister(
    _In_ CLIENT_REQUEST_COMPLETE_FN *ClientRequestComplete
    );

//
// Upon return, no client callbacks will be invoked.
//
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
RqClientDeregister(
    VOID
    );

//
// Push a request onto the queue to be processed.
// If successful, the result will be returned via the completion callback.
//
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
RqClientPushRequest(
    _In_ UINT64 Id,
    _In_ VOID *Context,
    _In_z_ CHAR *Command
    );

//
// Service API.
//

typedef struct _ISR_REQUEST {
    LIST_ENTRY Link;
    UINT64 Id;
    VOID *ClientContext;
    CHAR Command[ISR_MAX_COMMAND_LENGTH];
} ISR_REQUEST;

//
// Notifies the service when a request is available for processing.
//
typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
SERVICE_REQUEST_AVAILABLE_FN(
    VOID
    );

//
// Registers a request queue service. The service waits for requests to be
// added, processes the requests, and posts their results.
//
// Only a single service may be registered at a time.
//
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
RqServiceRegister(
    _In_ SERVICE_REQUEST_AVAILABLE_FN *ServerRequestAvailable
    );

//
// Upon return, no service callbacks will be invoked.
//
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
RqServiceDeregister(
    VOID
    );

//
// Pop a request from the queue.
// The service must post the request's result via RqServicePostRequestResult.
//
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
RqServicePopRequest(
    _Out_ ISR_REQUEST **Request
    );

//
// Post the result for the specified request.
//
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
RqServiceCompleteRequest(
    _In_ ISR_REQUEST *Request,
    _In_ INT Result
    );

//
// Module Init/Uninit.
//

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
RqInitialize(
    VOID
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
RqUninitialize(
    VOID
    );
