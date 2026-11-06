// LilyGO T-SIM7080G-S3 board support: the pins and rails the board itself
// fixes, from LilyGO's schematic (T-SIM7080G_Schematic.pdf, sheet 2, 3/29/2024)
// and its utilities.h. Plain macros, no ESP-IDF headers, so the host tier
// compiles it as is and any project for this board can reuse it. What an
// application wires to the header pins is the application's choice, not here.
#pragma once

// SIM7080G modem on the ESP32-S3's UART 1 (the board's level shifter between).
#define TSIM7080G_S3_MODEM_RX 4       // ESP32-S3 receives (modem TXD)
#define TSIM7080G_S3_MODEM_TX 5       // ESP32-S3 sends (modem RXD)
#define TSIM7080G_S3_MODEM_BAUD 115200
#define TSIM7080G_S3_MODEM_PWRKEY 41  // low, high 1 s, low: toggles power or wakes from PSM
#define TSIM7080G_S3_MODEM_DTR 42     // held low
#define TSIM7080G_S3_MODEM_RI 3       // also header P2 pin 10

// AXP2101 PMU on I2C controller 0. The bus is not on the headers, but an
// application may tap it and share it with its own I2C parts.
#define TSIM7080G_S3_PMU_ADDR 0x34
#define TSIM7080G_S3_PMU_SDA 15
#define TSIM7080G_S3_PMU_SCL 7
#define TSIM7080G_S3_PMU_IRQ 6

// PMU rails, in millivolts.
#define TSIM7080G_S3_RAIL_MODEM_DC3_MV 3000    // modem supply
#define TSIM7080G_S3_RAIL_SHIFTER_BLDO1_MV 3300  // level shifter, never off
#define TSIM7080G_S3_RAIL_GPS_BLDO2_MV 3300    // active GPS antenna

// microSD slot, SDMMC 1-bit.
#define TSIM7080G_S3_SD_CMD 39
#define TSIM7080G_S3_SD_CLK 38
#define TSIM7080G_S3_SD_D0 40

// BOOT key, active low, a strapping pin (header P1 pin 10).
#define TSIM7080G_S3_KEY_BOOT 0

// GPIOs on the two 16-pin headers (P1, P2) that an application may use; the
// camera connector shares them, so they are free only without a camera.
// GPIO 35 – 37 also reach P1 but belong to the octal PSRAM. GPIO 45 and 46
// are strapping pins.
#define TSIM7080G_S3_HEADER_GPIOS \
    {1, 2, 8, 9, 10, 11, 12, 13, 14, 16, 17, 18, 21, 43, 44, 45, 46, 47, 48}
