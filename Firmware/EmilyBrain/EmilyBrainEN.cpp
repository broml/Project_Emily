#include "EmilyBrain.h"
#include <ArduinoJson.h>

// --- Descriptions for Self-Awareness Report (Commercial v1.0) ---
const char* DESC_CORE =
    "I am a robot entity. My core logic runs on a unified processor named 'EmilyBrain'. "
    "This provides me with the basic abilities to hear (via OS), speak, and be activated by a physical button.\n";

const char* DESC_OS =
    "- An 'OS' (Base Unit): A stationary base with a rotating disc (0-359 degrees) for orientation and a 4-microphone array for sound localization.\n";

const char* DESC_CAMCANVAS =
    "- A 'CamCanvas' (CC) unit: A module mounted on the rotating disc that contains both the high-resolution camera (for vision) and the visual display (for generating images).\n";

const char* DESC_INPUTPAD =
    "- An 'InputPad' (IP): A separate, wireless handheld device with a screen and 3 buttons (A, B, C) used for specific game inputs or choices, like rolling dice or answering quizzes.\n";

// Constructor
EmilyBrain::EmilyBrain() : status_led(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800), display() {
    // Constructor body remains empty
}

void EmilyBrain::setup() {
    Serial.begin(115200);
    while (!Serial);
    Serial.println("EmilyBrain Dual-Bus Startup... (Release Sequence)");
    pinMode(PIN_WAKE_BUTTON, INPUT_PULLUP);
    Serial.println("Wake button configured.");

    // --- Step 1: Initialize Non-Conflicting Hardware ---
    status_led.begin();
    status_led.setPixelColor(0, 0, 10, 0); // Green
    status_led.show();

    Serial.println("Initializing SD Card on FSPI...");
    spiSD = new SPIClass(FSPI);
    spiSD->begin(PIN_SD_SCLK, PIN_SD_MISO, PIN_SD_MOSI);
    if (!SD.begin(PIN_SD_CS, *spiSD)) {
        Serial.println("!!! FATAL: SD Card Mount FAILED!");
        // We cannot proceed without SD card for config/logs
        while(true) { delay(1000); } 
    }
    Serial.println("SD Card OK.");
    
    // Load configurations (essential for tools, system prompt, etc.)
    loadConfigurations();

    // --- Step 2: Invoke WiFi/Display Setup Wizard ---
    // This handles all WiFi and Display initialization
    setupWiFi(); 

    // --- Step 3: Start Remaining Services ---
    // UDP and PTMS servers are started within setupWiFi() 
    
    Serial.println("Setup complete. Starting main loop.");
    last_display_text = "System Ready.";
    prev_display_text = "";
    
    // Note: renderDisplay() is already called in setupWiFi()
}    
    
const char* EmilyBrain::stateToString(EmilyState state) {
    switch (state) {
        case EmilyState::IDLE:                    return "Idle";
        case EmilyState::AWAITING_SPEECH:         return "Waiting for Speech";
        case EmilyState::RECORDING_SPEECH:        return "Recording...";
        case EmilyState::PROCESSING_STT:          return "Transcribing...";
        case EmilyState::PROCESSING_AI:           return "Thinking...";
        case EmilyState::GENERATING_SPEECH:       return "Preparing Speech...";
        case EmilyState::SPEAKING:                return "Speaking...";
        case EmilyState::EXECUTING_PHYSICAL_TOOL: return "Acting (OS)...";
        case EmilyState::SEEING:                  return "Seeing (CamCanvas)...";
        case EmilyState::VISUALIZING:             return "Visualizing (CamCanvas)...";
        case EmilyState::PLAYING_SOUND:           return "Playing Sound...";
        case EmilyState::AWAITING_INPUT:          return "Awaiting Input (Pad)...";
        case EmilyState::PROCESSING_GAMEDATA:     return "Retrieving Data...";
        default:                                  return "Unknown";
    }
}

void EmilyBrain::setState(EmilyState newState) {
    if (newState == currentState) {
        return; // No change needed
    }

    currentState = newState;
    const char* stateName = stateToString(currentState);
    Serial.printf("STATE CHANGE -> %s\n", stateName);

    // Update LED color based on the new state
    uint32_t color = status_led.Color(10, 5, 0); // Default Orange for unknown

    switch (currentState) {
        case EmilyState::IDLE:
            color = status_led.Color(0, 10, 0); // Green
            break;
        case EmilyState::AWAITING_SPEECH:
        case EmilyState::RECORDING_SPEECH:
            color = status_led.Color(15, 0, 0); // Red
            break;
        case EmilyState::PROCESSING_STT:
        case EmilyState::PROCESSING_AI:
            color = status_led.Color(0, 0, 15); // Blue
            break;
        case EmilyState::GENERATING_SPEECH:
        case EmilyState::SPEAKING:
            color = status_led.Color(0, 15, 15); // Cyan
            break;
        case EmilyState::EXECUTING_PHYSICAL_TOOL:
        case EmilyState::SEEING:
        case EmilyState::VISUALIZING:
            color = status_led.Color(15, 0, 15); // Magenta
            break;
    }
    status_led.setPixelColor(0, color);
    status_led.show();
    renderDisplay();
}

// Setup the Web Server (PTMS)
void EmilyBrain::setupWebServer() {
    // Standard PTMS handlers
    ptms_server.on("/upload", HTTP_POST, [this](){ this->handleFileUpload(); });
    ptms_server.on("/delete-history", HTTP_GET, [this](){ this->handleHistoryDelete(); });
    ptms_server.on("/download", HTTP_GET, [this](){ this->handleFileDownload(); });

    // --- WEB REMOTE HANDLERS ---
    ptms_server.on("/remote", HTTP_GET, [this](){ this->handleRemotePage(); });
    ptms_server.on("/send", HTTP_POST, [this](){ this->handleRemoteInput(); }); // POST for form data
    
    // Remote actions (GET)
    ptms_server.on("/force-idle", HTTP_GET, [this](){ this->handleForceIdle(); });
    ptms_server.on("/center-head", HTTP_GET, [this](){ this->handleCenterHead(); });
    ptms_server.on("/force-reboot", HTTP_GET, [this](){ this->handleForceReboot(); });
    // --- END WEB REMOTE ---
    
    // Root handler
    ptms_server.on("/", HTTP_GET, [this](){
        ptms_server.send(200, "text/plain", "EmilyBrain PTMS Server is online.");
    });
    ptms_server.onNotFound([this](){
         ptms_server.send(404, "text/plain", "Not Found.");
    });
    
    ptms_server.begin(); 
}

void EmilyBrain::handleFileUpload() {
    if (!ptms_server.hasArg("file") || !ptms_server.hasArg("plain")) {
        ptms_server.send(400, "text/plain", "400: BAD REQUEST");
        return;
    }

    String filename = ptms_server.arg("file");
    String content = ptms_server.arg("plain");
    Serial.printf("File upload request received for: %s\n", filename.c_str());

    if (!filename.startsWith("/")) {
        filename = "/" + filename;
    }

    File file = SD.open(filename, FILE_WRITE);
    if (file) {
        file.print(content);
        file.close();
        Serial.println("File successfully saved to SD card.");
        ptms_server.send(200, "text/plain", "SUCCESS: File saved.");
        
        // IMPORTANT: Reload configurations after upload
        loadConfigurations(); 
    } else {
        Serial.println("ERROR: Could not open file on SD card for writing.");
        ptms_server.send(500, "text/plain", "500: SERVER ERROR - Could not save file.");
    }
}

void EmilyBrain::handleFileDownload() {
    if (!ptms_server.hasArg("file")) {
        ptms_server.send(400, "text/plain", "400: BAD REQUEST - 'file' parameter is missing.");
        return;
    }

    String path = ptms_server.arg("file");
    Serial.printf("File download request received for: %s\n", path.c_str());

    if (!path.startsWith("/")) {
        path = "/" + path;
    }

    if (SD.exists(path)) {
        File file = SD.open(path, FILE_READ);
        String contentType = "text/plain"; 
        if (path.endsWith(".json") || path.endsWith(".jsonl")) {
            contentType = "application/json";
        }
        
        ptms_server.streamFile(file, contentType);
        file.close();
        Serial.println("File successfully streamed to client.");
    } else {
        Serial.println("ERROR: Requested file does not exist.");
        ptms_server.send(404, "text/plain", "404: NOT FOUND - File does not exist.");
    }
}

void EmilyBrain::handleHistoryDelete() {
    Serial.println("Received request to delete chat history...");
    if (SD.remove("/chat_history.jsonl")) {
        Serial.println("File /chat_history.jsonl deleted successfully.");
        ptms_server.send(200, "text/plain", "SUCCESS: Chat history has been deleted.");
    } else {
        Serial.println("ERROR: Failed to delete chat history file.");
        ptms_server.send(500, "text/plain", "ERROR: Failed to delete file. It might not exist.");
    }
}

bool EmilyBrain::checkWakeButton() {
    bool button_pressed_event = false;
    int reading = digitalRead(PIN_WAKE_BUTTON);

    if (reading != last_button_state) {
        last_debounce_time = millis();
    }

    if ((millis() - last_debounce_time) > DEBOUNCE_DELAY_MS) {
        if (reading != debounced_state) {
            debounced_state = reading;
            if (debounced_state == LOW) { // Button Pressed
                Serial.println("Wake Button Pressed!"); 
                button_pressed_event = true; 
            }
        }
    }

    last_button_state = reading;
    return button_pressed_event; 
}

