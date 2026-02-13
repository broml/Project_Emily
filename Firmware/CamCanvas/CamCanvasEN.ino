// === Emily's Sensory Processor (CamCanvas) v1.0 ===
// Role: Vision, Display, and Servo Control
// Hardware: ESP32-S3 Freenove, OV2640, TFT, Servos, NeoPixel

#define CONFIG_CAMERA_CONVERTERS_ENABLED 1
// --- [1. CAM DEFINITIONS] ---
#define CAMERA_MODEL_FREENOVE_S3

#if defined(CAMERA_MODEL_FREENOVE_S3)
  #define PWDN_GPIO_NUM     -1
  #define RESET_GPIO_NUM    -1
  #define XCLK_GPIO_NUM     15
  #define SIOD_GPIO_NUM     4
  #define SIOC_GPIO_NUM     5
  #define Y9_GPIO_NUM       16
  #define Y8_GPIO_NUM       17
  #define Y7_GPIO_NUM       18
  #define Y6_GPIO_NUM       12
  #define Y5_GPIO_NUM       11
  #define Y4_GPIO_NUM       10
  #define Y3_GPIO_NUM       9
  #define Y2_GPIO_NUM       8
  #define VSYNC_GPIO_NUM    6
  #define HREF_GPIO_NUM     7
  #define PCLK_GPIO_NUM     13
#else
  #error "Wrong camera model selected!"
#endif

// --- [2. FLASH/LED DEFINITIONS] ---
#include <Adafruit_NeoPixel.h>
#define LED_PIN    48  
#define NUMPIXELS  1   
Adafruit_NeoPixel pixels(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

// --- [3. SD CARD DEFINITIONS] ---
#include "FS.h"
#include "SD_MMC.h" 
#define MMC_CLK_PIN  39
#define MMC_CMD_PIN  38
#define MMC_D0_PIN   40

// --- [4. SERVO & PIN DEFINITIONS] ---
// #define TILT_SERVO_PIN 2
// #define PAN_SERVO_PIN  1
const int PAN_PIN = 1;   
const int TILT_PIN = 2;
// PWM Instellingen
const int PWM_FREQ = 50;      // 50Hz voor servo's
// const int PAN_CHANNEL = 2;    // Channel 2 (Camera gebruikt vaak 0 en 1)
// const int TILT_CHANNEL = 3;   // Channel 3
const int PWM_RESOLUTION = 14; // 14-bit resolutie

// --- [5. INCLUDES] ---
#include "esp_camera.h"
#include "img_converters.h" 
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <base64.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <TJpg_Decoder.h>
#include <TFT_eSPI.h>

// --- WIFI AP INCLUDES ---
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

// --- LED STATUS GLOBALS ---
String led_effect = "none";    // "none", "pulse", "blink"
uint8_t led_r = 0;
uint8_t led_g = 0;
uint8_t led_b = 0;
uint32_t led_speed = 1000;   
unsigned long led_last_update = 0;
bool led_override = false; 
unsigned long led_effect_end_time = 0; 

// --- TFT & SERVO OBJECTS ---
TFT_eSPI tft = TFT_eSPI(); 

// --- LEDC Method ---
#include "esp32-hal-ledc.h" // Vaak niet eens nodig, maar voor de zekerheid

// --- SERVO LIMITS ---
#define TILT_MIN_ANGLE 50
#define TILT_NEUTRAL_ANGLE 90
#define TILT_MAX_ANGLE 135

int current_pan_angle = 90;
int current_tilt_angle = TILT_NEUTRAL_ANGLE;

// --- QR Scanner Object ---
struct quirc *qr_recognizer = NULL;

// --- Network & Connectivity ---
enum ConnectivityState { WAITING_FOR_BRAIN, CONNECTED_TO_BRAIN };
ConnectivityState conn_state = WAITING_FOR_BRAIN;
unsigned long lastHeartbeatTime = 0;

// --- USER CONFIGURATION (PLACEHOLDERS) ---
const char* ssid = "";        // not required to fill in, AP will take care
const char* password = "";  // NR
const char* venice_api_key = "YOUR_VENICE_API_KEY"; // <-- CHANGE THIS

const char* vision_api_url = "https://api.venice.ai/api/v1/chat/completions";
const char* IMAGE_API_URL = "https://api.venice.ai/api/v1/image/generate";

// --- EmilyBrain Connection ---
const char* EMILYBRAIN_IP = "192.168.68.201"; 
const uint16_t EMILYBRAIN_UDP_PORT = 12345;
const uint16_t CAMCANVAS_LISTEN_PORT = 12347; 
WiFiUDP udp;

// --- WiFi AP Objects ---
WebServer server(80);
DNSServer dnsServer;
Preferences preferences;

camera_config_t config;
const char* CAM_SYSTEM_PROMPT_JSON = "You are the dedicated vision system for the robot entity Emily.\\nYou receive questions from her central reasoning core (CB).\\nYour task is to analyze the provided image to answer the core's question about the human user's environment.\\nProvide a concise, factual, and neutral description, limited to a maximum of two sentences.";

// --- Function Prototypes ---
void handleCommand(char* commandJson);
void analyzeImage(IPAddress remoteIp, uint16_t remotePort, const char* prompt, bool use_flash);
void sendUdpResponse(const IPAddress& remoteIp, uint16_t remotePort, const String& message);
void takeAndSavePhoto(IPAddress remoteIp, uint16_t remotePort);
void generateAndDisplayImage(const char* prompt, const char* model, IPAddress remoteIp, uint16_t remotePort);
void performQrScanLoop();
void updateLedEffect();
void sendHeartbeat();
bool initCamera(framesize_t frameSize);
void setupWiFi();
bool connectToWiFi(String ssid, String pass);
void startAPMode();
void handleRoot();
void handleSave();
void slowPan(int targetAngle, int move_delay_ms);
void slowTilt(int targetAngle, int move_delay_ms);
void slowNod(int targetAngle, int move_delay_ms);
static void rgb565_to_grayscale(uint16_t *src_rgb565, uint8_t *dst_grayscale, int num_pixels);

// --- TFT Callback ---
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (y >= tft.height()) return false;
  tft.pushImage(x, y, w, h, bitmap);
  return true;
}

