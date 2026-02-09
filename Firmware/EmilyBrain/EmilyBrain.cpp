#include "EmilyBrain.h"
#include <ArduinoJson.h>

// --- Descriptions for Self-Awareness Report (v5.0) ---
const char* DESC_CORE =
    "I am a robot entity. My core logic runs on a unified processor named 'EmilyBrain'. "
    "This provides me with the basic abilities to hear, speak, and be activated by a physical button.\n";

// DESC_OS removed (Component no longer exists as separate entity)

// --- CAMCANVAS DESCRIPTION ---
const char* DESC_CAMCANVAS =
    "- A 'CamCanvas' (CC) unit: A module equipped with a high-resolution camera (for vision), "
    "a visual display (for generating images), and a pan/tilt mechanism for head movement.\n";

// --- INPUTPAD DESCRIPTION ---
const char* DESC_INPUTPAD =
    "- An 'InputPad' (IP): A separate, wireless handheld device with a screen and 3 buttons (A, B, C) "
    "used for specific game inputs or choices, like rolling dice or answering quizzes.\n";

// Constructor. Called when the 'emily' object is instantiated.
EmilyBrain::EmilyBrain() : status_led(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800), display() {
    // The constructor body can remain empty
}

void EmilyBrain::setup() {
    Serial.begin(115200);
    while (!Serial);
    Serial.println("EmilyBrain Dual-Bus Startup... (Final Boot Sequence)");
    pinMode(PIN_WAKE_BUTTON, INPUT_PULLUP);
    Serial.println("Wake button pin configured.");

    // --- Step 1: Initialize Non-Conflicting Hardware ---
    status_led.begin();
    status_led.setPixelColor(0, 0, 10, 0); // Green
    status_led.show();

    Serial.println("Initializing SD Card on FSPI...");
    spiSD = new SPIClass(FSPI);
    spiSD->begin(PIN_SD_SCLK, PIN_SD_MISO, PIN_SD_MOSI);
    if (!SD.begin(PIN_SD_CS, *spiSD)) {
        Serial.println("!!! SD Card Mount FAILED!");
        // We cannot proceed without SD card
        while(true) { delay(1000); } 
    }
    Serial.println("SD Card OK.");
    
    // Load configurations (essential for tools, system prompts, etc.)
    loadConfigurations();

    // --- Step 2: Wi-Fi & Display Setup Wizard ---
    // This function handles ALL Wi-Fi AND Display initialization.
    setupWiFi(); 

    // --- Step 3: Start Remaining Services ---
    // UDP and PTMS server are started inside setupWiFi() 
    // at the correct moment (either in AP mode or STA mode).
    
    Serial.println("Setup complete. Starting main loop.");
    last_display_text = "System Ready.";
    prev_display_text = "";
    // renderDisplay() is already called within setupWiFi()

    // (If WiFi is NOT connected, the display shows AP instructions)
    last_display_text = "System Ready."; // Update default text
    prev_display_text = "";
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
        
        // Renamed from "Acting (OS)" to generic Hardware Action
        case EmilyState::EXECUTING_PHYSICAL_TOOL: return "Hardware Action..."; 
        
        // Simplified texts (removed CamCanvas references for cleaner UI)
        case EmilyState::SEEING:                  return "Analyzing Vision...";
        case EmilyState::VISUALIZING:             return "Generating Image...";
        
        case EmilyState::PLAYING_SOUND:           return "Playing Sound...";
        
        case EmilyState::AWAITING_INPUT:          return "Awaiting Input...";
        case EmilyState::PROCESSING_GAMEDATA:     return "Finding Data...";
        default:                                  return "Unknown";
    }
}

void EmilyBrain::setState(EmilyState newState) {
    if (newState == currentState) {
        return; // No change needed
    }

    currentState = newState;
    const char* stateName = stateToString(currentState);
    // Serial.printf("STATE CHANGE -> %s\n", stateName); // Optional: Uncomment for debug

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
        // InputPad uses Magenta/Purple as well for now, or define new color
        case EmilyState::AWAITING_INPUT:
        case EmilyState::PROCESSING_GAMEDATA:
             color = status_led.Color(15, 0, 15); // Magenta
             break;
    }
    status_led.setPixelColor(0, color);
    status_led.show();
    renderDisplay();
}

// Function to set up web server
void EmilyBrain::setupWebServer() {
    // Set 'normal' PTMS-handlers
    ptms_server.on("/upload", HTTP_POST, [this](){ this->handleFileUpload(); });
    ptms_server.on("/delete-history", HTTP_GET, [this](){ this->handleHistoryDelete(); });
    ptms_server.on("/download", HTTP_GET, [this](){ this->handleFileDownload(); });

    // ---WEB REMOTE ---
    ptms_server.on("/remote", HTTP_GET, [this](){ this->handleRemotePage(); });
    ptms_server.on("/send", HTTP_POST, [this](){ this->handleRemoteInput(); }); // Use POST for form data
    
    // Use GET for simple click-actions
    ptms_server.on("/force-idle", HTTP_GET, [this](){ this->handleForceIdle(); });
    ptms_server.on("/center-head", HTTP_GET, [this](){ this->handleCenterHead(); });
    ptms_server.on("/force-reboot", HTTP_GET, [this](){ this->handleForceReboot(); });
    
    // Add a simple 'not found' or 'root' handler for 'normal' mode
    ptms_server.on("/", HTTP_GET, [this](){
        ptms_server.send(200, "text/plain", "EmilyBrain PTMS Server is online.");
    });
    ptms_server.onNotFound([this](){
         ptms_server.send(404, "text/plain", "Not Found.");
    });
    
    ptms_server.begin(); // 
}

void EmilyBrain::handleFileUpload() {
    if (!ptms_server.hasArg("file") || !ptms_server.hasArg("plain")) {
        ptms_server.send(400, "text/plain", "400: BAD REQUEST");
        return;
    }

    String filename = ptms_server.arg("file");
    String content = ptms_server.arg("plain");
    Serial.printf("File upload request received for: %s\n", filename.c_str());

    // Pah shall begin with '/' for the SD-card
    if (!filename.startsWith("/")) {
        filename = "/" + filename;
    }

    File file = SD.open(filename, FILE_WRITE);
    if (file) {
        file.print(content);
        file.close();
        Serial.println("File successfully saved to SD card.");
        ptms_server.send(200, "text/plain", "SUCCESS: File saved.");
        
        // IMPORTANT: Reload the configurations after a succesful upload
        loadConfigurations(); 
    } else {
        Serial.println("ERROR: Could not open file on SD card for writing.");
        ptms_server.send(500, "text/plain", "500: SERVER ERROR - Could not save file.");
    }
}
/**
    * @brief Handler for downloading a file from the SD card.
    */
void EmilyBrain::handleFileDownload() {
    // Get filename from URL-parameter (bv. /download?file=/system_prompt.txt)
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
        String contentType = "text/plain"; // Standard content type
        if (path.endsWith(".json") || path.endsWith(".jsonl")) {
            contentType = "application/json";
        }
        
        // Stream the file directly to the client. This is very memory efficient.
        ptms_server.streamFile(file, contentType);
        file.close();
        Serial.println("File successfully streamed to client.");
    } else {
        Serial.println("ERROR: Requested file does not exist.");
        ptms_server.send(404, "text/plain", "404: NOT FOUND - File does not exist.");
    }
}
/**
* @brief Handler for deleting the chat history file.
*/
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
    bool button_pressed_event = false; // Vlag om een nieuwe druk te signaleren
    int reading = digitalRead(PIN_WAKE_BUTTON);

    // Als de fysieke staat is veranderd, reset de debounce timer
    if (reading != last_button_state) {
        last_debounce_time = millis();
    }

    // Als de staat stabiel is voor langer dan de delay
    if ((millis() - last_debounce_time) > DEBOUNCE_DELAY_MS) {
        // En als deze stabiele staat anders is dan de vorige stabiele staat
        if (reading != debounced_state) {
            debounced_state = reading;
            // Als de nieuwe stabiele staat LOW is (knop ingedrukt)
            if (debounced_state == LOW) {
                Serial.println("Wake Button Ingedrukt!"); // Debug output
                button_pressed_event = true; // Signaleer de nieuwe druk
            }
        }
    }

    // Onthoud de fysieke staat voor de volgende keer
    last_button_state = reading;

    return button_pressed_event; // Geef terug of er een schone druk was
}

void EmilyBrain::processAiProxyRequest(const char* current_prompt_content, JsonObject device_status) {
    // Note: State is already set to PROCESSING_AI by _start_ai_cycle

    Serial.println("Starting AI Proxy Request (StaticJsonDocument in PSRAM)...");

    // --- Step 1: Allocate PSRAM Buffer ---
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
    // Serial.println("DEBUG: Calling buildAiPayload...");
    // Serial.println("DEBUG: Calling buildAiPayload (SD test version)...");
    // Pass the actual document object (*doc_ptr) by reference
    buildAiPayload(*doc_ptr, current_prompt_content, device_status);
    // Serial.println("DEBUG: buildAiPayload finished.");

    // Check if buildAiPayload caused an overflow
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
    size_t len = measureJson(*doc_ptr); // Use *doc_ptr
    Serial.printf("Calculated payload string size: %u bytes\n", len);
    char *payload_buffer = (char *) malloc(len + 1); // Allocate from default heap (should be smaller)
    if (!payload_buffer) {
        Serial.println("FATAL: Malloc failed for payload STRING buffer!");
         // --- Cleanup on error ---
        doc_ptr->~StaticJsonDocument(); // Explicitly call destructor
        heap_caps_free(json_psram_buffer); // Free the buffer
        // --- End Cleanup ---
        setState(EmilyState::IDLE);
        return;
    }
    Serial.println("DEBUG: String buffer allocated. Serializing JSON...");
    serializeJson(*doc_ptr, payload_buffer, len + 1); // Use *doc_ptr
    Serial.println("DEBUG: JSON Serialized to string buffer.");


    // Step 5: Make the HTTPS POST request
    String response_body = ""; // To store the result
    WiFiClientSecure client;
    HTTPClient http;
    client.setInsecure(); // Allow connection without checking certificate (easier for ESP32)

    Serial.println("Connecting to Venice API...");
    if (http.begin(client, VENICE_API_URL)) {
        http.addHeader("Authorization", "Bearer " + String(VENICE_API_KEY));
        http.addHeader("Content-Type", "application/json");
        http.setTimeout(90000); // Increased timeout (90 seconds) for AI

        Serial.println("Sending POST request...");
        int httpResponseCode = http.POST((uint8_t*)payload_buffer, len);

        // --- Free the buffer ASAP ---
        free(payload_buffer);
        payload_buffer = nullptr; // Good practice

        if (httpResponseCode == HTTP_CODE_OK) {
            response_body = http.getString();
            Serial.println("API response OK.");
            // --- Optional Debug: Print raw response ---
            // Serial.println("\n--- RAW API RESPONSE ---");
            // Serial.println(response_body);
            // Serial.println("------------------------\n");
        } else {
            Serial.printf("[HTTP] POST failed, error: %d\n", httpResponseCode);
            response_body = http.getString(); // Get error message from server
            Serial.println("Error payload: " + response_body);
            // Create a JSON error message for the planner
            response_body = "{\"error\":\"API call failed\", \"code\": " + String(httpResponseCode) + "}";
        }
        http.end();
    } else {
         Serial.println("Failed to connect to API URL!");
         if (payload_buffer) free(payload_buffer); // Ensure buffer is freed on connection error
         response_body = "{\"error\":\"Connection failed\"}";
    }


    // --- Step 6: CRUCIAL Cleanup ---
    Serial.println("DEBUG: Cleaning up JSON document and buffers...");
    // free(payload_buffer); // Free the string buffer from default heap
    if (doc_ptr != nullptr) {
        doc_ptr->~StaticJsonDocument<JSON_DOC_CAPACITY>(); // Explicitly call destructor for placement new object
    }
    if (json_psram_buffer != nullptr) {
        heap_caps_free(json_psram_buffer); // Free the main PSRAM buffer
    }
    Serial.println("DEBUG: Cleanup complete.");
    // --- End Cleanup ---



    // --- Step 7: Parse response and call Planner (Identical to before) ---
    Serial.println("Parsing API response...");


    DynamicJsonDocument response_doc(4096); // Adjust size based on expected response complexity
    DeserializationError error = deserializeJson(response_doc, response_body);

    if (error) {
        Serial.printf("Failed to parse API response JSON: %s\n", error.c_str());
        // Handle error - maybe try again or inform user via TTS?
        // For now, just go back to IDLE
        setState(EmilyState::IDLE);
    } else {
        // Extract the tool calls array (handle potential errors/missing keys)
        JsonArray tool_calls;
        if (response_doc.containsKey("choices") &&
            response_doc["choices"][0].containsKey("message") &&
            response_doc["choices"][0]["message"].containsKey("tool_calls")) {
            tool_calls = response_doc["choices"][0]["message"]["tool_calls"].as<JsonArray>();
        }

        // Check if tool_calls is valid before calling the planner
        if (!tool_calls.isNull() && tool_calls.size() > 0) {
            Serial.println("Tool calls received, calling planner...");
            // We pass the original user prompt (as string?) and the tool_calls array
            // NOTE: PB passed the user_prompt JSON object. We might need to adjust _handle_ai_response
            // For now, let's pass nullptr for the user_prompt part.
             _handle_ai_response(nullptr, tool_calls); // Call the planner!
        } else {
             Serial.println("API response received, but no tool calls found or format is unexpected.");
             // Handle situation where AI didn't return a tool call
             // Maybe call planner with a message like "AI did not provide an action"?
             setState(EmilyState::IDLE); // Go back to IDLE for now
        }
    }
    // State transition happens inside _handle_ai_response or if an error occurred.
    // setState(EmilyState::IDLE); // for testing
}