void EmilyBrain::processAiProxyRequest(const char* current_prompt_content, JsonObject device_status) {
    // Note: State is already set to PROCESSING_AI by _start_ai_cycle

    Serial.println("Starting AI Proxy Request (StaticJsonDocument in PSRAM)...");

    // --- Step 1: Allocate PSRAM Buffer ---
    // INDUSTRIAL GRADE MEMORY MANAGEMENT
    Serial.printf("DEBUG: Allocating %u byte buffer in PSRAM...\n", JSON_DOC_CAPACITY);
    byte* json_psram_buffer = (byte*)heap_caps_malloc(JSON_DOC_CAPACITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (json_psram_buffer == nullptr) {
        Serial.println("FATAL: Failed to allocate PSRAM buffer!");
        setState(EmilyState::IDLE);
        return;
    }
    Serial.println("DEBUG: PSRAM buffer allocated.");

    // --- Step 2: Construct StaticJsonDocument in the buffer ---
    // Use placement new to construct the object AT the buffer address
    StaticJsonDocument<JSON_DOC_CAPACITY>* doc_ptr = new (json_psram_buffer) StaticJsonDocument<JSON_DOC_CAPACITY>();
    Serial.println("DEBUG: StaticJsonDocument constructed in buffer.");

    // --- Step 3: Build the payload INTO the document ---  
    buildAiPayload(*doc_ptr, current_prompt_content, device_status);

    if (doc_ptr->overflowed()) {
        Serial.println("ERROR: JSON Document overflowed during build!");
        // --- Cleanup on error ---
        doc_ptr->~StaticJsonDocument(); // Explicitly call destructor
        heap_caps_free(json_psram_buffer); // Free the buffer
        // --- End Cleanup ---
        setState(EmilyState::IDLE);
        return;
    }

    // --- Step 4: Allocate buffer for string and serialize ---
    Serial.println("DEBUG: Measuring final JSON...");
    size_t len = measureJson(*doc_ptr); 
    Serial.printf("Calculated payload string size: %u bytes\n", len);
    
    char *payload_buffer = (char *) malloc(len + 1); // Allocate from default heap
    if (!payload_buffer) {
        Serial.println("FATAL: Malloc failed for payload STRING buffer!");
         // --- Cleanup on error ---
        doc_ptr->~StaticJsonDocument(); 
        heap_caps_free(json_psram_buffer); 
        // --- End Cleanup ---
        setState(EmilyState::IDLE);
        return;
    }
    Serial.println("DEBUG: String buffer allocated. Serializing JSON...");
    serializeJson(*doc_ptr, payload_buffer, len + 1); 
    Serial.println("DEBUG: JSON Serialized to string buffer.");

    // Step 5: Make the HTTPS POST request
    String response_body = ""; 
    WiFiClientSecure client;
    HTTPClient http;
    client.setInsecure(); 

    Serial.println("Connecting to AI API...");
    if (http.begin(client, VENICE_API_URL)) {
        http.addHeader("Authorization", "Bearer " + String(VENICE_API_KEY));
        http.addHeader("Content-Type", "application/json");
        http.setTimeout(90000); // 90 seconds timeout for LLM inference

        Serial.println("Sending POST request...");
        int httpResponseCode = http.POST((uint8_t*)payload_buffer, len);

        // --- Free the buffer ASAP ---
        free(payload_buffer);
        payload_buffer = nullptr; 

        if (httpResponseCode == HTTP_CODE_OK) {
            response_body = http.getString();
            Serial.println("API response OK.");
        } else {
            Serial.printf("[HTTP] POST failed, error: %d\n", httpResponseCode);
            response_body = http.getString(); 
            Serial.println("Error payload: " + response_body);
            response_body = "{\"error\":\"API call failed\", \"code\": " + String(httpResponseCode) + "}";
        }
        http.end();
    } else {
         Serial.println("Failed to connect to API URL!");
         if (payload_buffer) free(payload_buffer); 
         response_body = "{\"error\":\"Connection failed\"}";
    }

    // --- Step 6: CRUCIAL Cleanup ---
    Serial.println("DEBUG: Cleaning up JSON document and buffers...");
    if (doc_ptr != nullptr) {
        doc_ptr->~StaticJsonDocument<JSON_DOC_CAPACITY>(); // Explicitly call destructor
    }
    if (json_psram_buffer != nullptr) {
        heap_caps_free(json_psram_buffer); // Free the main PSRAM buffer
    }
    Serial.println("DEBUG: Cleanup complete.");

    // --- Step 7: Parse response and call Planner ---
    Serial.println("Parsing API response...");

    DynamicJsonDocument response_doc(4096); 
    DeserializationError error = deserializeJson(response_doc, response_body);

    if (error) {
        Serial.printf("Failed to parse API response JSON: %s\n", error.c_str());
        setState(EmilyState::IDLE);
    } else {
        JsonArray tool_calls;
        if (response_doc.containsKey("choices") &&
            response_doc["choices"][0].containsKey("message") &&
            response_doc["choices"][0]["message"].containsKey("tool_calls")) {
            tool_calls = response_doc["choices"][0]["message"]["tool_calls"].as<JsonArray>();
        }

        if (!tool_calls.isNull() && tool_calls.size() > 0) {
            Serial.println("Tool calls received, calling planner...");
             _handle_ai_response(nullptr, tool_calls); // Call the planner!
        } else {
             Serial.println("API response received, but no tool calls found or format is unexpected.");
             setState(EmilyState::IDLE); 
        }
    }
}

void EmilyBrain::addTask(const String& type, JsonVariantConst args_variant) {
    Task new_task;
    new_task.type = type;
    new_task.args = args_variant; 
    task_queue.push_back(new_task);
    Serial.printf("Task added: %s\n", type.c_str()); 
}

// --- The Planner (Brain of the Operation) ---
void EmilyBrain::_handle_ai_response(const char* user_prompt_json_str, JsonArray tool_calls) {
    Serial.println("Planner (_handle_ai_response) called!");

    if (tool_calls.isNull() || tool_calls.size() == 0) {
        Serial.println("Planner Warning: Received empty or null tool_calls array.");
        _start_ai_cycle("My cognitive core returned no specific action. What should I do?");
        return;
    }

    // --- Log the AI response ---
    StaticJsonDocument<1024> log_doc; 
    log_doc["role"] = "assistant";
    JsonArray log_tool_calls = log_doc.createNestedArray("tool_calls");
    for(JsonObject tc : tool_calls) {
        log_tool_calls.add(tc);
    }
    logInteractionToSd(log_doc.as<JsonObject>()); 

    // --- Process the FIRST tool call ---
    JsonObject tool_call = tool_calls[0].as<JsonObject>();
    if (tool_call.isNull()) {
         Serial.println("Planner Error: First tool call is invalid JSON.");
         setState(EmilyState::IDLE); 
         return;
    }

    active_tool_call_id = tool_call["id"] | ""; 
    JsonObject function_call = tool_call["function"].as<JsonObject>();
    active_tool_call_name = function_call["name"] | "unknown_tool"; 

    Serial.printf("Planner: Processing tool '%s' (ID: %s)\n", active_tool_call_name.c_str(), active_tool_call_id.c_str());

    // --- Parse ALL Arguments ONCE ---
    StaticJsonDocument<1024> all_args_doc; 
    DeserializationError error = deserializeJson(all_args_doc, function_call["arguments"].as<const char*>());

    if (error) {
        Serial.printf("Planner Error: Failed to parse arguments for tool '%s': %s\n", active_tool_call_name.c_str(), error.c_str());
        logInteractionToSd_Error("tool", active_tool_call_id, active_tool_call_name, "ERROR: Invalid arguments received: " + String(error.c_str()));
        setState(EmilyState::IDLE);
        return;
    }
    JsonObject all_args = all_args_doc.as<JsonObject>(); 
    
    // --- Clear previous queue ---
    task_queue.clear();

    // --- REVISED Planning Logic with Argument Filtering ---
    bool main_task_planned = false;
    
    // === Step 0: Plan OPTIONAL LED Effects ===
    if (all_args.containsKey("led_effect")) {
        StaticJsonDocument<256> led_args_doc;
        led_args_doc["command"] = "set_status_led"; 
        led_args_doc["effect"] = all_args["led_effect"];
        led_args_doc["r"] = all_args["led_r"] | 0; 
        led_args_doc["g"] = all_args["led_g"] | 0;
        led_args_doc["b"] = all_args["led_b"] | 0;
        led_args_doc["speed"] = all_args["led_speed"] | 1000; 
        
        addTask("CAMCANVAS_SET_LED", led_args_doc); 
    }

    // === Step 1: Plan OPTIONAL Background Image FIRST ===
    // Note: English JSON Key used now: visual_prompt
    if (all_args.containsKey("visual_prompt")) {
        StaticJsonDocument<1024> image_args_doc;
        image_args_doc["prompt"] = all_args["visual_prompt"];
        image_args_doc["image_model"] = all_args["image_model"] | "venice-sd35"; 
        addTask("CANVAS_IMAGE_ASYNC", image_args_doc); 
        Serial.println("Planner: Added ASYNC image task first.");
    }

    // === Step 2: Plan the MAIN Action ===

    // 1. update_emotional_state
    if (!main_task_planned && active_tool_call_name == "update_emotional_state") {
        addTask("PB_EMOTION", all_args_doc); 
        if (all_args.containsKey("sound_effect")) {
            StaticJsonDocument<128> sound_args_doc;
            sound_args_doc["sound_effect"] = all_args["sound_effect"];
            addTask("CB_SOUND_EMOTION", sound_args_doc); 
        }
        main_task_planned = true;
    }
    // 2. analyze_visual_environment
    else if (!main_task_planned && active_tool_call_name == "analyze_visual_environment") {
        if (all_args.containsKey("angle")) {
             StaticJsonDocument<128> move_args_doc;
             move_args_doc["angle"] = all_args["angle"];
             addTask("OS_MOVE", move_args_doc); 
        }
        StaticJsonDocument<512> analyze_args_doc;
        analyze_args_doc["question"] = all_args["question"] | "Describe what you see.";
        analyze_args_doc["use_flash"] = all_args["use_flash"] | false;
        addTask("CAM_ANALYZE", analyze_args_doc); 
        main_task_planned = true;
    }

    // 3. Speech (announce_message OR start_conversation)
    else if (!main_task_planned && (active_tool_call_name == "announce_message" || active_tool_call_name == "start_conversation")) {
        StaticJsonDocument<1024> speak_args_doc; 
        if (all_args.containsKey("announcement")) speak_args_doc["announcement"] = all_args["announcement"];
        else if (all_args.containsKey("question")) speak_args_doc["question"] = all_args["question"];
        addTask("CB_SPEAK", speak_args_doc);
        main_task_planned = true;
    }

    // 4. rotate_to_angle
    else if (!main_task_planned && active_tool_call_name == "rotate_to_angle") {
        StaticJsonDocument<128> move_args_doc;
        move_args_doc["angle"] = all_args["angle"];
        addTask("OS_MOVE", move_args_doc);
        main_task_planned = true;
    }
    // 5. calibrate_base
    else if (!main_task_planned && active_tool_call_name == "calibrate_base") {
        StaticJsonDocument<1> calibrate_args_doc; 
        addTask("OS_CALIBRATE", calibrate_args_doc);
        main_task_planned = true;
    }

    // 6. move_head
    else if (!main_task_planned && active_tool_call_name == "move_head") {
        StaticJsonDocument<128> head_args_doc;
        head_args_doc["pan"] = all_args["pan"] | 90; 
        head_args_doc["tilt"] = all_args["tilt"] | 90; 
        addTask("CAMCANVAS_MOVE_HEAD", head_args_doc);
        main_task_planned = true;
    }

    // 7. generate_image
    else if (!main_task_planned && active_tool_call_name == "generate_image") {
        StaticJsonDocument<1024> gen_img_args_doc;
        gen_img_args_doc["prompt"] = all_args["prompt"];
        gen_img_args_doc["image_model"] = all_args["image_model"];
        addTask("CANVAS_IMAGE_SYNC", gen_img_args_doc);
        
        if (all_args.containsKey("sound_effect")) {
            StaticJsonDocument<128> sound_args;
            sound_args["sound_path"] = all_args["sound_effect"];
            addTask("CB_SOUND", sound_args); 
        }
        main_task_planned = true;
    }
    // 8. play_sound_effect
    else if (!main_task_planned && active_tool_call_name == "play_sound_effect") {
        StaticJsonDocument<128> sound_args_doc;
        sound_args_doc["sound_path"] = all_args["sound_path"];
        addTask("CB_SOUND", sound_args_doc);
        main_task_planned = true;
    }
    // 9. tilt_head
    else if (!main_task_planned && active_tool_call_name == "tilt_head") {
        StaticJsonDocument<128> tilt_args_doc;
        tilt_args_doc["angle"] = all_args["angle"];
        addTask("CAM_TILT", tilt_args_doc);
        main_task_planned = true;
    }
    // 10. nod_head
    else if (!main_task_planned && active_tool_call_name == "nod_head") {
        StaticJsonDocument<128> knod_args_doc;
        knod_args_doc["angle"] = all_args["angle"];
        addTask("CAM_NOD", knod_args_doc);
        main_task_planned = true;
    }
    // 11. take_photo
    else if (!main_task_planned && active_tool_call_name == "take_photo") {
        StaticJsonDocument<1> photo_args_doc; 
        addTask("CAM_TAKE_PICTURE", photo_args_doc);
        main_task_planned = true;
    }

    // 12. InputPad: activate_inputpad
    else if (!main_task_planned && active_tool_call_name == "activate_inputpad") {
        StaticJsonDocument<256> input_args_doc;
        input_args_doc["mode"] = all_args["mode"];
        input_args_doc["max_value"] = all_args["max_value"]; 
        addTask("INPUTPAD_SET_MODE", input_args_doc);
        main_task_planned = true;
    }
    
    // 13. retrieve_local_data
    else if (!main_task_planned && active_tool_call_name == "retrieve_local_data") {
        StaticJsonDocument<256> data_args_doc;
        data_args_doc["key"] = all_args["key"];
        addTask("EB_GET_LOCAL_DATA", data_args_doc);
        main_task_planned = true;
    }

    // === Step 3: Handle Unknown Tool or Start Execution ===

    if (task_queue.empty()) {
        // Unknown tool and no async image prompt
        Serial.printf("Planner Error: Unknown tool '%s' requested by AI.\n", active_tool_call_name.c_str());
        logInteractionToSd_Error("tool", active_tool_call_id, active_tool_call_name, "ERROR: Tool does not exist.");
        String error_message = "My cognitive core requested a tool '" + active_tool_call_name + "' which I do not have.";
        _start_ai_cycle(error_message.c_str()); 
        return; 
    }

    Serial.printf("Planner: Added %d task(s) to the queue. Starting execution...\n", task_queue.size());
    _continue_task(); // Start executing
}
 

void EmilyBrain::playWavFromSd(const char* filename) {
    Serial.printf("Attempting to play audio file: %s\n", filename);

    // --- Step 1: Parse Header ---
    WavHeader header = parseWavHeader(filename);
    if (header.sampleRate == 0 || header.bitsPerSample == 0 || header.channels == 0) {
        Serial.println("Error: Invalid WAV header data. Cannot play.");
        return; 
    }
    if (header.channels != 1) {
         Serial.println("Error: Only MONO (1 channel) WAV files are supported for playback.");
         return;
    }
    if (header.bitsPerSample != 16) {
         Serial.println("Error: Only 16-bit WAV files are supported for playback.");
         return;
    }

  // --- Step 2: Open File ---
    File audioFile = SD.open(filename, FILE_READ);
    if (!audioFile) {
        Serial.printf("Error: Could not re-open file %s for playback\n", filename);
        return;
    }

    // --- Memory Check ---
    Serial.println(">>> DEBUG: playWavFromSd - Checking memory before I2S install...");
    Serial.printf(">>> DEBUG: Free Heap: %u, Free PSRAM: %u\n", ESP.getFreeHeap(), ESP.getFreePsram());

  // --- Step 3: Configure I2S Dynamically ---
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = header.sampleRate,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT, 
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,  
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 256, 
        .use_apll = true,   
        .tx_desc_auto_clear = true
    };

  i2s_pin_config_t pin_config = {
      .bck_io_num = PIN_I2S_BCK,
      .ws_io_num = PIN_I2S_WS,
      .data_out_num = PIN_I2S_DATA_OUT,
      .data_in_num = I2S_PIN_NO_CHANGE 
  };

  // --- Step 4: Install Driver ---
    
    Serial.println(">>> DEBUG: Force uninstalling I2S driver before playback...");
    i2s_driver_uninstall(I2S_NUM_0);
    delay(20);
    Serial.println(">>> DEBUG: Installing I2S driver for playback...");

    esp_err_t install_result = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
     if (install_result != ESP_OK) {
        Serial.printf("Error installing I2S driver: %d\n", install_result);
        audioFile.close();
        return;
    }
    esp_err_t pin_result = i2s_set_pin(I2S_NUM_0, &pin_config);
     if (pin_result != ESP_OK) {
        Serial.printf("Error setting I2S pins: %d\n", pin_result);
        audioFile.close();
        i2s_driver_uninstall(I2S_NUM_0);
        return;
    }
    i2s_zero_dma_buffer(I2S_NUM_0);
    delay(50); 

    // --- Step 5: Playback Loop ---
    audioFile.seek(44); // Skip header

    const size_t bufferSize = 2048; 
    uint8_t buffer[bufferSize];
    size_t bytes_written = 0;
    size_t total_bytes_read = 0; 

    Serial.println("Starting playback...");
    while (audioFile.available()) {
        int bytesRead = audioFile.read(buffer, bufferSize);
        if (bytesRead <= 0) break;
        total_bytes_read += bytesRead; 

        // Write data to I2S
        esp_err_t write_result = i2s_write(I2S_NUM_0, buffer, bytesRead, &bytes_written, portMAX_DELAY);
        if (write_result != ESP_OK) {
            Serial.printf("Error writing to I2S: %d\n", write_result);
            break; 
        }
    }
     Serial.printf("Playback finished. Bytes read: %u\n", total_bytes_read);

    // --- Step 6: Cleanup ---
    audioFile.close();
    i2s_zero_dma_buffer(I2S_NUM_0); 
    delay(100); 
    i2s_driver_uninstall(I2S_NUM_0);
    
    Serial.println("I2S driver uninstalled.");
}

