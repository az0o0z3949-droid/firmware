#include "sx1280_jammer.h"
#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/utils.h"
#include <RadioLib.h>

#define SX1280_CS   5
#define SX1280_DIO1 9
#define SX1280_RST  8
#define SX1280_BUSY 7

static SX1280* sxRadio = nullptr;
static bool    sxReady = false;

static void showStatus(const char* line1, const char* line2, uint16_t color) {
    tft.fillRect(0, 100, tft.width(), 60, bruceConfig.bgColor);
    tft.setTextColor(color, bruceConfig.bgColor);
    tft.setTextSize(FP);
    tft.setCursor(10, 110);
    tft.println(line1);
    tft.setCursor(10, 130);
    tft.println(line2);
}

static bool sx1280_init() {
    if (sxReady && sxRadio != nullptr) return true;

    sxRadio = new SX1280(new Module(SX1280_CS, SX1280_DIO1, SX1280_RST, SX1280_BUSY));
    if (sxRadio == nullptr) {
        showStatus("Radio object failed", "", TFT_RED);
        return false;
    }

    int state = sxRadio->begin(2400.0, 812.5, 9, 7, 0x12);

    if (state != RADIOLIB_ERR_NONE) {
        char msg[40];
        snprintf(msg, sizeof(msg), "LoRa begin err: %d", state);
        showStatus("SX1280 Init Failed", msg, TFT_RED);
        delay(1500);
        delete sxRadio;
        sxRadio = nullptr;
        return false;
    }

    state = sxRadio->setOutputPower(2);
    if (state != RADIOLIB_ERR_NONE) {
        showStatus("Power set failed", "", TFT_ORANGE);
        delay(1000);
    }

    sxRadio->setFrequency(2437.0);
    sxReady = true;
    return true;
}

static void jam_data_flood(float startF, float endF, float step) {
    uint8_t payload[32];
    for (int i = 0; i < 32; i++) payload[i] = (uint8_t)random(0, 256);

    float f = startF;
    uint32_t lastUI = 0;

    while (!check(SelPress)) {
        sxRadio->setFrequency(f);
        sxRadio->transmit(payload, sizeof(payload));

        f += step;
        if (f > endF) f = startF;

        if (millis() - lastUI > 500) {
            char msg[32];
            snprintf(msg, sizeof(msg), "Freq: %.1f MHz", f);
            showStatus("Data Flood", msg, TFT_GREEN);
            lastUI = millis();
        }
    }
}

static void jam_channel_hop(float startF, float endF) {
    uint32_t lastUI = 0;

    while (!check(SelPress)) {
        float f = startF + (random(0, (int)((endF - startF) * 10)) / 10.0);
        sxRadio->setFrequency(f);
        sxRadio->transmit("X");

        if (millis() - lastUI > 500) {
            char msg[32];
            snprintf(msg, sizeof(msg), "Freq: %.1f MHz", f);
            showStatus("Channel Hop", msg, TFT_CYAN);
            lastUI = millis();
        }
    }
}

static void jam_cw(float freq) {
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

    while (!check(SelPress)) {
        showStatus("CW Mode", "Press SEL to stop", TFT_YELLOW);
        delay(200);
    }

    sxRadio->standby();
    delay(50);
}

void sx1280_jammer_run(int mode, float startFreq, float endFreq) {
    drawMainBorder();
    tft.setTextSize(FM);
    tft.setTextColor(TFT_WHITE, bruceConfig.bgColor);
    tft.setCursor(10, 30);
    tft.println("SX1280 Initializing...");
    tft.setCursor(10, 50);
    tft.println("Please wait...");

    if (!sx1280_init()) {
        showStatus("Init FAILED", "Check pins / power", TFT_RED);
        delay(3000);
        return;
    }

    drawMainBorder();
    tft.setTextSize(FM);
    tft.setTextColor(TFT_RED, bruceConfig.bgColor);
    tft.setCursor(10, 30);
    tft.println("SX1280 JAMMER");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setCursor(10, 55);
    tft.printf("Mode: %d\n", mode);
    tft.setCursor(10, 75);
    tft.printf("Range: %.0f - %.0f MHz", startFreq, endFreq);
    tft.setCursor(10, 95);
    tft.setTextColor(TFT_WHITE, bruceConfig.bgColor);
    tft.println("Press SEL to stop");

    switch (mode) {
        case JAM_CW:
            jam_cw(startFreq);
            break;
        case JAM_DATA_FLOOD:
            jam_data_flood(startFreq, endFreq, 5.0);
            break;
        case JAM_CHANNEL_HOP:
        default:
            jam_channel_hop(startFreq, endFreq);
            break;
    }

    sxRadio->standby();

    drawMainBorder();
    tft.setCursor(10, 60);
    tft.setTextColor(TFT_GREEN, bruceConfig.bgColor);
    tft.println("Jammer Stopped");
    delay(1500);
}

void sx1280_jammer_menu() {
    std::vector<Option> modes = {
        {"Data Flood",      [=]() { sx1280_jammer_run(JAM_DATA_FLOOD,   2400, 2500); }},
        {"WiFi Channels",   [=]() { sx1280_jammer_run(JAM_CHANNEL_HOP,  2401, 2495); }},
        {"BLE Advertising", [=]() { sx1280_jammer_run(JAM_CHANNEL_HOP,  2402, 2480); }},
        {"Zigbee",          [=]() { sx1280_jammer_run(JAM_CHANNEL_HOP,  2405, 2480); }},
        {"Drone FHSS",      [=]() { sx1280_jammer_run(JAM_CHANNEL_HOP,  2400, 2483); }},
        {"RC 2.4GHz",       [=]() { sx1280_jammer_run(JAM_CHANNEL_HOP,  2405, 2475); }},
        {"Full Band Sweep", [=]() { sx1280_jammer_run(JAM_CHANNEL_HOP,  2400, 2500); }},
        {"CW Single Freq",  [=]() { sx1280_jammer_run(JAM_CW,           2437, 2437); }},
        {"Back",            [=]() { backToMenu(); }}
    };
    loopOptions(modes);
}
