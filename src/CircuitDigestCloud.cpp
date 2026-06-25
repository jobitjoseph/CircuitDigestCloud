// Copyright (c) 2026 Jobit Joseph, Circuit Digest
// SPDX-License-Identifier: MIT
#include "CircuitDigestCloud.h"
#include "CDJson.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

// CircuitDigest Cloud runs on the Anedya IoT platform. Devices connect over TLS
// (port 8883) and publish telemetry to fixed project slots under the topic
// $anedya/device/<deviceId>/submitdata/json.
static const uint16_t CD_MQTT_PORT_TLS = 8883;
static const char     CD_TOPIC_PREFIX[] = "$anedya/device/";

// Default CircuitDigest Cloud HTTP API host for image uploads (sendImage).
static const char     CD_DEFAULT_API_HOST[] = "www.circuitdigest.cloud";
static const uint16_t CD_DEFAULT_API_PORT   = 443;

CircuitDigestCloud* CircuitDigestCloud::_instance = nullptr;

// ---- Construction ----------------------------------------------------------

CircuitDigestCloud::CircuitDigestCloud(Client& transport)
    : _transport(transport), _pubsub(transport),
      _deviceId(nullptr), _connKey(nullptr),
      _port(CD_MQTT_PORT_TLS),
      _apiPort(CD_DEFAULT_API_PORT), _apiKey(nullptr),
      _topicBase(nullptr), _topicBaseLen(0),
      _bufferSize(CD_DEFAULT_BUFFER_SIZE),
      _heartbeatInterval(CD_DEFAULT_HEARTBEAT_S), _lastHeartbeatMs(0),
      _autoAck(true), _initialized(false), _reqSeq(0),
      _debug(nullptr), _lastError(CD_OK),
      _transportReset(nullptr), _onlineSlot(nullptr),
      _state(CD_STATE_DISCONNECTED),
      _backoffNextMs(0), _backoffSeconds(1),
      _registryHead(nullptr), _globalControlCb(nullptr)
{
    setRegion(CD_DEFAULT_REGION);
    snprintf(_apiHost, sizeof(_apiHost), "%s", CD_DEFAULT_API_HOST);
    _instance = this;
}

CircuitDigestCloud::~CircuitDigestCloud() {
    free(_topicBase);
    CDVariable* v = _registryHead;
    while (v) { CDVariable* n = v->next; delete v; v = n; }
    if (_instance == this) _instance = nullptr;
}

// ---- Configuration ---------------------------------------------------------

bool CircuitDigestCloud::setCredentials(const char* deviceId, const char* connectionKey) {
    _lastError = CD_OK;
    if (!deviceId || !*deviceId || !connectionKey || !*connectionKey) {
        _lastError = CD_ERR_BAD_CREDENTIALS;
        _logf("[CD] setCredentials: missing field");
        return false;
    }
    _deviceId = deviceId;
    _connKey  = connectionKey;
    _logf("[CD] setCredentials: OK");
    return true;
}

void CircuitDigestCloud::setRegion(const char* region) {
    if (!region || !*region) region = CD_DEFAULT_REGION;
    snprintf(_region, sizeof(_region), "%s", region);   // kept for heartbeat(https)
    snprintf(_host, sizeof(_host), "mqtt.%s.anedya.io", region);
    _port = CD_MQTT_PORT_TLS;
}

void CircuitDigestCloud::setServer(const char* host, uint16_t port) {
    if (host && *host) {
        strncpy(_host, host, sizeof(_host) - 1);
        _host[sizeof(_host) - 1] = 0;
    }
    if (port) _port = port;
}

void CircuitDigestCloud::setApiHost(const char* host, uint16_t port) {
    if (host && *host) {
        strncpy(_apiHost, host, sizeof(_apiHost) - 1);
        _apiHost[sizeof(_apiHost) - 1] = 0;
    }
    if (port) _apiPort = port;
}

void CircuitDigestCloud::setApiKey(const char* apiKey) { _apiKey = apiKey; }

void CircuitDigestCloud::setBufferSize(uint16_t bytes) {
    _bufferSize = (bytes < 256) ? 256 : bytes;
}