// --- Helper Function to Add Task ---
void EmilyBrain::addTask(const String& type, JsonVariantConst args_variant) {
    Task new_task;
    new_task.type = type;
    new_task.args = args_variant; // Copy the arguments
    task_queue.push_back(new_task);
    Serial.printf("Task added: %s\n", type.c_str()); // Optional confirmation
}

// --- The Planner ---
void EmilyBrain::_handle_ai_response(const char* user_prompt_json_str, JsonArray tool_calls) {
    Serial.println("Planner (_handle_ai_response) called!");

    if (tool_calls.isNull() || tool_calls.size() == 0) {
        Serial.println("Planner Warning: Received empty or null tool_calls array.");
        _start_ai_cycle("My cognitive core returned no specific action. What should I do?"); // Ask again
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

    // --- Parse ALLE Argumenten EENMALIG ---
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

    // --- REVISED Planning Logic met Argument Filtering ---
    bool main_task_planned = false;
    
    // === Stap 0: Plan OPTIONELE SFEERVERLICHTING ===
    if (all_args.containsKey("led_effect")) {
        StaticJsonDocument<256> led_args_doc;
        led_args_doc["command"] = "set_status_led"; 
        led_args_doc["effect"] = all_args["led_effect"];
        led_args_doc["r"] = all_args["led_r"] | 0; 
        led_args_doc["g"] = all_args["g"] | 0;
        led_args_doc["b"] = all_args["b"] | 0;
        led_args_doc["speed"] = all_args["speed"] | 1000; 
        
        addTask("CAMCANVAS_SET_LED", led_args_doc); 
    }

    // === Step 1: Plan OPTIONAL Background Image FIRST ===
    // JSON Key changed from 'begeleidende_afbeelding_prompt' to 'visual_prompt'
    if (all_args.containsKey("visual_prompt")) {
        StaticJsonDocument<1024> image_args_doc;
        image_args_doc["prompt"] = all_args["visual_prompt"];
        image_args_doc["image_model"] = all_args["image_model"] | "venice-sd35"; 
        addTask("CANVAS_IMAGE_ASYNC", image_args_doc); 
        Serial.println("Planner: Added ASYNC image task first.");
    }
    
    // === Step 2: Plan the MAIN Action ===

    // 1. Emotion: update_emotional_state
    if (!main_task_planned && active_tool_call_name == "update_emotional_state") {
        addTask("PB_EMOTION", all_args_doc); 
        if (all_args.containsKey("sound_effect")) {
            StaticJsonDocument<128> sound_args_doc;
            sound_args_doc["sound_effect"] = all_args["sound_effect"];
            addTask("CB_SOUND_EMOTION", sound_args_doc); 
        }
        main_task_planned = true;
    }
    // 2. Vision: analyze_visual_environment
    else if (!main_task_planned && active_tool_call_name == "analyze_visual_environment") {
        // (Old 'hoek' logic removed as requested)
        StaticJsonDocument<512> analyze_args_doc;
        analyze_args_doc["question"] = all_args["question"] | "Describe what you see.";
        analyze_args_doc["use_flash"] = all_args["use_flash"] | false;
        addTask("CAM_ANALYZE", analyze_args_doc); 
        main_task_planned = true;
    }

    // 3. Speech: announce_message OR start_conversation
    // (Replaces geef_mededeling / start_gesprek)
    else if (!main_task_planned && (active_tool_call_name == "announce_message" || active_tool_call_name == "start_conversation")) {
        StaticJsonDocument<1024> speak_args_doc; 
        
        // Map English keys to Internal keys
        if (all_args.containsKey("announcement")) speak_args_doc["announcement"] = all_args["announcement"];
        else if (all_args.containsKey("question")) speak_args_doc["question"] = all_args["question"];
        
        addTask("CB_SPEAK", speak_args_doc);
        main_task_planned = true;
    }

    // 4. Movement: move_head
    // (Replaces draai_naar_hoek AND adds tilt)
    else if (!main_task_planned && active_tool_call_name == "move_head") {
        StaticJsonDocument<128> head_args_doc;
        head_args_doc["pan"] = all_args["pan"] | 90; 
        head_args_doc["tilt"] = all_args["tilt"] | 90; 
        addTask("CAMCANVAS_MOVE_HEAD", head_args_doc);
        main_task_planned = true;
    }

    // 5. Image: generate_image
    // (Replaces genereer_afbeelding)
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
    // 6. Sound: play_sound_effect
    else if (!main_task_planned && active_tool_call_name == "play_sound_effect") {
        StaticJsonDocument<128> sound_args_doc;
        sound_args_doc["sound_path"] = all_args["sound_path"];
        addTask("CB_SOUND", sound_args_doc);
        main_task_planned = true;
    }
    // 7. Nod: nod_head
    else if (!main_task_planned && active_tool_call_name == "nod_head") {
        StaticJsonDocument<128> knod_args_doc;
        knod_args_doc["angle"] = all_args["angle"];
        addTask("CAM_NOD", knod_args_doc);
        main_task_planned = true;
    }
    // 8. Photo: take_photo
    else if (!main_task_planned && active_tool_call_name == "take_photo") {
        StaticJsonDocument<1> photo_args_doc; 
        addTask("CAM_TAKE_PICTURE", photo_args_doc);
        main_task_planned = true;
    }

    // 9. InputPad: activate_inputpad
    // (Replaces activeer_inputpad)
    else if (!main_task_planned && active_tool_call_name == "activate_inputpad") {
        StaticJsonDocument<256> input_args_doc;
        input_args_doc["mode"] = all_args["mode"];
        input_args_doc["max_value"] = all_args["max_value"]; // English key
        addTask("INPUTPAD_SET_MODE", input_args_doc);
        main_task_planned = true;
    }

    // 10. CMS Data: retrieve_local_data
    // (Replaces zoek_data_op)
    else if (!main_task_planned && active_tool_call_name == "retrieve_local_data") {
        StaticJsonDocument<256> data_args_doc;
        data_args_doc["key"] = all_args["key"]; // English key
        addTask("EB_GET_LOCAL_DATA", data_args_doc);
        main_task_planned = true;
    }

    // === Step 3: Handle Unknown Tool or Start Execution ===

    if (task_queue.empty()) {
        Serial.printf("Planner Error: Unknown tool '%s' requested by AI.\n", active_tool_call_name.c_str());
        logInteractionToSd_Error("tool", active_tool_call_id, active_tool_call_name, "ERROR: Tool does not exist.");
        String error_message = "My cognitive core requested a tool '" + active_tool_call_name + "' which I do not have.";
        _start_ai_cycle(error_message.c_str()); 
        return; 
    }

    Serial.printf("Planner: Added %d task(s) to the queue. Starting execution...\n", task_queue.size());
    _continue_task(); 
}
 

void EmilyBrain::playWavFromSd(const char* filename) {
    Serial.printf("Attempting to play audio file: %s\n", filename);


    // --- Step 1: Parse Header ---
    WavHeader header = parseWavHeader(filename);
    if (header.sampleRate == 0 || header.bitsPerSample == 0 || header.channels == 0) {
        Serial.println("Error: Invalid WAV header data. Cannot play.");
        return; // Stop if header is invalid
    }
    // Basic validation (add more checks if needed)
    if (header.channels != 1) {
         Serial.println("Error: Only MONO (1 channel) WAV files are supported for playback.");
         return;
    }
    if (header.bitsPerSample != 16) {
         Serial.println("Error: Only 16-bit WAV files are supported for playback.");
         return;
    }

  // --- Step 2: Open the file AGAIN for playback ---
    File audioFile = SD.open(filename, FILE_READ);
    if (!audioFile) {
        Serial.printf("Error: Could not re-open file %s for playback\n", filename);
        return;
    }

    // --- Add Memory Check before I2S initialization ---
    // Serial.println(">>> DEBUG: playWavFromSd - Checking memory before I2S install...");
    // Serial.printf(">>> DEBUG: Free Heap: %u, Free PSRAM: %u\n", ESP.getFreeHeap(), ESP.getFreePsram());
    // --- End Memory Check ---

  // --- Step 3: Configureer I2S Dynamisch ---
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        // Use values from the header!
        .sample_rate = header.sampleRate,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT, // Assuming 16-bit based on check above
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,  // Assuming Mono based on check above
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 256, // Keep smaller buffer for now
        .use_apll = true,   // Keep APLL enabled
        .tx_desc_auto_clear = true
    };

  i2s_pin_config_t pin_config = {
      .bck_io_num = PIN_I2S_BCK,
      .ws_io_num = PIN_I2S_WS,
      .data_out_num = PIN_I2S_DATA_OUT,
      .data_in_num = I2S_PIN_NO_CHANGE // We ontvangen geen data
  };

  // --- Step 4: Install and start the driver -
  
    // --- Force Uninstall/Reinstall ---
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
    delay(50); // Small delay after setup

    // --- Step 5: Play the file ---
    audioFile.seek(44); // Skip header NOW that I2S is configured

    const size_t bufferSize = 2048; // Keep buffer size reasonable
    uint8_t buffer[bufferSize];
    size_t bytes_written = 0;
    size_t total_bytes_read = 0; // Track total bytes read

    Serial.println("Starting playback...");
    while (audioFile.available()) {
        int bytesRead = audioFile.read(buffer, bufferSize);
        if (bytesRead <= 0) break;
        total_bytes_read += bytesRead; // Add to total

        // Write data to I2S
        esp_err_t write_result = i2s_write(I2S_NUM_0, buffer, bytesRead, &bytes_written, portMAX_DELAY);
        if (write_result != ESP_OK) {
            Serial.printf("Error writing to I2S: %d\n", write_result);
            break; // Stop playback on error
        }
        if (bytes_written != bytesRead) {
             Serial.printf("Warning: I2S write underrun? Read %d, wrote %d\n", bytesRead, bytes_written);
             // Continue playback? May cause glitches.
        }
    }
     Serial.printf("Playback loop finished. Total bytes read from file: %u\n", total_bytes_read);

    // --- Stap 6: Ruim netjes op ---
    audioFile.close();
    i2s_zero_dma_buffer(I2S_NUM_0); // Flush buffer with silence
    delay(100); // Give buffer time to play silence
    i2s_driver_uninstall(I2S_NUM_0);
    
    Serial.println("Playback finished. I2S driver uninstalled.");
}

// --- WAV Header Function (Helper for recording) ---
void EmilyBrain::createWavHeader(byte* header, size_t total_data_size) {
    const int sampleRate = 16000;
    const int bitsPerSample = 16;
    const int channels = 1;

    long total_file_size = total_data_size + 36;
    long byteRate = sampleRate * channels * bitsPerSample / 8;
    int blockAlign = channels * bitsPerSample / 8;

    memcpy(&header[0], "RIFF", 4);                                  // ChunkID
    header[4] = (byte)(total_file_size & 0xFF);                     // ChunkSize
    header[5] = (byte)((total_file_size >> 8) & 0xFF);
    header[6] = (byte)((total_file_size >> 16) & 0xFF);
    header[7] = (byte)((total_file_size >> 24) & 0xFF);
    memcpy(&header[8], "WAVE", 4);                                  // Format
    memcpy(&header[12], "fmt ", 4);                                 // Subchunk1ID
    header[16] = 16;                                                // Subchunk1Size (16 for PCM)
    header[17] = 0;
    header[18] = 0;
    header[19] = 0;
    header[20] = 1;                                                 // AudioFormat (1 for PCM)
    header[21] = 0;
    header[22] = channels;                                          // NumChannels
    header[23] = 0;
    header[24] = (byte)(sampleRate & 0xFF);                         // SampleRate
    header[25] = (byte)((sampleRate >> 8) & 0xFF);
    header[26] = 0;
    header[27] = 0;
    header[28] = (byte)(byteRate & 0xFF);                           // ByteRate
    header[29] = (byte)((byteRate >> 8) & 0xFF);
    header[30] = (byte)((byteRate >> 16) & 0xFF);
    header[31] = (byte)((byteRate >> 24) & 0xFF);
    header[32] = blockAlign;                                        // BlockAlign
    header[33] = 0;
    header[34] = bitsPerSample;                                     // BitsPerSample
    header[35] = 0;
    memcpy(&header[36], "data", 4);                                 // Subchunk2ID
    header[40] = (byte)(total_data_size & 0xFF);                    // Subchunk2Size
    header[41] = (byte)((total_data_size >> 8) & 0xFF);
    header[42] = (byte)((total_data_size >> 16) & 0xFF);
    header[43] = (byte)((total_data_size >> 24) & 0xFF);


}

