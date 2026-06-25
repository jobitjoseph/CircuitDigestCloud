// Copyright (c) 2026 Jobit Joseph, Circuit Digest
// SPDX-License-Identifier: MIT
#pragma once
#include <Arduino.h>
#include <Client.h>
#include <PubSubClient.h>
#include "CDTypes.h"
#include "CDValue.h"

typedef void (*CDControlCallback)(const char* variable, CDValue value);
typedef void (*CDTransportResetCallback)();

struct CDVariable {
    const char*       name;      // friendly name used in your sketch (e.g. "temperature")
    const char*       slot;      // Anedya slot from the dashboard (e.g. "float0")
    CDDirection       direction;
    CDType            type;
    bool              typeLocked;
    CDControlCallback callback;
    CDAckMode         ackMode;
    CDVariable*       next;
};

// Connects an Arduino-core device to the CircuitDigest Cloud, which is backed by the
// Anedya IoT platform. Telemetry is published to fixed project "slots" (float0..9,
// status0..4); your sketch refers to variables by a friendly name and supplies the
// slot shown on the dashboard. Transport-agnostic — pass a TLS-capable Client
// (e.g. WiFiClientSecure) since Anedya MQTT requires TLS on port 8883.
class CircuitDigestCloud {
public:
    explicit CircuitDigestCloud(Client& transport);
    ~CircuitDigestCloud();

    // Device credentials from the dashboard's device setup panel.
    //   deviceId      — the Physical Device ID (MQTT client id/username; must be the
    //                   device bound to the node, NOT the Anedya node id)
    //   connectionKey — the device's Connection Key (MQTT password)
    bool setCredentials(const char* deviceId, const char* connectionKey);

    // Anedya region (default "ap-in-1"). Sets the broker host to mqtt.<region>.anedya.io.
    void setRegion(const char* region);
    // Override broker host/port directly (takes priority over region).
    void setServer(const char* host, uint16_t port);

    // CircuitDigest Cloud HTTP API — used by sendImage(). Host defaults to
    // "www.circuitdigest.cloud":443. The API key is your dashboard API key
    // (cd_live_…), which is DIFFERENT from the MQTT Connection Key.
    void setApiHost(const char* host, uint16_t port = 443);
    void setApiKey (const char* apiKey);

    void setBufferSize        (uint16_t bytes);
    // Cadence (seconds) for the automatic Anedya heartbeat published while connected,
    // and the MQTT keepalive. Default 60s. Pass 0 to disable the auto heartbeat (the
    // MQTT keepalive is then left at the PubSubClient default). Anedya counts heartbeats
    // per 5s window, so the effective floor is 5s. See also heartbeat().
    void setHeartbeatInterval (uint32_t seconds);
    void setAutoAck           (bool enabled);
    void setDebug             (Stream* stream);
    void setTransportResetCallback(CDTransportResetCallback cb);

    // Publish online/offline presence to a boolean dashboard slot.
    // On connect : publishes true  to the slot.
    // On disconnect (clean): publishes false before closing the session.
    // On unexpected drop : the MQTT broker fires the LWT (false) automatically.
    // Call before begin(). Slot must be a boolean-type variable on the dashboard.
    void setOnlineStatusSlot(const char* slot);

    bool begin();
    void loop();

    bool    connected();
    bool    isConnecting();  // true while attempting to connect or in backoff
    void    disconnect();    // cleanly close the MQTT session and reset state
    CDError lastError();

    // Register a sensor variable. `slot` is the Anedya slot from the dashboard
    // (e.g. "float0"). If slot is nullptr, `name` is used as the slot directly.
    bool registerVariable(const char* name, CDType type = CD_AUTO, const char* slot = nullptr);

    // Register a control (output) variable + callback. `slot` as above.
    bool onChange(const char* name, CDControlCallback cb,
                   CDAckMode ack = CD_ACK_AUTO, CDType type = CD_AUTO,
                   const char* slot = nullptr);
    void onChange(CDControlCallback cb);   // global fallback

    bool publishVariable(const char* name, int         value, bool retain = true);
    bool publishVariable(const char* name, long        value, bool retain = true);
    bool publishVariable(const char* name, float       value, bool retain = true);
    bool publishVariable(const char* name, double      value, bool retain = true);
    bool publishVariable(const char* name, bool        value, bool retain = true);
    bool publishVariable(const char* name, const char* value, bool retain = true);