// ===================================
// ===         SETUP               ===
// ===================================

void setup() {
    Serial.begin(115200);
    Serial.println("\n--- CamCanvas Processor v1.0 (Release) ---");

    // --- Step 1: Initialize NeoPixel ---
    pixels.begin(); 
    pixels.clear(); 
    pixels.show();  

    // --- Step 2: Initialize SD Card ---
    Serial.println("Initializing SD_MMC...");
    SD_MMC.setPins(MMC_CLK_PIN, MMC_CMD_PIN, MMC_D0_PIN);
    
    if(!SD_MMC.begin("/sdcard", true)){ 
        Serial.println("!!! SD_MMC Mount Failed! Check hardware. !!!");
    } else {
        Serial.println(">>> SD_MMC Mounted Successfully. <<<");
    }

    // --- Step 3: Initialize Camera ---
    Serial.println("Initializing Camera (SVGA)...");
    if (!initCamera(FRAMESIZE_SVGA)) { 
        Serial.println("!!! FATAL: Camera Init Failed!");
        while(true); 
    }

    // --- Step 4: Initialize Servos ---
    
    setupServos();

    // --- Step 5: Wi-Fi & Display Setup ---
    setupWiFi(); 

    // --- Step 6: Start UDP (only if WiFi connected) ---
    if (WiFi.status() == WL_CONNECTED) {
        udp.begin(CAMCANVAS_LISTEN_PORT);
        Serial.printf("UDP Server online on port %d\n", CAMCANVAS_LISTEN_PORT);
        Serial.println("Waiting for Brain PING...");
        
        tft.fillScreen(TFT_BLACK);
        tft.drawString("CamCanvas Ready.", 45, 220, 4); 
    } else {
        Serial.println("Setup paused: No WiFi. AP Mode Active.");
    }
}

// Helper voor ESP32 Core v3.x
void moveServoRaw(int pin, int angle) {
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;

    // Mapping blijft hetzelfde (SG90, 50Hz, 14-bit)
    // 0.5ms (410) tot 2.5ms (2048)
    int duty = map(angle, 0, 180, 410, 2048);
    
    // In v3.x schrijf je naar de PIN
    ledcWrite(pin, duty);
}