void CircuitDigestCloud::setHeartbeatInterval(uint32_t seconds) {
    _heartbeatInterval = seconds;
}

void CircuitDigestCloud::setAutoAck(bool enabled) { _autoAck = enabled; }

void CircuitDigestCloud::setDebug(Stream* stream) { _debug = stream; }

void CircuitDigestCloud::setTransportResetCallback(CDTransportResetCallback cb) {
    _transportReset = cb;
}

void CircuitDigestCloud::setOnlineStatusSlot(const char* slot) {
    _onlineSlot = slot;
}

void CircuitDigestCloud::_resetTransport() {
    _transport.stop();
    if (_transportReset) _transportReset();
}

// ---- begin() ---------------------------------------------------------------

bool CircuitDigestCloud::begin() {
    _lastError = CD_OK;
    if (!_deviceId || !_connKey) {
        _lastError = CD_ERR_BAD_CREDENTIALS;
        return false;
    }
    free(_topicBase); _topicBase = nullptr;

    // Build topic base: "$anedya/device/<deviceId>"
    size_t blen = strlen(CD_TOPIC_PREFIX) + strlen(_deviceId) + 1;
    _topicBase = (char*)malloc(blen);
    if (!_topicBase) { _lastError = CD_ERR_OUT_OF_MEMORY; return false; }
    snprintf(_topicBase, blen, "%s%s", CD_TOPIC_PREFIX, _deviceId);
    _topicBaseLen = (uint16_t)(blen - 1);

    _pubsub.setServer(_host, _port);
    _pubsub.setBufferSize(_bufferSize);
    _pubsub.setCallback(_staticCallback);
    // Anedya tracks device liveness via the MQTT session; map the heartbeat
    // interval onto the MQTT keepalive so PubSubClient pings keep us "online".
    if (_heartbeatInterval > 0 && _heartbeatInterval <= 0xFFFF) {
        _pubsub.setKeepAlive((uint16_t)_heartbeatInterval);
    }

    _state = CD_STATE_DISCONNECTED;
    _backoffSeconds = 1;
    _backoffNextMs  = 0;
    _initialized = true;

    _logf("[CD] begin: host=%s:%u base=%s", _host, _port, _topicBase);
    return true;
}

// ---- loop() ----------------------------------------------------------------

void CircuitDigestCloud::loop() {
    if (!_initialized) return;

    if (_state == CD_STATE_CONNECTED && !_pubsub.connected()) {
        _logf("[CD] connection lost → DISCONNECTED");
        _pubsub.disconnect();
        _resetTransport();
        delay(250);
        _state = CD_STATE_DISCONNECTED;
    }

    if (_state == CD_STATE_DISCONNECTED) {
        _state = CD_STATE_CONNECTING;
        if (!_attemptConnect()) {
            _scheduleBackoff();
            _state = CD_STATE_BACKOFF;
        }
    } else if (_state == CD_STATE_BACKOFF) {
        if ((uint32_t)(millis() - _backoffNextMs) < 0x80000000UL) {
            _state = CD_STATE_CONNECTING;
            if (!_attemptConnect()) {
                _scheduleBackoff();
                _state = CD_STATE_BACKOFF;
            }
        }
    } else if (_state == CD_STATE_CONNECTED) {
        _pubsub.loop();
        // Auto Anedya heartbeat. ponytail: 5s floor matches Anedya's per-5s counting —
        // raise the interval, never lower; 0 disables.
        if (_heartbeatInterval > 0) {
            uint32_t periodMs = (_heartbeatInterval < 5 ? 5 : _heartbeatInterval) * 1000UL;
            if ((uint32_t)(millis() - _lastHeartbeatMs) >= periodMs) {
                _publishHeartbeat();
                _lastHeartbeatMs = millis();
            }
        }
    }
}

// ---- Connection ------------------------------------------------------------

bool CircuitDigestCloud::connected()    { return _state == CD_STATE_CONNECTED && _pubsub.connected(); }
bool CircuitDigestCloud::isConnecting() { return _state == CD_STATE_CONNECTING || _state == CD_STATE_BACKOFF; }

