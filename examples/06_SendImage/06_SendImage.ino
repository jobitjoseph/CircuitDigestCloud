// 06_SendImage — upload an image to a device on CircuitDigest Cloud over HTTPS.
//
// sendImage() uses the cloud HTTP API (not MQTT), so it needs:
//   1) a dashboard API key (cd_live_...)  — setApiKey()      [≠ the MQTT Connection Key]
//   2) a SEPARATE TLS client for the upload — never reuse the MQTT transport.
//
// This sketch sends a tiny 1x1 JPEG on a button-style interval so you can see the flow
// end to end. For a real ESP32-CAM, capture a frame and pass its buffer instead — see
// the ESP32-CAM section at the bottom.
//
// Targets ESP32 / ESP8266. Open Serial Monitor at 115200 for [CD] debug output.

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <CircuitDigestCloud.h>

// ── Fill these in ───────────────────────────────────────────────────────────
const char* WIFI_SSID = "your-ssid";
const char* WIFI_PASS = "your-password";

const char* DEVICE_ID       = "your-physical-device-id";   // from the device setup panel
const char* CONNECTION_KEY  = "your-connection-key";        // MQTT password
const char* API_KEY         = "cd_live_xxxxxxxxxxxxxxxx";   // dashboard API key (for uploads)
// ────────────────────────────────────────────────────────────────────────────

WiFiClientSecure mqttNet;   // TLS transport for MQTT telemetry (port 8883)
WiFiClientSecure httpsNet;  // SEPARATE TLS client, only for sendImage (port 443)
CircuitDigestCloud cd(mqttNet);

// Re-apply TLS config after the library stops the MQTT transport between connects —
// WiFiClientSecure loses setInsecure()/setCACert() on stop(), so without this the
// next TLS handshake fails (PubSubClient state=-2).
void resetTransport() {
  mqttNet.stop();
  mqttNet.setInsecure();   // dev only — pin the Anedya CA in production
}

// A minimal valid 1x1 white JPEG, just so the example uploads something real.
static const uint8_t SAMPLE_JPEG[] = {
    0xFF,0xD8,0xFF,0xE0,0x00,0x10,0x4A,0x46,0x49,0x46,0x00,0x01,0x01,0x00,0x00,0x01,
    0x00,0x01,0x00,0x00,0xFF,0xDB,0x00,0x43,0x00,0x08,0x06,0x06,0x07,0x06,0x05,0x08,
    0x07,0x07,0x07,0x09,0x09,0x08,0x0A,0x0C,0x14,0x0D,0x0C,0x0B,0x0B,0x0C,0x19,0x12,
    0x13,0x0F,0x14,0x1D,0x1A,0x1F,0x1E,0x1D,0x1A,0x1C,0x1C,0x20,0x24,0x2E,0x27,0x20,
    0x22,0x2C,0x23,0x1C,0x1C,0x28,0x37,0x29,0x2C,0x30,0x31,0x34,0x34,0x34,0x1F,0x27,
    0x39,0x3D,0x38,0x32,0x3C,0x2E,0x33,0x34,0x32,0xFF,0xC0,0x00,0x0B,0x08,0x00,0x01,
    0x00,0x01,0x01,0x01,0x11,0x00,0xFF,0xC4,0x00,0x1F,0x00,0x00,0x01,0x05,0x01,0x01,
    0x01,0x01,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x02,0x03,0x04,
    0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0xFF,0xC4,0x00,0x14,0x10,0x01,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xDA,0x00,0x08,
    0x01,0x01,0x00,0x00,0x3F,0x00,0xD2,0xCF,0x20,0xFF,0xD9
};

void setup() {
    Serial.begin(115200);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) { delay(200); Serial.print('.'); }
    Serial.println("\nWiFi connected");

    mqttNet.setInsecure();   // dev only — pin the Anedya CA in production
    cd.setTransportResetCallback(resetTransport); // re-apply TLS after transport stops

    cd.setCredentials(DEVICE_ID, CONNECTION_KEY);
    cd.setApiKey(API_KEY);   // enables sendImage()
    cd.setDebug(&Serial);
    // Heartbeat is automatic — pings Anedya every 60s to stay shown "online".
    // cd.setHeartbeatInterval(30);   // optional: change cadence (5s floor; 0 disables). See example 08.
    cd.begin();
}

void loop() {
    cd.loop();

    static uint32_t last = 0;
    if (millis() - last > 30000) {   // upload every 30 s
        last = millis();
        httpsNet.setInsecure();      // dev only — pin the CA for production
        bool ok = cd.sendImage(httpsNet, SAMPLE_JPEG, sizeof(SAMPLE_JPEG), "image/jpeg");
        Serial.printf("sendImage: %s (err=%d)\n", ok ? "OK" : "FAILED", cd.lastError());
    }
}

// ── ESP32-CAM: send a real frame ────────────────────────────────────────────
// #include "esp_camera.h"
// void uploadCameraFrame() {
//     camera_fb_t* fb = esp_camera_fb_get();
//     if (!fb) { Serial.println("capture failed"); return; }
//     httpsNet.setInsecure();
//     bool ok = cd.sendImage(httpsNet, fb->buf, fb->len, "image/jpeg");
//     Serial.printf("sendImage: %s (err=%d)\n", ok ? "OK" : "FAILED", cd.lastError());
//     esp_camera_fb_return(fb);
// }
