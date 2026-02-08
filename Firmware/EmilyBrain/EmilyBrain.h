#ifndef EMILYBRAIN_H
#define EMILYBRAIN_H

#include <Arduino.h>
#include <TFT_eSPI.h> 
#include "SPI.h"
#include "SD.h"
#include "FS.h"
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "driver/i2s.h" // For direct I2S control
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>

#include <DNSServer.h>
#include <Preferences.h> // For saving WiFi creds to flash

#include <deque>
#include <ArduinoJson.h>
#include "esp_heap_caps.h"

// --- SD Card Pins ---
#define PIN_SD_SCLK 45
#define PIN_SD_MISO 47
#define PIN_SD_MOSI 38
#define PIN_SD_CS   1

// --- I2S Audio Pins ---
#define PIN_I2S_BCK     17 
#define PIN_I2S_WS      18 
#define PIN_I2S_DATA_IN 19 // (Microphone SD to ESP32)
#define PIN_I2S_DATA_OUT 21 // (ESP32 to Amplifier DIN)

// --- Other Pins ---
#define PIN_WAKE_BUTTON  4
#define PIN_NEOPIXEL 48

// --- WiFi Configuration (User to Update) ---
#define WIFI_SSID "YOUR_WIFI_SSID"      // <-- CHANGE THIS
#define WIFI_PASS "YOUR_WIFI_PASSWORD"  // <-- CHANGE THIS
#define UDP_LISTEN_PORT 12345

// --- IP Addresses for Modules (Static Config) ---
#define OS_IP_ADDRESS "192.168.68.200"
#define OS_UDP_PORT 12346
#define CAMCANVAS_IP_ADDRESS "192.168.68.203"
#define CAMCANVAS_UDP_PORT 12347
#define INPUTPAD_IP_ADDRESS "192.168.68.205" 
#define INPUTPAD_UDP_PORT 12349            

// --- Timeouts & Intervals ---
#define CONNECTION_TIMEOUT_MS 10000 // 10 second timeout for modules
#define PING_INTERVAL_MS 3000       // Send a PING every 3 seconds
#define INPUT_TASK_TIMEOUT_MS 60000 // 60 seconds waiting for user input
#define PHYSICAL_TASK_TIMEOUT_MS 30000 // Timeout for motor movements
#define TTS_GENERATION_TIMEOUT_MS 30000 // Timeout for Speech Generation
#define CAM_TASK_TIMEOUT_MS 60000    // Timeout for vision analysis
#define CANVAS_TASK_TIMEOUT_MS 60000 // Timeout for image generation

#define DEBOUNCE_DELAY_MS 50 // Button debounce time

// --- Emotional Model Parameters ---
#define AROUSAL_DECAY_RATE 0.05 // Decrease per second
#define AROUSAL_THRESHOLD 0.1   // Activity threshold
#define LOUD_SOUND_THRESHOLD 800

// --- API Configuration ---
// WARNING: Do not share your API keys publicly!
#define VENICE_API_KEY "YOUR_VENICE_API_KEY" // <-- CHANGE THIS
#define VENICE_API_URL "https://api.venice.ai/api/v1/chat/completions"
#define OPENAI_API_KEY "YOUR_OPENAI_API_KEY" // <-- CHANGE THIS

// --- JSON Capacity ---
// Memory reserved for the main LLM context window
#define JSON_DOC_CAPACITY 32768 // 32KB

// --- VAD (Voice Activity Detection) Parameters ---
#define SPEECH_START_THRESHOLD  145
#define SILENCE_THRESHOLD       25
#define SILENCE_DURATION_MS     1500
#define MAX_RECORDING_MS        20000

#define SOUND_RADAR_THRESHOLD 70 // Minimum intensity for radar
#define MAX_SOUND_EVENTS 3       // Track last 3 significant sounds

// --- State Machine Definitions ---
enum class EmilyState {
    IDLE,
    AWAITING_SPEECH,
    RECORDING_SPEECH,
    PROCESSING_STT,
    PROCESSING_AI,
    GENERATING_SPEECH,
    SPEAKING,
    EXECUTING_PHYSICAL_TOOL,
    SEEING,
    VISUALIZING,
    PLAYING_SOUND,
    AWAITING_INPUT,
    PROCESSING_GAMEDATA
};

// --- Task Structure ---
struct Task {
    String type;
    StaticJsonDocument<1024> args; 
};

struct WavHeader {
    int sampleRate = 0; 
    int bitsPerSample = 0;
    int channels = 0;
};

// --- Sound Event Structure ---
struct SoundEvent {
    int intensity = 0;
    float direction = 0.0;
};


class EmilyBrain {
public:
    EmilyBrain();
    void setup();
    void loop();

private:   
    // --- Hardware Objects ---
    TFT_eSPI display;
    Adafruit_NeoPixel status_led;
    WiFiUDP udp; 

    WebServer ptms_server;
    DNSServer dnsServer; // Captive Portal
    Preferences preferences; 

    // --- WiFi Helpers ---
    void setupWiFi(); 
    bool connectToWiFi(String ssid, String pass);
    void startAPMode(); 
    void handleRoot(); 
    void handleSave(); 

    // --- Web Remote Handlers ---
    void handleRemotePage(); 
    void handleRemoteInput();  

    enum class WiFiStatus { DISCONNECTED, CONNECTING, CONNECTED };
    WiFiStatus wifi_status = WiFiStatus::DISCONNECTED;