void CircuitDigestCloud::disconnect() {
    if (_onlineSlot && _pubsub.connected() && _topicBase) {
        cdValueBool(_valueScratch, sizeof(_valueScratch), false);
        _publishSubmit(_onlineSlot, _valueScratch, true);
        _logf("[CD] online status → false (%s)", _onlineSlot);
    }
    if (_pubsub.connected()) _pubsub.disconnect();
    _resetTransport();
    _state = CD_STATE_DISCONNECTED;
    _backoffSeconds = 1;
    _backoffNextMs  = 0;
    _logf("[CD] disconnect: session closed");
}

CDError CircuitDigestCloud::lastError() { return _lastError; }

bool CircuitDigestCloud::_attemptConnect() {
    // Reset MQTT and TLS state before each attempt — WiFiClientSecure keeps stale
    // SSL context after a disconnect, causing connects to fail at the TLS
    // handshake level (PubSubClient state=-2).
    _pubsub.disconnect();
    _resetTransport();
    delay(250);
    // Anedya: MQTT client id = username = deviceId, password = connectionKey.
    _logf("[CD] connecting to %s:%u as %s ...", _host, _port, _deviceId);
    bool ok;
    if (_onlineSlot && _topicBase) {
        // Build LWT topic + payload: broker fires this automatically on unexpected drop.
        char lwtTopic[CD_TOPIC_BUFFER_SIZE];
        char lwtMsg[128];
        snprintf(lwtTopic, sizeof(lwtTopic), "%s/submitdata/json", _topicBase);
        snprintf(lwtMsg, sizeof(lwtMsg),
            "{\"reqID\":\"0\",\"data\":[{\"variable\":\"%s\",\"value\":false,\"timestamp\":0}]}",
            _onlineSlot);
        ok = _pubsub.connect(_deviceId, _deviceId, _connKey, lwtTopic, 1, true, lwtMsg);
    } else {
        ok = _pubsub.connect(_deviceId, _deviceId, _connKey);
    }
    if (ok) {
        _state = CD_STATE_CONNECTED;
        _onConnected();
        _logf("[CD] connected");
    } else {
        int state = _pubsub.state();
        if (state == -2) {
            _logf("[CD] TLS handshake failed; transport reset before retry");
        }
        _logf("[CD] connect failed (state=%d)", state);
    }
    return ok;
}

void CircuitDigestCloud::_onConnected() {
    // Subscribe to value-store updates (control writes) + errors/response.
    snprintf(_topicScratch, sizeof(_topicScratch), "%s/valuestore/updates/json", _topicBase);
    _pubsub.subscribe(_topicScratch, 1);
    _logf("[CD] subscribed %s", _topicScratch);

    snprintf(_topicScratch, sizeof(_topicScratch), "%s/errors", _topicBase);
    _pubsub.subscribe(_topicScratch, 1);
    snprintf(_topicScratch, sizeof(_topicScratch), "%s/response", _topicBase);
    _pubsub.subscribe(_topicScratch, 1);

    if (_onlineSlot) {
        cdValueBool(_valueScratch, sizeof(_valueScratch), true);
        _publishSubmit(_onlineSlot, _valueScratch, true);
        _logf("[CD] online status → true (%s)", _onlineSlot);
    }

    // Beat once right away so the platform sees us online without waiting a full interval.
    _publishHeartbeat();
    _lastHeartbeatMs = millis();

    _backoffSeconds = 1;
}

void CircuitDigestCloud::_scheduleBackoff() {
    _backoffNextMs = millis() + (uint32_t)_backoffSeconds * 1000UL;
    _logf("[CD] backoff %us", _backoffSeconds);
    _backoffSeconds = (_backoffSeconds * 2 > CD_BACKOFF_MAX_S)
                      ? CD_BACKOFF_MAX_S : _backoffSeconds * 2;
}

// ---- Registry --------------------------------------------------------------

CDVariable* CircuitDigestCloud::_findVariable(const char* name, CDDirection dir) {
    for (CDVariable* v = _registryHead; v; v = v->next)
        if (v->direction == dir && strcmp(v->name, name) == 0) return v;
    return nullptr;
}