// --- WAV Header Function (Helper) ---
void EmilyBrain::createWavHeader(byte* header, size_t total_data_size) {
    const int sampleRate = 16000;
    const int bitsPerSample = 16;
    const int channels = 1;

    long total_file_size = total_data_size + 36;
    long byteRate = sampleRate * channels * bitsPerSample / 8;
    int blockAlign = channels * bitsPerSample / 8;

    memcpy(&header[0], "RIFF", 4);                                      
    header[4] = (byte)(total_file_size & 0xFF);                         
    header[5] = (byte)((total_file_size >> 8) & 0xFF);
    header[6] = (byte)((total_file_size >> 16) & 0xFF);
    header[7] = (byte)((total_file_size >> 24) & 0xFF);
    memcpy(&header[8], "WAVE", 4);                                      
    memcpy(&header[12], "fmt ", 4);                                     
    header[16] = 16;                                                    
    header[17] = 0;
    header[18] = 0;
    header[19] = 0;
    header[20] = 1;                                                     
    header[21] = 0;
    header[22] = channels;                                              
    header[23] = 0;
    header[24] = (byte)(sampleRate & 0xFF);                             
    header[25] = (byte)((sampleRate >> 8) & 0xFF);
    header[26] = 0;
    header[27] = 0;
    header[28] = (byte)(byteRate & 0xFF);                               
    header[29] = (byte)((byteRate >> 8) & 0xFF);
    header[30] = (byte)((byteRate >> 16) & 0xFF);
    header[31] = (byte)((byteRate >> 24) & 0xFF);
    header[32] = blockAlign;                                            
    header[33] = 0;
    header[34] = bitsPerSample;                                         
    header[35] = 0;
    memcpy(&header[36], "data", 4);                                     
    header[40] = (byte)(total_data_size & 0xFF);                        
    header[41] = (byte)((total_data_size >> 8) & 0xFF);
    header[42] = (byte)((total_data_size >> 16) & 0xFF);
    header[43] = (byte)((total_data_size >> 24) & 0xFF);
}

WavHeader EmilyBrain::parseWavHeader(const char* path) {
    WavHeader header;
    File file = SD.open(path);
    if (!file) {
        Serial.printf("parseWavHeader ERROR: Could not open file %s\n", path);
        return header; 
    }

    if (file.size() < 44) {
        Serial.printf("parseWavHeader ERROR: File %s too small.\n", path);
        file.close();
        return header; 
    }

    byte buffer[44];
    file.read(buffer, 44);
    file.close(); 

    // Bytes 22-23: Channels
    header.channels = (buffer[23] << 8) | buffer[22];
    // Bytes 24-27: Sample Rate
    header.sampleRate = (buffer[27] << 24) | (buffer[26] << 16) | (buffer[25] << 8) | buffer[24];
    // Bytes 34-35: Bits per Sample
    header.bitsPerSample = (buffer[35] << 8) | buffer[34];

    return header;
}

bool EmilyBrain::recordAudioToWav(const char* filename) {
    Serial.println("Starting VAD Recording to WAV...");

    // --- Step 1: Configure I2S for RX (Mic) ---
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX), 
        .sample_rate = 16000,                               
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,       
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 256,
        .use_apll = true 
    };
    i2s_pin_config_t pin_config = {
        .bck_io_num = PIN_I2S_BCK,       
        .ws_io_num = PIN_I2S_WS,        
        .data_out_num = I2S_PIN_NO_CHANGE, 
        .data_in_num = PIN_I2S_DATA_IN   
    };

    i2s_driver_uninstall(I2S_NUM_0);
    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);
    i2s_zero_dma_buffer(I2S_NUM_0); 
    delay(50);

    // --- Step 2: VAD Logic ---
    File file = SD.open(filename, FILE_WRITE);
    if (!file) { i2s_driver_uninstall(I2S_NUM_0); return false; }

    byte wav_header[44];
    createWavHeader(wav_header, 0); // Write dummy header
    file.write(wav_header, 44);

    const int record_buffer_size = 1024;
    byte record_buffer[record_buffer_size];
    size_t bytes_read;
    long silence_started_at = 0;
    bool speech_started = false; 
    size_t total_data_size = 0;
    long recording_started_at = 0;
    int quiet_buffers_in_a_row = 0; 

    // --- Calibration ---
    Serial.println("VAD: Waiting for initial silence...");
    setState(EmilyState::AWAITING_SPEECH); 
    while (quiet_buffers_in_a_row < 10) { 
        esp_err_t read_result = i2s_read(I2S_NUM_0, record_buffer, record_buffer_size, &bytes_read, (100 / portTICK_PERIOD_MS));
        if (read_result == ESP_OK && bytes_read > 0) {
            long long total_amplitude = 0;
            for (int i = 0; i < bytes_read; i += 2) {
                total_amplitude += abs((int16_t)(record_buffer[i] | (record_buffer[i+1] << 8)));
            }
            int average_amplitude = (bytes_read > 0) ? (total_amplitude / (bytes_read / 2)) : 0;

            if (average_amplitude < SILENCE_THRESHOLD) {
                quiet_buffers_in_a_row++;
            } else {
                quiet_buffers_in_a_row = 0;
            }
        } 
    }
    Serial.println("VAD: Silence confirmed. Listening...");

    // --- Main VAD Loop ---
    recording_started_at = millis(); 
    while (true) {
        if (millis() - recording_started_at > MAX_RECORDING_MS) {
            Serial.println("VAD: Max recording duration reached.");
            break;
        }

        esp_err_t read_result = i2s_read(I2S_NUM_0, record_buffer, record_buffer_size, &bytes_read, (100 / portTICK_PERIOD_MS));

        if (read_result == ESP_OK && bytes_read > 0) {
            long long total_amplitude = 0;
            for (int i = 0; i < bytes_read; i += 2) {
                total_amplitude += abs((int16_t)(record_buffer[i] | (record_buffer[i+1] << 8)));
            }
             int average_amplitude = (bytes_read > 0) ? (total_amplitude / (bytes_read / 2)) : 0;

            if (!speech_started && average_amplitude > SPEECH_START_THRESHOLD) {
                speech_started = true;
                setState(EmilyState::RECORDING_SPEECH); 
                Serial.println("VAD: Speech started, recording...");
            }

            if (speech_started) {
                file.write(record_buffer, bytes_read);
                total_data_size += bytes_read;
                Serial.print("+"); 

                if (average_amplitude < SILENCE_THRESHOLD) {
                    if (silence_started_at == 0) { silence_started_at = millis(); }
                    if (millis() - silence_started_at > SILENCE_DURATION_MS) {
                        Serial.println("VAD: Silence detected, stopping recording.");
                        break; 
                    }
                } else {
                    silence_started_at = 0; 
                }
            }
        } else if (read_result == ESP_ERR_TIMEOUT) {
             if (speech_started && silence_started_at > 0 && (millis() - silence_started_at > SILENCE_DURATION_MS)) {
                break;
             }
        } else {
             break; 
        }
    } 

    // --- Update Header ---
    if (total_data_size > 0) {
        createWavHeader(wav_header, total_data_size);
        file.seek(0);
        file.write(wav_header, 44);
        Serial.printf("VAD: Recording complete. %u bytes written.\n", total_data_size);
    } else {
        Serial.println("VAD: No audio captured.");
        speech_started = false; 
    }
    file.close();
    i2s_driver_uninstall(I2S_NUM_0); 
    i2s_set_pin(I2S_NUM_0, NULL); 
    delay(300); 
    return speech_started; 
}

void EmilyBrain::logInteractionToSd(JsonObject log_data) {
    File history_file = SD.open("/chat_history.jsonl", FILE_APPEND);
    if (history_file) {
        serializeJson(log_data, history_file);
        history_file.println();
        history_file.close();
    } else {
        Serial.println("ERROR: Could not open chat_history.jsonl");
    }
}

void EmilyBrain::logInteractionToSd_Error(const char* role, String tool_call_id, String tool_name, String content) {
     StaticJsonDocument<512> log_doc;
     log_doc["role"] = role;
     log_doc["tool_call_id"] = tool_call_id;
     log_doc["name"] = tool_name;
     log_doc["content"] = content;
     logInteractionToSd(log_doc.as<JsonObject>());
}