void setupServos() {
    Serial.println("Initializing Servos (LEDC v3 Method)...");

    // Syntax: ledcAttach(pin, freq, resolution)
    // Dit koppelt de pin automatisch aan een vrije timer/channel.
    if (!ledcAttach(PAN_PIN, PWM_FREQ, PWM_RESOLUTION)) {
        Serial.println("ERROR: Failed to attach PAN servo!");
    }
    if (!ledcAttach(TILT_PIN, PWM_FREQ, PWM_RESOLUTION)) {
        Serial.println("ERROR: Failed to attach TILT servo!");
    }

    // Startposities
    moveServoRaw(PAN_PIN, 90);
    moveServoRaw(TILT_PIN, 90);
    
    // Globals updaten
    current_pan_angle = 90;
    current_tilt_angle = 90;
    
    Serial.println("Servos Initialized.");
}
// ===================================
// ===         MAIN LOOP           ===
// ===================================

void loop() {
    // --- AP Mode Handling ---
    if (WiFi.status() != WL_CONNECTED) {
        server.handleClient(); 
        dnsServer.processNextRequest(); 
        delay(10);
        return; 
    }

    // --- Standard Operation ---
    const int UDP_BUFFER_SIZE = 2048; 
    char packetBuffer[UDP_BUFFER_SIZE];
    
    // 1. Check UDP
    while (udp.parsePacket() > 0) {
        int len = udp.read(packetBuffer, UDP_BUFFER_SIZE - 1);
        if (len > 0) {
            packetBuffer[len] = 0;
        }
        
        if (strcmp(packetBuffer, "PING") == 0) {
            if (conn_state == WAITING_FOR_BRAIN) {
                Serial.println("PING received from Brain. Connected.");
                conn_state = CONNECTED_TO_BRAIN;
            }
            return; 
        }

        Serial.printf("\nCommand received from %s\n", udp.remoteIP().toString().c_str());
        handleCommand(packetBuffer); 
    }
    
    // 2. Heartbeat
    sendHeartbeat(); 

    // 3. LED Effects
    updateLedEffect(); 
}


// ===================================
// ===         FUNCTIONS           ===
// ===================================

void handleCommand(char* commandJson) {
  JsonDocument doc; 
  DeserializationError error = deserializeJson(doc, commandJson);

  if (error) {
    Serial.println("JSON Deserialization Error");
    return;
  }
  const char* command = doc["command"];

  if (strcmp(command, "take_picture") == 0) {
    Serial.println("Action: take_picture");
    takeAndSavePhoto(udp.remoteIP(), udp.remotePort());
  }
  
  else if (strcmp(command, "analyze_vision") == 0) {
    const char* prompt = doc["prompt"];
    bool use_flash = (doc["use_flash"] == 1); 
    Serial.printf("Action: analyze_vision. Prompt: %s\n", prompt);
    analyzeImage(udp.remoteIP(), udp.remotePort(), prompt, use_flash);
  }

  else if (command && strcmp(command, "generate_image") == 0) {
      const char* prompt = doc["prompt"];
      const char* model = doc["model"] | "venice-sd35";
      Serial.printf("Action: generate_image. Model: %s\n", model);
      if (prompt) {
        generateAndDisplayImage(prompt, model, udp.remoteIP(), udp.remotePort());
      }
  }

  else if (command && strcmp(command, "move_head") == 0) {
      int pan_target = doc["pan"] | current_pan_angle;
      int tilt_target = doc["tilt"] | current_tilt_angle;
      
      // Safety Clamp
      tilt_target = constrain(tilt_target, TILT_MIN_ANGLE, TILT_MAX_ANGLE);

      Serial.printf("Action: move_head. Target Pan: %d, Tilt: %d\n", pan_target, tilt_target);
      slowPan(pan_target, 15);  
      slowTilt(tilt_target, 15);
  }

  else if (command && strcmp(command, "nod_head") == 0) {
      int angle = doc["angle"] | 105; 
      angle = constrain(angle, TILT_MIN_ANGLE, TILT_MAX_ANGLE);
      
      Serial.printf("Action: nod_head. Angle: %d\n", angle);
      slowNod(angle, 25);
  }

  

  else if (command && strcmp(command, "set_status_led") == 0) {
      led_r = doc["r"] | 0;
      led_g = doc["g"] | 0;
      led_b = doc["b"] | 0;
      led_effect = doc["effect"] | "none"; 
      led_speed = doc["speed"] | 1000;   
      
      led_last_update = millis(); 
      led_override = false;     

      if (led_r > 0 || led_g > 0 || led_b > 0 || led_effect != "none") {
          led_effect_end_time = millis() + 5000; // 30s failsafe
          Serial.printf("LED set: %s (5s timeout)\n", led_effect.c_str());
      } else {
          led_effect_end_time = 0; 
          Serial.println("LED set: OFF (permanent)");
      }
  }
  else {
      Serial.printf("Unknown command: %s\n", command);
  }
}

