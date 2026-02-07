// =======================================================
// ==   Setup_IdeaSpark_19.h                            ==
// ==   Hardware: IdeaSpark ESP32 + 1.9" ST7789         ==
// =======================================================

#define USER_SETUP_ID 994 

#define ST7789_DRIVER

// --- Screen Dimensions (1.9 inch) ---
#define TFT_WIDTH  170
#define TFT_HEIGHT 320

// --- Pins ---
#define TFT_SCLK 18
#define TFT_MOSI 23
#define TFT_CS   15
#define TFT_DC   2
#define TFT_RST  4
#define TFT_BL   32

// --- Configuration ---
#define TFT_BACKLIGHT_ON HIGH 

// --- Fonts ---
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT7 // Digital 7-segment font (Crucial for Dice Mode)
#define LOAD_GFXFF
#define SMOOTH_FONT

// --- Font References ---
// Ensure these fonts are available in your SPIFFS/LittleFS if loading from there,
// otherwise TFT_eSPI uses its internal arrays for standard fonts.
#define FONT_FSB9  &FreeSansBold9pt7b
#define FONT_FSB12 &FreeSansBold12pt7b
#define FONT_FSB24 &FreeSansBold24pt7b

#define SPI_FREQUENCY  40000000