WavHeader EmilyBrain::parseWavHeader(const char* path) {
    WavHeader header;
    File file = SD.open(path);
    if (!file) {
        Serial.printf("parseWavHeader ERROR: Could not open file %s\n", path);
        return header; // Return default (empty) header
    }

    if (file.size() < 44) {
        Serial.printf("parseWavHeader ERROR: File %s is too small to be a valid WAV file.\n", path);
        file.close();
        return header; // Return default header
    }

    byte buffer[44];
    file.read(buffer, 44);
    file.close(); // Close file after reading header

    // Bytes 22-23: Channels
    header.channels = (buffer[23] << 8) | buffer[22];
    // Bytes 24-27: Sample Rate
    header.sampleRate = (buffer[27] << 24) | (buffer[26] << 16) | (buffer[25] << 8) | buffer[24];
    // Bytes 34-35: Bits per Sample
    header.bitsPerSample = (buffer[35] << 8) | buffer[34];

    Serial.println("--- Parsed WAV Header ---");
    Serial.printf("  Sample Rate: %d Hz\n", header.sampleRate);
    Serial.printf("  Bits/Sample: %d\n", header.bitsPerSample);
    Serial.printf("  Channels:    %d\n", header.channels);
    Serial.println("-------------------------");

    return header;
}

bool EmilyBrain::recordAudioToWav(const char* filename) {
    Serial.println("Starting VAD Recording to WAV...");

    // --- Step 1: Configure and install I2S driver for RX (Microphone) ---
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX), // Set to RX mode
        .sample_rate = 16000,                               // Standard for STT
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,      // Mono mic
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 256,
        .use_apll = true // Use APLL for stable clock
    };
    i2s_pin_config_t pin_config = {
        .bck_io_num = PIN_I2S_BCK,       // Pin 17
        .ws_io_num = PIN_I2S_WS,        // Pin 18
        .data_out_num = I2S_PIN_NO_CHANGE, // Not sending data
        .data_in_num = PIN_I2S_DATA_IN   // *** CORRECT: Pin 19 (Mic SD) ***
    };

    // Uninstall previous driver (might be TX from speaker) before installing RX
    i2s_driver_uninstall(I2S_NUM_0);
    esp_err_t install_result = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    if (install_result != ESP_OK) { /* handle error */ return false; }
    esp_err_t pin_result = i2s_set_pin(I2S_NUM_0, &pin_config);
    if (pin_result != ESP_OK) { /* handle error */ i2s_driver_uninstall(I2S_NUM_0); return false; }
    i2s_zero_dma_buffer(I2S_NUM_0); // Clear any old data
    delay(50);

    // --- Step 2: VAD Logic and Recording ---
    File file = SD.open(filename, FILE_WRITE);
    if (!file) { /* handle error */ i2s_driver_uninstall(I2S_NUM_0); return false; }

    byte wav_header[44];
    createWavHeader(wav_header, 0); // Write dummy header first
    file.write(wav_header, 44);

    const int record_buffer_size = 1024;
    byte record_buffer[record_buffer_size];
    size_t bytes_read;
    long silence_started_at = 0;
    bool speech_started = false; // Flag to indicate if speech has begun
    size_t total_data_size = 0;
    long recording_started_at = 0;
    int quiet_buffers_in_a_row = 0; // For initial silence calibration

    // --- Initial Silence Calibration (Wait for quiet before listening) ---
    Serial.println("VAD: Waiting for initial silence...");
    setState(EmilyState::AWAITING_SPEECH); // Update state
    while (quiet_buffers_in_a_row < 10) { // ~0.5s of quiet
        // Use a short timeout to prevent blocking forever if mic is noisy
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
        } else if (read_result == ESP_ERR_TIMEOUT) {
             // Continue if timeout, maybe mic disconnected?
             Serial.print("."); // Indicate waiting
        } else {
            Serial.printf("VAD Error during initial silence read: %d\n", read_result);
             break; // Exit calibration on read error
        }
    }
     if (quiet_buffers_in_a_row < 10) { // If calibration failed
        Serial.println("VAD Error: Could not detect initial silence.");
        file.close();
        i2s_driver_uninstall(I2S_NUM_0);
        return false;
     }
    Serial.println("VAD: Silence confirmed. Listening...");

    // --- Main VAD Loop ---
    recording_started_at = millis(); // Start max recording timer now
    while (true) {
        // Check for external stop command (e.g., interrupt button)
        // We need a global flag for this, let's call it 'force_stop_listening_flag'
        // if (force_stop_listening_flag) {
        //     Serial.println("VAD: Recording stopped externally.");
        //     force_stop_listening_flag = false; // Reset flag
        //     break;
        // }

        // Check max recording duration
        if (millis() - recording_started_at > MAX_RECORDING_MS) {
            Serial.println("VAD: Max recording duration reached.");
            break;
        }

        esp_err_t read_result = i2s_read(I2S_NUM_0, record_buffer, record_buffer_size, &bytes_read, (100 / portTICK_PERIOD_MS)); // Short timeout

        if (read_result == ESP_OK && bytes_read > 0) {
            long long total_amplitude = 0;
            for (int i = 0; i < bytes_read; i += 2) {
                total_amplitude += abs((int16_t)(record_buffer[i] | (record_buffer[i+1] << 8)));
            }
             int average_amplitude = (bytes_read > 0) ? (total_amplitude / (bytes_read / 2)) : 0;

            // Start recording?
            if (!speech_started && average_amplitude > SPEECH_START_THRESHOLD) {
                speech_started = true;
                setState(EmilyState::RECORDING_SPEECH); // Update state
                Serial.println("VAD: Speech started, recording...");
            }

            // Record if speech detected
            if (speech_started) {
                file.write(record_buffer, bytes_read);
                total_data_size += bytes_read;
                Serial.print("+"); // Optional progress indicator

                // Check for end of speech (silence)
                if (average_amplitude < SILENCE_THRESHOLD) {
                    if (silence_started_at == 0) { silence_started_at = millis(); }
                    if (millis() - silence_started_at > SILENCE_DURATION_MS) {
                        Serial.println("VAD: Silence detected, stopping recording.");
                        break; // End recording loop
                    }
                } else {
                    silence_started_at = 0; // Reset silence timer if sound detected
                }
            }
        } else if (read_result == ESP_ERR_TIMEOUT) {
            // Timeout reading - check if silence duration met while recording
             if (speech_started && silence_started_at > 0 && (millis() - silence_started_at > SILENCE_DURATION_MS)) {
                Serial.println("VAD: Silence detected (via timeout), stopping recording.");
                break;
             }
             // Otherwise, just continue waiting if timeout occurred before speech started
        } else {
             Serial.printf("VAD Error during recording read: %d\n", read_result);
             break; // Exit loop on read error
        }
    } // End of main VAD loop

    // --- Step 3: Update header and clean up ---
    if (total_data_size > 0) {
        createWavHeader(wav_header, total_data_size);
        file.seek(0);
        file.write(wav_header, 44);
        Serial.printf("VAD: Recording complete. %u bytes written.\n", total_data_size);
    } else {
        Serial.println("VAD: Recording complete, but no audio data captured.");
        speech_started = false; // Ensure we return false if nothing was recorded
    }
    file.close();
    i2s_driver_uninstall(I2S_NUM_0); // Uninstall RX driver
    // --- NEW: Explicitly Detach Pins ---
    
    i2s_set_pin(I2S_NUM_0, NULL); // Detach pins from I2S peripheral
    // --- END NEW ---
    Serial.println(">>> DEBUG: Delaying after I2S RX cleanup (300ms)...");
    delay(300); // Keep delay for now to be safe
    return speech_started; // Return true only if speech was actually detected and recorded
}

// --- Need logInteractionToSd implementation (from CognitiveCore.ino) ---
void EmilyBrain::logInteractionToSd(JsonObject log_data) {
    File history_file = SD.open("/chat_history.jsonl", FILE_APPEND);
    if (history_file) {
        serializeJson(log_data, history_file);
        history_file.println();
        history_file.close();
        // Serial.println("Interaction logged to SD."); // Optional debug
    } else {
        Serial.println("ERROR: Could not open chat_history.jsonl for logging.");
    }
}
// --- Need a similar function for logging tool errors ---
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
    if (!audioFile) { /* handle error */ processSttResponseAndTriggerAi("{\"error\":\"Could not read audio file.\"}"); return; }
    size_t file_size = audioFile.size();
    if (file_size <= 44) { /* handle error */ audioFile.close(); processSttResponseAndTriggerAi("{\"error\":\"Audio file empty or invalid.\"}"); return; }

    Serial.printf("Starting STT streaming upload of %s (%d bytes)...\n", filename, file_size);
    setState(EmilyState::PROCESSING_STT); // Update state

    String boundary = "----EmilyBoundary" + String(random(0xFFFFF), HEX); // Add random element
    String host = "api.openai.com";
    String url = "/v1/audio/transcriptions";

    // --- Build request parts ---
    String prefix = "--" + boundary + "\r\n";
    prefix += "Content-Disposition: form-data; name=\"model\"\r\n\r\nwhisper-1\r\n";
    prefix += "--" + boundary + "\r\n";
    prefix += "Content-Disposition: form-data; name=\"language\"\r\n\r\nen\r\n";
    prefix += "--" + boundary + "\r\n";
    prefix += "Content-Disposition: form-data; name=\"file\"; filename=\"stt_input.wav\"\r\n";
    prefix += "Content-Type: audio/wav\r\n\r\n";
    String suffix = "\r\n--" + boundary + "--\r\n";
    size_t content_length = prefix.length() + file_size + suffix.length();

    // --- Open network connection ---
    WiFiClientSecure client;
    client.setInsecure();
    Serial.printf("Connecting to %s...\n", host.c_str());
    if (!client.connect(host.c_str(), 443)) { /* handle error */ audioFile.close(); processSttResponseAndTriggerAi("{\"error\":\"Connection failed\"}"); return; }
    Serial.println("Connected to OpenAI.");

    // --- Send HTTP Headers ---
    client.println("POST " + url + " HTTP/1.1");
    client.println("Host: " + host);
    String authHeader = "Bearer " + String(OPENAI_API_KEY);
    client.println("Authorization: " + authHeader);
    client.println("Content-Type: multipart/form-data; boundary=" + boundary);
    client.println("Content-Length: " + String(content_length));
    client.println("Connection: close");
    client.println(); // End of headers

    // --- Stream Body ---
    client.print(prefix); // Send prefix
    Serial.println("Streaming audio data...");
    uint8_t buffer[1024];
    size_t total_bytes_sent = 0;
    while (audioFile.available()) {
        size_t bytes_read = audioFile.read(buffer, sizeof(buffer));
        size_t bytes_sent = client.write(buffer, bytes_read);
        if (bytes_sent != bytes_read) {
             Serial.println("!!! ERROR sending audio chunk!");
             // Handle error - maybe abort?
             break;
        }
        total_bytes_sent += bytes_sent;
        // Serial.print("."); // Optional progress
    }
    audioFile.close();
    client.print(suffix); // Send suffix
    Serial.printf("Streaming upload complete (%u bytes sent).\n", total_bytes_sent);

    // --- Read Response ---
    String response_full = "";
    unsigned long timeout = millis();
    while (client.connected() && millis() - timeout < 30000) { // 30s timeout
         if (client.available()) {
            response_full += client.readString(); // Read available data
            timeout = millis(); // Reset timeout on receiving data
         }
         delay(10); // Small delay to prevent busy-waiting
    }
     if (!client.connected() && response_full.length() == 0) {
        Serial.println("!!! ERROR: Connection closed by server before response received.");
        processSttResponseAndTriggerAi("{\"error\":\"No response from server\"}");
        client.stop();
        return;
    }
    client.stop();
    // Serial.println("--- Full STT Response ---");
    // Serial.println(response_full);
    // Serial.println("-------------------------");

    // --- Extract Body and Send Result ---
    int body_start_index = response_full.indexOf("\r\n\r\n");
    String response_body = "{\"error\":\"Invalid server response\"}"; // Default error
    if (body_start_index != -1) {
        response_body = response_full.substring(body_start_index + 4);
        response_body.trim();
    }
    processSttResponseAndTriggerAi(response_body); // Send result back (need this function)
    
}