void sendHeartbeat() {
  if (conn_state != CONNECTED_TO_BRAIN) {
    return; 
  }
  
  if (millis() - lastHeartbeatTime > 1500) {
    JsonDocument heartbeat_doc;
    heartbeat_doc["id"] = "camcanvas_v1"; 
    heartbeat_doc["state"] = "Ready"; 
    heartbeat_doc["tilt_angle"] = current_tilt_angle;
    heartbeat_doc["pan_angle"] = current_pan_angle;
    String heartbeat_string;
    serializeJson(heartbeat_doc, heartbeat_string);
    
    udp.beginPacket(EMILYBRAIN_IP, EMILYBRAIN_UDP_PORT);
    udp.print(heartbeat_string);
    udp.endPacket();
    
    lastHeartbeatTime = millis();
  }
}


// --- VISION ANALYSIS ---
void analyzeImage(IPAddress remoteIp, uint16_t remotePort, const char* prompt, bool use_flash) {
  
  if (use_flash) {
    led_override = true; 
    pixels.setPixelColor(0, pixels.Color(255, 255, 255));
    pixels.show();
    delay(250); 
  }

  Serial.println("Capturing raw YUV frame...");
  camera_fb_t * fb = esp_camera_fb_get();
  
  if (use_flash) {
    pixels.clear(); 
    pixels.show();
    led_override = false; 
  }
  
  if (!fb) {
    Serial.println("!!! Capture Failed!");
    sendUdpResponse(remoteIp, remotePort, "{\"status\":\"error\", \"message\":\"Camera capture failed\"}");
    return;
  }

  Serial.println("Converting to JPEG...");
  size_t out_len;
  uint8_t * out_buf;
  bool jpeg_converted = fmt2jpg(fb->buf, fb->len, fb->width, fb->height, fb->format, 80, &out_buf, &out_len);
  esp_camera_fb_return(fb); 

  if (!jpeg_converted) {
    Serial.println("!!! JPEG Conversion Failed.");
    sendUdpResponse(remoteIp, remotePort, "{\"status\":\"error\", \"message\":\"Software JPEG conversion failed\"}");
    return;
  }

  // Display Preview
  tft.fillScreen(TFT_BLACK);
  TJpgDec.setJpgScale(4); 
  TJpgDec.setSwapBytes(true); 
  TJpgDec.setCallback(tft_output);
  int16_t x_offset = (tft.width() - (fb->width / 4)) / 2;
  int16_t y_offset = (tft.height() - (fb->height / 4)) / 2;
  TJpgDec.drawJpg(x_offset, y_offset, out_buf, out_len);
  tft.drawString("Analyzing...", 20, 20, 2);
  
  // Base64 Encode
  Serial.println("Encoding Base64...");
  String base64Image = base64::encode(out_buf, out_len); 
  free(out_buf); 
  
  if(base64Image.length() == 0){
    Serial.println("!!! Base64 Failed.");
    sendUdpResponse(remoteIp, remotePort, "{\"status\":\"error\", \"message\":\"Base64 encoding failed\"}");
    tft.fillScreen(TFT_BLACK);
    return;
  }

  // Build JSON
  Serial.println("Building Payload...");
  String payload_string = "{";
  payload_string += "\"model\": \"mistral-31-24b\",";
  payload_string += "\"messages\": [";
  payload_string += "  { \"role\": \"system\", \"content\": \"" + String(CAM_SYSTEM_PROMPT_JSON) + "\" },";
  payload_string += "  { \"role\": \"user\", \"content\": [";
  payload_string += "    { \"type\": \"text\", \"text\": \"" + String(prompt) + "\" },";
  payload_string += "    { \"type\": \"image_url\", \"image_url\": { \"url\": \"data:image/jpeg;base64," + base64Image + "\" }}";
  payload_string += "  ]}";
  payload_string += "]}";
  
  base64Image = ""; // Free RAM

  // Send Request
  Serial.println("Sending to Venice API...");
  WiFiClientSecure client;
  HTTPClient http;
  client.setInsecure(); 

  if (http.begin(client, vision_api_url)) {
    http.addHeader("Authorization", String("Bearer ") + venice_api_key);
    http.addHeader("Content-Type", "application/json");

    int httpResponseCode = http.POST(payload_string);
    
    if (httpResponseCode == 200) {
      String response_payload = http.getString();
      Serial.println("Vision response received.");
      
      JsonDocument doc; 
      deserializeJson(doc, response_payload);
      const char* description = doc["choices"][0]["message"]["content"];
      
      JsonDocument udp_response_doc;
      udp_response_doc["status"] = "success";
      udp_response_doc["result_type"] = "vision";
      udp_response_doc["description"] = description;
      
      String clean_response_string;
      serializeJson(udp_response_doc, clean_response_string);
      sendUdpResponse(remoteIp, remotePort, clean_response_string);
    } else {
      Serial.printf("HTTP Error: %d\n", httpResponseCode);
      sendUdpResponse(remoteIp, remotePort, "{\"status\":\"error\", \"message\":\"Vision API call failed\"}");
    }
    http.end();
  } else {
      sendUdpResponse(remoteIp, remotePort, "{\"status\":\"error\", \"message\":\"HTTP client begin failed\"}");
  }

  tft.fillScreen(TFT_BLACK);
  tft.drawString("CamCanvas Ready.", 45, 220, 4);
}