void EmilyBrain::transcribeAudioFromSd(const char* filename) {
    File audioFile = SD.open(filename, FILE_READ);
    if (!audioFile) { processSttResponseAndTriggerAi("{\"error\":\"Could not read audio file.\"}"); return; }
    size_t file_size = audioFile.size();
    if (file_size <= 44) { audioFile.close(); processSttResponseAndTriggerAi("{\"error\":\"Audio file empty or invalid.\"}"); return; }

    Serial.printf("Starting STT streaming upload of %s (%d bytes)...\n", filename, file_size);
    setState(EmilyState::PROCESSING_STT); 

    String boundary = "----EmilyBoundary" + String(random(0xFFFFF), HEX); 
    String host = "api.openai.com";
    String url = "/v1/audio/transcriptions";

    String prefix = "--" + boundary + "\r\n";
    prefix += "Content-Disposition: form-data; name=\"model\"\r\n\r\nwhisper-1\r\n";
    prefix += "--" + boundary + "\r\n";
    prefix += "Content-Disposition: form-data; name=\"language\"\r\n\r\nen\r\n"; // Changed to EN for international version
    prefix += "--" + boundary + "\r\n";
    prefix += "Content-Disposition: form-data; name=\"file\"; filename=\"stt_input.wav\"\r\n";
    prefix += "Content-Type: audio/wav\r\n\r\n";
    String suffix = "\r\n--" + boundary + "--\r\n";
    size_t content_length = prefix.length() + file_size + suffix.length();

    WiFiClientSecure client;
    client.setInsecure();
    if (!client.connect(host.c_str(), 443)) { audioFile.close(); processSttResponseAndTriggerAi("{\"error\":\"Connection failed\"}"); return; }
    
    client.println("POST " + url + " HTTP/1.1");
    client.println("Host: " + host);
    client.println("Authorization: Bearer " + String(OPENAI_API_KEY));
    client.println("Content-Type: multipart/form-data; boundary=" + boundary);
    client.println("Content-Length: " + String(content_length));
    client.println("Connection: close");
    client.println(); 

    client.print(prefix); 
    uint8_t buffer[1024];
    while (audioFile.available()) {
        size_t bytes_read = audioFile.read(buffer, sizeof(buffer));
        client.write(buffer, bytes_read);
    }
    audioFile.close();
    client.print(suffix); 

    String response_full = "";
    unsigned long timeout = millis();
    while (client.connected() && millis() - timeout < 30000) { 
         if (client.available()) {
            response_full += client.readString(); 
            timeout = millis(); 
         }
         delay(10); 
    }
    client.stop();

    int body_start_index = response_full.indexOf("\r\n\r\n");
    String response_body = "{\"error\":\"Invalid server response\"}"; 
    if (body_start_index != -1) {
        response_body = response_full.substring(body_start_index + 4);
        response_body.trim();
    }
    processSttResponseAndTriggerAi(response_body); 
}

void EmilyBrain::processSttResponseAndTriggerAi(const String& raw_api_payload) {
    Serial.println("Parsing STT transcript...");

    StaticJsonDocument<512> data_doc; 
    DeserializationError error = deserializeJson(data_doc, raw_api_payload);
    String transcript = "";
    String error_msg = "";

    if (!error && data_doc.containsKey("text")) {
        transcript = data_doc["text"].as<String>();
        Serial.printf("Transcript: '%s'\n", transcript.c_str());
    } else if (!error && data_doc.containsKey("error")) {
        error_msg = data_doc["error"].as<String>();
    } else {
        error_msg = "STT format error.";
    }
    
   if (!transcript.isEmpty()) {
    String trigger_string = "User said: " + transcript;
    _start_ai_cycle(trigger_string.c_str()); 
    } else {
        String trigger_string = "Listening failed. Error: " + error_msg;
        _start_ai_cycle(trigger_string.c_str()); 
    }
}

void EmilyBrain::listenAndTranscribe() {
    Serial.println("Starting VAD & Transcription process...");
    const char* filename = "/stt_input.wav";

    bool recording_success = recordAudioToWav(filename);

    if (recording_success) {
        transcribeAudioFromSd(filename); 
    } else {
        Serial.println("No speech detected. Returning to IDLE.");
        processSttResponseAndTriggerAi("{\"error\":\"No speech detected\"}"); 
    }
}

// --- The Executor (Task Handler) ---
void EmilyBrain::_continue_task() {
    
    if (task_queue.empty()) {
        Serial.println("Executor: Queue empty. Mission complete.");
        if (!active_tool_call_id.isEmpty()) {
            logInteractionToSd_Error("tool", active_tool_call_id, active_tool_call_name, "SUCCESS: Action completed.");
             active_tool_call_id = ""; 
             active_tool_call_name = "";
        }
        setState(EmilyState::IDLE); 
        return;
    }

    Task& next_task = task_queue.front(); 
    Serial.printf("Executor: Starting task '%s'\n", next_task.type.c_str());
    serializeJson(next_task.args, Serial);
    Serial.println();

    bool task_completed_immediately = false; 

    if (next_task.type == "PB_EMOTION") {
        arousal = next_task.args["new_arousal"] | arousal; 
        valence = next_task.args["new_valence"] | valence; 
        
        // Homeostasis check
        if (arousal <= AROUSAL_THRESHOLD) {
             if (current_arousal_context != nullptr) {
                Serial.println("Executor: Homeostasis reached, clearing context.");
                current_arousal_context = nullptr;
             }
        }
        task_completed_immediately = true; 
    }
    else if (next_task.type == "CB_SOUND" || next_task.type == "CB_SOUND_EMOTION") {
        setState(EmilyState::PLAYING_SOUND);
    }

    else if (next_task.type == "CAMCANVAS_MOVE_HEAD") {
        String command_string;
        StaticJsonDocument<128> cmd_doc;
        cmd_doc["command"] = "move_head";
        cmd_doc["pan"] = next_task.args["pan"];
        cmd_doc["tilt"] = next_task.args["tilt"];
        serializeJson(cmd_doc, command_string);

        udp.beginPacket(CAMCANVAS_IP_ADDRESS, CAMCANVAS_UDP_PORT);
        udp.print(command_string);
        udp.endPacket();

        task_completed_immediately = true; 
    }

    else if (next_task.type == "OS_MOVE" || next_task.type == "OS_CALIBRATE") { 
        physical_task_start_time = millis(); 

        String command;
        if (next_task.type == "OS_MOVE") {
             float angle = next_task.args["angle"] | 0.0f;
             command = "CMD:MOVE," + String(angle, 1);
        } else { // OS_CALIBRATE
             command = "CMD:CALIBRATE";
        }

        udp.beginPacket(OS_IP_ADDRESS, OS_UDP_PORT);
        udp.print(command);
        udp.endPacket();

        setState(EmilyState::EXECUTING_PHYSICAL_TOOL);
    }

    else if (next_task.type == "CB_SPEAK") {
        const char* text_to_speak = nullptr; 

        if (next_task.args.containsKey("announcement")) {
            text_to_speak = next_task.args["announcement"];
        } else if (next_task.args.containsKey("question")) {
            text_to_speak = next_task.args["question"];
        }

        if (text_to_speak != nullptr && strlen(text_to_speak) > 0) {
             Serial.printf("Executor: Starting TTS for: '%s'\n", text_to_speak);
            last_display_text = text_to_speak;
             if (active_tool_call_name == "start_conversation") {
                 next_state_after_audio = EmilyState::AWAITING_SPEECH; 
             } else { 
                 next_state_after_audio = EmilyState::IDLE;
             }

             processTtsRequest(text_to_speak); 
             tts_task_start_time = millis();
        } else {
             task_completed_immediately = true; 
        }
    }

    else if (next_task.type == "CAM_ANALYZE") {
        String question = next_task.args["question"] | "Describe what you see.";
        bool use_flash_bool = next_task.args["use_flash"].as<bool>(); 
        int use_flash_int = use_flash_bool ? 1 : 0;

        StaticJsonDocument<256> cmd_doc; 
        cmd_doc["command"] = "analyze_vision";
        cmd_doc["prompt"] = question;
        cmd_doc["use_flash"] = use_flash_int;
        String command_string;
        serializeJson(cmd_doc, command_string);

        udp.beginPacket(CAMCANVAS_IP_ADDRESS, CAMCANVAS_UDP_PORT);
        udp.print(command_string);
        udp.endPacket();

        camcanvas_task_start_time = millis(); 
        setState(EmilyState::SEEING); 
    }

    else if (next_task.type == "CAM_TILT") {
        int angle = next_task.args["angle"] | 90; 

        StaticJsonDocument<128> cmd_doc;
        cmd_doc["command"] = "tilt_head";
        cmd_doc["angle"] = angle;
        String command_string;
        serializeJson(cmd_doc, command_string);

        udp.beginPacket(CAMCANVAS_IP_ADDRESS, CAMCANVAS_UDP_PORT); 
        udp.print(command_string);
        udp.endPacket();

        task_completed_immediately = true; 
    }

    else if (next_task.type == "CAM_NOD") {
        int angle = next_task.args["angle"] | 90; 

        StaticJsonDocument<128> cmd_doc;
        cmd_doc["command"] = "knod_head"; // Keep internal command string as is or change in CAM code too
        cmd_doc["angle"] = angle;
        String command_string;
        serializeJson(cmd_doc, command_string);

        udp.beginPacket(CAMCANVAS_IP_ADDRESS, CAMCANVAS_UDP_PORT); 
        udp.print(command_string);
        udp.endPacket();

        task_completed_immediately = true; 
    }

    else if (next_task.type == "CAM_TAKE_PICTURE") {
        StaticJsonDocument<64> cmd_doc;
        cmd_doc["command"] = "take_picture"; 
        String command_string;
        serializeJson(cmd_doc, command_string);

        udp.beginPacket(CAMCANVAS_IP_ADDRESS, CAMCANVAS_UDP_PORT);
        udp.print(command_string);
        udp.endPacket();

        task_completed_immediately = true; 
    }

    else if (next_task.type == "CAMCANVAS_SET_LED") {
        String command_string;
        serializeJson(next_task.args, command_string); 

        udp.beginPacket(CAMCANVAS_IP_ADDRESS, CAMCANVAS_UDP_PORT);
        udp.print(command_string);
        udp.endPacket();

        task_completed_immediately = true; 
    }
     
    else if (next_task.type == "CANVAS_IMAGE_SYNC") {
        String prompt = next_task.args["prompt"] | "a robot";
        String model = next_task.args["image_model"] | "venice-sd35"; 

        StaticJsonDocument<512> cmd_doc; 
        cmd_doc["command"] = "generate_image";
        cmd_doc["prompt"] = prompt;
        cmd_doc["model"] = model;
        String command_string;
        serializeJson(cmd_doc, command_string);

        udp.beginPacket(CAMCANVAS_IP_ADDRESS, CAMCANVAS_UDP_PORT);
        udp.print(command_string);
        udp.endPacket();

        camcanvas_task_start_time = millis(); 
        setState(EmilyState::VISUALIZING); 
    }

    else if (next_task.type == "CANVAS_IMAGE_ASYNC") {
        String prompt = next_task.args["prompt"] | "a robot";
        String model = next_task.args["image_model"] | "venice-sd35";

        StaticJsonDocument<512> cmd_doc;
        cmd_doc["command"] = "generate_image";
        cmd_doc["prompt"] = prompt;
        cmd_doc["model"] = model;
        String command_string;
        serializeJson(cmd_doc, command_string);

        udp.beginPacket(CAMCANVAS_IP_ADDRESS, CAMCANVAS_UDP_PORT);
        udp.print(command_string);
        udp.endPacket();

        task_completed_immediately = true; // ASYNC: Don't wait
    }
    
    else if (next_task.type == "INPUTPAD_SET_MODE") {
        StaticJsonDocument<256> cmd_doc;
        cmd_doc["command"] = "set_mode";
        cmd_doc["mode"] = next_task.args["mode"];
        if (next_task.args.containsKey("max_value")) {
             cmd_doc["max"] = next_task.args["max_value"];
        }
        
        String command_string;
        serializeJson(cmd_doc, command_string);

        udp.beginPacket(INPUTPAD_IP_ADDRESS, INPUTPAD_UDP_PORT);
        udp.print(command_string);
        udp.endPacket();

        input_task_start_time = millis(); 
        setState(EmilyState::AWAITING_INPUT); 
    }
    
    else if (next_task.type == "EB_GET_LOCAL_DATA") {
        setState(EmilyState::PROCESSING_GAMEDATA);
    }
    
    else {
        Serial.printf("Executor Error: Unknown task type '%s'\n", next_task.type.c_str());
        task_completed_immediately = true;
    }

    if (task_completed_immediately) {
        physical_task_start_time = 0; 
        task_queue.pop_front(); 
        _continue_task();       
    }
}

