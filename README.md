# CircuitDigestCloud

Arduino library for connecting any Arduino-core device to the **CircuitDigest Cloud** MQTT platform
(backed by [Anedya](https://anedya.io)). Transport-agnostic — works with WiFi, Ethernet, GSM, or any
TLS-capable `Client`. Anedya MQTT requires TLS on port 8883.

---

## How variables map to "slots"

CircuitDigest Cloud stores telemetry in a fixed catalog of project **slots** — semantic keys like
`temperature-1`, `humidity-1`, `voltage-1` (hyphens only; Anedya identifiers can't contain underscores).
When you create a variable on the dashboard you pick a catalog key; that key is the slot. In your sketch you
keep a friendly name and tell the library which slot it maps to — copy the slot from the device setup panel.

```cpp
dashboard:  Motor Temp  → temperature-1
sketch:     cd.registerVariable("temperature", CD_FLOAT, "temperature-1");
            cd.publishVariable("temperature", 25.3f);   // published to temperature-1
```

---

## Quick Start

```cpp
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <CircuitDigestCloud.h>

WiFiClientSecure net;          // TLS required (port 8883)
CircuitDigestCloud cd(net);

void setup() {
    Serial.begin(115200);
    WiFi.begin("ssid", "password");
    while (WiFi.status() != WL_CONNECTED) delay(200);

    net.setInsecure();         // dev only — pin the Anedya CA for production

    cd.setCredentials("device-id", "connection-key");  // Physical Device ID + Connection Key from the device setup panel
    cd.setDebug(&Serial);                            // comment out for production
    cd.registerVariable("temperature", CD_FLOAT, "float0");
    cd.begin();
}

void loop() {
    cd.loop();
    static uint32_t last = 0;
    if (millis() - last > 5000) {
        last = millis();
        cd.publishVariable("temperature", 25.3f);
    }
}
```

---

## Installation

**Arduino IDE Library Manager:** Search for `CircuitDigestCloud`.

**Manual ZIP:** Download, unzip into `~/Documents/Arduino/libraries/CircuitDigestCloud/`.

**PlatformIO:**
```ini
lib_deps = CircuitDigestCloud
```

**Required dependency:** [PubSubClient](https://github.com/knolleary/pubsubclient) ≥ 2.8.
You also need a TLS client (`WiFiClientSecure` is bundled with the ESP32/ESP8266 cores).

---

## Credentials

Find these in the CircuitDigest Cloud dashboard → device setup panel:

| Parameter | Dashboard field |
|---|---|
| `deviceId` | Physical Device ID (the device bound to the node — NOT the Anedya node id) |
| `connectionKey` | Connection Key |
| slot (per variable) | Slot column next to each variable (e.g. `float0`) |

---

## API Reference

| Method | Description |
|---|---|
| `CircuitDigestCloud(Client&)` | Constructor. Pass a TLS-capable transport client. |
| `setCredentials(deviceId, connectionKey)` | Set Anedya MQTT credentials. Call before `begin()`. |
| `setRegion(region)` | Anedya region; sets broker to `mqtt.<region>.anedya.io`. Default `ap-in-1`. |
| `setServer(host, port)` | Override broker host/port directly. |
| `setApiHost(host, port)` | CircuitDigest Cloud HTTP API for `sendImage`. Default `www.circuitdigest.cloud:443`. |
| `setApiKey(apiKey)` | Dashboard API key (`cd_live_…`) for `sendImage`. Different from the Connection Key. |
| `sendImage(client, data, len, contentType, filename)` | Upload a captured image over HTTPS. Pass a **separate** TLS client. Blocks until done. |
| `setBufferSize(bytes)` | PubSubClient buffer size. Default 512, min 256. |
| `setHeartbeatInterval(seconds)` | MQTT keepalive cadence used for liveness. Default 60. |
| `setAutoAck(bool)` | Global auto-ack default for controls. Default `true`. |
| `setDebug(Stream*)` | Enable debug logging (e.g. `&Serial`). `nullptr` disables. |
| `begin()` | Initialize. Returns `false` on bad credentials. Connection is lazy. |
| `loop()` | Call every iteration. Drives connection + MQTT pump. Never blocks. |
| `connected()` | `true` when MQTT is up. |
| `lastError()` | Last `CDError` code. Read immediately after a method returns `false`. |
| `registerVariable(name, type, slot)` | Map a sensor variable name to its dashboard slot. |
| `onChange(name, cb, ack, type, slot)` | Register a control callback for a variable + slot. |
| `onChange(cb)` | Global fallback for unhandled controls. |
| `publishVariable(name, value, retain)` | Publish a sensor reading to the variable's slot. int/long/float/double/bool/const char*. |
| `ackChange(name, value)` | Report a control's actual value back to its slot. |

If you omit the `slot` argument (or pass `nullptr`), the `name` itself is used as the slot — useful if you
want to publish straight to a slot like `cd.publishVariable("float0", v)`.

---

## Variable Types

| Constant | Wire format | C++ type | Slot family |
|---|---|---|---|
| `CD_INT` | bare number | `long` | float |
| `CD_FLOAT` | bare number | `float` | float |
| `CD_BOOL` | `true`/`false` | `bool` | float |
| `CD_STRING` | quoted string | `const char*` | status |
| `CD_ENUM` | quoted string | `const char*` | status |
| `CD_AUTO` | auto-detected on first use | — | — |

> Geo (lat/long) variables are not supported in this release. String/enum (status-slot)
> catalog keys are not exposed yet either — send color as a packed integer (see
> `07_ColorPicker`) and use float/boolean keys for everything else.

### `CDValue` — inbound control payload

```cpp
void handleControl(const char* var, CDValue v) {
    v.type();  v.isInt(); v.isFloat(); v.isBool(); v.isString();
    v.asInt(); v.asFloat(); v.asBool(); v.asString();
}
```

Cross-type conversion rules:
- `asInt()` from float → truncated; from bool → 0/1
- `asFloat()` from int → exact; from bool → 0.0/1.0
- `asBool()` from int/float → `value != 0`; from string → `true` only if `"true"`
- `asString()` from non-string → `nullptr`

> **String lifetime:** `v.asString()` points into an internal buffer overwritten on the next inbound
> message. Copy it (`strncpy`) if you need it to persist.

---

## Controls

Control writes from the dashboard arrive on the device's Anedya value-store update channel as
`{"<slot>": <value>}`. The library maps the slot back to your registered variable and invokes its
`onChange` callback.

- **`CD_ACK_AUTO`** (default): after your callback returns, the received value is reported back to the slot.
- **`CD_ACK_MANUAL`**: you call `cd.ackChange(name, actualValue)` yourself — typically after reading the
  hardware state back.

```cpp
cd.onChange("relay", handleRelay, CD_ACK_MANUAL, CD_BOOL, "float0");

void handleRelay(const char* var, CDValue v) {
    digitalWrite(RELAY_PIN, v.asBool() ? HIGH : LOW);
    cd.ackChange("relay", digitalRead(RELAY_PIN) == HIGH);
}
```

---

## Liveness

Device online/offline status is derived from the MQTT session. `setHeartbeatInterval(seconds)` maps onto
the MQTT keepalive so PubSubClient pings keep the session — and the dashboard's online indicator — alive.
The default (60 s) keeps the device comfortably inside the cloud's offline threshold; no separate heartbeat
call is needed.

---

## Sending Images

Upload a captured frame (e.g. from an ESP32-CAM) to the device's image on CircuitDigest Cloud over HTTPS.
This uses the cloud **HTTP API**, not MQTT, so it needs two things the telemetry path doesn't:

1. A **dashboard API key** (`cd_live_…`, from the dashboard → API Keys) — set with `setApiKey()`. This is
   **different** from the MQTT Connection Key.
2. A **separate** TLS client — don't reuse the one driving MQTT. The call blocks until the upload finishes.

```cpp
#include <WiFiClientSecure.h>
WiFiClientSecure httpsNet;     // separate client, just for the upload

void setup() {
    // ... usual cd.setCredentials(...) / cd.begin() for MQTT telemetry ...
    cd.setApiKey("cd_live_xxxxxxxxxxxxxxxx");   // dashboard API key
    // cd.setApiHost("www.circuitdigest.cloud", 443);  // default — override only for self-hosting
}

void uploadFrame(const uint8_t* jpeg, size_t len) {
    httpsNet.setInsecure();    // dev only — pin the CA for production
    if (cd.sendImage(httpsNet, jpeg, len, "image/jpeg")) {
        Serial.println("image uploaded");
    } else {
        Serial.printf("upload failed, err=%d\n", cd.lastError());
    }
}
```

Accepts `image/jpeg`, `image/png`, `image/gif`, `image/webp`, up to **5 MB**. The image bytes are streamed
as-is (no extra in-RAM copy). See example `06_SendImage`.

---

## Reconnect & Backoff

On disconnect the library reconnects with exponential backoff: 1 s, 2 s, 4 s, 8 s, 16 s, 30 s, … reset to
1 s on success. `loop()` never blocks.

---

## TLS Notes

Anedya MQTT requires TLS. The examples call `net.setInsecure()` for convenience, which skips certificate
validation — fine for development. For production, pin the Anedya root CA with `net.setCACert(...)`.

---

## One Instance Per Sketch

Only one `CircuitDigestCloud` instance is supported per sketch. The library uses your Physical Device ID as the MQTT
client id, so each device connects with a unique identity.

---

## Examples

| Sketch | What it shows |
|---|---|
| `01_SensorAndControl` | Publish a temperature sensor + control a GPIO from the dashboard — **start here** |
| `02_BasicSensor` | Publish a single float sensor every 5 seconds |
| `03_BasicControl` | Receive a boolean control and drive `LED_BUILTIN`, auto-ack |
| `04_AllVariableTypes` | Float / int / bool sensors and controls with their catalog slots |
| `05_ManualAckAndManyControls` | Manual ack with GPIO read-back, mixed ack modes, global fallback |
| `06_SendImage` | Upload an image to the device over HTTPS with `sendImage` (ESP32-CAM notes inside) |
| `07_ColorPicker` | Drive an RGB LED from the Color Picker widget — unpack the packed `0xRRGGBB` integer |

All examples target **ESP32 and ESP8266**. Fill in WiFi credentials, your Physical Device ID / Connection Key, and the
per-variable slots, then open Serial Monitor at 115200 baud for `[CD]` debug output.

---

## Acknowledgements

Builds on [PubSubClient](https://github.com/knolleary/pubsubclient) by **Nick O'Leary** (MIT).

## License

MIT — Copyright (c) 2026 Jobit Joseph, Circuit Digest. See [LICENSE](LICENSE).