CDVariable* CircuitDigestCloud::_findBySlot(const char* slot, CDDirection dir) {
    for (CDVariable* v = _registryHead; v; v = v->next)
        if (v->direction == dir && _slotOf(v) && strcmp(_slotOf(v), slot) == 0) return v;
    return nullptr;
}

const char* CircuitDigestCloud::_slotOf(CDVariable* v) const {
    // Fall back to the friendly name when no explicit slot was provided.
    return (v->slot && *v->slot) ? v->slot : v->name;
}

CDVariable* CircuitDigestCloud::_registerVar(const char* name, CDDirection dir,
                                             CDType type, const char* slot) {
    CDVariable* v = _findVariable(name, dir);
    if (v) {
        if (slot && *slot) v->slot = slot;  // allow late slot assignment
        return v;
    }
    v = new CDVariable();
    if (!v) { _lastError = CD_ERR_OUT_OF_MEMORY; return nullptr; }
    v->name       = name;
    v->slot       = slot;
    v->direction  = dir;
    v->type       = type;
    v->typeLocked = (type != CD_AUTO);
    v->callback   = nullptr;
    v->ackMode    = CD_ACK_AUTO;
    v->next       = _registryHead;
    _registryHead = v;
    return v;
}

bool CircuitDigestCloud::registerVariable(const char* name, CDType type, const char* slot) {
    _lastError = CD_OK;
    return _registerVar(name, CD_DIR_SENSOR, type, slot) != nullptr;
}

bool CircuitDigestCloud::onChange(const char* name, CDControlCallback cb,
                                   CDAckMode ack, CDType type, const char* slot) {
    _lastError = CD_OK;
    if (!name || !cb) { _lastError = CD_ERR_BAD_CREDENTIALS; return false; }
    CDVariable* v = _registerVar(name, CD_DIR_CONTROL, type, slot);
    if (!v) return false;
    v->callback   = cb;
    v->ackMode    = ack;
    v->type       = type;
    v->typeLocked = (type != CD_AUTO);
    return true;
}

void CircuitDigestCloud::onChange(CDControlCallback cb) {
    _globalControlCb = cb;
}

// ---- Publish helpers -------------------------------------------------------

bool CircuitDigestCloud::_publishSubmit(const char* slot, const char* valueToken, bool retain) {
    if (!_pubsub.connected()) { _lastError = CD_ERR_NOT_CONNECTED; return false; }
    if (!slot || !*slot || !valueToken || !*valueToken) {
        _lastError = CD_ERR_PAYLOAD_TOO_LONG; return false;
    }
    int n = snprintf(_topicScratch, sizeof(_topicScratch),
                     "%s/submitdata/json", _topicBase);
    if (n <= 0 || (size_t)n >= sizeof(_topicScratch)) {
        _lastError = CD_ERR_TOPIC_TOO_LONG; return false;
    }
    // {"reqID":"<seq>","data":[{"variable":"<slot>","value":<token>,"timestamp":0}]}
    int p = snprintf(_submitScratch, sizeof(_submitScratch),
        "{\"reqID\":\"%lu\",\"data\":[{\"variable\":\"%s\",\"value\":%s,\"timestamp\":0}]}",
        (unsigned long)(++_reqSeq), slot, valueToken);
    if (p <= 0 || (size_t)p >= sizeof(_submitScratch)) {
        _lastError = CD_ERR_PAYLOAD_TOO_LONG; return false;
    }
    bool ok = _pubsub.publish(_topicScratch, (const uint8_t*)_submitScratch, (unsigned int)p, retain);
    if (!ok) _lastError = CD_ERR_PUBLISH_FAILED;
    _logf("[CD] submit %s=%s [%s]", slot, valueToken, ok ? "ok" : "fail");
    return ok;
}