    SPIClass* spiSD = nullptr; 

    // --- Connectivity Tracking ---
    bool os_connected = false;
    bool camcanvas_connected = false;
    bool inputpad_connected = false;

    unsigned long last_os_contact = 0;
    unsigned long last_camcanvas_contact = 0;
    unsigned long last_inputpad_contact = 0;
    unsigned long last_ping_time = 0;

    // --- Emotional Model ---
    double arousal = 0.0;
    double valence = 0.0; 
    unsigned long last_decay_time = 0;

    // --- Previous States (Display Optimization) ---
    WiFiStatus prev_wifi_status = WiFiStatus::DISCONNECTED;
    EmilyState prev_EmilyState = EmilyState::IDLE;

    bool prev_os_connected = false; 
    bool prev_camcanvas_connected = false;
    bool prev_inputpad_connected = false; 
    
    double prev_arousal = -1.0; 
    double prev_valence = -1.0;
    
    String prev_line1_L = "", prev_line1_R = ""; // State | Mode
    String prev_line2_L = "", prev_line2_R = ""; // CC Status | Pan
    String prev_line3_R = "";                    // Tilt

    String last_display_text = "Booting..."; 
    String prev_display_text = "";

    // --- Wake Button ---
    int last_button_state = HIGH;      
    int debounced_state = HIGH;    
    unsigned long last_debounce_time = 0; 

    // --- OS State Storage ---
    String os_act_state = "Offline";
    float os_angle = 0.0; // Was: os_hoek
    String current_attention_mode = "PASSIVE";
    int last_sound_int = 0;
    float last_sound_dir = 0.0;
    std::deque<SoundEvent> sound_radar;

    // --- CamCanvas State ---
    float current_cam_tilt = 90.0; 
    float current_cam_pan = 90.0;   
    float prev_cam_tilt = -999.0;
    float prev_cam_pan = -999.0;

    unsigned long physical_task_start_time = 0; 
    
    EmilyState currentState = EmilyState::IDLE;
    const char* current_arousal_context = nullptr; 
    unsigned long ai_simulation_start_time = 0;
    String system_prompt_content;

    // --- Task Queue & Execution ---
    std::deque<Task> task_queue;
    JsonObject active_tool_call_args; 
    String active_tool_call_id;       
    String active_tool_call_name;     

    EmilyState next_state_after_audio = EmilyState::IDLE; 
    unsigned long tts_task_start_time = 0; 
    unsigned long camcanvas_task_start_time = 0;      
    unsigned long inputpad_task_start_time = 0;   
    unsigned long input_task_start_time = 0;

    // --- Significant Event Tracking ---
    std::deque<String> significant_events;
    const size_t MAX_SIGNIFICANT_EVENTS = 5; 
    StaticJsonDocument<512> prev_os_heartbeat_data; 

    // --- Response Mailboxes ---
    JsonDocument last_vision_response;       
    StaticJsonDocument<256> last_camcanvas_confirmation; 
    StaticJsonDocument<128> last_inputpad_response; 

    // --- Private Helper Functions ---
    void renderDisplay();
    void sendPings(); 
    void handleUdpPackets();
    void setupWebServer();
    void handleFileUpload();
    void handleFileDownload();
    void handleHistoryDelete();

    void checkTimeouts(); 
    bool checkWakeButton();
    void decayArousal();
    const char* stateToString(EmilyState state);
    void setState(EmilyState newState);
    void _start_ai_cycle(const char* trigger_reason);
    String buildSelfAwarenessReport(JsonObject device_status);
    void addChatHistoryToMessages(JsonArray messages, int max_history_items);
    
    void buildAiPayload(StaticJsonDocument<JSON_DOC_CAPACITY>& doc, const char* current_prompt_content, JsonObject device_status);
    void loadConfigurations();
    void processAiProxyRequest(const char* current_prompt_content, JsonObject device_status);
    void processTtsRequest(const char* text);
    bool downloadTtsToSd(const char* textToSpeak, const char* filename);
    bool recordAudioToWav(const char* filename);
    void createWavHeader(byte* header, size_t total_data_size);
    void transcribeAudioFromSd(const char* filename);
    void listenAndTranscribe();
    void processSttResponseAndTriggerAi(const String& raw_api_payload);
    void _handle_ai_response(const char* user_prompt_json_str, JsonArray tool_calls); 
    void _continue_task(); 
    void logInteractionToSd(JsonObject log_data); 
    void logInteractionToSd_Error(const char* role, String tool_call_id, String tool_name, String content);
    void playWavFromSd(const char* filename);
    WavHeader parseWavHeader(const char* path);
    void addTask(const String& type, JsonVariantConst args_variant);
    bool isOsEventSignificant(const JsonDocument& current_heartbeat);
    void handlePlayingSoundState();
    void addSignificantEvent(const String& event_desc);
    String getDataFromJson(const char* key);
    
    // State Handlers
    void handleIdleState();
    void handleAwaitingSpeechState();
    void handleRecordingSpeechState();
    void handleProcessingSttState();
    void handleProcessingAiState();
    void handleGeneratingSpeechState();
    void handleSpeakingState();
    void handleExecutingPhysicalToolState();
    void handleSeeingState();
    void handleVisualizingState();
    void handleAwaitingInputState();
    void handleProcessGamedataState();

    // Web Remote Handlers
    void handleForceIdle();
    void handleCenterHead();
    void handleForceReboot();
};

#endif // EMILYBRAIN_H
