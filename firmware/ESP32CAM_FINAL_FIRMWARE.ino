#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>

// ===== AI-Thinker camera pin map =====
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

#define PIR_PIN 13
#define LED_GREEN 14    // idle / no motion
#define LED_RED 15      // motion detected
#define BUZZER_PIN 12   // moved off GPIO16 — that pin is reserved for PSRAM on this board

// ===== Your hotspot credentials =====
const char* ssid = "Redmi 12 5G";
const char* password = "Rootuser1";

// ===== Static IP config — matches confirmed hotspot subnet 10.203.29.x =====
IPAddress local_IP(10, 203, 29, 184);
IPAddress gateway(10, 203, 29, 35);
IPAddress subnet(255, 255, 255, 0);

// ===== Python face recognition server (Kali VM, static IP) =====
const char* serverUrl = "http://10.203.29.40:5000/upload";

WebServer server(80);

// Buffer holding the last captured JPEG, so it can be served repeatedly
uint8_t* lastImage = nullptr;
size_t lastImageLen = 0;
bool motionCaptured = false;
int captureCount = 0;
unsigned long lastCaptureTime = 0;
const unsigned long captureCooldown = 5000; // 5 sec between captures

const char PAGE_TEMPLATE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Motion Capture</title>
  <style>
    body { font-family: Arial, sans-serif; text-align: center; background: #111; color: #eee; padding: 20px; }
    h1 { color: #4fd8ff; }
    .status { font-size: 1.6em; margin: 15px 0; padding: 15px; border-radius: 12px; }
    .motion-yes { background: #1e5a2e; color: #7dffa0; }
    .motion-no { background: #333; color: #999; }
    .count { font-size: 1em; color: #aaa; margin-bottom: 15px; }
    img { max-width: 100%; border-radius: 8px; border: 2px solid #333; }
    .placeholder { color: #666; padding: 60px 0; }
  </style>
</head>
<body>
  <h1>Motion Detection - Live Capture</h1>
  <div id="status" class="status motion-no">Waiting for motion...</div>
  <div class="count" id="count">Captures: 0</div>
  <div id="imgbox">
    <div class="placeholder">No image captured yet</div>
  </div>
  <script>
    let lastCount = -1;
    function poll() {
      fetch('/status').then(r => r.json()).then(data => {
        const el = document.getElementById('status');
        if (data.motion) {
          el.textContent = "MOTION CAPTURED";
          el.className = "status motion-yes";
        } else {
          el.textContent = "Waiting for motion...";
          el.className = "status motion-no";
        }
        document.getElementById('count').textContent = "Captures: " + data.count;

        if (data.count !== lastCount && data.count > 0) {
          lastCount = data.count;
          document.getElementById('imgbox').innerHTML =
            '<img src="/image?t=' + Date.now() + '">';
        }
      });
    }
    setInterval(poll, 500);
    poll();
  </script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send(200, "text/html", PAGE_TEMPLATE);
}

void handleStatus() {
  String json = "{\"motion\":";
  json += motionCaptured ? "true" : "false";
  json += ",\"count\":";
  json += captureCount;
  json += "}";
  server.send(200, "application/json", json);
}

void handleImage() {
  if (lastImage == nullptr || lastImageLen == 0) {
    server.send(404, "text/plain", "No image yet");
    return;
  }
  server.send_P(200, "image/jpeg", (const char*)lastImage, lastImageLen);
}

bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size   = FRAMESIZE_VGA;
    config.jpeg_quality  = 10;
    config.fb_count      = 2;
    config.fb_location   = CAMERA_FB_IN_PSRAM;
    config.grab_mode     = CAMERA_GRAB_WHEN_EMPTY;
  } else {
    config.frame_size   = FRAMESIZE_SVGA;
    config.jpeg_quality  = 12;
    config.fb_count      = 1;
    config.grab_mode     = CAMERA_GRAB_WHEN_EMPTY;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return false;
  }
  return true;
}

void captureOnMotion() {
  // discard one stale frame so we get a fresh one
  camera_fb_t* stale = esp_camera_fb_get();
  if (stale) esp_camera_fb_return(stale);

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Capture failed");
    return;
  }

  // free old buffer, allocate new one, copy frame data into it
  if (lastImage != nullptr) {
    free(lastImage);
    lastImage = nullptr;
  }

  lastImage = (uint8_t*)malloc(fb->len);
  if (lastImage != nullptr) {
    memcpy(lastImage, fb->buf, fb->len);
    lastImageLen = fb->len;
    captureCount++;
    Serial.printf("Motion captured! Image #%d, %u bytes\n", captureCount, fb->len);

    // Send this same image to the Python face recognition server
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      http.begin(serverUrl);
      http.addHeader("Content-Type", "image/jpeg");
      int httpResponseCode = http.POST(lastImage, lastImageLen);

      if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.printf("Server response (%d): %s\n", httpResponseCode, response.c_str());
      } else {
        Serial.printf("POST failed, error: %s\n", http.errorToString(httpResponseCode).c_str());
      }
      http.end();
    } else {
      Serial.println("WiFi not connected — cannot send to server");
    }
  } else {
    Serial.println("Failed to allocate memory for image");
    lastImageLen = 0;
  }

  esp_camera_fb_return(fb);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(PIR_PIN, INPUT_PULLDOWN);  // extra noise immunity on the signal line

  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_RED, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  if (!initCamera()) {
    Serial.println("Halting — camera init failed");
    return;
  }
  Serial.println("Camera init OK");

  WiFi.disconnect(true, true);
  delay(1000);
  WiFi.mode(WIFI_STA);
  delay(100);

  if (!WiFi.config(local_IP, gateway, subnet)) {
    Serial.println("Static IP config failed");
  }
  delay(100);

  WiFi.setSleep(false);
  WiFi.begin(ssid, password);

  Serial.print("Connecting to hotspot");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected! IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi connection failed");
  }

  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/image", handleImage);
  server.begin();

  Serial.println("Web server started — open http://10.203.29.184");
  Serial.println("PIR warming up for 45 seconds — ignoring motion during this time...");

  unsigned long warmupStart = millis();
  while (millis() - warmupStart < 45000) {
    server.handleClient();   // keep webpage responsive during warm-up
    delay(50);
  }

  Serial.println("Warm-up complete. System ready — waiting for motion...");

  // Green LED on by default — idle / no motion state
  digitalWrite(LED_GREEN, HIGH);
}

void loop() {
  server.handleClient();

  int reading = digitalRead(PIR_PIN);

  if (reading == HIGH) {
    // Debounce: require PIR to stay HIGH continuously for 400ms before
    // treating it as real motion, filtering out brief electrical glitches
    unsigned long stableStart = millis();
    bool stillHigh = true;
    while (millis() - stableStart < 400) {
      if (digitalRead(PIR_PIN) == LOW) {
        stillHigh = false;
        break;
      }
      delay(10);
    }

    if (stillHigh) {
      unsigned long now = millis();
      if (now - lastCaptureTime > captureCooldown) {
        motionCaptured = true;

        // Motion state indicators: red on, green off, short buzzer beep
        digitalWrite(LED_GREEN, LOW);
        digitalWrite(LED_RED, HIGH);
        digitalWrite(BUZZER_PIN, HIGH);
        delay(150);
        digitalWrite(BUZZER_PIN, LOW);

        captureOnMotion();
        lastCaptureTime = now;
      }
    }
  } else {
    motionCaptured = false;
    // Idle state: green on, red off
    digitalWrite(LED_RED, LOW);
    digitalWrite(LED_GREEN, HIGH);
  }
}