// --- processSttResponseAndTriggerAi function (from CognitiveCore) ---
void EmilyBrain::processSttResponseAndTriggerAi(const String& raw_api_payload) {
    // ... (Implementation remains the same as in CognitiveCore, BUT sends result to AI cycle) ...

    // --- ADJUSTMENT NEEDED (former sendSerialResult functie)---
    // Instead of sending over SerialPB, if the result_type is "stt_transcript",
    // we need to parse the transcript and call _start_ai_cycle with it.
    // If it's an error, we might still call _start_ai_cycle with the error message.

    Serial.println("processSttResponseAndTriggerAi (STT): Parsing transcript...");

    StaticJsonDocument<512> data_doc; // Adjust size
    DeserializationError error = deserializeJson(data_doc, raw_api_payload);
    String transcript = "";
    String error_msg = "";

    if (!error && data_doc.containsKey("text")) {
        transcript = data_doc["text"].as<String>();
        Serial.printf("Transcript received: '%s'\n", transcript.c_str());
    } else if (!error && data_doc.containsKey("error")) {
        error_msg = data_doc["error"].as<String>();
        Serial.printf("STT API Error received: '%s'\n", error_msg.c_str());
    } else {
        error_msg = "Failed to parse STT response or unknown format.";
        Serial.println(error_msg);
    }
    
    // --- Trigger AI Cycle with Result ---
   if (!transcript.isEmpty()) {
    // Combine the reason and the transcript into ONE string
    
    String trigger_string = "The user just said: " + transcript;
    _start_ai_cycle(trigger_string.c_str()); // Pass ONE argument using .c_str()
    } else {
        // Combine the reason and the error into ONE string
        // --- Optioneel: Log de STT error ook? ---
        StaticJsonDocument<256> error_log_doc;
        error_log_doc["role"] = "system"; // Of "internal"?
        error_log_doc["content"] = "STT Error: " + error_msg;
        logInteractionToSd(error_log_doc.as<JsonObject>());
        // --- Einde optioneel ---
        String trigger_string = "I tried listening but received no clear transcript. Error: " + error_msg;
        _start_ai_cycle(trigger_string.c_str()); // Pass ONE argument using .c_str()
    }
    // Serial.println(">>> DEBUG: Returned from _start_ai_cycle (after STT).");
    
    // Note: State change happens inside _start_ai_cycle
}

void EmilyBrain::listenAndTranscribe() {
    Serial.println("Starting listen_and_transcribe process...");
    // State is already AWAITING_SPEECH or set by recordAudioToWav

    const char* filename = "/stt_input.wav";

    // Step 1: Record audio using VAD
    bool recording_success = recordAudioToWav(filename);

    // Step 2: If recording successful (speech detected), transcribe it
    if (recording_success) {
        transcribeAudioFromSd(filename); // This function will set state and call AI cycle
    } else {
        // No speech detected or recording failed
        Serial.println("No speech detected or recording failed. Returning to IDLE.");
        processSttResponseAndTriggerAi("{\"error\":\"No speech detected\"}"); // Inform AI cycle
        // setState(EmilyState::IDLE); // processSttResponseAndTriggerAi now triggers AI cycle which sets state
    }
}

// --- The Executor ---
void EmilyBrain::_continue_task() {
    
    if (task_queue.empty()) {
        Serial.println("Executor: Task queue empty. Mission complete.");
        // Log SUCCESS result back to AI history
        if (!active_tool_call_id.isEmpty()) {
            logInteractionToSd_Error("tool", active_tool_call_id, active_tool_call_name, "SUCCESS: Action completed.");
             active_tool_call_id = ""; // Clear after logging
             active_tool_call_name = "";
        }
        setState(EmilyState::IDLE); // Return to idle
        return;
    }

    Task& next_task = task_queue.front(); // Get reference to next task
    Serial.printf("Executor: Starting task type '%s'\n", next_task.type.c_str());
    // Print args for debugging
    Serial.print("Executor: Args: ");
    serializeJson(next_task.args, Serial);
    Serial.println();

    // --- Task Execution Logic ---
    bool task_completed_immediately = false; // Flag for quick tasks

    if (next_task.type == "PB_EMOTION") {
        // Directly update internal emotional state
        arousal = next_task.args["new_arousal"] | arousal; // Keep current if missing
        valence = next_task.args["new_valence"] | valence; // Keep current if missing
        Serial.printf("Executor: Emotion updated - Arousal=%.2f, Valence=%.2f\n", arousal, valence);

        // Clear context if arousal drops below threshold (homeostasis)
        if (arousal <= AROUSAL_THRESHOLD) {
             if (current_arousal_context != nullptr) {
                Serial.println("Executor: Homeostasis reached, clearing context.");
                current_arousal_context = nullptr;
             }
        }
        task_completed_immediately = true; // This task is instant
    }
    else if (next_task.type == "CB_SOUND" || next_task.type == "CB_SOUND_EMOTION") {
        // Play a sound effect using state
        Serial.println("Executor: Setting state to PLAYING_SOUND.");
        setState(EmilyState::PLAYING_SOUND);
    }

    // --- CAMCANVAS_MOVE_HEAD (Vervangt oude OS_MOVE en Head moves) ---
    else if (next_task.type == "CAMCANVAS_MOVE_HEAD") {
        int pan_angle = next_task.args["pan"] | 90;
        int tilt_angle = next_task.args["tilt"] | 90;

        StaticJsonDocument<128> cmd_doc;
        cmd_doc["command"] = "move_head"; 
        cmd_doc["pan"] = pan_angle;
        cmd_doc["tilt"] = tilt_angle;
        String command_string;
        serializeJson(cmd_doc, command_string);

        Serial.printf("Executor: Sending CamCanvas command: %s\n", command_string.c_str());
        udp.beginPacket(CAMCANVAS_IP_ADDRESS, CAMCANVAS_UDP_PORT);
        udp.print(command_string);
        udp.endPacket();

        task_completed_immediately = true; // "Fire-and-forget"
        Serial.println("Executor: move_head command sent to CamCanvas.");
    }

    // --- CB_SPEAK Task (Aangepast naar Engels) ---
    else if (next_task.type == "CB_SPEAK") {
        const char* text_to_speak = nullptr; 

        // Check English keys first
        if (next_task.args.containsKey("announcement")) {
            text_to_speak = next_task.args["announcement"];
        } else if (next_task.args.containsKey("question")) {
            text_to_speak = next_task.args["question"];
        }
        // Fallback for legacy keys (safety)
        else if (next_task.args.containsKey("mededeling")) {
            text_to_speak = next_task.args["mededeling"];
        } else if (next_task.args.containsKey("vraag")) {
            text_to_speak = next_task.args["vraag"];
        }

        if (text_to_speak != nullptr && strlen(text_to_speak) > 0) {
             Serial.printf("Executor: Starting TTS for: '%s'\n", text_to_speak);
            last_display_text = text_to_speak;
             
             // Check against English tool name
             if (active_tool_call_name == "start_conversation" || active_tool_call_name == "start_gesprek") {
                 next_state_after_audio = EmilyState::AWAITING_SPEECH; 
                 Serial.println("Executor: Will transition to AWAITING_SPEECH after speaking.");
             } else { 
                 next_state_after_audio = EmilyState::IDLE;
                 Serial.println("Executor: Will transition to IDLE after speaking.");
             }

             // Start the TTS process (will set state internally)
             processTtsRequest(text_to_speak); 

             // Set timer for TTS timeout monitoring
             tts_task_start_time = millis();

             // Task is NOT completed immediately, TTS takes time.
        } else {
             Serial.println("Executor Error: No text found for CB_SPEAK task.");
             task_completed_immediately = true; // Skip this malformed task
        }
    }

    // --- CAM_ANALYZE Task ---
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

        Serial.printf("Executor: Sending CAM command: %s\n", command_string.c_str());
        udp.beginPacket(CAMCANVAS_IP_ADDRESS, CAMCANVAS_UDP_PORT);
        udp.print(command_string);
        udp.endPacket();

        camcanvas_task_start_time = millis(); // Start timer
        setState(EmilyState::SEEING); // Set waiting state
    }

    // --- CAM_TILT Task ---
    else if (next_task.type == "CAM_TILT") {
        int angle = next_task.args["angle"] | 90; 

        StaticJsonDocument<128> cmd_doc;
        cmd_doc["command"] = "tilt_head";
        cmd_doc["angle"] = angle;
        String command_string;
        serializeJson(cmd_doc, command_string);

        Serial.printf("Executor: Sending CAMCANVAS command (Tilt): %s\n", command_string.c_str());
        udp.beginPacket(CAMCANVAS_IP_ADDRESS, CAMCANVAS_UDP_PORT); 
        udp.print(command_string);
        udp.endPacket();

        task_completed_immediately = true; 
        Serial.println("Executor: Tilt command sent to CAMCANVAS."); 
    }

    // --- CAM_NOD Task ---
    else if (next_task.type == "CAM_NOD") {
        int angle = next_task.args["angle"] | 90; 

        StaticJsonDocument<128> cmd_doc;
        cmd_doc["command"] = "nod_head"; 
        cmd_doc["angle"] = angle;
        String command_string;
        serializeJson(cmd_doc, command_string);

        Serial.printf("Executor: Sending CAMCANVAS command (Nod): %s\n", command_string.c_str());
        udp.beginPacket(CAMCANVAS_IP_ADDRESS, CAMCANVAS_UDP_PORT); 
        udp.print(command_string);
        udp.endPacket();

        task_completed_immediately = true; 
        Serial.println("Executor: Nod command sent to CAMCANVAS.");
    }

    else if (next_task.type == "CAM_TAKE_PICTURE") {
        StaticJsonDocument<64> cmd_doc;
        cmd_doc["command"] = "take_picture"; 
        String command_string;
        serializeJson(cmd_doc, command_string);

        Serial.println("Executor: Sending CAMCANVAS command: take_picture");
        udp.beginPacket(CAMCANVAS_IP_ADDRESS, CAMCANVAS_UDP_PORT);
        udp.print(command_string);
        udp.endPacket();

        task_completed_immediately = true; 
    }

    // --- CAMCANVAS_SET_LED Taak ---
    else if (next_task.type == "CAMCANVAS_SET_LED") {
        String command_string;
        serializeJson(next_task.args, command_string); 

        Serial.printf("Executor: Sending CamCanvas LED command: %s\n", command_string.c_str());
        udp.beginPacket(CAMCANVAS_IP_ADDRESS, CAMCANVAS_UDP_PORT);
        udp.print(command_string);
        udp.endPacket();

        task_completed_immediately = true; 
    }
     // --- CANVAS_IMAGE_SYNC Task (Blocking) ---
    else if (next_task.type == "CANVAS_IMAGE_SYNC") {
        String prompt = next_task.args["prompt"] | "a robot";
        String model = next_task.args["image_model"] | "venice-sd35"; 

        StaticJsonDocument<512> cmd_doc; 
        cmd_doc["command"] = "generate_image";
        cmd_doc["prompt"] = prompt;
        cmd_doc["model"] = model;
        String command_string;
        serializeJson(cmd_doc, command_string);

        Serial.printf("Executor: Sending CAMCANVAS command (SYNC): %s\n", command_string.c_str());
        udp.beginPacket(CAMCANVAS_IP_ADDRESS, CAMCANVAS_UDP_PORT);
        udp.print(command_string);
        udp.endPacket();

        camcanvas_task_start_time = millis(); 
        setState(EmilyState::VISUALIZING); 
    }

     // --- CANVAS_IMAGE_ASYNC Task (Non-blocking) ---
    else if (next_task.type == "CANVAS_IMAGE_ASYNC") {
        String prompt = next_task.args["prompt"] | "a robot";
        String model = next_task.args["image_model"] | "venice-sd35";

        StaticJsonDocument<512> cmd_doc;
        cmd_doc["command"] = "generate_image";
        cmd_doc["prompt"] = prompt;
        cmd_doc["model"] = model;
        String command_string;
        serializeJson(cmd_doc, command_string);

        Serial.printf("Executor: Sending CAMCANVAS command (ASYNC): %s\n", command_string.c_str());
        udp.beginPacket(CAMCANVAS_IP_ADDRESS, CAMCANVAS_UDP_PORT);
        udp.print(command_string);
        udp.endPacket();

        task_completed_immediately = true; // ASYNC: Don't wait
        Serial.println("Executor: Async image command sent.");
    }
    
    // --- INPUTPAD Tools (Aangepast naar Engels) ---
    else if (next_task.type == "INPUTPAD_SET_MODE") {
        StaticJsonDocument<256> cmd_doc;
        cmd_doc["command"] = "set_mode";
        cmd_doc["mode"] = next_task.args["mode"];
        
        // Changed to max_value
        if (next_task.args.containsKey("max_value")) {
             cmd_doc["max"] = next_task.args["max_value"];
        }
        
        String command_string;
        serializeJson(cmd_doc, command_string);

        Serial.printf("Executor: Sending INPUTPAD command: %s\n", command_string.c_str());
        udp.beginPacket(INPUTPAD_IP_ADDRESS, INPUTPAD_UDP_PORT);
        udp.print(command_string);
        udp.endPacket();

        input_task_start_time = millis(); 
        setState(EmilyState::AWAITING_INPUT); 
    }
    
    // --- EB_GET_LOCAL_DATA ---
    else if (next_task.type == "EB_GET_LOCAL_DATA") {
        Serial.println("Executor: Setting state to PROCESS_GAMEDATA");
        setState(EmilyState::PROCESSING_GAMEDATA);
    }
    
    else {
        Serial.printf("Executor Error: Unknown task type '%s'\n", next_task.type.c_str());
        task_completed_immediately = true;
    }

    // --- Task Completion & Next Step ---
    if (task_completed_immediately) {
        physical_task_start_time = 0; 
        task_queue.pop_front(); 
        _continue_task();       
    }
}