bool CircuitDigestCloud::_publishHeartbeat() {
    if (!_pubsub.connected()) { _lastError = CD_ERR_NOT_CONNECTED; return false; }
    int n = snprintf(_topicScratch, sizeof(_topicScratch), "%s/heartbeat/json", _topicBase);
    if (n <= 0 || (size_t)n >= sizeof(_topicScratch)) {
        _lastError = CD_ERR_TOPIC_TOO_LONG; return false;
    }
    // Dataless beat — payload is an empty object. Not retained.
    bool ok = _pubsub.publish(_topicScratch, (const uint8_t*)"{}", 2, false);
    if (!ok) _lastError = CD_ERR_PUBLISH_FAILED;
    _logf("[CD] heartbeat [%s]", ok ? "ok" : "fail");
    return ok;
}

bool CircuitDigestCloud::heartbeat() {
    _lastError = CD_OK;
    return _publishHeartbeat();
}

bool CircuitDigestCloud::heartbeat(Client& https) {
    _lastError = CD_OK;
    if (!_deviceId || !_connKey) {
        _lastError = CD_ERR_BAD_CREDENTIALS;
        _logf("[CD] heartbeat(http): missing credentials");
        return false;
    }
    char host[64];
    snprintf(host, sizeof(host), "device.%s.anedya.io", _region);
    if (!https.connect(host, 443)) {
        _lastError = CD_ERR_HTTP_CONNECT;
        _logf("[CD] heartbeat(http): connect %s:443 failed", host);
        return false;
    }
    // POST /v1/heartbeat with an empty body, authed by the device Connection Key.
    char line[96];
    https.print("POST /v1/heartbeat HTTP/1.1\r\n");
    snprintf(line, sizeof(line), "Host: %s\r\n", host);                https.print(line);
    https.print("Auth-mode: key\r\n");
    https.print("Authorization: "); https.print(_connKey); https.print("\r\n");
    https.print("Content-Type: application/json\r\n");
    https.print("Content-Length: 2\r\n");
    https.print("Connection: close\r\n\r\n");
    https.print("{}");
    https.flush();

    int status = _readHttpStatus(https);
    https.stop();
    if (status >= 200 && status < 300) {
        _logf("[CD] heartbeat(http): OK (HTTP %d)", status);
        return true;
    }
    _lastError = CD_ERR_HTTP_STATUS;
    _logf("[CD] heartbeat(http): HTTP %d", status);
    return false;
}

bool CircuitDigestCloud::_doPublishSensor(const char* name, CDType type,
                                          const char* valueToken, bool retain) {
    CDVariable* v = _findVariable(name, CD_DIR_SENSOR);
    if (!v) v = _registerVar(name, CD_DIR_SENSOR, type, nullptr);
    if (v && !v->typeLocked) { v->type = type; v->typeLocked = true; }
    const char* slot = v ? _slotOf(v) : name;
    return _publishSubmit(slot, valueToken, retain);
}

// ---- publishVariable overloads ---------------------------------------------

bool CircuitDigestCloud::publishVariable(const char* name, long value, bool retain) {
    _lastError = CD_OK;
    if (!cdValueInt(_valueScratch, sizeof(_valueScratch), value))
        { _lastError = CD_ERR_PAYLOAD_TOO_LONG; return false; }
    return _doPublishSensor(name, CD_INT, _valueScratch, retain);
}
bool CircuitDigestCloud::publishVariable(const char* name, int value, bool retain) {
    return publishVariable(name, (long)value, retain);
}
bool CircuitDigestCloud::publishVariable(const char* name, float value, bool retain) {
    _lastError = CD_OK;
    if (!cdValueFloat(_valueScratch, sizeof(_valueScratch), value))
        { _lastError = CD_ERR_PAYLOAD_TOO_LONG; return false; }
    return _doPublishSensor(name, CD_FLOAT, _valueScratch, retain);
}
bool CircuitDigestCloud::publishVariable(const char* name, double value, bool retain) {
    return publishVariable(name, (float)value, retain);
}
bool CircuitDigestCloud::publishVariable(const char* name, bool value, bool retain) {
    _lastError = CD_OK;
    if (!cdValueBool(_valueScratch, sizeof(_valueScratch), value))
        { _lastError = CD_ERR_PAYLOAD_TOO_LONG; return false; }
    return _doPublishSensor(name, CD_BOOL, _valueScratch, retain);
}
bool CircuitDigestCloud::publishVariable(const char* name, const char* value, bool retain) {
    _lastError = CD_OK;
    if (!cdValueString(_valueScratch, sizeof(_valueScratch), value))
        { _lastError = CD_ERR_PAYLOAD_TOO_LONG; return false; }
    return _doPublishSensor(name, CD_STRING, _valueScratch, retain);
}

