# Hardware Guide: Emily OS v1.0

This guide details the hardware components required to build an Emily robot unit. The system is designed to be modular, using specific breakout boards to minimize soldering and simplify wiring.

## 1. System Overview
The system consists of three physical modules:
1.  **The Brain (Base Unit):** Houses the main processor (ESP32-S3), audio system, and base rotation servo.
2.  **CamCanvas (Head Unit):** Houses the vision system (ESP32-S3 CAM), main display, and head tilt servo.
3.  **InputPad (Remote):** A wireless, battery-powered handheld controller.

![PHOTO: Overview of the 3 units together]

---

## 2. Bill of Materials (BOM)

### Module A: The Brain (Base Unit)
*Based on the Freenove Breakout Board ecosystem.*

| Component | Description | Est. Cost |
| :--- | :--- | :--- |
| **Main Board** | Freenove Breakout Board for ESP32/ESP32-S3 (Inputs: 12V/5V/3.3V) | €25 - €30 |
| **Microcontroller** | ESP32-S3 DevKit (WROOM-1 Module, e.g., Hosyond) | €13.00 |
| **Audio Amp** | MAX98357 I2S Amplifier (3W) | €2.50 |
| **Power Reg** | Mini DC-DC Buck Converter (Fixed 5V for Amp stability) | €2.00 |
| **Speaker** | 3W 8Ohm Mini Speaker | €3.00 |
| **Microphone** | INMP441 MEMS Microphone (Omnidirectional) | €2.50 |
| **Storage** | Micro SD Card Reader Module + 1GB Micro SD Card | €4.00 |
| **Display** | 2.5" TFT SPI Display (ILI9341 Driver) | €15.00 |
| **Controls** | Push Button (Wake) + Toggle Switch (Power) | €1.00 |
| **Servo (Pan)** | 25kg High-Torque Servo (for base rotation) | €15.00 |
| **Bracket** | Metal Servo Bracket (U-Shape) for Pan mechanism | €3.50 |

### Module B: CamCanvas (Head Unit)
*Mounts on top of the Pan Servo. Uses a specialized breakout for the camera module.*

| Component | Description | Est. Cost |
| :--- | :--- | :--- |
| **Breakout** | Goouuu ESP32-S3 v1.3 Breakout Board | €8.00 |
| **Microcontroller** | ESP32-S3 CAM (WROOM N16RB, OV2640, SD Slot) | €12.00 |
| **Display** | 3.5" TFT SPI Display (ST7796 Driver, wide viewing angle) | €25.00 |
| **Servo (Tilt)** | 25kg High-Torque Servo | €15.00 |
| **Bracket** | Metal Servo Bracket for Tilt mechanism | €3.50 |

### Module C: InputPad (Wireless Remote)
*Handheld unit powered by Li-Ion batteries.*

| Component | Description | Est. Cost |
| :--- | :--- | :--- |
| **All-in-One** | IdeaSpark ESP32 with 1.9" IPS Display (ST7789) | €13.00 |
| **Controls** | 3x Push Buttons (Tactile), 1x Power Switch | €1.00 |
| **Power Module** | 18650 Li-Ion Charging Module (USB-C, 5V out) | €2.50 |
| **Battery** | 2x 18650 Li-Ion Batteries + Holder | €8.00 |

---

## 3. Power Architecture

The power distribution is simplified by using the Freenove breakout board as the central hub.

* **Main Power Source:** A single **12V DC Adapter** connects to the Freenove Breakout Board (Brain).
* **The Brain:**
    * The Freenove board distributes power to the ESP32-S3 and the Pan Servo.
    * **Crucial:** A separate **Mini DC-DC Buck Converter** takes power from the rail and converts it to a stable **5V** specifically for the MAX98357 amplifier. This prevents audio cutouts or damage when the servos draw high current.
* **CamCanvas (The Head):**
    * Receives **5V Power & GND** via a cable coming from the Freenove board (Brain). It does **not** require a separate power adapter.
    * The Goouuu breakout board handles the power for the Camera ESP32 and the Tilt Servo.
* **Communication:** There are NO data wires between the Head and the Brain. All communication is wireless (WiFi/UDP). Only power wires run up the neck.



---

## 4. Assembly Notes
* **Enclosures:** The prototypes are built using standard wooden pencil boxes (approx. 20x10x5cm), which provide ample space for the breakout boards and easy mounting for switches/screens.
* **Wiring:** Thanks to the breakout boards, most connections can be made with DuPont headers or screw terminals, minimizing soldering.

![PHOTO: Close up of the wiring inside the Brain box]
![PHOTO: Close up of the CamCanvas head mechanism]
