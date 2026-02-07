// === Emily's InputPad v1.0 ===
// Role: External Input Device (Buttons, Dice, Choices)
// Hardware: ESP32, TFT Display, 3 Buttons

#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h> // Ensure User_Setup.h matches your hardware!
#include <SPI.h>

// --- USER CONFIGURATION ---
const char* WIFI_SSID = "YOUR_WIFI_SSID";     // <-- CHANGE THIS
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD"; // <-- CHANGE THIS

// EmilyBrain Address (Target)
const char* EMILYBRAIN_IP = "192.168.68.201"; 
const int EMILYBRAIN_PORT = 12345;

// This Device (Source)
// NOTE: Adjust Gateway/Subnet to match your local network!
IPAddress INPUTPAD_STATIC_IP(192, 168, 68, 205); 
IPAddress GATEWAY(192, 168, 68, 1);
IPAddress SUBNET(255, 255, 255, 0);
IPAddress PRIMARY_DNS(8, 8, 8, 8);
const int INPUTPAD_LISTEN_PORT = 12349;

// --- HARDWARE PINS ---
// Ensure these do not conflict with your TFT pins defined in User_Setup.h
#define BUTTON_A_PIN 13  
#define BUTTON_B_PIN 14  
#define BUTTON_C_PIN 19  

// --- OBJECTS ---
TFT_eSPI tft = TFT_eSPI();
WiFiUDP udp;

// --- GLOBALS ---
String current_mode = "IDLE";
int dice_max = 6;
unsigned long last_press_time = 0;
const int DEBOUNCE_DELAY = 250;
unsigned long lastHeartbeatTime = 0;

// --- PROTOTYPES ---
void setupWiFi();
void checkForCommands();
void checkForButtonPress();
void sendHeartbeat();
void updateDisplay();
void sendInputResult(String value);
void sendUdpResponse(const JsonDocument& doc);

// ========================
// ===      SETUP       ===
// ========================
void setup() {
  Serial.begin(115200);
  Serial.println("\n--- InputPad Firmware v1.0 ---");

  // Buttons
  pinMode(BUTTON_A_PIN, INPUT_PULLUP);
  pinMode(BUTTON_B_PIN, INPUT_PULLUP);
  pinMode(BUTTON_C_PIN, INPUT_PULLUP);

  // TFT Init
  tft.init();
  tft.setRotation(3); // Landscape usually
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, 10);
  tft.setTextSize(2);
  tft.println("InputPad Booting...");

  // Wi-Fi & UDP
  setupWiFi();
  
  if (WiFi.status() == WL_CONNECTED) {
    udp.begin(INPUTPAD_LISTEN_PORT);
    Serial.printf("UDP Listening on port %d\n", INPUTPAD_LISTEN_PORT);
    current_mode = "IDLE";
    updateDisplay();
  } else {
    tft.fillScreen(TFT_RED); 
    tft.setCursor(10, 10);
    tft.println("Wi-Fi Error!");
    while(true) delay(100); // Halt
  }
}

// ========================
// ===    MAIN LOOP     ===
// ========================
void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    checkForCommands();
    checkForButtonPress();
    sendHeartbeat();
  }
  delay(20);
}

// ========================
// === HELPER FUNCTIONS ===
// ========================

void setupWiFi() {
  tft.fillScreen(TFT_BLACK); 
  tft.setCursor(10, 10); 
  tft.setTextSize(2);
  tft.println("Configuring IP...");
  
  if (!WiFi.config(INPUTPAD_STATIC_IP, GATEWAY, SUBNET, PRIMARY_DNS)) {
    Serial.println("Static IP configuration failed!");
  }
  
  tft.println("Connecting to WiFi...");
  Serial.print("Connecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long startTijd = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTijd < 15000) {
    delay(500); Serial.print("."); tft.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected!");
    Serial.print("IP Address: "); Serial.println(WiFi.localIP());
    tft.fillScreen(TFT_BLACK); 
    tft.setCursor(10, 10);
    tft.println("Connected!"); 
    tft.setTextSize(1);
    tft.println(WiFi.localIP().toString());
    delay(2000);
  }
}