// --- PHOTO TAKING ---
void takeAndSavePhoto(IPAddress remoteIp, uint16_t remotePort) {
  Serial.println("Taking Photo for SD...");
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    sendUdpResponse(remoteIp, remotePort, "{\"status\":\"error\", \"message\":\"Camera capture failed\"}");
    return;
  }
  
  size_t out_len;
  uint8_t * out_buf;
  bool jpeg_converted = fmt2jpg(fb->buf, fb->len, fb->width, fb->height, fb->format, 80, &out_buf, &out_len);
  esp_camera_fb_return(fb); 

  if (!jpeg_converted) {
    sendUdpResponse(remoteIp, remotePort, "{\"status\":\"error\", \"message\":\"Software JPEG conversion failed\"}");
    return; 
  }

  // Preview
  tft.fillScreen(TFT_BLACK);
  TJpgDec.setJpgScale(4); 
  TJpgDec.setSwapBytes(true); 
  TJpgDec.setCallback(tft_output);
  int16_t x_offset = (tft.width() - (fb->width / 4)) / 2;
  int16_t y_offset = (tft.height() - (fb->height / 4)) / 2;
  TJpgDec.drawJpg(x_offset, y_offset, out_buf, out_len);

  // Save
  String path = "/photo_" + String(millis()) + ".jpg";
  File file = SD_MMC.open(path.c_str(), FILE_WRITE);
  
  JsonDocument udp_response_doc;
  udp_response_doc["id"] = "vision_cortex_v2_S3";
  udp_response_doc["result_type"] = "picture_taken";

  if (file) {
    file.write(out_buf, out_len); 
    file.close();
    Serial.printf("Saved: %s\n", path.c_str());
    udp_response_doc["status"] = "success";
    udp_response_doc["path"] = path;
  } else {
    Serial.println("!!! SD Write Failed.");
    udp_response_doc["status"] = "error";
    udp_response_doc["message"] = "Failed to save photo to SD card.";
  }

  String response_string;
  serializeJson(udp_response_doc, response_string);
  sendUdpResponse(remoteIp, remotePort, response_string);

  delay(3000); 
  
  tft.fillScreen(TFT_BLACK);
  tft.drawString("CamCanvas Ready.", 45, 220, 4);
  free(out_buf);
}


// --- SERVO MOVEMENT (LEDC / PWM VERSION) ---

void slowPan(int targetAngle, int move_delay_ms) {
    int startAngle = current_pan_angle; 
    
    if (targetAngle > startAngle) {
        for (int angle = startAngle; angle <= targetAngle; angle++) {
            moveServoRaw(PAN_PIN, angle); // <--- PIN meegeven
            current_pan_angle = angle;        
            delay(move_delay_ms);
        }
    } else {
        for (int angle = startAngle; angle >= targetAngle; angle--) {
            moveServoRaw(PAN_PIN, angle); // <--- PIN meegeven
            current_pan_angle = angle;
            delay(move_delay_ms);
        }
    }
}