// ---- ackChange overloads ---------------------------------------------------
// On Anedya a control acknowledgement is simply the device reporting the actual
// value of the control variable back to its slot (so the dashboard reflects real
// hardware state). It is published exactly like a sensor reading.

bool CircuitDigestCloud::ackChange(const char* name, long value) {
    _lastError = CD_OK;
    CDVariable* v = _findVariable(name, CD_DIR_CONTROL);
    if (!v && !_globalControlCb) { _lastError = CD_ERR_UNKNOWN_VARIABLE; return false; }
    if (!cdValueInt(_valueScratch, sizeof(_valueScratch), value))
        { _lastError = CD_ERR_PAYLOAD_TOO_LONG; return false; }
    return _publishSubmit(v ? _slotOf(v) : name, _valueScratch, true);
}
bool CircuitDigestCloud::ackChange(const char* name, int value) {
    return ackChange(name, (long)value);
}
bool CircuitDigestCloud::ackChange(const char* name, float value) {
    _lastError = CD_OK;
    CDVariable* v = _findVariable(name, CD_DIR_CONTROL);
    if (!v && !_globalControlCb) { _lastError = CD_ERR_UNKNOWN_VARIABLE; return false; }
    if (!cdValueFloat(_valueScratch, sizeof(_valueScratch), value))
        { _lastError = CD_ERR_PAYLOAD_TOO_LONG; return false; }
    return _publishSubmit(v ? _slotOf(v) : name, _valueScratch, true);
}
bool CircuitDigestCloud::ackChange(const char* name, double value) {
    return ackChange(name, (float)value);
}
bool CircuitDigestCloud::ackChange(const char* name, bool value) {
    _lastError = CD_OK;
    CDVariable* v = _findVariable(name, CD_DIR_CONTROL);
    if (!v && !_globalControlCb) { _lastError = CD_ERR_UNKNOWN_VARIABLE; return false; }
    if (!cdValueBool(_valueScratch, sizeof(_valueScratch), value))
        { _lastError = CD_ERR_PAYLOAD_TOO_LONG; return false; }
    return _publishSubmit(v ? _slotOf(v) : name, _valueScratch, true);
}
bool CircuitDigestCloud::ackChange(const char* name, const char* value) {
    _lastError = CD_OK;
    CDVariable* v = _findVariable(name, CD_DIR_CONTROL);
    if (!v && !_globalControlCb) { _lastError = CD_ERR_UNKNOWN_VARIABLE; return false; }
    if (!cdValueString(_valueScratch, sizeof(_valueScratch), value))
        { _lastError = CD_ERR_PAYLOAD_TOO_LONG; return false; }
    return _publishSubmit(v ? _slotOf(v) : name, _valueScratch, true);
}

// ---- Inbound dispatch ------------------------------------------------------

void CircuitDigestCloud::_staticCallback(char* topic, uint8_t* payload, unsigned int len) {
    if (_instance) _instance->_onMqttMessage(topic, payload, len);
}

