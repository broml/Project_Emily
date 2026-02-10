// === Emily's InputPad v2.0 (Fixed for ESP32 v3.x) ===
// Role: External Input Device (Buttons, Dice, Choices)
// Features: WiFi Captive Portal, UDP Comms, TFT Feedback
// Hardware: ESP32, TFT Display (ST7789), 3 Buttons

// --- BELANGRIJKE FIX VOOR ESP32 V3.x ---
#include <FS.h> 
// ---------------------------------------

#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h> // Ensure User_Setup.h matches your hardware!
#include <SPI.h>

// --- WIFI MANAGER INCLUDES ---
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

// --- CONFIGURATION ---
// EmilyBrain Address (Target) - Zorg dat dit matcht met Emily's Static IP!
const char* EMILYBRAIN_IP = "192.168.68.201"; 
const int EMILYBRAIN_PORT = 12345;
const int INPUTPAD_LISTEN_PORT = 12349;

// This Device (Source) - Static IP Optional but recommended for speed
// Set to false if you want DHCP
bool use_static_ip = true;
IPAddress INPUTPAD_STATIC_IP(192, 168, 68, 205); 
IPAddress GATEWAY(192, 168, 68, 1);
IPAddress SUBNET(255, 255, 255, 0);
IPAddress PRIMARY_DNS(8, 8, 8, 8);

// --- HARDWARE PINS ---
// Ensure these do not conflict with your TFT pins defined in User_Setup.h
#define BUTTON_A_PIN 13  
#define BUTTON_B_PIN 14  
#define BUTTON_C_PIN 19  

// --- OBJECTS ---
TFT_eSPI tft = TFT_eSPI();
WiFiUDP udp;
WebServer server(80);
DNSServer dnsServer;
Preferences preferences;

// --- GLOBALS ---
String current_mode = "IDLE";
int dice_max = 6;
unsigned long last_press_time = 0;
const int DEBOUNCE_DELAY = 250;
unsigned long lastHeartbeatTime = 0;

// --- PROTOTYPES ---
void setupWiFi();
bool connectToWiFi(String ssid, String pass);
void startAPMode();
void handleRoot();
void handleSave();
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
  Serial.println("\n--- InputPad Firmware v2.0 ---");

  // Buttons
  pinMode(BUTTON_A_PIN, INPUT_PULLUP);
  pinMode(BUTTON_B_PIN, INPUT_PULLUP);
  pinMode(BUTTON_C_PIN, INPUT_PULLUP);

  // TFT Init
  tft.init();
  tft.setRotation(3); // Landscape
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  
  // Wi-Fi Setup (Auto or AP Mode)
  setupWiFi();

  // Only proceed if connected
  if (WiFi.status() == WL_CONNECTED) {
    udp.begin(INPUTPAD_LISTEN_PORT);
    Serial.printf("UDP Listening on port %d\n", INPUTPAD_LISTEN_PORT);
    current_mode = "IDLE";
    updateDisplay();
  }
}

// ========================
// ===    MAIN LOOP     ===
// ========================
void loop() {
  // If we are in AP Mode (Setup), handle web clients
  if (WiFi.status() != WL_CONNECTED) {
      server.handleClient();
      dnsServer.processNextRequest();
      delay(10);
      return; 
  }

  // Normal Operation
  checkForCommands();
  checkForButtonPress();
  sendHeartbeat();
  delay(20);
}

// ========================
// === WIFI MANAGER     ===
// ========================

void setupWiFi() {
    preferences.begin("inputpad-wifi", false); 
    String stored_ssid = preferences.getString("ssid", "");
    String stored_pass = preferences.getString("pass", "");
    preferences.end();

    if (stored_ssid.length() > 0) {
        tft.fillScreen(TFT_BLACK);
        tft.setCursor(10, 10); tft.setTextSize(2);
        tft.println("Connecting to:");
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.println(stored_ssid);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);

        if (connectToWiFi(stored_ssid, stored_pass)) {
            tft.fillScreen(TFT_BLACK);
            tft.setCursor(10, 10);
            tft.println("Connected!");
            tft.setTextSize(1);
            tft.setCursor(10, 40);
            tft.println(WiFi.localIP().toString());
            delay(1500);
            return;
        }
        
        tft.fillScreen(TFT_RED);
        tft.setCursor(10, 10); tft.println("Connect Failed");
        delay(1000);
    }
    
    startAPMode();
}

bool connectToWiFi(String ssid, String pass) {
    WiFi.mode(WIFI_STA);
    if(use_static_ip) {
        WiFi.config(INPUTPAD_STATIC_IP, GATEWAY, SUBNET, PRIMARY_DNS);
    }

    WiFi.begin(ssid.c_str(), pass.c_str());
    unsigned long startTijd = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTijd < 15000) {
        delay(500); Serial.print(".");
    }
    return (WiFi.status() == WL_CONNECTED);
}