void slowTilt(int targetAngle, int move_delay_ms) {
    int startAngle = current_tilt_angle; 
    
    if (targetAngle > startAngle) {
        for (int angle = startAngle; angle <= targetAngle; angle++) {
            moveServoRaw(TILT_PIN, angle); // <--- PIN meegeven
            current_tilt_angle = angle;
            delay(move_delay_ms);
        }
    } else {
        for (int angle = startAngle; angle >= targetAngle; angle--) {
            moveServoRaw(TILT_PIN, angle); // <--- PIN meegeven
            current_tilt_angle = angle;
            delay(move_delay_ms);
        }
    }
}

void slowNod(int targetAngle, int move_delay_ms) {
    int startAngle = current_tilt_angle; 
    
    // Stap 1: Naar target
    if (targetAngle < startAngle) {
        for (int angle = startAngle; angle >= targetAngle; angle--) {
            moveServoRaw(TILT_PIN, angle); // <--- PIN meegeven
            current_tilt_angle = angle; 
            delay(move_delay_ms);
        }
    } else {
         for (int angle = startAngle; angle <= targetAngle; angle++) {
            moveServoRaw(TILT_PIN, angle); // <--- PIN meegeven
            current_tilt_angle = angle; 
            delay(move_delay_ms);
        }
    }

    delay(100); 

    // Stap 2: Terug
    if (targetAngle < startAngle) {
        for (int angle = targetAngle; angle <= startAngle; angle++) {
            moveServoRaw(TILT_PIN, angle); // <--- PIN meegeven
            current_tilt_angle = angle; 
            delay(move_delay_ms);
        }
    } else {
        for (int angle = targetAngle; angle >= startAngle; angle--) {
            moveServoRaw(TILT_PIN, angle); // <--- PIN meegeven
            current_tilt_angle = angle; 
            delay(move_delay_ms);
        }
    }
}

// --- IMAGE GENERATION ---
void generateAndDisplayImage(const char* prompt, const char* model, IPAddress remoteIp, uint16_t remotePort) {
  tft.fillScreen(TFT_BLACK);
  tft.drawString("Generating image...", 20, 20, 2);
  Serial.println("Generating image...");

  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();

  if (http.begin(client, IMAGE_API_URL)) {
    http.addHeader("Authorization", "Bearer " + String(venice_api_key));
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(45000);

    JsonDocument payload_doc;
    payload_doc["model"] = model; 
    payload_doc["safe_mode"] = false;
    payload_doc["lora_strength"] = 100;
    payload_doc["cfg_scale"] = 8.5;
    payload_doc["prompt"] = prompt;
    payload_doc["width"] = 512;
    payload_doc["height"] = 512;
    payload_doc["format"] = "jpeg";
    payload_doc["return_binary"] = true;
    payload_doc["steps"] = 25;
    
    String payload_string;
    serializeJson(payload_doc, payload_string);
    int httpCode = http.POST(payload_string);

        if (httpCode == HTTP_CODE_OK) {
        Serial.println("Image receiving...");
        
        int imageSize = http.getSize();
        if (imageSize <= 0) {
            http.end(); return;
        }

        uint8_t *jpg_buffer = (uint8_t *) malloc(imageSize);
        if (!jpg_buffer) {
            Serial.println("ERROR: Malloc failed!");
            http.end(); return;
        }

        // Chunked Download Logic
        WiFiClient* stream = http.getStreamPtr();
        int bytes_read = 0;
        while(bytes_read < imageSize) {
            if (stream->available()) {
                bytes_read += stream->read(jpg_buffer + bytes_read, imageSize - bytes_read);
            }
            delay(1); 
        }
        
        Serial.println("Displaying...");
        tft.fillScreen(TFT_BLACK);
        
        TJpgDec.setSwapBytes(true);
        TJpgDec.setJpgScale(2); // 512px -> 256px
        TJpgDec.setCallback(tft_output);
        
        int16_t x_offset = (tft.width() - 256) / 2;
        int16_t y_offset = (tft.height() - 256) / 2;
        TJpgDec.drawJpg(x_offset, y_offset, jpg_buffer, imageSize);
        
        free(jpg_buffer);
        Serial.println("Done.");

    } else {
      Serial.printf("API Error: %d\n", httpCode);
      tft.drawString("Error: API Call Failed", 20, 50, 2);
    }
    http.end();
  } else {
    tft.drawString("Error: Connection Failed", 20, 50, 2);
  }

  // Confirmation
  JsonDocument confirmation_doc;
  confirmation_doc["id"] = "camcanvas_v1";
  confirmation_doc["result_type"] = "image_complete";
  
  String confirmation_string;
  serializeJson(confirmation_doc, confirmation_string);
  sendUdpResponse(remoteIp, remotePort, confirmation_string);
  
  delay(8000); // Show image for 8 seconds
  
  tft.fillScreen(TFT_BLACK);
  tft.drawString("CamCanvas Ready.", 45, 220, 4);
}

