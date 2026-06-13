#pragma once

// Pin map for the Waveshare ESP32-S3-Touch-AMOLED-1.8
// Taken verbatim from Waveshare's official examples (Mylibrary/pin_config.h).

#define XPOWERS_CHIP_AXP2101

// --- AMOLED (SH8601) over QSPI ---
#define LCD_SDIO0 4
#define LCD_SDIO1 5
#define LCD_SDIO2 6
#define LCD_SDIO3 7
#define LCD_SCLK 11
#define LCD_CS 12
#define LCD_WIDTH 368
#define LCD_HEIGHT 448

// --- Touch (FT3168) + onboard I2C bus (also AXP2101, QMI8658, PCF85063) ---
#define IIC_SDA 15
#define IIC_SCL 14
#define TP_INT 21

// --- Audio codec (ES8311) over I2S ---
#define I2S_MCK_IO 16
#define I2S_BCK_IO 9
#define I2S_DI_IO 10
#define I2S_WS_IO 45
#define I2S_DO_IO 8

#define MCLKPIN 16
#define BCLKPIN 9
#define WSPIN 45
#define DOPIN 10
#define DIPIN 8
#define PA 46

// --- microSD (SDMMC 1-bit) ---
const int SDMMC_CLK = 2;
const int SDMMC_CMD = 1;
const int SDMMC_DATA = 3;