void CircuitDigestCloud::_onMqttMessage(char* topic, uint8_t* payload, unsigned int len) {
    if (strncmp(topic, _topicBase, _topicBaseLen) != 0) return;
    const char* rest = topic + _topicBaseLen;

    // Errors / responses are logged only.
    if (strncmp(rest, "/errors", 7) == 0 || strncmp(rest, "/response", 9) == 0) {
        _logf("[CD] %s: %.*s", rest + 1, (int)len, (const char*)payload);
        return;
    }

    // Control writes arrive on /valuestore/updates/json. Accept both a flat
    // {"<slot>": <value>} and a structured {"key":"<slot>","value":<v>,...} shape.
    if (strncmp(rest, "/valuestore/updates/json", 24) != 0) return;

    _logf("[CD] valuestore raw: %.*s", (int)len, (const char*)payload);

    char slot[33];
    CDValue val;

    // Anedya value-store update shape (confirmed):
    //   {"namespace":{...},"type":"float","key":"<slot>","value":<v>,"modified":..}
    // Extract the named "key" and "value" fields (the first object key is
    // "namespace", so a first-key parse is not enough). Copy the slot out before
    // parsing "value", since both reuse the inbound string buffer.
    CDValue keyVal;
    if (cdParseField(payload, len, "key", &keyVal, _inStringBuf, sizeof(_inStringBuf))
            && keyVal.isString()) {
        strncpy(slot, keyVal.asString(), sizeof(slot) - 1);
        slot[sizeof(slot) - 1] = 0;
        if (!cdParseField(payload, len, "value", &val, _inStringBuf, sizeof(_inStringBuf))) {
            _logf("[CD] valuestore: 'value' field missing");
            return;
        }
    } else if (!cdParseJson(payload, len, &val, slot, sizeof(slot),
                            _inStringBuf, sizeof(_inStringBuf))) {
        // Fallback: flat {"<slot>": <value>}
        _lastError = CD_ERR_INVALID_JSON;
        _logf("[CD] valuestore parse failed");
        return;
    }

    CDVariable* v = _findBySlot(slot, CD_DIR_CONTROL);
    if (!v) v = _findVariable(slot, CD_DIR_CONTROL);  // also try friendly name
    const char* reportName = v ? v->name : slot;
    _logf("[CD] control %s (slot %s) type=%d", reportName, slot, (int)val.type());

    if (v && v->callback) {
        if (v->type == CD_AUTO && !v->typeLocked) {
            v->type = val.type(); v->typeLocked = true;
        } else if (v->typeLocked && v->type != val.type() &&
                   !(v->type == CD_ENUM && val.type() == CD_STRING)) {
            _lastError = CD_ERR_TYPE_MISMATCH;
            _logf("[CD] type mismatch %s expected=%d got=%d",
                  reportName, (int)v->type, (int)val.type());
        }
        v->callback(reportName, val);
        bool doAuto = (v->ackMode == CD_ACK_AUTO) ||
                      (v->ackMode != CD_ACK_MANUAL && _autoAck);
        if (doAuto) _autoAckValue(v, val);
    } else if (_globalControlCb) {
        _globalControlCb(reportName, val);
    } else {
        _logf("[CD] no handler for control %s", reportName);
    }
}

void CircuitDigestCloud::_autoAckValue(CDVariable* v, CDValue& val) {
    size_t n = 0;
    switch (val.type()) {
        case CD_INT:    n = cdValueInt   (_valueScratch, sizeof(_valueScratch), val.asInt());    break;
        case CD_FLOAT:  n = cdValueFloat (_valueScratch, sizeof(_valueScratch), val.asFloat());  break;
        case CD_BOOL:   n = cdValueBool  (_valueScratch, sizeof(_valueScratch), val.asBool());   break;
        case CD_STRING:
        case CD_ENUM:   n = cdValueString(_valueScratch, sizeof(_valueScratch), val.asString()); break;
        default:
            _logf("[CD] autoAck: null value suppressed for %s", v->name);
            return;
    }
    if (!n) return;
    _publishSubmit(_slotOf(v), _valueScratch, true);
}

// ---- Image upload (HTTPS) --------------------------------------------------