String EmilyBrain::getDataFromJson(const char* key) {
    if (strcmp(key, "ERROR_KEY") == 0) {
        return "ERROR"; 
    }
    
    File file = SD.open("/adventure.json");
    if (!file) { return "ERROR"; }

    DynamicJsonDocument filter(JSON_OBJECT_SIZE(1));
    filter[key] = true; 

    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, file, DeserializationOption::Filter(filter));
    file.close();

    if (error || !doc.containsKey(key)) {
        return "ERROR";
    }

    JsonObject field_info = doc[key]; 
    String output_string;
    serializeJson(field_info, output_string);
    
    return output_string;
}

void EmilyBrain::addSignificantEvent(const String& event_desc) {
    if (significant_events.size() >= MAX_SIGNIFICANT_EVENTS) {
        significant_events.pop_front(); 
    }
    significant_events.push_back(event_desc);
    Serial.println("DEBUG: Added significant event: " + event_desc);
}

bool EmilyBrain::downloadTtsToSd(const char* textToSpeak, const char* filename) {
    
    setState(EmilyState::GENERATING_SPEECH);
    Serial.printf("TTS Download: Requesting audio for '%s' to %s\n", textToSpeak, filename);
    WiFiClientSecure https_client;
    HTTPClient http;
    https_client.setInsecure();

    if (http.begin(https_client, "https://api.venice.ai/api/v1/audio/speech")) {
        String authHeader = "Bearer " + String(VENICE_API_KEY); 
        http.addHeader("Authorization", authHeader);
        http.addHeader("Content-Type", "application/json");

        StaticJsonDocument<256> payload_doc; 
        payload_doc["model"] = "tts-kokoro";
        payload_doc["input"] = textToSpeak;
        payload_doc["voice"] = "af_nova";
        payload_doc["response_format"] = "wav";
        String payload_string;
        serializeJson(payload_doc, payload_string);

        int httpCode = http.POST(payload_string);

        if (httpCode == HTTP_CODE_OK) {
            File file = SD.open(filename, FILE_WRITE);
            if (file) {
                int bytesWritten = http.writeToStream(&file); 
                file.close();
                if (bytesWritten > 0) {
                     Serial.printf("TTS Download: Success (%d bytes written).\n", bytesWritten);
                     http.end(); 
                     return true;
                } else {
                     Serial.println("TTS Download ERROR: Failed to write to SD file.");
                }
            } else {
                Serial.println("TTS Download ERROR: Could not open SD file.");
            }
        } else {
            Serial.printf("TTS Download ERROR: API Error Code %d\n", httpCode);
        }
        http.end(); 
    } else {
        Serial.println("TTS Download ERROR: Failed to connect to API.");
    }
    return false; 
}

void EmilyBrain::processTtsRequest(const char* text) {
    Serial.println("Processing TTS request...");
    const char* filename = "/tts_output.wav";
    tts_task_start_time = millis(); 

    bool download_success = downloadTtsToSd(text, filename);

    if (download_success) {
        setState(EmilyState::SPEAKING); 
    } else {
        Serial.println("TTS Request FAILED.");
        if (!active_tool_call_id.isEmpty()) {
             logInteractionToSd_Error("tool", active_tool_call_id, active_tool_call_name, "ERROR: Failed to generate speech audio.");
        }
         setState(EmilyState::IDLE); 
         tts_task_start_time = 0; 
    }
}

String EmilyBrain::buildSelfAwarenessReport(JsonObject device_status) {
    String report = "--- SELF-AWARENESS REPORT ---\n";
    report += "This section describes my physical body and its capabilities.\n\n";
    report += DESC_CORE; 
    report += "\nMy physical body has the following additional components currently online:\n";

    if (device_status["os_online"]) {
        report += DESC_OS;
    }
    if (device_status["camcanvas_online"]) {
        report += DESC_CAMCANVAS;
    }
    if (device_status["inputpad_online"]) {
        report += DESC_INPUTPAD;
    }
    return report;
}

void EmilyBrain::addChatHistoryToMessages(JsonArray messages, int max_history_items) {
    File history_file = SD.open("/chat_history.jsonl");
    if (!history_file || !history_file.size()) {
        Serial.println("Chat history not found or empty.");
        if (history_file) history_file.close();
        return;
    }

    std::deque<String> history_lines;
    while (history_file.available()) {
        String line = history_file.readStringUntil('\n');
        line.trim(); 
        if (line.length() > 0) {
            history_lines.push_back(line);
            if (history_lines.size() > max_history_items) {
                history_lines.pop_front();
            }
        }
    }
    history_file.close();

    for (const String& line : history_lines) {
        StaticJsonDocument<4096> history_item_doc; 
        DeserializationError error = deserializeJson(history_item_doc, line);

        if (error == DeserializationError::Ok) {
            messages.add(history_item_doc.as<JsonObject>());
        } 
    }
}

void EmilyBrain::buildAiPayload(StaticJsonDocument<JSON_DOC_CAPACITY>& api_payload_doc, 
                                const char* current_prompt_content,
                                JsonObject device_status) {

    Serial.println("DEBUG: Entering buildAiPayload...");

    // --- Step A: Read & Filter Tools ---
    File tools_file = SD.open("/tools_config.json");
    if (tools_file) {
        StaticJsonDocument<16384> all_tools_doc; 
        DeserializationError error = deserializeJson(all_tools_doc, tools_file);
        tools_file.close();
        
        if (!error) {
            JsonArray filtered_tools = api_payload_doc.createNestedArray("tools");
            for (JsonObject tool : all_tools_doc.as<JsonArray>()) {
                bool is_tool_available = true;
                JsonObject function_obj = tool["function"];
                JsonArray required = function_obj["required_devices"];

                if (required) { 
                    for (JsonVariant device_var : required) { 
                        const char* device = device_var.as<const char*>();
                         if (!device) continue; 

                        if (strcmp(device, "OS") == 0 && !device_status["os_online"]) {
                            is_tool_available = false; break;
                        }
                        if (strcmp(device, "CAMCANVAS") == 0 && !device_status["camcanvas_online"]) {
                            is_tool_available = false; break;
                        }
                        if (strcmp(device, "INPUTPAD") == 0 && !device_status["inputpad_online"]) {
                            is_tool_available = false; break;
                        }
                    }
                }

                if (is_tool_available) {
                    JsonObject new_tool = filtered_tools.createNestedObject();
                    new_tool["type"] = tool["type"];
                    JsonObject new_function = new_tool.createNestedObject("function");
                    for (JsonPair kvp : function_obj) {
                        if (strcmp(kvp.key().c_str(), "required_devices") != 0) {
                            new_function[kvp.key()] = kvp.value();
                        }
                    }
                }
            }
            if (api_payload_doc.overflowed()) { Serial.println("FATAL: Overflow after tools!"); return; } 
        } 
    } 

    // --- Step B: Build Messages ---
    api_payload_doc["model"] = "llama-3.3-70b";
    JsonArray messages = api_payload_doc.createNestedArray("messages");

    // 1. System Prompt
    String self_awareness_report = buildSelfAwarenessReport(device_status);
    String final_system_prompt = self_awareness_report + "\n\n" + system_prompt_content;
    JsonObject system_msg = messages.createNestedObject();
    system_msg["role"] = "system";
    system_msg["content"] = final_system_prompt;
    
    // 2. Chat History
    addChatHistoryToMessages(messages, 120);
    
    // 3. User Message
    JsonObject user_msg = messages.createNestedObject();
    user_msg["role"] = "user";
    user_msg["content"] = current_prompt_content;
}

void EmilyBrain::decayArousal() {
    unsigned long current_time = millis();
    if (arousal > 0.0 && (current_time - last_decay_time) >= 1000) { 
        double decay_amount = AROUSAL_DECAY_RATE;
        arousal -= decay_amount;
        if (arousal < 0.0) {
            arousal = 0.0; 
        }
        last_decay_time = current_time;
    }
}

void EmilyBrain::loadConfigurations() {
    Serial.println("Loading configurations from SD card...");

    File prompt_file = SD.open("/system_prompt.txt");
    if (prompt_file) {
        system_prompt_content = prompt_file.readString();
        prompt_file.close();
    } else {
        Serial.println("ERROR: Could not open system_prompt.txt!");
        system_prompt_content = "You are a helpful assistant.";
    }
}

void EmilyBrain::sendPings() {
    unsigned long current_time = millis();
    if (current_time - last_ping_time >= PING_INTERVAL_MS) {
        if (wifi_status == WiFiStatus::CONNECTED) { 
            const char* ping_msg = "PING";
            udp.beginPacket(OS_IP_ADDRESS, OS_UDP_PORT);
            udp.print(ping_msg);
            udp.endPacket();

            udp.beginPacket(CAMCANVAS_IP_ADDRESS, CAMCANVAS_UDP_PORT);
            udp.print(ping_msg);
            udp.endPacket();

            udp.beginPacket(INPUTPAD_IP_ADDRESS, INPUTPAD_UDP_PORT);
            udp.print(ping_msg);
            udp.endPacket();
        }
        last_ping_time = current_time;
    }
}


void EmilyBrain::handleUdpPackets() {
    if (wifi_status != WiFiStatus::CONNECTED) {
        return; 
    }

    int packetSize = udp.parsePacket();
    if (packetSize) {
        char packetBuffer[1536]; 
        int len = udp.read(packetBuffer, 1536);
        if (len > 0) {
            packetBuffer[len] = 0; 
        } else {
            udp.flush(); 
            return; 
        }

        String sender_ip_str = udp.remoteIP().toString();
        unsigned long current_time = millis();

        // --- Identify Sender & Update Status ---
        if (sender_ip_str == OS_IP_ADDRESS) {
            last_os_contact = current_time;
            if (!os_connected) { os_connected = true; }
            
            StaticJsonDocument<512> os_doc; 
            DeserializationError error = deserializeJson(os_doc, packetBuffer);

            if (!error) {
                os_act_state = os_doc["act_state"] | "Unknown"; 
                os_angle = os_doc["angle"] | 0.0f; // Was: hoek
                current_attention_mode = os_doc["attention_mode"] | "PASSIVE";

                // --- Sound Data ---
                JsonObject sound = os_doc["last_sound"];
                if (!sound.isNull()) {
                    last_sound_int = sound["int"] | 0;
                    last_sound_dir = sound["dir"] | 0.0f;

                    if (last_sound_int >= SOUND_RADAR_THRESHOLD) {
                        if (sound_radar.size() >= MAX_SOUND_EVENTS) {
                            sound_radar.pop_front(); 
                        }
                        SoundEvent current_sound;
                        current_sound.intensity = last_sound_int;
                        current_sound.direction = last_sound_dir;
                        sound_radar.push_back(current_sound); 
                    }
                } else {
                    last_sound_int = 0;
                    last_sound_dir = 0.0;
                }
                
                // --- Significant Event Check ---
                if (isOsEventSignificant(os_doc)) { 
                    String event_desc = "OS Update: ";
                    bool desc_set = false;

                    if (strcmp((os_doc["act_state"] | ""), (prev_os_heartbeat_data["act_state"] | "")) != 0) {
                        event_desc += "State->" + os_act_state;
                        desc_set = true;
                    }
                    if (!desc_set && abs((os_doc["angle"] | 0.0f) - (prev_os_heartbeat_data["angle"] | 0.0f)) > 2.0) {
                        event_desc += "Angle->" + String(os_angle, 0);
                        desc_set = true;
                    }
                    if (!desc_set && strcmp((os_doc["attention_mode"] | ""), (prev_os_heartbeat_data["attention_mode"] | "")) != 0) {
                         event_desc += "Mode->" + current_attention_mode;
                         desc_set = true;
                    }
                    if (!desc_set) { event_desc += "State Updated"; }

                    addSignificantEvent(event_desc);
                    prev_os_heartbeat_data = os_doc; 
                }
            } 
        }

        // --- CAMCANVAS Packets ---
        else if (sender_ip_str == CAMCANVAS_IP_ADDRESS) {
            last_camcanvas_contact = current_time;
            if (!camcanvas_connected) { camcanvas_connected = true; }

            // 1. Expecting VISION?
            if (currentState == EmilyState::SEEING) {
                StaticJsonDocument<1024> vision_doc;
                DeserializationError error = deserializeJson(vision_doc, packetBuffer);
                if (!error && vision_doc.containsKey("result_type") && strcmp(vision_doc["result_type"], "vision") == 0) {
                    last_vision_response.set(vision_doc);
                } 
            }
            
            // 2. Expecting IMAGE COMPLETE?
            else if (currentState == EmilyState::VISUALIZING) {
                StaticJsonDocument<256> confirmation_doc;
                DeserializationError error = deserializeJson(confirmation_doc, packetBuffer);
                if (!error && confirmation_doc.containsKey("result_type") && strcmp(confirmation_doc["result_type"], "image_complete") == 0) {
                    last_camcanvas_confirmation.set(confirmation_doc);
                } 
            }

            // 3. Heartbeat or Photo Confirmation
            else {
                StaticJsonDocument<512> other_doc; 
                DeserializationError error = deserializeJson(other_doc, packetBuffer);
                if (!error && other_doc.containsKey("result_type") && strcmp(other_doc["result_type"], "picture_taken") == 0) {
                    const char* status = other_doc["status"] | "error";
                    if (strcmp(status, "success") == 0) { addSignificantEvent("CAM: Photo saved."); }
                    else { addSignificantEvent("CAM: ERROR saving photo."); }
                }
                else if (other_doc.containsKey("id") && other_doc.containsKey("state")) {
                    current_cam_tilt = other_doc["tilt_angle"] | 100.0f; 
                    current_cam_pan = other_doc["pan_angle"] | 90.0f;
                }
            }
        } 
        
        // --- INPUTPAD Packets ---
        else if (sender_ip_str == INPUTPAD_IP_ADDRESS) {
            last_inputpad_contact = current_time;
            if (!inputpad_connected) { inputpad_connected = true; }
            
            StaticJsonDocument<96> peek_doc;
            deserializeJson(peek_doc, packetBuffer); 
            const char* event_type = peek_doc["event"] | "unknown";

            if (strcmp(event_type, "input_received") == 0 && currentState == EmilyState::AWAITING_INPUT) {
                last_inputpad_response.clear();
                deserializeJson(last_inputpad_response, packetBuffer); 
            } 
        }
        udp.flush();
    }
}