void startAPMode() {
    Serial.println("Starting AP Mode...");
    WiFi.softAP("InputPad_Setup", "12345678"); 
    
    IPAddress AP_IP(192, 168, 4, 1);
    WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
    dnsServer.start(53, "*", AP_IP);

    // Display Instructions
    tft.fillScreen(TFT_BLUE);
    tft.setTextColor(TFT_WHITE, TFT_BLUE);
    tft.setCursor(5, 5); tft.setTextSize(2);
    tft.println("SETUP MODE");
    tft.setTextSize(1);
    tft.setCursor(5, 30);
    tft.println("1. Connect to WiFi:");
    tft.println("   'InputPad_Setup'");
    tft.println("2. Password:");
    tft.println("   '12345678'");
    tft.println("3. Go to 192.168.4.1");

    server.onNotFound([](){ handleRoot(); });
    server.on("/", HTTP_GET, [](){ handleRoot(); });
    server.on("/save", HTTP_POST, [](){ handleSave(); });
    server.begin(); 
}

void handleRoot() {
    String html = "<html><body><h2>InputPad WiFi Setup</h2><form action='/save' method='POST'>SSID: <input type='text' name='ssid'><br>Pass: <input type='password' name='pass'><br><input type='submit' value='Save'></form></body></html>";
    server.send(200, "text/html", html);
}

void handleSave() {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    
    preferences.begin("inputpad-wifi", false); 
    preferences.putString("ssid", ssid);
    preferences.putString("pass", pass);
    preferences.end();

    server.send(200, "text/html", "<html><body><h1>Saved!</h1>Rebooting...</body></html>");
    delay(1000);
    ESP.restart();
}

// ========================
// === LOGIC FUNCTIONS  ===
// ========================

void sendHeartbeat() {
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

  int mid_x = 160; 
  int mid_y = 85;

  if (current_mode == "IDLE") {
    tft.drawCentreString("InputPad Ready", mid_x, mid_y - 20, 4);
    tft.drawCentreString("Waiting for Emily...", mid_x, mid_y + 20, 2);
  } 
  else if (current_mode == "DICE") {
    tft.drawCentreString("Dice Mode", mid_x, mid_y - 30, 4);
    tft.drawCentreString("(A) Press to Roll", mid_x, mid_y + 10, 2);
    tft.drawCentreString("Max: " + String(dice_max), mid_x, mid_y + 30, 2);
  } 
  else if (current_mode == "YES_NO") {
    tft.drawCentreString("Make a Choice", mid_x, 40, 4); 
    tft.drawString("(A) YES", mid_x - 100, mid_y + 20, 4); 
    tft.drawString("(B) NO", mid_x + 40, mid_y + 20, 4);
  } 
  else if (current_mode == "A_B_C") {
     tft.drawCentreString("Select Option", mid_x, 30, 4); 
     tft.drawString("(A) Opt 1", 40, mid_y + 20, 2); 
     tft.drawCentreString("(B) Opt 2", mid_x, mid_y + 20, 2);
     tft.drawString("(C) Opt 3", tft.width() - 110, mid_y + 20, 2);
  }
}

void checkForCommands() {
  int packetSize = udp.parsePacket();
  if (packetSize) {
    char packetBuffer[packetSize + 1];
    udp.read(packetBuffer, packetSize);
    packetBuffer[packetSize] = '\0';
    
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, packetBuffer);

    if (!error) {
        const char* command = doc["command"];
        if (command && strcmp(command, "set_mode") == 0) {
          current_mode = doc["mode"] | "IDLE";
          dice_max = doc["max"] | 6; 
          updateDisplay(); 
        }
    }
  }
}

// Zorg dat de prototypes kloppen, we verplaatsen de logica iets voor de zekerheid
// Maar met de prototypes bovenin zou dit moeten werken.

void sendUdpResponse(const JsonDocument& doc) {
  String output_string;
  serializeJson(doc, output_string);
  udp.beginPacket(EMILYBRAIN_IP, EMILYBRAIN_PORT);
  udp.print(output_string);
  udp.endPacket();
}

void sendInputResult(String value) {
  StaticJsonDocument<128> doc;
  doc["event"] = "input_received";
  doc["value"] = value;
  sendUdpResponse(doc);
}

void checkForButtonPress() {
  if (current_mode == "IDLE" || millis() - last_press_time < DEBOUNCE_DELAY) {
    return;
  }

  String value_str = "";

  if (digitalRead(BUTTON_A_PIN) == LOW) {
    value_str = "A";
    if (current_mode == "DICE") {
      int roll = random(1, dice_max + 1);
      value_str = String(roll);
      tft.fillScreen(TFT_BLACK);
      tft.drawCentreString(value_str, 160, 95, 7); // Big Font
      delay(1500);
    } else if (current_mode == "YES_NO") { value_str = "YES"; }
  }
  else if (digitalRead(BUTTON_B_PIN) == LOW) {
    value_str = "B";
    if (current_mode == "YES_NO") { value_str = "NO"; }
  }
  else if (digitalRead(BUTTON_C_PIN) == LOW) {
    value_str = "C";
  }

  if (value_str.length() > 0) {
    sendInputResult(value_str); 
    current_mode = "IDLE";
    updateDisplay();
    last_press_time = millis();
  }
}
