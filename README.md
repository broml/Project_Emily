# Emily OS v1.0 🤖✨
> A Sentient, Physical AI Companion based on ESP32-S3.

**Emily** is not just a chatbot. She is a physical entity with eyes, ears, a voice, and a personality. She strives for *homeostasis*—balancing her internal emotional state through interaction with the real world.

![PHOTO: Hero shot of Emily looking at the camera]

## 🏗 System Architecture
Emily consists of three independent modules communicating over a local UDP network:

1.  **🧠 The Brain (EmilyBrain):** The central reasoning core. Handles Speech-to-Text (STT), Text-to-Speech (TTS), and the emotional model.
2.  **👁️ The Eyes (CamCanvas):** A vision module that can look around (Pan/Tilt), analyze images, and display visual feedback.
3.  **🎮 The Hands (InputPad):** *Optional.* A wireless controller for physical inputs (Dice rolling, Yes/No choices) to play tabletop games.

## 🚀 Features
* **True Autonomy:** Emily decides when to sleep, when to wake up, and when to look around based on her internal Arousal/Valence levels.
* **Vision & Voice:** She sees the world through an OV2640 camera and speaks via high-quality TTS generation.
* **Modular Personality:** Change her entire character by swapping the `adventure.json` CMS file (e.g., from "Helpful Assistant" to "Dungeon Master").
* **Wireless Body:** The head and body are physically separate (connected only by power and a servo signal line), communicating wirelessly for a clean build.

## 💰 Prerequisites & API Costs
To run Emily, you need API keys. We prioritized privacy and cost-efficiency:

1.  **Intelligence & Vision (Venice.ai):**
    * Emily uses **Venice.ai** for LLM inference and Image Generation.
    * *Cost:* Paid per token (DIEM) or via subscription (VVV).
    * *Note:* Venice respects privacy and offers uncensored models.
2.  **Hearing (OpenAI):**
    * Emily uses **OpenAI Whisper** for Speech-to-Text.
    * *Cost:* Extremely low. A $5 credit typically lasts for months of personal use.

## 📂 Repository Structure
* `/Firmware`: The Arduino code for the 3 modules.
* `/Drivers`: **Crucial!** Pre-configured setup files for the TFT screens.
* `/SD_Card_Template`: The required file structure for the SD card (System prompts, JSON configs).
* `/Tools`: The Python-based **Emily Manager** to manage save slots and files without removing the SD card.
* `HARDWARE.md`: Full Bill of Materials and wiring guide.

## 🛠 Quick Start Guide
1.  **Build the Hardware:** Follow the guide in `HARDWARE.md`.
2.  **Prepare the Drivers:**
    * Copy the relevant `.h` file from `/Drivers` to your Arduino `TFT_eSPI/User_Setups/` folder.
    * Select it in `User_Setup_Select.h`.
3.  **Flash the Firmware:** Upload the code to the Brain, CamCanvas, and (optionally) InputPad.
4.  **Prepare the SD Card:**
    * Format a MicroSD card to FAT32.
    * Copy contents of `/SD_Card_Template` to the **ROOT** of the card.
    * Add your WiFi and API keys in the code (or `secrets.h`).
5.  **Wake Her Up:** Press the wake button to start Emily's interaction with the world.

---
*Created with ❤️ by the Emily Project Team.*