void sendHeartbeat() {
  if (WiFi.status() != WL_CONNECTED) return;
  
  if (millis() - lastHeartbeatTime > 1500) { 
    StaticJsonDocument<128> doc;
    doc["id"] = "inputpad_v1"; 
    doc["state"] = current_mode; 
    doc["result_type"] = "heartbeat";

    sendUdpResponse(doc); 
    lastHeartbeatTime = millis();
  }
}

void updateDisplay() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  // Center of screen (approx 320x170 usable)
  int mid_x = 160; 
  int mid_y = 85;

  if (current_mode == "IDLE") {
    tft.drawCentreString("InputPad Ready", mid_x, mid_y - 20, 4);
    tft.drawCentreString("Waiting for Emily...", mid_x, mid_y + 20, 2);
  } 
  else if (current_mode == "DICE") {
    tft.drawCentreString("Dice Mode", mid_x, mid_y - 30, 4);
    
    tft.drawCentreString("(A) Press to Roll", mid_x, mid_y + 10, 2);
    tft.drawCentreString("(1-" + String(dice_max) + ")", mid_x, mid_y + 30, 2);
  } 
  else if (current_mode == "YES_NO") {
    tft.drawCentreString("Make a Choice", mid_x, 40, 4); 

    tft.drawString("(A) YES", mid_x - 100, mid_y + 20, 4); 
    tft.drawString("(B) NO", mid_x + 40, mid_y + 20, 4);
  } 
  else if (current_mode == "A_B_C") {
     tft.drawCentreString("Make a Choice", mid_x, 30, 4); 

     tft.drawString("(A) Option A", 40, mid_y + 20, 2); 
     tft.drawCentreString("(B) Option B", mid_x, mid_y + 20, 2);
     tft.drawString("(C) Option C", tft.width() - 110, mid_y + 20, 2);
  }
}

void checkForCommands() {
  int packetSize = udp.parsePacket();
  if (packetSize) {
    char packetBuffer[packetSize + 1];
    udp.read(packetBuffer, packetSize);
    packetBuffer[packetSize] = '\0';
    Serial.printf("UDP Command: %s\n", packetBuffer);

    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, packetBuffer);

    if (error) {
      Serial.print("JSON Error: "); Serial.println(error.c_str());
      return;
    }

    const char* command = doc["command"];
    if (command && strcmp(command, "set_mode") == 0) {
      current_mode = doc["mode"] | "IDLE";
      dice_max = doc["max"] | 6; 
      updateDisplay(); 
    }
  }
}

void checkForButtonPress() {
  if (current_mode == "IDLE" || millis() - last_press_time < DEBOUNCE_DELAY) {
    return;
  }

  String value_str = "";

  // Check Button A
  if (digitalRead(BUTTON_A_PIN) == LOW) {
    Serial.println("Button A Pressed");
    value_str = "A";
    
    if (current_mode == "DICE") {
      int roll = random(1, dice_max + 1);
      value_str = String(roll);

      // Show Result Big
      tft.fillScreen(TFT_BLACK);
      tft.drawCentreString(value_str, 160, 95, 7); 
      delay(1500);
      
    } else if (current_mode == "YES_NO") { 
        value_str = "YES"; 
    }
  }
  // Check Button B
  else if (digitalRead(BUTTON_B_PIN) == LOW) {
    Serial.println("Button B Pressed");
    value_str = "B";
    if (current_mode == "YES_NO") { 
        value_str = "NO"; 
    }
  }
  // Check Button C
  else if (digitalRead(BUTTON_C_PIN) == LOW) {
    Serial.println("Button C Pressed");
    value_str = "C";
  }

  if (value_str.length() > 0) {
    sendInputResult(value_str); 
    current_mode = "IDLE";
    updateDisplay();
    last_press_time = millis();
  }
}

void sendInputResult(String value) {
  StaticJsonDocument<128> doc;
  doc["event"] = "input_received";
  doc["value"] = value;
  sendUdpResponse(doc);
}

void sendUdpResponse(const JsonDocument& doc) {
  String output_string;
  serializeJson(doc, output_string);
  
  udp.beginPacket(EMILYBRAIN_IP, EMILYBRAIN_PORT);
  udp.print(output_string);
  udp.endPacket();
  Serial.printf("UDP Sent: %s\n", output_string.c_str());
}