String EmilyBrain::getDataFromJson(const char* key) {
    if (strcmp(key, "ERROR_KEY") == 0) {
        return "ERROR"; // Invalid key passed
    }
    
    File file = SD.open("/adventure.json");
    if (!file) { 
        Serial.println("ERROR: Could not open /adventure.json");
        return "ERROR"; 
    }

    // Filter to only read the specific key we need (saves memory)
    DynamicJsonDocument filter(JSON_OBJECT_SIZE(1));
    filter[key] = true; 

    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, file, DeserializationOption::Filter(filter));
    file.close();

    if (error) { 
        Serial.print("ERROR: JSON Deserialization failed: ");
        Serial.println(error.c_str());
        return "ERROR"; 
    }

    if (!doc.containsKey(key)) { 
        Serial.printf("ERROR: getDataFromJson - Key '%s' not found in json.\n", key);
        return "ERROR";
    }

    JsonObject field_info = doc[key]; 
    String output_string;
    serializeJson(field_info, output_string);
    
    return output_string;
}


// --- Helper Function for Significant Events ---
void EmilyBrain::addSignificantEvent(const String& event_desc) {
    // Check if deque is full
    if (significant_events.size() >= MAX_SIGNIFICANT_EVENTS) {
        significant_events.pop_front(); // Remove oldest event
    }
    // Add new event
    significant_events.push_back(event_desc);
    Serial.println("DEBUG: Added significant event: " + event_desc);
}

bool EmilyBrain::downloadTtsToSd(const char* textToSpeak, const char* filename) {
    // This function remains almost IDENTICAL to the CognitiveCore version
    
    setState(EmilyState::GENERATING_SPEECH);
    Serial.printf("TTS Download: Requesting audio for '%s' to %s\n", textToSpeak, filename);
    WiFiClientSecure https_client;
    HTTPClient http;
    https_client.setInsecure();

    if (http.begin(https_client, "https://api.venice.ai/api/v1/audio/speech")) {
        // ... (add headers: Authorization, Content-Type) ...
        String authHeader = "Bearer " + String(VENICE_API_KEY); // Use define
        http.addHeader("Authorization", authHeader);
        http.addHeader("Content-Type", "application/json");

        StaticJsonDocument<256> payload_doc; // Small doc is enough for TTS payload
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
                int bytesWritten = http.writeToStream(&file); // Get bytes written
                file.close();
                if (bytesWritten > 0) {
                     Serial.printf("TTS Download: Success (%d bytes written).\n", bytesWritten);
                     http.end(); // End connection here on success
                     return true;
                } else {
                     Serial.println("TTS Download ERROR: Failed to write to SD file.");
                     // SD might be full or corrupted
                }
            } else {
                Serial.println("TTS Download ERROR: Could not open SD file for writing.");
            }
        } else {
            Serial.printf("TTS Download ERROR: API Error Code %d\n", httpCode);
            String errorPayload = http.getString();
            Serial.println("Error Payload: " + errorPayload);
        }
        http.end(); // End connection here on failure too
    } else {
        Serial.println("TTS Download ERROR: Failed to connect to API.");
    }
    return false; // Return false if any error occurred
}

void EmilyBrain::processTtsRequest(const char* text) {
    
    Serial.println("Processing TTS request...");
    // setState(EmilyState::GENERATING_SPEECH); // NEW state for download/API call

    const char* filename = "/tts_output.wav";
    tts_task_start_time = millis(); // Start timer just before download attempt

    // Step 1: Download the audio to SD card (BLOCKING for now)
    bool download_success = downloadTtsToSd(text, filename);

    // Step 2: If download successful, start playback state
    if (download_success) {
        // --- DON'T PLAY HERE ---
        // Playback happens when the state machine reaches SPEAKING
        
        setState(EmilyState::SPEAKING); // Transition to SPEAKING state
        
    } else {
        // Handle download failure - maybe log error, go back to IDLE?
        Serial.println("TTS Request FAILED during download.");
        // Log error back to AI history?
        if (!active_tool_call_id.isEmpty()) {
             logInteractionToSd_Error("tool", active_tool_call_id, active_tool_call_name, "ERROR: Failed to generate speech audio.");
        }
         setState(EmilyState::IDLE); // Go back to IDLE on failure
         tts_task_start_time = 0; // Reset timer on failure
    }
    // Note: Playback doesn't happen in this function anymore.
    Serial.println(">>> DEBUG: Exiting processTtsRequest."); // <-- ADD
}

