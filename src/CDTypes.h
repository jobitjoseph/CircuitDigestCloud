// Copyright (c) 2026 Jobit Joseph, Circuit Digest
// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

#ifndef CD_TOPIC_BUFFER_SIZE
#define CD_TOPIC_BUFFER_SIZE 160
#endif
#ifndef CD_PAYLOAD_BUFFER_SIZE
#define CD_PAYLOAD_BUFFER_SIZE 192  // holds a single JSON value token (doubles capacity for string values)
#endif
#ifndef CD_SUBMIT_BUFFER_SIZE
#define CD_SUBMIT_BUFFER_SIZE 256   // holds the full Anedya submitdata payload
#endif
#ifndef CD_INBOUND_STRING_BUFFER
#define CD_INBOUND_STRING_BUFFER 64
#endif
#ifndef CD_DEFAULT_BUFFER_SIZE
#define CD_DEFAULT_BUFFER_SIZE 512
#endif
#ifndef CD_DEFAULT_HEARTBEAT_S
#define CD_DEFAULT_HEARTBEAT_S 60
#endif
#ifndef CD_BACKOFF_MAX_S
#define CD_BACKOFF_MAX_S 30
#endif
#ifndef CD_DEFAULT_REGION
#define CD_DEFAULT_REGION "ap-in-1"
#endif

enum CDType : uint8_t {
    CD_AUTO   = 0,
    CD_INT    = 1,
    CD_FLOAT  = 2,
    CD_BOOL   = 3,
    CD_STRING = 4,
    CD_ENUM   = 5,
    CD_GEO    = 6  // reserved — geo/location types not yet supported in this release
};

enum CDAckMode : uint8_t {
    CD_ACK_AUTO   = 0,
    CD_ACK_MANUAL = 1
};

enum CDDirection : uint8_t {
    CD_DIR_SENSOR  = 0,
    CD_DIR_CONTROL = 1
};

enum CDError : uint8_t {
    CD_OK                   = 0,
    CD_ERR_NOT_INITIALIZED  = 1,
    CD_ERR_BAD_CREDENTIALS  = 2,
    CD_ERR_NOT_CONNECTED    = 3,
    CD_ERR_PUBLISH_FAILED   = 4,
    CD_ERR_TOPIC_TOO_LONG   = 5,
    CD_ERR_PAYLOAD_TOO_LONG = 6,
    CD_ERR_INVALID_JSON     = 7,
    CD_ERR_TYPE_MISMATCH    = 8,
    CD_ERR_UNKNOWN_VARIABLE = 9,
    CD_ERR_OUT_OF_MEMORY    = 10,
    CD_ERR_NO_API_KEY       = 11,  // sendImage: setApiKey() not called
    CD_ERR_HTTP_CONNECT     = 12,  // sendImage: TLS/TCP connect to the API host failed
    CD_ERR_HTTP_STATUS      = 13,  // sendImage: server returned a non-2xx status
    CD_ERR_BAD_ARGUMENT     = 14   // sendImage: empty image / bad parameter
};
