#pragma once
#include <stdint.h>
// Heltec Vision Master E213 (ESP32-S3R8, SX1262, e-ink 2,13" 250x122, dwa przyciski).
// Piny wg definicji wariantu Meshtastic "heltec_vision_master_e213" (variant.h).
constexpr int PIN_BTN_A=0;      // PRG / BOOT (przytrzymany przy resecie wchodzi w tryb wgrywania)
constexpr int PIN_BTN_B=21;     // drugi przycisk na plytce (KEY)
constexpr int PIN_VEXT=18;      // zasilanie e-ink i zlacza QuickLink (VEXT_ENABLE), HIGH = wlaczone
constexpr int PIN_EINK_CS=5, PIN_EINK_BUSY=1, PIN_EINK_DC=2, PIN_EINK_RST=3, PIN_EINK_SCK=4, PIN_EINK_MOSI=6;
constexpr int PIN_BAT_ADC=7, PIN_BAT_CTRL=46;   // pomiar baterii (ADC1_CH6), PIN_BAT_CTRL HIGH = dzielnik wlaczony
constexpr float BAT_MULT=4.9f*1.03f;             // mnoznik dzielnika wg Meshtastic (ADC 2,5 dB)
constexpr int PIN_LORA_CS=8, PIN_LORA_SCK=9, PIN_LORA_MOSI=10, PIN_LORA_MISO=11, PIN_LORA_RST=12, PIN_LORA_BUSY=13, PIN_LORA_DIO1=14;
constexpr float LORA_TCXO_V=1.8f;                // TCXO zasilany z DIO3
constexpr int SCR_W=250, SCR_H=122;   // e-ink w orientacji poziomej

// ---- Skan pasma: wlasne slowo synchronizacji (Meshtastic 0x2B nie jest tu potrzebne) ----
constexpr uint8_t SCAN_SYNC=0x12;
// ---- Podsluch Meshtastic ----
constexpr uint8_t MESH_SYNC=0x2B;
constexpr uint16_t MESH_PREAMBLE=16;
constexpr unsigned MESH_MAX_NODES=40;
// ---- Pomiar szumu ----
constexpr uint32_t NOISE_PERIOD_MS=100;          // chwilowy RSSI 10 razy na sekunde
constexpr int NOISE_MIN=-130, NOISE_MAX=-40;     // zakres wykresow [dBm]
// ---- Skan pasma ----
constexpr float SCAN_START=863.0f, SCAN_STEP=0.1f;
constexpr unsigned SCAN_N=71;                    // 863.0 ... 870.0 MHz