    bool ackChange(const char* name, int         value);
    bool ackChange(const char* name, long        value);
    bool ackChange(const char* name, float       value);
    bool ackChange(const char* name, double      value);
    bool ackChange(const char* name, bool        value);
    bool ackChange(const char* name, const char* value);

    // Upload a captured image (e.g. from an ESP32-CAM) to this device on CircuitDigest
    // Cloud over HTTPS. Pass a SEPARATE TLS-capable client — do NOT reuse the MQTT
    // transport; this call blocks until the upload finishes. Requires setApiKey(), and
    // setCredentials() so the device id is known. The image is sent as-is (no copy).
    //   contentType — image/jpeg | image/png | image/gif | image/webp  (≤ 5 MB)
    // Returns true on an HTTP 2xx response; otherwise see lastError().
    bool sendImage(Client& https, const uint8_t* data, size_t length,
                   const char* contentType = "image/jpeg",
                   const char* filename    = "capture.jpg");

    // Send one Anedya heartbeat (a dataless "I'm alive" signal, distinct from the MQTT
    // keepalive ping). A heartbeat is also sent automatically on connect and every
    // setHeartbeatInterval() seconds; call this for an extra on-demand beat.
    //   heartbeat()        — publish over the existing MQTT connection (cheap; use this).
    //   heartbeat(https)   — one-shot blocking HTTP POST /v1/heartbeat via a SEPARATE
    //                        TLS-capable client, for sketches that don't run the MQTT loop.
    // Returns true on success; otherwise see lastError().
    bool heartbeat();
    bool heartbeat(Client& https);

private:
    Client&      _transport;
    PubSubClient _pubsub;

    const char* _deviceId;
    const char* _connKey;

    char     _host[64];
    uint16_t _port;
    char     _region[24];    // retained so heartbeat(https) can derive device.<region>.anedya.io

    // CircuitDigest Cloud HTTP API (sendImage). Separate from the MQTT broker above.
    char        _apiHost[64];
    uint16_t    _apiPort;
    const char* _apiKey;

    char*    _topicBase;     // "$anedya/device/<deviceId>"
    uint16_t _topicBaseLen;

    uint16_t _bufferSize;
    uint32_t _heartbeatInterval;   // Anedya heartbeat cadence + MQTT keepalive (seconds; 0 = off)
    uint32_t _lastHeartbeatMs;     // millis() of the last auto/connect heartbeat
    bool     _autoAck;
    bool     _initialized;
    uint32_t _reqSeq;

    Stream*  _debug;
    CDError  _lastError;
    CDTransportResetCallback _transportReset;
    const char* _onlineSlot;   // slot for online/offline LWT presence (nullptr = disabled)

    enum State : uint8_t {
        CD_STATE_DISCONNECTED,
        CD_STATE_BACKOFF,
        CD_STATE_CONNECTING,
        CD_STATE_CONNECTED
    } _state;

    uint32_t _backoffNextMs;
    uint16_t _backoffSeconds;

    CDVariable*       _registryHead;
    CDControlCallback _globalControlCb;

    char _topicScratch  [CD_TOPIC_BUFFER_SIZE];
    char _valueScratch  [CD_PAYLOAD_BUFFER_SIZE];
    char _submitScratch [CD_SUBMIT_BUFFER_SIZE];
    char _inStringBuf   [CD_INBOUND_STRING_BUFFER];

    static CircuitDigestCloud* _instance;
    static void _staticCallback(char* topic, uint8_t* payload, unsigned int len);

    void _onMqttMessage(char* topic, uint8_t* payload, unsigned int len);
    bool _attemptConnect();
    void _onConnected();
    void _scheduleBackoff();

    CDVariable* _findVariable  (const char* name, CDDirection dir);
    CDVariable* _findBySlot    (const char* slot, CDDirection dir);
    CDVariable* _registerVar   (const char* name, CDDirection dir, CDType type, const char* slot);
    const char* _slotOf        (CDVariable* v) const;

    int  _readHttpStatus (Client& c);   // parse "HTTP/1.1 <code> …" → code, or -1

    bool _publishSubmit  (const char* slot, const char* valueToken, bool retain);
    bool _publishHeartbeat();   // publish "{}" to <base>/heartbeat/json over MQTT
    bool _doPublishSensor(const char* name, CDType type, const char* valueToken, bool retain);
    void _autoAckValue   (CDVariable* v, CDValue& val);
    void _resetTransport();

    void _logf(const char* fmt, ...);
};