// --- LED LOGIC ---
void updateLedEffect() {
    if (led_effect_end_time != 0 && millis() > led_effect_end_time) {
        led_effect = "none";
        led_r = 0; led_g = 0; led_b = 0;
        led_effect_end_time = 0; 
    }
    if (led_override) {
        return; 
    }

    float brightness = 1.0;
    bool on = true;

    if (led_effect == "pulse") {
        brightness = (sin(millis() * 6.283 / led_speed) + 1.0) / 2.0; 
    } else if (led_effect == "blink") {
        on = (millis() % led_speed) < (led_speed / 2);
    }

    if (on) {
        pixels.setPixelColor(0, pixels.Color(led_r * brightness, led_g * brightness, led_b * brightness));
    } else {
        pixels.clear();
    }
    pixels.show();
}

static void rgb565_to_grayscale(uint16_t *src_rgb565, uint8_t *dst_grayscale, int num_pixels) {
    for (int i = 0; i < num_pixels; i++) {
        uint16_t pixel_swapped = src_rgb565[i];
        uint16_t pixel = (pixel_swapped << 8) | (pixel_swapped >> 8); // Byte Swap

        uint8_t r = (pixel >> 11) & 0x1F; 
        uint8_t g = (pixel >> 5) & 0x3F;  
        uint8_t b = (pixel >> 0) & 0x1F;  

        r = (r * 255) / 31;
        g = (g * 255) / 63;
        b = (b * 255) / 31;

        uint8_t gray = (r * 299 + g * 587 + b * 114) / 1000;
        dst_grayscale[i] = gray;
    }
}

bool initCamera(framesize_t frameSize) {
  esp_camera_deinit();

  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
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
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000; 
  
  if (frameSize == FRAMESIZE_SVGA) {
    config.pixel_format = PIXFORMAT_YUV422; 
  } else {
    config.pixel_format = PIXFORMAT_RGB565; 
  }
  config.frame_size = frameSize;
  config.jpeg_quality = 12; 
  config.fb_location = CAMERA_FB_IN_PSRAM; 
  config.fb_count = 2; 
  config.grab_mode = CAMERA_GRAB_LATEST; 

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("!!! Camera Init Failed 0x%x !!!\n", err);
    return false;
  } 

  sensor_t *s = esp_camera_sensor_get();
  if (s != NULL) {
    s->set_hmirror(s, 1); 
  }

  Serial.printf(">>> Camera Initialized (Mode: %d) <<<\n", frameSize);
  return true;
}

void sendUdpResponse(const IPAddress& remoteIp, uint16_t remotePort, const String& message) {
  udp.beginPacket(remoteIp, remotePort);
  udp.print(message); 
  udp.endPacket();
  Serial.printf("Response sent: %s\n", message.c_str());
}

// --- WIFI HELPER FUNCTIONS ---

