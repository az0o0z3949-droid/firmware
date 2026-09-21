#include "sx1280_jammer.h"
#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/utils.h"
#include <RadioLib.h>

// ============================================================
//  SX1280 2.4 GHz Jamming Module for Bruce Firmware
//  Target: LILYGO T-Watch-S3 (ESP32-S3 + SX1280)
// ============================================================

#define SX1280_CS   5
#define SX1280_DIO1 9
#define SX1280_RST  8
#define SX1280_BUSY 7

static SX1280 *sxRadio = nullptr;

// ============================================================
//  Radio init (called once, cached)
// ============================================================
static bool sx1280_ensureReady() {
    if (sxRadio != nullptr) return true;

    sxRadio = new SX1280(new Module(SX1280_CS, SX1280_DIO1, SX1280_RST, SX1280_BUSY));
    if (sxRadio == nullptr) {
        displayError("Radio alloc fail");
        return false;
    }

    int state = sxRadio->begin(2400.0, 812.5, 9, 7, 0x12);
    if (state != RADIOLIB_ERR_NONE) {
        char msg[32];
        snprintf(msg, sizeof(msg), "Begin err %d", state);
        displayError(msg, true);
        delete sxRadio;
        sxRadio = nullptr;
        return false;
    }

    // Safe power for T-Watch-S3 PA (max 3 dBm per LILYGO docs)
    sxRadio->setOutputPower(2);
    return true;
}

// ============================================================
//  Bruce-style HUD header for jammer screen
// ============================================================
static void drawJamHeader(const char *presetName, const char *modeName) {
    drawMainBorder();
    tft.setTextSize(FM);
    tft.setTextColor(TFT_RED, bruceConfig.bgColor);
    tft.setCursor(10, 28);
    tft.print("SX1280 JAMMER");

    tft.setTextSize(FP);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setCursor(10, 52);
    tft.printf("Preset : %s", presetName);
    tft.setCursor(10, 70);
    tft.printf("Mode   : %s", modeName);
    tft.setCursor(10, 88);
    tft.print("Stop   : [SEL]");
}

// ============================================================
//  Live status line (updated every 400 ms)
// ============================================================
static void drawJamStatus(float freq, uint32_t count) {
    tft.fillRect(0, 115, tft.width(), 45, bruceConfig.bgColor);
    tft.setTextSize(FP);
    tft.setTextColor(TFT_GREEN, bruceConfig.bgColor);
    tft.setCursor(10, 122);
    tft.printf("Freq  : %.1f MHz", freq);
    tft.setCursor(10, 140);
    tft.printf("TX    : %lu pkt", (unsigned long)count);
}

// ============================================================
//  Mode 1: Data Flood
// ============================================================
static void jam_data_flood(float startF, float endF, float step, const char *presetName) {
    drawJamHeader(presetName, "Data Flood");
    uint8_t payload[32];
    for (int i = 0; i < 32; i++) payload[i] = (uint8_t)random(0, 256);

    float f = startF;
    uint32_t count = 0;
    uint32_t lastUI = 0;

    while (!check(SelPress)) {
        sxRadio->setFrequency(f);
        sxRadio->transmit(payload, sizeof(payload));
        count++;

        f += step;
        if (f > endF) f = startF;

        if (millis() - lastUI > 400) {
            drawJamStatus(f, count);
            lastUI = millis();
        }
        if (check(EscPress)) break;
    }
}

// ============================================================
//  Mode 2: Channel Hopping
// ============================================================
static void jam_channel_hop(float startF, float endF, const char *presetName) {
    drawJamHeader(presetName, "Channel Hop");
    uint32_t count = 0;
    uint32_t lastUI = 0;

    while (!check(SelPress)) {
        float f = startF + (random(0, (int)((endF - startF) * 10)) / 10.0f);
        sxRadio->setFrequency(f);
        sxRadio->transmit("X");
        count++;

        if (millis() - lastUI > 400) {
            drawJamStatus(f, count);
            lastUI = millis();
        }
        if (check(EscPress)) break;
    }
}

// ============================================================
//  Mode 3: Continuous Wave
// ============================================================
static void jam_cw(float freq, const char *presetName) {
    drawJamHeader(presetName, "Continuous Wave");

    sxRadio->setFrequency(freq);
    delay(20);
    sxRadio->standby();
    delay(20);

    pinMode(SX1280_CS, OUTPUT);
    digitalWrite(SX1280_CS, HIGH);
    delayMicroseconds(5);
    digitalWrite(SX1280_CS, LOW);
    delayMicroseconds(5);
    SPI.transfer(0xD1);
    delayMicroseconds(5);
    digitalWrite(SX1280_CS, HIGH);

    uint32_t lastUI = 0;
    while (!check(SelPress)) {
        if (millis() - lastUI > 400) {
            drawJamStatus(freq, 0);
            lastUI = millis();
        }
        if (check(EscPress)) break;
        delay(50);
    }
    sxRadio->standby();
}

// ============================================================
//  Public entry point
// ============================================================
void sx1280_jammer_run(int mode, float startFreq, float endFreq, const char *presetName) {
    if (!sx1280_ensureReady()) return;

    switch (mode) {
        case JAM_DATA_FLOOD:
            jam_data_flood(startFreq, endFreq, 5.0f, presetName);
            break;
        case JAM_CW:
            jam_cw(startFreq, presetName);
            break;
        case JAM_CHANNEL_HOP:
        default:
            jam_channel_hop(startFreq, endFreq, presetName);
            break;
    }

    sxRadio->standby();

    displayInfo("Jammer stopped", true);
}

// ============================================================
//  Presets
// ============================================================
struct JamPreset {
    const char *label;
    float start;
    float end;
    int   mode;
};

static const JamPreset PRESETS[] = {
    { "WiFi 2.4G",  2401.0f, 2495.0f, JAM_CHANNEL_HOP },
    { "BLE Adv",    2402.0f, 2480.0f, JAM_CHANNEL_HOP },
    { "Zigbee",     2405.0f, 2480.0f, JAM_CHANNEL_HOP },
    { "Drone FHSS", 2400.0f, 2483.0f, JAM_CHANNEL_HOP },
    { "RC 2.4G",    2405.0f, 2475.0f, JAM_CHANNEL_HOP },
    { "Full 2.4G",  2400.0f, 2500.0f, JAM_DATA_FLOOD  },
    { "CW 2437",    2437.0f, 2437.0f, JAM_CW          },
};
static constexpr int PRESET_COUNT = sizeof(PRESETS) / sizeof(PRESETS[0]);

// ============================================================
//  Menu
// ============================================================
void sx1280_jammer_menu() {
    std::vector<Option> options;
    options.reserve(PRESET_COUNT + 1);

    for (int i = 0; i < PRESET_COUNT; i++) {
        const JamPreset &p = PRESETS[i];
        options.push_back({
            p.label,
            [=]() { sx1280_jammer_run(p.mode, p.start, p.end, p.label); }
        });
    }
    options.push_back({ "Back", [=]() { backToMenu(); } });

    loopOptions(options, MENU_TYPE_SUBMENU, "SX1280 Jammer");
}
