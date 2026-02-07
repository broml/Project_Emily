// =======================================================
// ==   Setup_EmilyBrain.h                              ==
// ==   Hardware: Freenove ESP32-S3 + ILI9341           ==
// =======================================================

#define USER_SETUP_ID 998 

#define ILI9341_DRIVER

// --- Screen Dimensions ---
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// --- Pins (Freenove Breakout Mapping) ---
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_CS   10
#define TFT_DC   8
#define TFT_RST  9
#define TFT_BL   -1 // Backlight hardwired or not controlled

// --- Configuration ---
#define TFT_INVERSION_OFF 
#define USE_HSPI_PORT // Use HSPI (Hardware SPI)

// --- Fonts ---
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_GFXFF
#define SMOOTH_FONT

#define SPI_FREQUENCY  40000000