void setupWiFi() {
    preferences.begin("camcanvas-wifi", false); 
    String stored_ssid = preferences.getString("ssid", "");
    String stored_pass = preferences.getString("pass", "");
    preferences.end();

    if (stored_ssid.length() > 0) {
        Serial.println("Initializing Display...");
        tft.init();
        tft.setRotation(0); 
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.setCursor(10, 10);
        tft.setTextSize(2);
        tft.println("Connecting to:");
        tft.println(stored_ssid);

        if (connectToWiFi(stored_ssid, stored_pass)) {
            Serial.println("Connection successful.");
            tft.fillScreen(TFT_BLACK);
            tft.setCursor(10, 10);
            tft.setTextSize(2);
            tft.println("Connected!");
            tft.setTextSize(1);
            tft.setCursor(10, 40);
            tft.println(WiFi.localIP().toString());
            delay(1500);
            return; 
        }
        Serial.println("Stored WiFi Connection failed.");
        tft.fillScreen(TFT_RED);
        tft.setCursor(10, 10);
        tft.setTextSize(2);
        tft.println("Connect Failed");
        delay(1000);
    } else {
        Serial.println("No WiFi credentials found.");
    }
    
    startAPMode();
}

bool connectToWiFi(String ssid, String pass) {
    WiFi.mode(WIFI_STA);

    // Static IP for CamCanvas
    IPAddress staticIP(192, 168, 68, 203); 
    IPAddress gateway(192, 168, 68, 1);
    IPAddress subnet(255, 255, 255, 0);
    IPAddress primaryDNS(8, 8, 8, 8);
    WiFi.config(staticIP, gateway, subnet, primaryDNS);

    WiFi.begin(ssid.c_str(), pass.c_str());
    Serial.print("Connecting to " + ssid);
    unsigned long startTijd = millis();

    while (WiFi.status() != WL_CONNECTED && millis() - startTijd < 15000) {
        delay(500); Serial.print(".");
    }
    return (WiFi.status() == WL_CONNECTED);
}

void startAPMode() {
    Serial.println("Starting AP Mode ('CamCanvas_Setup')...");

    WiFi.softAP("CamCanvas_Setup", "12345678"); 
    IPAddress AP_IP(192, 168, 4, 1);
    WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
    dnsServer.start(53, "*", AP_IP);
    delay(500); 

    tft.init(); 
    tft.setRotation(0); 
    tft.fillScreen(TFT_BLACK);
    
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(10, 10);
    tft.setTextSize(2);
    tft.println("Setup Mode");
    tft.setTextSize(1);
    tft.setCursor(10, 40);
    tft.println("1. Connect to WiFi:");
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.println("   'CamCanvas_Setup'");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.println("2. Password:");
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.println("   '12345678'");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.println("3. Go to 192.168.4.1");

    server.onNotFound([](){ handleRoot(); });
    server.on("/", HTTP_GET, [](){ handleRoot(); });
    server.on("/save", HTTP_POST, [](){ handleSave(); });
    server.begin(); 
    Serial.println("AP Web Server started.");
}

void handleRoot() {
    String html = R"(
<!DOCTYPE html><html><head><title>CamCanvas WiFi Setup</title>
<meta name='viewport' content='width=device-width, initial-scale=1'>
<style>
body { font-family: Arial, sans-serif; background: #222; color: #fff; text-align: center; padding: 20px; }
div { background: #333; border-radius: 10px; padding: 20px; max-width: 400px; margin: auto; }
input { width: 90%; padding: 10px; margin: 10px 0; border: none; border-radius: 5px; }
button { background: #007bff; color: white; padding: 12px 20px; border: none; border-radius: 5px; cursor: pointer; font-size: 16px; }
</style></head><body>
<div>
<h2>CamCanvas WiFi Setup</h2>
<p>Enter your WiFi details:</p>
<form action='/save' method='POST'>
<input type='text' name='ssid' placeholder='WiFi Name (SSID)' required><br>
<input type='password' name='pass' placeholder='Password' required><br>
<button type='submit'>Save & Reboot</button>
</form></div></body></html>
)";
    server.send(200, "text/html", html);
}

void handleSave() {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    preferences.begin("camcanvas-wifi", false); 
    preferences.putString("ssid", ssid);
    preferences.putString("pass", pass);
    preferences.end();

    String html = R"(
<!DOCTYPE html><html><head><title>Saved</title>
<meta name='viewport' content='width=device-width, initial-scale=1'>
<style>body { font-family: Arial, sans-serif; background: #222; color: #fff; text-align: center; padding: 50px; }</style>
</head><body>
<h2>Saved!</h2>
<p>CamCanvas is rebooting and will connect to your network...</p>
</body></html>
)";
    server.send(200, "text/html", html);
    delay(1000);
    ESP.restart();
}