void EmilyBrain::checkTimeouts() {
    unsigned long current_time = millis();

    if (os_connected && (current_time - last_os_contact > CONNECTION_TIMEOUT_MS) &&
        currentState != EmilyState::EXECUTING_PHYSICAL_TOOL) 
    {
        Serial.println("OS Disconnected (Timeout).");
        os_connected = false;
        os_act_state = "Offline";
        current_attention_mode = "PASSIVE";
    }
    
    if (camcanvas_connected && (current_time - last_camcanvas_contact > CONNECTION_TIMEOUT_MS) &&
        currentState != EmilyState::SEEING &&            
        currentState != EmilyState::VISUALIZING)      
    {
        Serial.println("CamCanvas Disconnected (Timeout).");
        camcanvas_connected = false;
    }
    
    if (inputpad_connected && (current_time - last_inputpad_contact > CONNECTION_TIMEOUT_MS) &&
        currentState != EmilyState::AWAITING_INPUT)   
    {
        Serial.println("InputPad Disconnected (Timeout).");
        inputpad_connected = false;
    }
}

void EmilyBrain::renderDisplay() {
    bool force_redraw = false;
    static bool layout_drawn = false;
    
    // --- Static Layout ---
    if (!layout_drawn) {
        display.fillScreen(TFT_BLACK);
        display.fillRect(0, 0, display.width(), 30, TFT_DARKCYAN);
        display.setTextColor(TFT_WHITE, TFT_DARKCYAN);
        display.drawString("Emily", 5, 5, 4);

        int y_lineA = 45; int y_lineV = 60;
        int y_line1 = 75; int y_line2 = 90;
        int x_col1_lbl = 10;
        
        display.setTextColor(TFT_WHITE, TFT_BLACK);
        display.drawString("A:", x_col1_lbl, y_lineA + 2, 1); 
        display.drawString("V:", x_col1_lbl, y_lineV + 2, 1); 
        display.drawString("State:", x_col1_lbl, y_line1 + 2, 1); // Was "Staat"
        display.drawString("CC:", x_col1_lbl, y_line2 + 2, 1);
        layout_drawn = true;
    }

    // --- Connectivity Dots ---
    if (wifi_status != prev_wifi_status || force_redraw) {
        int wifiX = display.width() - 25; int wifiY = 15; int radius = 6;
        uint16_t wifiColor = (wifi_status == WiFiStatus::CONNECTED) ? TFT_BLUE : TFT_RED;
        display.fillCircle(wifiX, wifiY, radius, wifiColor);
        prev_wifi_status = wifi_status;
    }
    if (camcanvas_connected != prev_camcanvas_connected || force_redraw) {
        int ccDotX = display.width() - 75; int cY = 15; int radius = 6;
        uint16_t ccDotColor = camcanvas_connected ? TFT_GREEN : TFT_RED;
        display.fillCircle(ccDotX, cY, radius, ccDotColor);
        prev_camcanvas_connected = camcanvas_connected; 
    }
    if (inputpad_connected != prev_inputpad_connected || force_redraw) {
        int ipDotX = display.width() - 100; 
        int ipY = 15; int radius = 6;
        uint16_t ipDotColor = inputpad_connected ? TFT_GREEN : TFT_RED;
        display.fillCircle(ipDotX, ipY, radius, ipDotColor);
        prev_inputpad_connected = inputpad_connected;
    }

    // --- Arousal Bar ---
    if (arousal != prev_arousal || force_redraw) {
        int barY = 40; int barX = 30; int barHeight = 10;
        int barWidth = display.width() - barX - 10;
        int filledWidth = (int)(arousal * barWidth);
        display.fillRect(barX, barY, barWidth, barHeight, TFT_DARKGREY);
        if (filledWidth > 0) display.fillRect(barX, barY, filledWidth, barHeight, TFT_RED);
        prev_arousal = arousal;
    }

    // --- Valence Bar ---
    if (valence != prev_valence || force_redraw) {
        int barY = 55; int barX = 30; int barHeight = 10;
        int barWidth = display.width() - barX - 10;
        int filledWidth = (int)(((valence + 1.0) / 2.0) * barWidth);
        int zeroPoint = barWidth / 2;
        display.fillRect(barX, barY, barWidth, barHeight, TFT_DARKGREY);
        uint16_t valenceColor = (valence >= 0) ? TFT_GREEN : TFT_ORANGE;
         if (filledWidth > zeroPoint) display.fillRect(barX + zeroPoint, barY, filledWidth - zeroPoint, barHeight, valenceColor);
         else if (filledWidth < zeroPoint) display.fillRect(barX + filledWidth, barY, zeroPoint - filledWidth, barHeight, valenceColor);
        prev_valence = valence;
    }

    // --- Dynamic Text Lines ---
    char text_buffer[50]; 
    int y_line1 = 75; int y_line2 = 90; int y_line3 = 105;
    int x_col1_val = 50; int x_col2_lbl = 160; int x_col2_val = 200; 

    // Line 1: State & Mode
    const char* stateStr = stateToString(currentState);
    String modeStr = current_attention_mode; 
    
    if (prev_line1_L != stateStr || prev_line1_R != modeStr) {
        display.fillRect(x_col1_val, y_line1, display.width() - x_col1_val, 10, TFT_BLACK); 
        display.setTextColor(TFT_YELLOW, TFT_BLACK);
        display.drawString(stateStr, x_col1_val, y_line1 + 2, 1);
        
        display.setTextColor(TFT_WHITE, TFT_BLACK);
        display.drawString("Mode:", x_col2_lbl, y_line1 + 2, 1);
        display.setTextColor(TFT_CYAN, TFT_BLACK); 
        display.drawString(modeStr, x_col2_val, y_line1 + 2, 1);
        
        prev_line1_L = stateStr;
        prev_line1_R = modeStr;
    }

    // Line 2: CC Status & Pan
    String cc_text = camcanvas_connected ? "Online" : "Offline";
    sprintf(text_buffer, "%.0f deg", current_cam_pan);
    String pan_text = camcanvas_connected ? text_buffer : "N/A";
    
    if (prev_line2_L != cc_text || prev_line2_R != pan_text) {
        display.fillRect(x_col1_val, y_line2, display.width() - x_col1_val, 10, TFT_BLACK);
        display.setTextColor(camcanvas_connected ? TFT_GREEN : TFT_RED, TFT_BLACK);
        display.drawString(cc_text, x_col1_val, y_line2 + 2, 1);
        
        display.setTextColor(TFT_WHITE, TFT_BLACK);
        display.drawString("Pan:", x_col2_lbl, y_line2 + 2, 1); 
        display.setTextColor(camcanvas_connected ? TFT_SKYBLUE : TFT_RED, TFT_BLACK);
        display.drawString(pan_text, x_col2_val, y_line2 + 2, 1);

        prev_line2_L = cc_text;
        prev_line2_R = pan_text;
    }

    // Line 3: Tilt
    sprintf(text_buffer, "%.0f deg", current_cam_tilt);
    String tilt_text = camcanvas_connected ? text_buffer : "N/A";

    if (prev_line3_R != tilt_text) {
        display.fillRect(x_col1_val, y_line3, display.width() - x_col1_val, 10, TFT_BLACK); 
        display.setTextColor(TFT_WHITE, TFT_BLACK);
        display.drawString("Tilt:", x_col2_lbl, y_line3 + 2, 1); 
        display.setTextColor(camcanvas_connected ? TFT_SKYBLUE : TFT_RED, TFT_BLACK);
        display.drawString(tilt_text, x_col2_val, y_line3 + 2, 1); 
        prev_line3_R = tilt_text;
    }

    // --- Chat / Info Window ---
    int chat_start_y = 125; 
    
    if (last_display_text != prev_display_text || force_redraw) {
        display.fillRect(0, chat_start_y, display.width(), display.height() - chat_start_y, TFT_BLACK);
        display.setTextColor(TFT_WHITE, TFT_BLACK);
        display.setTextSize(1); 
        display.setCursor(5, chat_start_y + 2); 
        display.setTextWrap(true);
        display.print(last_display_text);
        prev_display_text = last_display_text; 
    }
}

// --- State Handlers ---
void EmilyBrain::handleIdleState() {
    // Check 1: High Arousal (Must Act)
    if (arousal > AROUSAL_THRESHOLD) {
        if (current_arousal_context != nullptr) {
            Serial.println("Idle Handling: Continuing previous arousal context.");
            _start_ai_cycle("CONTINUATION: Previous action did not resolve high arousal.");
        } else {
            Serial.println("Idle Handling: Arousal is high with NO context. Investigating.");
            _start_ai_cycle("INTERNAL_TRIGGER: Arousal is high without context, investigating cause.");
        }
        return; 
    }

    // Check 2: Low Arousal (Look for triggers)
    if (arousal <= AROUSAL_THRESHOLD) {
        if (current_arousal_context != nullptr) {
             Serial.println("Idle Handling: Arousal low, clearing context.");
             current_arousal_context = nullptr;
        }

        const char* new_trigger_reason = nullptr;
        double new_arousal = 0.0;
        double new_valence = 0.0;
        bool trigger_found = false; 

        // 1. Wake Button
        if (checkWakeButton()) { 
            Serial.println("Idle Handling: Wake Button pressed!");
            new_trigger_reason = "USER_ACTION: Wake button pressed.";
            new_arousal = 0.8; 
            new_valence = 0.2; 
            trigger_found = true;
        }

        // 2. Autonomous Sound (Only if mode is AUTONOMOUS)
        if (!trigger_found && current_attention_mode == "AUTONOMOUS") {
            if (last_sound_int > LOUD_SOUND_THRESHOLD) {
                Serial.println("Idle Handling: Loud Sound trigger.");
                new_trigger_reason = "ENVIRONMENT: Loud sound detected.";
                new_arousal = 0.9;
                new_valence = -0.7;
                trigger_found = true;
            }
        }

        // Trigger found?
        if (trigger_found) {
            arousal = new_arousal;
            valence = new_valence;
            last_decay_time = millis(); 
            current_arousal_context = new_trigger_reason; 

            if (wifi_status == WiFiStatus::CONNECTED) {
                 _start_ai_cycle(current_arousal_context);
            } else {
                 Serial.println("Trigger detected, but WiFi is offline. Cannot start AI cycle.");
            }
        }
    }
}