String EmilyBrain::buildSelfAwarenessReport(JsonObject device_status) {
    String report = "--- SELF-AWARENESS REPORT ---\n";
    report += "This section describes my physical body and its capabilities.\n\n";
    report += DESC_CORE; // Core description
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

// --- SIMPLIFIED Forward Reading Version ---
void EmilyBrain::addChatHistoryToMessages(JsonArray messages, int max_history_items) {
    File history_file = SD.open("/chat_history.jsonl");
    if (!history_file || !history_file.size()) {
        Serial.println("Chat history not found or is empty.");
        if (history_file) history_file.close();
        return;
    }

    Serial.printf("Reading chat history (forward, max %d items)...\n", max_history_items);

    // Use a deque to store lines temporarily to easily limit the count
    std::deque<String> history_lines;

    // Read file line by line, forward
    while (history_file.available()) {
        String line = history_file.readStringUntil('\n');
        line.trim(); // Remove potential \r or extra whitespace
        if (line.length() > 0) {
            history_lines.push_back(line);
            // If deque exceeds max size, remove the oldest item (at the front)
            if (history_lines.size() > max_history_items) {
                history_lines.pop_front();
            }
        }
    }
    history_file.close();

    Serial.printf("DEBUG: Found %d relevant lines in history file.\n", history_lines.size());

    int added_count = 0;
    // Iterate through the collected lines (now in correct chronological order)
    for (const String& line : history_lines) {
        // Serial.println("DEBUG: Attempting to parse history line:"); // Optional
        // Serial.println(line);                                     // Optional

        StaticJsonDocument<4096> history_item_doc; // Keep buffer reasonably large
        DeserializationError error = deserializeJson(history_item_doc, line);

        if (error == DeserializationError::Ok) {
            // Serial.println("DEBUG: Parse SUCCESSFUL."); // Optional
            messages.add(history_item_doc.as<JsonObject>());
            added_count++;
        } else {
            Serial.print("!!! ERROR parsing history line: ");
            Serial.println(error.c_str());
        }
    }
    Serial.printf("%d history items added to payload.\n", added_count);
}

// --- Function to build the payload IN the provided document ---
void EmilyBrain::buildAiPayload(StaticJsonDocument<JSON_DOC_CAPACITY>& api_payload_doc, // Pass by reference
                                const char* current_prompt_content,
                                JsonObject device_status) {

    Serial.println("DEBUG: Entering buildAiPayload (StaticJsonDocument version)...");

    // --- Step A: Read, Filter, and Clean Tools ---
    Serial.println("DEBUG: Adding tools...");
    File tools_file = SD.open("/tools_config.json");
    if (tools_file) {
        // Serial.println("DEBUG: SD.open() SUCCESSFUL."); // <-- Added check
        StaticJsonDocument<16384> all_tools_doc; // Temp doc for tools
        DeserializationError error = deserializeJson(all_tools_doc, tools_file);
        tools_file.close();
        
        if (!error) {
            JsonArray filtered_tools = api_payload_doc.createNestedArray("tools");
            for (JsonObject tool : all_tools_doc.as<JsonArray>()) {
                bool is_tool_available = true;
                JsonObject function_obj = tool["function"];
                JsonArray required = function_obj["required_devices"];

                if (required) { // Check if required array exists
                    for (JsonVariant device_var : required) { // Iterate using JsonVariant
                        const char* device = device_var.as<const char*>();
                         if (!device) continue; // Skip if conversion fails

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
                    // Copy the tool, excluding 'required_devices'
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
            Serial.printf("DEBUG: Tools added (%d).\n", filtered_tools.size());
            if (api_payload_doc.overflowed()) { Serial.println("FATAL: Overflow after tools!"); return; } // Exit if overflowed
        } else { Serial.printf("DEBUG: Error parsing tools: %s\n", error.c_str()); }
    } else { Serial.println("DEBUG: Error opening tools file!"); }



    // --- Step B: Build the 'messages' List ---
    Serial.println("DEBUG: Adding model and messages array...");
    api_payload_doc["model"] = "llama-3.3-70b";
    JsonArray messages = api_payload_doc.createNestedArray("messages");
    if (api_payload_doc.overflowed()) { Serial.println("FATAL: Overflow after model/array!"); return; }

    // 1. System Prompt
    Serial.println("DEBUG: Building/Adding system prompt...");
    String self_awareness_report = buildSelfAwarenessReport(device_status);
    String final_system_prompt = self_awareness_report + "\n\n" + system_prompt_content;
    JsonObject system_msg = messages.createNestedObject();
    system_msg["role"] = "system";
    system_msg["content"] = final_system_prompt;
    Serial.printf("DEBUG: System prompt added. size: %d bytes\n", measureJson(api_payload_doc));
    if (api_payload_doc.overflowed()) { Serial.println("FATAL: Overflow after system prompt!"); return; }

    // 2. Chat History
    Serial.println("DEBUG: Adding chat history...");
    addChatHistoryToMessages(messages, 120);
    Serial.printf("DEBUG: History added. Size: %d bytes\n", measureJson(api_payload_doc));
    if (api_payload_doc.overflowed()) { Serial.println("FATAL: Overflow after history!"); return; }

    // 3. User Message
    Serial.println("DEBUG: Adding user message...");
    JsonObject user_msg = messages.createNestedObject();
    user_msg["role"] = "user";
    user_msg["content"] = current_prompt_content;
    Serial.printf("DEBUG: User message added. FINAL measured size: %d bytes\n", measureJson(api_payload_doc));
    if (api_payload_doc.overflowed()) { Serial.println("FATAL: Overflow after user message!"); return; }

    Serial.println("DEBUG: Exiting buildAiPayload...");
    // No return value needed
    // return;
}

void EmilyBrain::decayArousal() {
    unsigned long current_time = millis();
    // Only decay if arousal is positive and enough time has passed
    if (arousal > 0.0 && (current_time - last_decay_time) >= 1000) { // Check every second
        double decay_amount = AROUSAL_DECAY_RATE;
        arousal -= decay_amount;
        if (arousal < 0.0) {
            arousal = 0.0; // Don't go below zero
        }
        // Serial.printf("Arousal decayed to: %.2f\n", arousal); // Optional debug
        last_decay_time = current_time;
    }
}

void EmilyBrain::loadConfigurations() {
    Serial.println("Loading configurations from SD card...");

    File prompt_file = SD.open("/system_prompt.txt");
    if (prompt_file) {
        // Lees de volledige inhoud van het bestand in de String-variabele
        system_prompt_content = prompt_file.readString();
        prompt_file.close();
        
        if (system_prompt_content.length() > 0) {
            Serial.println("System prompt successfully loaded.");
        } else {
            Serial.println("WARNING: system_prompt.txt is empty.");
        }
    } else {
        Serial.println("ERROR: Could not open system_prompt.txt!");
        // We kunnen hier een standaard-prompt laden als terugval
        system_prompt_content = "You are a helpful assistant.";
    }
}

void EmilyBrain::sendPings() {
    unsigned long current_time = millis();
    if (current_time - last_ping_time >= PING_INTERVAL_MS) {
        if (wifi_status == WiFiStatus::CONNECTED) { // Only send if connected
            const char* ping_msg = "PING";
            
            // Removed OS Ping
            
            udp.beginPacket(CAMCANVAS_IP_ADDRESS, CAMCANVAS_UDP_PORT);
            udp.print(ping_msg);
            udp.endPacket();

            udp.beginPacket(INPUTPAD_IP_ADDRESS, INPUTPAD_UDP_PORT);
            udp.print(ping_msg);
            udp.endPacket();

            // Serial.println("Pings sent."); // Optional debug
        }
        last_ping_time = current_time;
    }
}

void EmilyBrain::handleUdpPackets() {
    if (wifi_status != WiFiStatus::CONNECTED) {
        return; // Don't process if not connected
    }

    int packetSize = udp.parsePacket();
    if (packetSize) {
        
        // --- STEP 1: READ THE PACKET (ONCE!) ---
        // Use a buffer large enough for the LARGEST expected packet (e.g. vision data)
        char packetBuffer[1536]; // 1.5K buffer
        int len = udp.read(packetBuffer, 1536);
        if (len > 0) {
            packetBuffer[len] = 0; // Null-terminate
        } else {
            udp.flush(); // Empty buffer
            return; // Empty packet
        }

        String sender_ip_str = udp.remoteIP().toString();
        unsigned long current_time = millis();

        // --- Identify Sender & Update Status ---

        // (OS Block Removed)

        // --- Handle CAMCANVAS Packets ---
        if (sender_ip_str == CAMCANVAS_IP_ADDRESS) {
            last_camcanvas_contact = current_time;
            if (!camcanvas_connected) { Serial.println("CamCanvas Connected."); camcanvas_connected = true; }

            // --- STATE-BASED PROCESSING ---
            // What do we expect right now?

            // 1. Expecting VISION response?
            if (currentState == EmilyState::SEEING) {
                // Serial.println(">>> DEBUG: (State=SEEING) Looking for 'vision'...");
                StaticJsonDocument<1024> vision_doc;
                DeserializationError error = deserializeJson(vision_doc, packetBuffer);
                if (!error && vision_doc.containsKey("result_type") && strcmp(vision_doc["result_type"], "vision") == 0) {
                    Serial.println(">>> DEBUG: 'vision' result FOUND and processing.");
                    last_vision_response.set(vision_doc);
                } 
            }
            
            // 2. Expecting IMAGE COMPLETE response?
            else if (currentState == EmilyState::VISUALIZING) {
                // Serial.println(">>> DEBUG: (State=VISUALIZING) Looking for 'image_complete'...");
                StaticJsonDocument<256> confirmation_doc;
                DeserializationError error = deserializeJson(confirmation_doc, packetBuffer);
                if (!error && confirmation_doc.containsKey("result_type") && strcmp(confirmation_doc["result_type"], "image_complete") == 0) {
                    Serial.println(">>> DEBUG: 'image_complete' FOUND and processing.");
                    last_camcanvas_confirmation.set(confirmation_doc);
                } 
            }

            // 3. General Updates (Heartbeats, Photos, etc.)
            else {
                StaticJsonDocument<512> other_doc; 
                DeserializationError error = deserializeJson(other_doc, packetBuffer);
                
                // Check for Photo Confirmation
                if (!error && other_doc.containsKey("result_type") && strcmp(other_doc["result_type"], "picture_taken") == 0) {
                    const char* status = other_doc["status"] | "error";
                    if (strcmp(status, "success") == 0) { addSignificantEvent("CAM: Photo saved."); }
                    else { addSignificantEvent("CAM: ERROR saving photo."); }
                }
                // Check for Heartbeat (Tilt/Pan info)
                else if (!error && other_doc.containsKey("id") && other_doc.containsKey("state")) {
                    current_cam_tilt = other_doc["tilt_angle"] | 90.0f; 
                    current_cam_pan = other_doc["pan_angle"] | 90.0f;
                }
            }
        } // --- END CAMCANVAS BLOCK ---    
        
        // --- Handle INPUTPAD Packets ---
        else if (sender_ip_str == INPUTPAD_IP_ADDRESS) {
            last_inputpad_contact = current_time;
            if (!inputpad_connected) { Serial.println("InputPad Connected."); inputpad_connected = true; }

            // Peek at JSON to determine event type
            StaticJsonDocument<96> peek_doc;
            deserializeJson(peek_doc, packetBuffer); 
            const char* event_type = peek_doc["event"] | "unknown";

            if (strcmp(event_type, "input_received") == 0 && currentState == EmilyState::AWAITING_INPUT) {
                Serial.println("Received INPUT_RECEIVED from InputPad.");
                last_inputpad_response.clear();
                DeserializationError err = deserializeJson(last_inputpad_response, packetBuffer); 
                if (err) Serial.println("ERROR: Could not parse input_received!");
            }
        }
        
        // --- Clear the UDP buffer ---
        udp.flush();
    }
}

void EmilyBrain::checkTimeouts() {
    unsigned long current_time = millis();

    // (OS Check Removed)

    // --- CAMCANVAS CHECK ---
    if (camcanvas_connected && (current_time - last_camcanvas_contact > CONNECTION_TIMEOUT_MS) &&
        currentState != EmilyState::SEEING &&           
        currentState != EmilyState::VISUALIZING)      
    {
        Serial.println("CamCanvas Disconnected (Timeout).");
        camcanvas_connected = false;
    }
    // --- INPUTPAD CHECK ---
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
        // --- Title Bar (1x) ---
        display.fillRect(0, 0, display.width(), 30, TFT_DARKCYAN);
        display.setTextColor(TFT_WHITE, TFT_DARKCYAN);
        display.drawString("Emily", 5, 5, 4);

        // --- Static Labels (1x) ---
        int y_lineA = 45;
        int y_lineV = 60;
        int y_line1 = 75;
        int y_line2 = 90;
        int x_col1_lbl = 10;
        
        display.setTextColor(TFT_WHITE, TFT_BLACK);
        
        display.drawString("A:", x_col1_lbl, y_lineA + 2, 1); // Arousal Label
        display.drawString("V:", x_col1_lbl, y_lineV + 2, 1); // Valence Label
        display.drawString("State:", x_col1_lbl, y_line1 + 2, 1); // Translated
        display.drawString("CC:", x_col1_lbl, y_line2 + 2, 1);

        layout_drawn = true;
    }

    // --- Connectivity Dots (Simple Update Logic) ---
    // WiFi
    if (wifi_status != prev_wifi_status || force_redraw) {
        int wifiX = display.width() - 25; int wifiY = 15; int radius = 6;
        uint16_t wifiColor = TFT_RED;
        if (wifi_status == WiFiStatus::CONNECTING) { /* Blink logic placeholder */ }
        else if (wifi_status == WiFiStatus::CONNECTED) { wifiColor = TFT_BLUE; }
        display.fillCircle(wifiX, wifiY, radius, wifiColor);
        prev_wifi_status = wifi_status;
    }
    
    // CAMCANVAS Dot
    if (camcanvas_connected != prev_camcanvas_connected || force_redraw) {
        int ccDotX = display.width() - 50; int cY = 15; int radius = 6;
        uint16_t ccDotColor = camcanvas_connected ? TFT_GREEN : TFT_RED;
        display.fillCircle(ccDotX, cY, radius, ccDotColor);
        prev_camcanvas_connected = camcanvas_connected; 
    }
    // InputPad (IP) Status Dot
    if (inputpad_connected != prev_inputpad_connected || force_redraw) {
        int ipDotX = display.width() - 75; 
        int ipY = 15; int radius = 6;
        uint16_t ipDotColor = inputpad_connected ? TFT_GREEN : TFT_RED;
        display.fillCircle(ipDotX, ipY, radius, ipDotColor);
        prev_inputpad_connected = inputpad_connected;
    }

    // --- Arousal Bar ---
    if (arousal != prev_arousal || force_redraw) {
        int barY = 40; 
        int barX = 30;
        int barHeight = 10;
        int barWidth = display.width() - barX - 10;
        int filledWidth = (int)(arousal * barWidth);

        display.fillRect(barX, barY, barWidth, barHeight, TFT_DARKGREY);
        if (filledWidth > 0) {
             display.fillRect(barX, barY, filledWidth, barHeight, TFT_RED);
        }
        prev_arousal = arousal;
    }

    // --- Valence Bar ---
    if (valence != prev_valence || force_redraw) {
        int barY = 55; 
        int barX = 30;
        int barHeight = 10;
        int barWidth = display.width() - barX - 10;
        
        // Map valence [-1.0, 1.0] to [0, barWidth]
        int zeroPoint = barWidth / 2;
        int valPixel = (int)(((valence + 1.0) / 2.0) * barWidth);
        
        display.fillRect(barX, barY, barWidth, barHeight, TFT_DARKGREY);
        uint16_t valenceColor = (valence >= 0) ? TFT_GREEN : TFT_ORANGE;
        
        if (valPixel > zeroPoint) {
             display.fillRect(barX + zeroPoint, barY, valPixel - zeroPoint, barHeight, valenceColor);
        } else if (valPixel < zeroPoint) {
             display.fillRect(barX + valPixel, barY, zeroPoint - valPixel, barHeight, valenceColor);
        }
        prev_valence = valence;
    }

    // --- Dynamic Text Lines ---
    char text_buffer[50]; 
    int y_line1 = 75;
    int y_line2 = 90;
    int y_line3 = 105;
    int x_col1_val = 50;  
    int x_col2_lbl = 160; 
    int x_col2_val = 200; 

    // --- Line 1: Emily State ---
    const char* stateStr = stateToString(currentState);
    // Removed Mode string logic
    
    if (prev_line1_L != stateStr) {
        display.fillRect(x_col1_val, y_line1, display.width() - x_col1_val, 10, TFT_BLACK); 
        display.setTextColor(TFT_YELLOW, TFT_BLACK);
        display.drawString(stateStr, x_col1_val, y_line1 + 2, 1);
        
        prev_line1_L = stateStr;
    }

    // --- Line 2: CC Status & Pan ---
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

    // --- Line 3: Tilt ---
    sprintf(text_buffer, "%.0f deg", current_cam_tilt);
    String tilt_text = camcanvas_connected ? text_buffer : "N/A";

    if (prev_line3_R != tilt_text) {
        // Only clearing the right side to avoid flickering the left side if it was used
        // But since Line 3 left is empty, we can clear it or just draw over.
        // Let's clear the area for Tilt Label + Value
        display.fillRect(x_col2_lbl, y_line3, display.width() - x_col2_lbl, 10, TFT_BLACK); 
        
        display.setTextColor(TFT_WHITE, TFT_BLACK);
        display.drawString("Tilt:", x_col2_lbl, y_line3 + 2, 1); 
        display.setTextColor(camcanvas_connected ? TFT_SKYBLUE : TFT_RED, TFT_BLACK);
        display.drawString(tilt_text, x_col2_val, y_line3 + 2, 1); 
        
        prev_line3_R = tilt_text;
    }
    
    // --- Chat / Info Window ---
    int chat_start_y = 125; 
    int chat_line_height = 10; 

    if (last_display_text != prev_display_text || force_redraw) {
        // Clear the chat area
        display.fillRect(0, chat_start_y, display.width(), display.height() - chat_start_y, TFT_BLACK);

        // Set text properties
        display.setTextColor(TFT_WHITE, TFT_BLACK);
        display.setTextSize(1); 
        display.setCursor(5, chat_start_y + 2); 

        // Enable text wrapping
        display.setTextWrap(true);

        // Print the text 
        display.print(last_display_text);

        prev_display_text = last_display_text; 
    }
}

// --- State Handlers ---
void EmilyBrain::handleIdleState() {
    // This function is only called when currentState == IDLE.
    
    // --- CHECK 1: HIGH AROUSAL (Active Context) ---
    // If arousal is high, we MUST react or continue reacting.
    if (arousal > AROUSAL_THRESHOLD) {
        
        if (current_arousal_context != nullptr) {
            // Option A: CONTINUATION. Previous action didn't resolve arousal.
            Serial.println("Idle Handling: Continuing previous arousal context.");
            _start_ai_cycle("CONTINUATION: Previous action did not resolve high arousal.");
        
        } else {
            // Option B: High arousal but NO context (e.g. web trigger).
            // Investigate cause.
            Serial.println("Idle Handling: Arousal is high with NO context. Investigating.");
            _start_ai_cycle("INTERNAL_TRIGGER: Arousal is high without context, investigating cause.");
        }
        return; // Exit function, AI cycle started.
    }

    // --- CHECK 2: LOW AROUSAL (Scan for NEW Triggers) ---
    // If arousal is low, reset context and look for new stimuli.
    if (arousal <= AROUSAL_THRESHOLD) {
        if (current_arousal_context != nullptr) {
             Serial.println("Idle Handling: Arousal low, clearing context.");
             current_arousal_context = nullptr;
        }

        const char* new_trigger_reason = nullptr;
        double new_arousal = 0.0;
        double new_valence = 0.0;
        bool trigger_found = false; 

        // --- PRIORITY 1: WAKE BUTTON (Physical Interrupt) ---
        if (checkWakeButton()) { 
            Serial.println("Idle Handling: Wake Button pressed!");
            new_trigger_reason = "USER_ACTION: Wake button pressed.";
            new_arousal = 0.8; 
            new_valence = 0.2; 
            trigger_found = true;
        }

        // --- FUTURE PRIORITIES (Placeholders) ---
        // 2. Agenda / Timer check
        // 3. Vision-based presence detection or human presence detection

        // --- DECISION: TRIGGER FOUND? ---
        if (trigger_found) {
            // Update internal state
            arousal = new_arousal;
            valence = new_valence;
            last_decay_time = millis(); // Reset decay timer
            current_arousal_context = new_trigger_reason; // Store new context

            // Start AI Cycle (if online)
            if (wifi_status == WiFiStatus::CONNECTED) {
                 _start_ai_cycle(current_arousal_context);
            } else {
                 Serial.println("Trigger detected, but WiFi is offline. Cannot start AI cycle.");
            }
        }
    }
    // If we reach here: Either arousal was high (handled in Check 1), 
    // or arousal was low but no NEW trigger was found. Do nothing.
}

// ---  _start_ai_cycle ---
void EmilyBrain::_start_ai_cycle(const char* trigger_reason) {
    
    Serial.printf("AI Cycle Triggered! Reason: %s\n", trigger_reason);

    // --- Log the trigger context as a 'user' message ---
    StaticJsonDocument<1536> log_doc; 
    log_doc["role"] = "user";
    
    // Construct the full "INTERNAL_REPORT:" string here
    String report_content = "INTERNAL_REPORT:\n";
    report_content += "- Triggering Event: " + String(trigger_reason) + "\n";
    report_content += "- My Current Emotional State: arousal=" + String(arousal, 2) + ", valence=" + String(valence, 2) + "\n";
    
    log_doc["content"] = report_content;
    logInteractionToSd(log_doc.as<JsonObject>()); // Log it!
    
    // --- Set State ---
    setState(EmilyState::PROCESSING_AI);

    // --- 1. Collect Context Data (JSON) ---
    StaticJsonDocument<1024> context_doc; 

    context_doc["trigger"] = trigger_reason;
    
    JsonObject emotion = context_doc.createNestedObject("emotion");
    emotion["arousal"] = arousal;
    emotion["valence"] = valence;

    JsonObject device_status = context_doc.createNestedObject("device_status");
    // Removed os_online
    device_status["camcanvas_online"] = camcanvas_connected;
    device_status["inputpad_online"] = inputpad_connected;

    // --- 2. Build Full Prompt String (Text) ---
    String ai_prompt = "INTERNAL_REPORT:\n";
    ai_prompt += "- Triggering Event: " + String(trigger_reason) + "\n";
    ai_prompt += "- My Current Emotional State: arousal=" + String(arousal, 2) + ", valence=" + String(valence, 2) + "\n";
    
    // --- World Summary (Cleaned Up) ---
    ai_prompt += "- Peripherals: ";
    ai_prompt += "CamCanvas=" + String(camcanvas_connected ? "Online" : "Offline") + ", ";
    ai_prompt += "InputPad=" + String(inputpad_connected ? "Online" : "Offline") + ".\n";

    // Only Pan/Tilt remains (Head Position)
    ai_prompt += "- Head Position: Pan=" + String(current_cam_pan, 0) + " deg, ";
    ai_prompt += "Tilt=" + String(current_cam_tilt, 0) + " deg.\n";
    
    // --- Significant Events ---
    if (!significant_events.empty()) {
        ai_prompt += "- Recent Events: ";
        for (const String& evt : significant_events) {
            ai_prompt += "[" + evt + "] ";
        }
        ai_prompt += "\n";
    }
    
    // --- End Prompt Build ---
    Serial.println("--- Context for AI (Prompt String) ---");
    Serial.println(ai_prompt);
    Serial.println("------------------------------------");
    
    // --- AI Call ---
    Serial.println("Calling processAiProxyRequest...");
    processAiProxyRequest(ai_prompt.c_str(), context_doc["device_status"].as<JsonObject>());
}

void EmilyBrain::handleAwaitingSpeechState() { 
    // We are actively waiting for sound > SPEECH_START_THRESHOLD.
    // The actual VAD logic is currently *blocking* inside recordAudioToWav.
    // If recordAudioToWav were non-blocking, this handler would read I2S.
    // For now, this handler doesn't do much besides indicate the state.
    // We might add a timeout here later.
    // Serial.print("A"); // Optional debug indicator
 }
void EmilyBrain::handleRecordingSpeechState() { 
    // We are actively recording audio (sound > SILENCE_THRESHOLD).
    // The actual recording logic is *blocking* inside recordAudioToWav.
    // If recording were non-blocking, this handler would write to SD.
    // For now, this handler doesn't do much.
    // Serial.print("R"); // Optional debug indicator
 }
void EmilyBrain::handleProcessingSttState() { 
    // We are waiting for the transcribeAudioFromSd function (Whisper API call)
    // to complete. That function is currently *blocking*.
    // If transcription were asynchronous, this handler would check for completion.
    // We *could* add a timeout check here for the API call.
    // Serial.print("T"); // Optional debug indicator 
}

void EmilyBrain::handleProcessGamedataState() {
    Serial.println("Handler: Entering PROCESS_GAMEDATA state.");

    // 1. Get the task (we know it's at the front)
    if (task_queue.empty() || task_queue.front().type != "EB_GET_LOCAL_DATA") {
        Serial.println("Handler ERROR: PROCESS_GAMEDATA state entered, but task queue is invalid!");
        setState(EmilyState::IDLE); 
        return;
    }
    
    Task& current_task = task_queue.front();

    // 2. Get the data key
    // --- KEY CORRECTION ---
    const char* key = current_task.args["key"] | "ERROR_KEY"; // Changed from 'sleutel' to 'key'
    // --- END CORRECTION ---

    Serial.printf("Handler: Reading local CMS data for key: %s...\n", key);
    
    // Call helper function (assuming getDataFromJson exists and works)
    String field_data_str = getDataFromJson(key); 

    // 3. Process the result
    if (field_data_str == "ERROR") {
        // Log error
        logInteractionToSd_Error("tool", active_tool_call_id, active_tool_call_name, "ERROR: Failed to read local data.");
        String error_trigger = "GAME_DATA_ERROR: Failed to read local game data for key " + String(key);
        
        // 4. Remove task
        task_queue.pop_front();
        
        // 5. Start NEXT AI cycle with error context
        _start_ai_cycle(error_trigger.c_str());
    
    } else {
        // Log success
        logInteractionToSd_Error("tool", active_tool_call_id, active_tool_call_name, "SUCCESS: Data retrieved.");
        
        // Construct natural language trigger for AI
        String success_trigger = "GAME_DATA: Retrieved info for key '" + String(key) + "': " + field_data_str;

        // 4. Remove task
        task_queue.pop_front();
        
        // 5. Start NEXT AI cycle with data context
        _start_ai_cycle(success_trigger.c_str());
    }
}

void EmilyBrain::handleProcessingAiState() {
    // This function simulates waiting for the AI response non-blockingly.

    unsigned long current_time = millis();

    // Check if 2 seconds have passed since the simulation started
    if (current_time - ai_simulation_start_time >= 2000) {
        Serial.println("Simulating AI response: applying homeostasis...");

        // Apply simulated homeostasis
        arousal = 0.0;
        valence = 0.5; // Simulate positive outcome
        // current_arousal_context = nullptr; // Clear context now that it's resolved

        // Transition back to IDLE
        setState(EmilyState::IDLE);
        Serial.println("Homeostasis applied (simulation). Back to Idle.");
    }
    // While waiting, the main loop() continues to run, checking buttons and network.
}
void EmilyBrain::handleGeneratingSpeechState() {
    // This state is currently very short because downloadTtsToSd is blocking.
    // If we make download asynchronous later, this handler will check completion.
    // For now, it might not even be called if download is fast.
    // We *could* add a timeout check here.
    if (tts_task_start_time != 0 && (millis() - tts_task_start_time > TTS_GENERATION_TIMEOUT_MS)) {
        Serial.println("!!! TIMEOUT during TTS Generation/Download!");
         if (!active_tool_call_id.isEmpty()) {
             logInteractionToSd_Error("tool", active_tool_call_id, active_tool_call_name, "ERROR: Timeout generating speech audio.");
         }
         setState(EmilyState::IDLE); // Go back to IDLE on failure
         tts_task_start_time = 0; // Reset timer
    }
}

void EmilyBrain::handleSpeakingState() {
    // This state is entered AFTER TTS audio is successfully downloaded.
    // Play the audio file NOW.
    Serial.println("Handler: Entering SPEAKING state. Starting playback...");
    playWavFromSd("/tts_output.wav"); // Play the standard output file

    Serial.println("Handler: Playback finished.");
    tts_task_start_time = 0; // Reset timer after successful playback

    // --- Task Completion ---
    // Remove the CB_SPEAK task from the queue
    if (!task_queue.empty() && task_queue.front().type == "CB_SPEAK") {
        task_queue.pop_front();
    } else {
        Serial.println("Handler Warning: SPEAKING finished, but queue was empty or front task wasn't CB_SPEAK?");
    }

    // --- Transition to Next State ---
    // Use the state we decided on in the executor
    Serial.printf("Handler: Transitioning to next state: %s\n", stateToString(next_state_after_audio));
    EmilyState intendedNextState = next_state_after_audio; // Remember the plan
    _continue_task(); // Process next task (might set state to IDLE if queue becomes empty)

    // If the plan was to listen, AND _continue_task didn't start another waiting task
    // (i.e., state is now IDLE because queue is empty), set state correctly and start listening.
     if (intendedNextState == EmilyState::AWAITING_SPEECH && currentState == EmilyState::IDLE) {
         Serial.println("Handler: Overriding IDLE, setting AWAITING_SPEECH and starting listening.");
         setState(EmilyState::AWAITING_SPEECH);
         listenAndTranscribe();
     }
}

void EmilyBrain::handleExecutingPhysicalToolState() { 
    // Not used anymore (OS removed). 
    // If we somehow end up here, return to IDLE immediately.
    setState(EmilyState::IDLE);
}


void EmilyBrain::handleSeeingState() {
    // Check 1: Have we received the vision result?
    if (!last_vision_response.isNull()) {
        Serial.println("Handler: Vision result received.");
        // Process the result - For now, just log it and trigger AI cycle
        String description = last_vision_response["description"] | "Vision error.";
        last_display_text = description;
        Serial.printf("Vision Result: %s\n", description.c_str());

        // Log result back to AI
         if (!active_tool_call_id.isEmpty()) {
             logInteractionToSd_Error("tool", active_tool_call_id, active_tool_call_name, description);
             // Don't clear active_tool_call IDs here, AI cycle needs them
         }

        // Clear the postbus
        last_vision_response.clear();
        camcanvas_task_start_time = 0; // Reset timer

        // Remove the CAM_ANALYZE task from queue (it should be the front one)
        if (!task_queue.empty() && task_queue.front().type == "CAM_ANALYZE") {
            task_queue.pop_front();
        } else { /* Log warning */ }

        // Start next AI cycle with the description
        _start_ai_cycle(("My visual analysis returned: " + description).c_str());
        return; // AI cycle started, exit handler
    }

    // Check 2: Timeout
    if (camcanvas_task_start_time != 0 && (millis() - camcanvas_task_start_time > CAM_TASK_TIMEOUT_MS)) {
        Serial.println("!!! Handler TIMEOUT: Waiting for CAM vision result!");
        // Log error, clear postbus, reset timer, remove task, trigger AI cycle with error
        if (!active_tool_call_id.isEmpty()) { logInteractionToSd_Error("tool", active_tool_call_id, active_tool_call_name, "ERROR: Timeout waiting for vision analysis."); }
        last_vision_response.clear();
        camcanvas_task_start_time = 0;
        if (!task_queue.empty() && task_queue.front().type == "CAM_ANALYZE") { task_queue.pop_front(); }
        _start_ai_cycle("I requested visual analysis but received no response in time.");
        return; // AI cycle started, exit handler
    }
    // Still waiting...
}

void EmilyBrain::handleVisualizingState() {
     // Check 1: Have we received the image complete confirmation?
    if (!last_camcanvas_confirmation.isNull()) {
        Serial.println("Handler: Image generation complete confirmation received.");
        // Confirmation received, the task is done.

        // Clear postbus, reset timer
        last_camcanvas_confirmation.clear();
        camcanvas_task_start_time = 0;

        // Remove the CANVAS_IMAGE_SYNC task from queue
         if (!task_queue.empty() && task_queue.front().type == "CANVAS_IMAGE_SYNC") {
            task_queue.pop_front();
        } else { /* Log warning */ }

        // Continue to the next task
        _continue_task();
        return; // Next task started (or went IDLE), exit handler
    }

    // Check 2: Timeout
    if (camcanvas_task_start_time != 0 && (millis() - camcanvas_task_start_time > CANVAS_TASK_TIMEOUT_MS)) {
        Serial.println("!!! Handler TIMEOUT: Waiting for CANVAS image completion!");
        // Log error, clear postbus, reset timer, remove task, trigger AI cycle with error? Or just continue?
        // Let's log error and continue the task queue for now.
         if (!active_tool_call_id.isEmpty()) { logInteractionToSd_Error("tool", active_tool_call_id, active_tool_call_name, "ERROR: Timeout waiting for image generation."); }
        last_camcanvas_confirmation.clear();
        camcanvas_task_start_time = 0;
        if (!task_queue.empty() && task_queue.front().type == "CANVAS_IMAGE_SYNC") { task_queue.pop_front(); }
        _continue_task(); // Try to continue with the next task
        return; // Exit handler
    }
    // Still waiting...
}

void EmilyBrain::handlePlayingSoundState() {
    // Serial.println("Handler: Entering PLAYING_SOUND state."); // Optional Debug

    // Check if the task queue is valid and the front task is a sound task
    if (!task_queue.empty() &&
        (task_queue.front().type == "CB_SOUND" || task_queue.front().type == "CB_SOUND_EMOTION"))
    {
        Task& sound_task = task_queue.front(); 
        const char* path = nullptr;

        // --- ARGUMENT EXTRACTION ---
        // 1. Check 'sound_path' (Standard for play_sound_effect tool)
        if (sound_task.args.containsKey("sound_path")) {
            path = sound_task.args["sound_path"];
        }
        // 2. Check 'sound_effect' (Standard for update_emotional_state tool)
        else if (sound_task.args.containsKey("sound_effect")) {
            path = sound_task.args["sound_effect"];
        }
        
        // --- EXECUTION ---
        if (path != nullptr) {
            Serial.printf("Handler: Playing sound '%s'...\n", path);
            delay(100); // Safety delay for SD/Audio stability
            playWavFromSd(path); // This function plays the file (blocking)
            Serial.println("Handler: Sound finished.");
        } else {
            Serial.println("Handler Warning: PLAYING_SOUND state entered but no valid path found.");
        }

        // --- COMPLETION ---
        task_queue.pop_front(); // Remove the finished task
        _continue_task();       // Move to next task immediately
        return;                 // Exit, state has changed

    } else {
        // Error recovery
        Serial.println("Handler Error: Entered PLAYING_SOUND state unexpectedly or queue empty.");
        setState(EmilyState::IDLE); 
    }
}

void EmilyBrain::handleAwaitingInputState() {
    // Check 1: Did we receive input?
    if (!last_inputpad_response.isNull()) {
        Serial.println("Handler: InputPad result received.");
        String value = last_inputpad_response["value"] | "ERROR";
        
        // Log the result back to the AI history
        if (!active_tool_call_id.isEmpty()) {
             logInteractionToSd_Error("tool", active_tool_call_id, active_tool_call_name, value);
        }

        last_inputpad_response.clear(); // Clear the mailbox
        input_task_start_time = 0;      // Reset timer

        // Remove the INPUTPAD_SET_MODE task from the queue
        if (!task_queue.empty()) task_queue.pop_front();

        // Start the AI cycle with the received value
        _start_ai_cycle(("USER_INPUT: Received '" + value + "' from InputPad.").c_str());
        return; // AI cycle started
    }

    // Check 2: Timeout
    if (input_task_start_time != 0 && (millis() - input_task_start_time > INPUT_TASK_TIMEOUT_MS)) {
        Serial.println("!!! Handler TIMEOUT: Waiting for InputPad!");
        if (!active_tool_call_id.isEmpty()) { 
            logInteractionToSd_Error("tool", active_tool_call_id, active_tool_call_name, "ERROR: Timeout waiting for user input."); 
        }
        
        last_inputpad_response.clear();
        input_task_start_time = 0;
        if (!task_queue.empty()) task_queue.pop_front();

        _start_ai_cycle("I requested user input via the InputPad, but received no response in time.");
        return;
    }
}


void EmilyBrain::loop() {
    // Serial.printf(">>> DEBUG: Top of loop. Current State = %s\n", stateToString(currentState)); // Optional Debug

    // --- If NO WiFi, run AP Server ---
    if (WiFi.status() != WL_CONNECTED) {
        ptms_server.handleClient();     // Handle AP web page
        dnsServer.processNextRequest(); // Handle Captive Portal
        delay(10);                      // Give AP tasks breathing room
        return;                         // Stop the rest of the loop
    }
    
    // --- Handle Web Server ---
    ptms_server.handleClient(); // Check for incoming web requests FIRST

    // --- Check for INTERRUPT first ---
    if (currentState != EmilyState::IDLE && checkWakeButton()) {
        Serial.println("INTERRUPT received via button press!");
        // Force back to IDLE
        arousal = 0.0;
        valence = 0.0;
        // TODO: Clear task queue and stop active processes (like STT) later
        setState(EmilyState::IDLE);
        
        delay(100); // Small delay to prevent immediate re-trigger
        return; // Skip the rest of this loop cycle
    }

    // --- Update Emotional State ---
    // decayArousal(); // Let arousal decrease over time (Currently disabled)

    // --- Handle Network ---
    sendPings();         // Send periodic pings
    handleUdpPackets();  // Check for incoming data
    checkTimeouts();     // Check if modules have gone offline

    // --- State Machine Dispatcher ---
    switch (currentState) {
        case EmilyState::IDLE:
            handleIdleState();
            break;
        case EmilyState::AWAITING_SPEECH:
            handleAwaitingSpeechState();
            break;
        case EmilyState::RECORDING_SPEECH:
            handleRecordingSpeechState();
            break;
        case EmilyState::PROCESSING_STT:
            handleProcessingSttState();
            break;
        case EmilyState::PROCESSING_AI:
            handleProcessingAiState();
            break;
        case EmilyState::GENERATING_SPEECH:
            handleGeneratingSpeechState();
            break;
        case EmilyState::SPEAKING:
            handleSpeakingState();
            break;
        case EmilyState::EXECUTING_PHYSICAL_TOOL:
            handleExecutingPhysicalToolState();
            break;
        case EmilyState::SEEING:
            handleSeeingState();
            break;
        case EmilyState::VISUALIZING:
            handleVisualizingState();
            break;
        case EmilyState::PLAYING_SOUND:
            handlePlayingSoundState();
            break;
        case EmilyState::AWAITING_INPUT:
            handleAwaitingInputState();
            break;
        case EmilyState::PROCESSING_GAMEDATA: 
            handleProcessGamedataState();
            break;
    }

    // --- Update Display ---
    renderDisplay();

    delay(30);
}


// --- setupWiFi() ---
// Determines mode (STA/AP) and initializes display.
void EmilyBrain::setupWiFi() {
    preferences.begin("emily-wifi", false);
    String stored_ssid = preferences.getString("ssid", "");
    String stored_pass = preferences.getString("pass", "");
    preferences.end();

    // --- TRY CONNECTING FIRST ---
    if (stored_ssid.length() > 0) {
        Serial.println("Stored credentials found. Attempting to connect...");
        
        // --- DISPLAY INIT (for STA mode) ---
        display.init();
        display.setRotation(1);
        display.fillScreen(TFT_BLACK);
        display.setCursor(10, 10);
        display.setTextSize(2);
        display.println("Connecting...");
        // --- END DISPLAY INIT ---

        if (connectToWiFi(stored_ssid, stored_pass)) {
            Serial.println("Connection successful with stored credentials.");
            display.fillScreen(TFT_BLACK);
            display.setCursor(10, 10);
            display.println("Connected!");
            display.setTextSize(1);
            display.println(WiFi.localIP().toString());
            delay(1000);
            
            // --- STATE FIX ---
            wifi_status = WiFiStatus::CONNECTED; 
            // --- END FIX ---
            
            // Start normal services
            udp.begin(UDP_LISTEN_PORT);
            setupWebServer(); // Calls ptms_server.begin()
            Serial.println("PTMS server started.");
            return; // Success!
        }
        Serial.println("Connection failed with stored credentials.");
        display.fillScreen(TFT_RED);
        display.setCursor(10, 10);
        display.println("Config Error.");
        delay(1000);
    } else {
        Serial.println("No stored credentials found.");
    }
    
    // --- FAILED OR NO CREDS: START AP MODE ---
    startAPMode();
}

bool EmilyBrain::connectToWiFi(String ssid, String pass) {
    WiFi.mode(WIFI_STA);
    
    // --- STATIC IP CONFIG ---
    IPAddress staticIP(192, 168, 68, 201);
    IPAddress gateway(192, 168, 68, 1);
    IPAddress subnet(255, 255, 255, 0);
    IPAddress primaryDNS(8, 8, 8, 8);
    
    if (!WiFi.config(staticIP, gateway, subnet, primaryDNS)) {
        Serial.println("Static IP configuration failed!");
    }
    // --- End Static IP ---

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
    
    // --- STEP 1: START WI-FI AP FIRST ---
    WiFi.softAP("Emily_Setup", "12345678");
    IPAddress AP_IP(192, 168, 4, 1);
    dnsServer.start(53, "*", AP_IP);
    Serial.println("WiFi AP Mode active.");
    delay(500); 

    // --- STEP 2: INITIALIZE DISPLAY ---
    Serial.println("Initializing display for AP instructions...");
    display.init();
    display.setRotation(1);
    display.fillScreen(TFT_BLACK);
    
    // Show AP instructions
    display.setCursor(10, 10);
    display.setTextSize(2);
    display.println("SETUP MODE");
    display.setTextSize(1);
    display.println("\nConnect phone to WiFi:");
    display.setTextColor(TFT_CYAN, TFT_BLACK);
    display.println("Emily_Setup");
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.println("\nPassword: 12345678");
    display.println("\nGo to 192.168.4.1 in browser.");

    // Reconfigure server for AP mode
    ptms_server.onNotFound([this](){ this->handleRoot(); });
    ptms_server.on("/", HTTP_GET, [this](){ this->handleRoot(); });
    ptms_server.on("/save", HTTP_POST, [this](){ this->handleSave(); });
    ptms_server.begin(); 
    Serial.println("AP Web Server started at 192.168.4.1");
}

void EmilyBrain::handleRoot() {
    // HTML for setup page
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
        String html = "<html><body style='font-family: sans-serif; background: #222; color: #EEE; text-align: center;'><h2>Success!</h2><p>Credentials saved. Emily is rebooting and connecting to '" + ssid + "'.</p><p>Reconnect your phone to normal WiFi.</p></body></html>";
        ptms_server.send(200, "text/html", html);
        
        delay(2000);
        ESP.restart();
    } else {
        ptms_server.send(400, "text/plain", "SSID or password missing.");
    }
}

// --- Serve Remote Control Page ---
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
            
            /* Button styles */
            .button_bar { display: flex; justify-content: space-around; padding: 10px; }
            .button { padding: 10px 15px; text-decoration: none; background: #555; color: #FFF; border-radius: 5px; font-size: 0.9em; }
            .button.red { background: #900; }
        </style>
        </head>
        <body>
            <h2>Emily Web Remote</h2>
            
            <form action='/send' method='POST'>
                <input type='text' name='user_text' placeholder='Type message...' autofocus>
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

// --- Process Remote Input ---
void EmilyBrain::handleRemoteInput() {
    Serial.println(">>> Web Remote: Input received via POST...");

    // Check IDLE state
    if (currentState != EmilyState::IDLE) {
        ptms_server.send(429, "text/plain", "Emily is busy. Try again shortly.");
        return; 
    }

    if (ptms_server.hasArg("user_text")) {
        String text = ptms_server.arg("user_text");
        Serial.printf(">>> Web Remote: Text = %s\n", text.c_str());

        if (text.length() > 0) {
            // --- TRIGGER ---
            String trigger_string = "USER_INPUT (Web): " + text;
            _start_ai_cycle(trigger_string.c_str());
            // --- END TRIGGER ---
            
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

// --- FORCE IDLE Handler ---
void EmilyBrain::handleForceIdle() {
    Serial.println(">>> Web Remote: FORCE IDLE received.");
    
    // Interrupt logic
    arousal = 0.0;
    valence = 0.0;
    current_arousal_context = nullptr;
    task_queue.clear(); 
    
    setState(EmilyState::IDLE);
    ptms_server.sendHeader("Location", "/remote");
    ptms_server.send(302, "text/plain", "Forced to IDLE.");
}

// --- CENTER HEAD Handler ---
void EmilyBrain::handleCenterHead() {
    Serial.println(">>> Web Remote: CENTER HEAD received.");
    
    // Build command
    StaticJsonDocument<128> cmd_doc;
    cmd_doc["command"] = "move_head";
    cmd_doc["pan"] = 90;
    cmd_doc["tilt"] = 90;
    String command_string;
    serializeJson(cmd_doc, command_string);

    // Send direct
    udp.beginPacket(CAMCANVAS_IP_ADDRESS, CAMCANVAS_UDP_PORT);
    udp.print(command_string);
    udp.endPacket();

    ptms_server.sendHeader("Location", "/remote");
    ptms_server.send(302, "text/plain", "Center command sent.");
}

// --- REBOOT Handler ---
void EmilyBrain::handleForceReboot() {
    Serial.println(">>> Web Remote: FORCE REBOOT received. Rebooting...");
    
    String html = "<html><body style='font-family: sans-serif; background: #222; color: #EEE; text-align: center;'><h2>EmilyBrain Rebooting...</h2><p>Page reloads in 5 seconds.</p><script>setTimeout(() => { window.location.href = '/remote'; }, 5000);</script></body></html>";
    ptms_server.send(200, "text/html", html);
    
    delay(1000); 
    ESP.restart(); 
}