bool CircuitDigestCloud::sendImage(Client& https, const uint8_t* data, size_t length,
                                   const char* contentType, const char* filename) {
    _lastError = CD_OK;
    if (!_deviceId) {
        _lastError = CD_ERR_BAD_CREDENTIALS;
        _logf("[CD] sendImage: call setCredentials() first");
        return false;
    }
    if (!_apiKey || !*_apiKey) {
        _lastError = CD_ERR_NO_API_KEY;
        _logf("[CD] sendImage: call setApiKey() first");
        return false;
    }
    if (!data || length == 0) {
        _lastError = CD_ERR_BAD_ARGUMENT;
        _logf("[CD] sendImage: empty image");
        return false;
    }
    if (!contentType || !*contentType) contentType = "image/jpeg";
    if (!filename    || !*filename)    filename    = "capture.jpg";

    // multipart/form-data: one "image" file part. Body = head + raw bytes + tail.
    static const char BOUNDARY[] = "----CircuitDigestCloudImageBoundary";
    char head[224];
    int headLen = snprintf(head, sizeof(head),
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"image\"; filename=\"%s\"\r\n"
        "Content-Type: %s\r\n\r\n",
        BOUNDARY, filename, contentType);
    char tail[48];
    int tailLen = snprintf(tail, sizeof(tail), "\r\n--%s--\r\n", BOUNDARY);
    if (headLen <= 0 || headLen >= (int)sizeof(head) || tailLen <= 0) {
        _lastError = CD_ERR_BAD_ARGUMENT;
        _logf("[CD] sendImage: header too long");
        return false;
    }

    size_t contentLength = (size_t)headLen + length + (size_t)tailLen;

    if (!https.connect(_apiHost, _apiPort)) {
        _lastError = CD_ERR_HTTP_CONNECT;
        _logf("[CD] sendImage: connect %s:%u failed", _apiHost, _apiPort);
        return false;
    }

    // Request line + headers. deviceId may be a UUID or mqtt_u_<uuid>; the server
    // resolves both. The dashboard API key goes in the Authorization header.
    char line[160];
    https.print("POST /api/v1/devices/");
    https.print(_deviceId);
    https.print("/image HTTP/1.1\r\n");
    snprintf(line, sizeof(line), "Host: %s\r\n", _apiHost);                 https.print(line);
    https.print("Authorization: "); https.print(_apiKey); https.print("\r\n");
    snprintf(line, sizeof(line),
             "Content-Type: multipart/form-data; boundary=%s\r\n", BOUNDARY); https.print(line);
    snprintf(line, sizeof(line),
             "Content-Length: %lu\r\n", (unsigned long)contentLength);        https.print(line);
    https.print("Connection: close\r\n\r\n");

    https.write((const uint8_t*)head, (size_t)headLen);

    // Stream the image in chunks so we never need a second copy in RAM.
    const size_t CHUNK = 1024;
    size_t sent = 0;
    while (sent < length) {
        size_t want  = (length - sent < CHUNK) ? (length - sent) : CHUNK;
        size_t wrote = https.write(data + sent, want);
        if (wrote == 0) {
            https.stop();
            _lastError = CD_ERR_PUBLISH_FAILED;
            _logf("[CD] sendImage: write stalled at %u/%u", (unsigned)sent, (unsigned)length);
            return false;
        }
        sent += wrote;
    }
    https.write((const uint8_t*)tail, (size_t)tailLen);
    https.flush();

    int status = _readHttpStatus(https);
    https.stop();

    if (status >= 200 && status < 300) {
        _logf("[CD] sendImage: OK (HTTP %d, %u bytes)", status, (unsigned)length);
        return true;
    }
    _lastError = CD_ERR_HTTP_STATUS;
    _logf("[CD] sendImage: HTTP %d", status);
    return false;
}

int CircuitDigestCloud::_readHttpStatus(Client& c) {
    // Read only the status line "HTTP/1.1 <code> <reason>" and return <code>, or -1.
    char buf[48];
    size_t i = 0;
    uint32_t start = millis();
    bool sawData = false;
    while (millis() - start < 10000) {
        int ch = c.read();
        if (ch < 0) {
            if (sawData && !c.connected() && !c.available()) break;
            delay(2);
            continue;
        }
        sawData = true;
        if (ch == '\n') break;
        if (ch != '\r' && i < sizeof(buf) - 1) buf[i++] = (char)ch;
    }
    buf[i] = 0;
    const char* sp = strchr(buf, ' ');
    return sp ? atoi(sp + 1) : -1;
}

// ---- Debug -----------------------------------------------------------------

void CircuitDigestCloud::_logf(const char* fmt, ...) {
    if (!_debug) return;
    char buf[160];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    _debug->println(buf);
}
