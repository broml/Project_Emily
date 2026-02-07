// =======================================================
// ==   Setup_Goouu_S3_CAM.h                            ==
// ==   Hardware: Goouu S3 CAM + ST7796                 ==
// =======================================================

#define USER_SETUP_ID 996 

#define ST7796_DRIVER

// --- Screen Dimensions ---
#define TFT_WIDTH  320
#define TFT_HEIGHT 480

// --- Pins (Goouu Breakout Mapping) ---
#define TFT_MOSI 45  // Pin 9 (SDI)
#define TFT_SCLK 3   // Pin 8 (SCK)
#define TFT_CS   14  // Pin 12 (CS)
#define TFT_DC   47  // Pin 10 (DC)
#define TFT_RST  21  // Pin 11 (RESET)

// --- Unused / Hardwired Pins ---
#define TFT_MISO -1  // Not used for write-only display
#define TFT_BL   -1  // Hardwired to 3V3

// --- Configuration ---
#define TFT_INVERSION_ON 
#define USE_HSPI_PORT 

// --- Fonts ---
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_GFXFF
#define SMOOTH_FONT

#define SPI_FREQUENCY  40000000