void EmilyBrain::_start_ai_cycle(const char* trigger_reason) {
    
    Serial.printf("AI Cycle Triggered! Reason: %s\n", trigger_reason);
    
    // --- Log Trigger ---
    StaticJsonDocument<1536> log_doc; 
    log_doc["role"] = "user";
    String report_content = "INTERNAL_REPORT:\n";
    report_content += "- Triggering Event: " + String(trigger_reason) + "\n";
    report_content += "- My Current Emotional State: arousal=" + String(arousal, 2) + ", valence=" + String(valence, 2) + "\n";
    
    log_doc["content"] = report_content;
    logInteractionToSd(log_doc.as<JsonObject>()); 
    
    setState(EmilyState::PROCESSING_AI);
    
    // --- Collect Context Data ---
    StaticJsonDocument<1024> context_doc; 
    context_doc["trigger"] = trigger_reason;
    JsonObject emotion = context_doc.createNestedObject("emotion");
    emotion["arousal"] = arousal;
    emotion["valence"] = valence;

    JsonObject device_status = context_doc.createNestedObject("device_status");
    device_status["os_online"] = os_connected;
    device_status["camcanvas_online"] = camcanvas_connected;
    device_status["inputpad_online"] = inputpad_connected;

    // --- Build Full Prompt String ---
    String ai_prompt = "INTERNAL_REPORT:\n";
    ai_prompt += "- Triggering Event: " + String(trigger_reason) + "\n";
    ai_prompt += "- My Current Emotional State: arousal=" + String(arousal, 2) + ", valence=" + String(valence, 2) + "\n";
    ai_prompt += "- World Summary: Attention Mode='" + current_attention_mode + "', ";
    ai_prompt += "- Head Position: Camera Pan=" + String(current_cam_pan, 0) + " deg, ";
    ai_prompt += "Camera Tilt=" + String(current_cam_tilt, 0) + " deg.\n";
    
    if (current_attention_mode != "PASSIVE" && !sound_radar.empty()) {
         ai_prompt += "- Sound Radar (Recent >" + String(SOUND_RADAR_THRESHOLD) + "): ";
         bool first_sound = true;
         for (auto it = sound_radar.rbegin(); it != sound_radar.rend(); ++it) {
             if (!first_sound) ai_prompt += ", ";
             ai_prompt += "[Int:" + String(it->intensity) + " Dir:" + String(it->direction, 0) + "]";
             first_sound = false;
         }
         ai_prompt += "\n";
    }

    if (!significant_events.empty()) {
        ai_prompt += "- Recent Events: ";
        for (const String& evt : significant_events) {
            ai_prompt += "[" + evt + "] ";
        }
        ai_prompt += "\n";
    }

    Serial.println("Calling processAiProxyRequest...");
    processAiProxyRequest(ai_prompt.c_str(), context_doc["device_status"].as<JsonObject>());
}

void EmilyBrain::handleAwaitingSpeechState() { 
    // Blocking in recordAudioToWav currently.
 }
void EmilyBrain::handleRecordingSpeechState() { 
    // Blocking in recordAudioToWav currently.
 }
void EmilyBrain::handleProcessingSttState() { 
    // Blocking in transcribeAudioFromSd currently.
}

void EmilyBrain::handleProcessGamedataState() {
    Serial.println("Handler: Entering PROCESSING_GAMEDATA state.");

    if (task_queue.empty() || task_queue.front().type != "EB_GET_LOCAL_DATA") {
        Serial.println("Handler ERROR: PROCESSING_GAMEDATA entered with invalid queue.");
        setState(EmilyState::IDLE); 
        return;
    }
    Task& current_task = task_queue.front();

    const char* key = current_task.args["key"] | "ERROR_KEY"; // Key changed to 'key'

    Serial.printf("Handler: Reading local CMS data for key: %s...\n", key);
    String field_data_str = getDataFromJson(key); 

    if (field_data_str == "ERROR") {
        logInteractionToSd_Error("tool", active_tool_call_id, active_tool_call_name, "ERROR: Failed to read local data.");
        String error_trigger = "GAME_DATA_ERROR: Failed to read local game data for key " + String(key);
        task_queue.pop_front();
        _start_ai_cycle(error_trigger.c_str());
    
    } else {
        logInteractionToSd_Error("tool", active_tool_call_id, active_tool_call_name, "SUCCESS: " + field_data_str);
        String success_trigger = "GAME_DATA: Received info: " + field_data_str;
        task_queue.pop_front();
        _start_ai_cycle(success_trigger.c_str());
    }
}

void EmilyBrain::handleProcessingAiState() {
    // Non-blocking simulation wait would happen here.
}

void EmilyBrain::handleGeneratingSpeechState() {
    if (tts_task_start_time != 0 && (millis() - tts_task_start_time > TTS_GENERATION_TIMEOUT_MS)) {
        Serial.println("!!! TIMEOUT during TTS Generation!");
         if (!active_tool_call_id.isEmpty()) {
             logInteractionToSd_Error("tool", active_tool_call_id, active_tool_call_name, "ERROR: Timeout generating speech audio.");
         }
         setState(EmilyState::IDLE); 
         tts_task_start_time = 0; 
    }
}

void EmilyBrain::handleSpeakingState() {
    Serial.println("Handler: Entering SPEAKING state. Starting playback...");
    playWavFromSd("/tts_output.wav"); 

    Serial.println("Handler: Playback finished.");
    tts_task_start_time = 0; 

    if (!task_queue.empty() && task_queue.front().type == "CB_SPEAK") {
        task_queue.pop_front();
    } 

    Serial.printf("Handler: Transitioning to next state: %s\n", stateToString(next_state_after_audio));
    EmilyState intendedNextState = next_state_after_audio; 
    _continue_task(); 

     if (intendedNextState == EmilyState::AWAITING_SPEECH && currentState == EmilyState::IDLE) {
         Serial.println("Handler: Overriding IDLE -> AWAITING_SPEECH.");
         setState(EmilyState::AWAITING_SPEECH);
         listenAndTranscribe();
     }
}

void EmilyBrain::handleExecutingPhysicalToolState() { 
    unsigned long current_time = millis();
    if (physical_task_start_time == 0 || (current_time - physical_task_start_time < 200)) {
         return; // Grace period
    }
    
    if (os_connected && os_act_state == "Idle") {
        Serial.println("Handler: OS reports task complete ('Idle').");
        if (!task_queue.empty()) {
            task_queue.pop_front();
        } 
        _continue_task();
        return; 
    }

    if (physical_task_start_time != 0 && (millis() - physical_task_start_time > PHYSICAL_TASK_TIMEOUT_MS)) {
        Serial.println("!!! Handler TIMEOUT: Waiting for OS!");
        if (!active_tool_call_id.isEmpty()) {
             logInteractionToSd_Error("tool", active_tool_call_id, active_tool_call_name, "ERROR: Timeout waiting for OS.");
             active_tool_call_id = "";
             active_tool_call_name = "";
        }
        if (!task_queue.empty()) {
            task_queue.pop_front();
        }
        current_arousal_context = nullptr; 
        arousal = 0.0; 
        setState(EmilyState::IDLE);
        return; 
    }
 }

void EmilyBrain::handleSeeingState() {
    // 1. Check Result
    if (!last_vision_response.isNull()) {
        Serial.println("Handler: Vision result received.");
        String description = last_vision_response["description"] | "Vision error.";
        last_display_text = description;
        Serial.printf("Vision Result: %s\n", description.c_str());

         if (!active_tool_call_id.isEmpty()) {
             logInteractionToSd_Error("tool", active_tool_call_id, active_tool_call_name, description);
         }

        last_vision_response.clear();
        camcanvas_task_start_time = 0; 

        if (!task_queue.empty() && task_queue.front().type == "CAM_ANALYZE") {
            task_queue.pop_front();
        } 

        _start_ai_cycle(("My visual analysis returned: " + description).c_str());
        return; 
    }

    // 2. Check Timeout
    if (camcanvas_task_start_time != 0 && (millis() - camcanvas_task_start_time > CAM_TASK_TIMEOUT_MS)) {
        Serial.println("!!! Handler TIMEOUT: Waiting for Vision!");
        if (!active_tool_call_id.isEmpty()) { logInteractionToSd_Error("tool", active_tool_call_id, active_tool_call_name, "ERROR: Timeout waiting for vision analysis."); }
        last_vision_response.clear();
        camcanvas_task_start_time = 0;
        if (!task_queue.empty() && task_queue.front().type == "CAM_ANALYZE") { task_queue.pop_front(); }
        _start_ai_cycle("I requested visual analysis but received no response in time.");
        return; 
    }
}

void EmilyBrain::handleVisualizingState() {
    // 1. Check Confirmation
    if (!last_camcanvas_confirmation.isNull()) {
        Serial.println("Handler: Image generation complete.");
        last_camcanvas_confirmation.clear();
        camcanvas_task_start_time = 0;

         if (!task_queue.empty() && task_queue.front().type == "CANVAS_IMAGE_SYNC") {
            task_queue.pop_front();
        } 
        _continue_task();
        return; 
    }

    // 2. Check Timeout
    if (camcanvas_task_start_time != 0 && (millis() - camcanvas_task_start_time > CANVAS_TASK_TIMEOUT_MS)) {
        Serial.println("!!! Handler TIMEOUT: Waiting for Image!");
         if (!active_tool_call_id.isEmpty()) { logInteractionToSd_Error("tool", active_tool_call_id, active_tool_call_name, "ERROR: Timeout waiting for image generation."); }
        last_camcanvas_confirmation.clear();
        camcanvas_task_start_time = 0;
        if (!task_queue.empty() && task_queue.front().type == "CANVAS_IMAGE_SYNC") { task_queue.pop_front(); }
        _continue_task(); 
        return; 
    }
}

void EmilyBrain::handlePlayingSoundState() {
    Serial.println("Handler: Entering PLAYING_SOUND state.");
    if (!task_queue.empty() &&
        (task_queue.front().type == "CB_SOUND" || task_queue.front().type == "CB_SOUND_EMOTION"))
    {
        Task& sound_task = task_queue.front(); 
        const char* sound_path = nullptr;
        
         if (sound_task.args.containsKey("sound_effect")) sound_path = sound_task.args["sound_effect"];
         else if (sound_task.args.containsKey("sound_path")) sound_path = sound_task.args["sound_path"];


        if (sound_path != nullptr) {
            Serial.printf("Handler: Playing sound '%s'...\n", sound_path);
            delay(100); 
            playWavFromSd(sound_path);
            Serial.println("Handler: Sound finished.");
        } 

        task_queue.pop_front(); 
        _continue_task(); 
        return; 

    } else {
        Serial.println("Handler Error: Entered PLAYING_SOUND unexpectedly.");
        setState(EmilyState::IDLE); 
    }
}

void EmilyBrain::handleAwaitingInputState() {
    // 1. Check Input
    if (!last_inputpad_response.isNull()) {
        Serial.println("Handler: InputPad result received.");
        String value = last_inputpad_response["value"] | "ERROR";
        
        if (!active_tool_call_id.isEmpty()) {
             logInteractionToSd_Error("tool", active_tool_call_id, active_tool_call_name, value);
        }

        last_inputpad_response.clear(); 
        input_task_start_time = 0;      

        if (!task_queue.empty()) task_queue.pop_front();

        _start_ai_cycle(("USER_INPUT: Received '" + value + "' from InputPad.").c_str());
        return; 
    }

    // 2. Check Timeout
    if (input_task_start_time != 0 && (millis() - input_task_start_time > INPUT_TASK_TIMEOUT_MS)) {
        Serial.println("!!! Handler TIMEOUT: Waiting for InputPad!");
        if (!active_tool_call_id.isEmpty()) { logInteractionToSd_Error("tool", active_tool_call_id, active_tool_call_name, "ERROR: Timeout waiting for user input."); }
        last_inputpad_response.clear();
        input_task_start_time = 0;
        if (!task_queue.empty()) task_queue.pop_front();

        _start_ai_cycle("I requested user input via the InputPad, but received no response in time.");
        return;
    }
}

bool EmilyBrain::isOsEventSignificant(const JsonDocument& current_heartbeat) {
    if (prev_os_heartbeat_data.isNull()) {
        return current_heartbeat.containsKey("act_state"); 
    }

    bool changed = false;
    const char* current_state = current_heartbeat["act_state"] | "INVALID";
    const char* prev_state = prev_os_heartbeat_data["act_state"] | "INVALID";
    float current_angle = current_heartbeat["angle"] | -999.0f;
    float prev_angle = prev_os_heartbeat_data["angle"] | -999.0f;
    const char* current_mode = current_heartbeat["attention_mode"] | "INVALID";
    const char* prev_mode = prev_os_heartbeat_data["attention_mode"] | "INVALID";

    if (strcmp(current_state, prev_state) != 0) {
        changed = true;
    }
    if (!changed && abs(current_angle - prev_angle) > 2.0) {
        changed = true;
    }
     if (!changed && strcmp(current_mode, prev_mode) != 0) {
        changed = true;
    }
    return changed;
}

void EmilyBrain::loop() {
    // --- If NO WiFi, run AP Server ---
    if (WiFi.status() != WL_CONNECTED) {
        ptms_server.handleClient(); 
        dnsServer.processNextRequest(); 
        delay(10); 
        return; 
    }
    
    // --- Handle Web Server ---
    ptms_server.handleClient(); 

    // --- Check for INTERRUPT ---
    if (currentState != EmilyState::IDLE && checkWakeButton()) {
        Serial.println("INTERRUPT received via button press!");
        arousal = 0.0;
        valence = 0.0;
        setState(EmilyState::IDLE);
        delay(100); 
        return; 
    }

    // --- Handle Network ---
    sendPings();         
    handleUdpPackets();  
    checkTimeouts();     

    // --- State Machine Dispatcher ---
    switch (currentState) {
        case EmilyState::IDLE:                    handleIdleState(); break;
        case EmilyState::AWAITING_SPEECH:         handleAwaitingSpeechState(); break;
        case EmilyState::RECORDING_SPEECH:        handleRecordingSpeechState(); break;
        case EmilyState::PROCESSING_STT:          handleProcessingSttState(); break;
        case EmilyState::PROCESSING_AI:           handleProcessingAiState(); break;
        case EmilyState::GENERATING_SPEECH:       handleGeneratingSpeechState(); break;
        case EmilyState::SPEAKING:                handleSpeakingState(); break;
        case EmilyState::EXECUTING_PHYSICAL_TOOL: handleExecutingPhysicalToolState(); break;
        case EmilyState::SEEING:                  handleSeeingState(); break;
        case EmilyState::VISUALIZING:             handleVisualizingState(); break;
        case EmilyState::PLAYING_SOUND:           handlePlayingSoundState(); break;
        case EmilyState::AWAITING_INPUT:          handleAwaitingInputState(); break;
        case EmilyState::PROCESSING_GAMEDATA:     handleProcessGamedataState(); break;
    }

    // --- Update Display ---
    renderDisplay();
    delay(30);
}

// --- setupWiFi() ---
void EmilyBrain::setupWiFi() {
    preferences.begin("emily-wifi", false);
    String stored_ssid = preferences.getString("ssid", "");
    String stored_pass = preferences.getString("pass", "");
    preferences.end();

    if (stored_ssid.length() > 0) {
        Serial.println("Stored credentials found. Attempting connection...");
        
        display.init();
        display.setRotation(1);
        display.fillScreen(TFT_BLACK);
        display.setCursor(10, 10);
        display.setTextSize(2);
        display.println("Connecting...");

        if (connectToWiFi(stored_ssid, stored_pass)) {
            Serial.println("Connection successful.");
            display.fillScreen(TFT_BLACK);
            display.setCursor(10, 10);
            display.println("Connected!");
            display.setTextSize(1);
            display.println(WiFi.localIP().toString());
            delay(1000);
            
            wifi_status = WiFiStatus::CONNECTED; 
            udp.begin(UDP_LISTEN_PORT);
            setupWebServer(); 
            Serial.println("PTMS server started.");
            return; 
        }
        Serial.println("Connection failed.");
        display.fillScreen(TFT_RED);
        display.setCursor(10, 10);
        display.println("Config Error.");
        delay(1000);
    } else {
        Serial.println("No stored credentials.");
    }
    
    startAPMode();
}

bool EmilyBrain::connectToWiFi(String ssid, String pass) {
    WiFi.mode(WIFI_STA);
    
    // Static IP Config (Optional - can be moved to config file later)
    IPAddress staticIP(192, 168, 68, 201);
    IPAddress gateway(192, 168, 68, 1);
    IPAddress subnet(255, 255, 255, 0);
    IPAddress primaryDNS(8, 8, 8, 8);
    if (!WiFi.config(staticIP, gateway, subnet, primaryDNS)) {
        Serial.println("Static IP config failed!");
    }

    WiFi.begin(ssid.c_str(), pass.c_str());
    Serial.print("Connecting to " + ssid);

    unsigned long startTijd = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTijd < 15000) {
        delay(500); Serial.print(".");
    }
    return (WiFi.status() == WL_CONNECTED);
}

void EmilyBrain::startAPMode() {
    Serial.println("Starting AP Mode ('Emily_Setup')...");
    
    WiFi.softAP("Emily_Setup", "12345678");
    IPAddress AP_IP(192, 168, 4, 1);
    dnsServer.start(53, "*", AP_IP);
    Serial.println("WiFi AP Mode active.");
    delay(500); 

    Serial.println("Initializing Display for AP instructions...");
    display.init();
    display.setRotation(1);
    display.fillScreen(TFT_BLACK);
    
    display.setCursor(10, 10);
    display.setTextSize(2);
    display.println("SETUP MODE");
    display.setTextSize(1);
    display.println("\nConnect phone to WiFi:");
    display.setTextColor(TFT_CYAN, TFT_BLACK);
    display.println("Emily_Setup");
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.println("\nPassword: 12345678");
    display.println("\nGo to 192.168.4.1");

    ptms_server.onNotFound([this](){ this->handleRoot(); });
    ptms_server.on("/", HTTP_GET, [this](){ this->handleRoot(); });
    ptms_server.on("/save", HTTP_POST, [this](){ this->handleSave(); });
    ptms_server.begin(); 
    Serial.println("AP Web Server started at 192.168.4.1");
}

void EmilyBrain::handleRoot() {
    String html = R"(
        <!DOCTYPE HTML><html><head><title>Emily WiFi Setup</title><meta name='viewport' content='width=device-width, initial-scale=1'></head>
        <body style='font-family: sans-serif; background: #222; color: #EEE; text-align: center;'>
        <h2 style='color: #0AF;'>Emily WiFi Setup</h2>
        <p>Choose your network:</p>
        <form action='/save' method='POST' style='display: grid; max-width: 300px; margin: auto; gap: 10px;'>
            <input type='text' name='ssid' placeholder='Network Name (SSID)' style='padding: 10px; border-radius: 5px; border: none;'>
            <input type='password' name='pass' placeholder='Password' style='padding: 10px; border-radius: 5px; border: none;'>
            <input type='submit' value='Save & Reboot' style='padding: 10px; background: #0AF; color: white; border: none; border-radius: 5px; font-weight: bold;'>
        </form>
        </body></html>
    )";
    ptms_server.send(200, "text/html", html);
}

void EmilyBrain::handleSave() {
    Serial.println("New credentials received!");
    String ssid = ptms_server.arg("ssid");
    String pass = ptms_server.arg("pass");

    if (ssid.length() > 0 && pass.length() > 0) {
        preferences.begin("emily-wifi", false);
        preferences.putString("ssid", ssid);
        preferences.putString("pass", pass);
        preferences.end();

        Serial.println("Credentials saved to flash. Rebooting...");
        String html = "<html><body style='font-family: sans-serif; background: #222; color: #EEE; text-align: center;'><h2>Success!</h2><p>Credentials saved. Emily is rebooting and will connect to '" + ssid + "'.</p><p>Reconnect your phone to normal WiFi.</p></body></html>";
        ptms_server.send(200, "text/html", html);
        
        delay(2000);
        ESP.restart();
    } else {
        ptms_server.send(400, "text/plain", "SSID or Password missing.");
    }
}

void EmilyBrain::handleRemotePage() {
    String html = R"(
        <!DOCTYPE HTML><html><head><title>Emily Remote</title>
        <meta name='viewport' content='width=device-width, initial-scale=1'>
        <style>
            body { font-family: sans-serif; background: #222; color: #EEE; display: flex; flex-direction: column; height: 90vh; }
            h2 { color: #0AF; text-align: center; }
            form { display: flex; width: 100%; padding: 10px; box-sizing: border-box; }
            input[type='text'] { flex-grow: 1; padding: 15px; border-radius: 5px; border: none; font-size: 1.1em; background: #444; color: #EEE; }
            input[type='submit'] { padding: 15px; background: #0AF; color: white; border: none; border-radius: 5px; font-weight: bold; margin-left: 10px; }
            
            .button_bar { display: flex; justify-content: space-around; padding: 10px; }
            .button { padding: 10px 15px; text-decoration: none; background: #555; color: #FFF; border-radius: 5px; font-size: 0.9em; }
            .button.red { background: #900; }
        </style>
        </head>
        <body>
            <h2>Emily Web Remote</h2>
            
            <form action='/send' method='POST'>
                <input type='text' name='user_text' placeholder='Type your message...' autofocus>
                <input type='submit' value='Send'>
            </form>

            <div class='button_bar'>
                <a href='/force-idle' class='button red'>STOP (IDLE)</a>
                <a href='/center-head' class='button'>Center Head</a>
                <a href='/delete-history' class='button'>Clear History</a>
                <a href='/force-reboot' class='button'>Reboot EB</a>
            </div>
            
        </body></html>
    )";
    ptms_server.send(200, "text/html", html);
}

void EmilyBrain::handleRemoteInput() {
    Serial.println(">>> Web Remote: Input received via POST...");

    if (currentState != EmilyState::IDLE) {
        ptms_server.send(429, "text/plain", "Emily is busy. Try again shortly.");
        return; 
    }

    if (ptms_server.hasArg("user_text")) {
        String text = ptms_server.arg("user_text");
        Serial.printf(">>> Web Remote: Text = %s\n", text.c_str());

        if (text.length() > 0) {
            String trigger_string = "USER_INPUT (Web): " + text;
            _start_ai_cycle(trigger_string.c_str());
            
            ptms_server.sendHeader("Location", "/remote");
            ptms_server.send(302, "text/plain", "Input received, processing...");
        } else {
            ptms_server.sendHeader("Location", "/remote");
            ptms_server.send(302, "text/plain", "Empty message, try again.");
        }
    } else {
        ptms_server.send(400, "text/plain", "Error: 'user_text' field missing.");
    }
}

void EmilyBrain::handleForceIdle() {
    Serial.println(">>> Web Remote: FORCE IDLE received.");
    
    arousal = 0.0;
    valence = 0.0;
    current_arousal_context = nullptr;
    task_queue.clear(); 
    
    setState(EmilyState::IDLE);
    ptms_server.sendHeader("Location", "/remote");
    ptms_server.send(302, "text/plain", "Forced to IDLE.");
}

void EmilyBrain::handleCenterHead() {
    Serial.println(">>> Web Remote: CENTER HEAD received.");
    
    StaticJsonDocument<128> cmd_doc;
    cmd_doc["command"] = "move_head";
    cmd_doc["pan"] = 90;
    cmd_doc["tilt"] = 90;
    String command_string;
    serializeJson(cmd_doc, command_string);

    udp.beginPacket(CAMCANVAS_IP_ADDRESS, CAMCANVAS_UDP_PORT);
    udp.print(command_string);
    udp.endPacket();

    ptms_server.sendHeader("Location", "/remote");
    ptms_server.send(302, "text/plain", "Center command sent.");
}

void EmilyBrain::handleForceReboot() {
    Serial.println(">>> Web Remote: FORCE REBOOT received. Rebooting...");
    
    String html = "<html><body style='font-family: sans-serif; background: #222; color: #EEE; text-align: center;'><h2>EmilyBrain Rebooting...</h2><p>Page will refresh in 5 seconds.</p><script>setTimeout(() => { window.location.href = '/remote'; }, 5000);</script></body></html>";
    ptms_server.send(200, "text/html", html);
    
    delay(1000); 
    ESP.restart(); 
}