#ifndef SX1280_JAMMER_H
#define SX1280_JAMMER_H
#include <Arduino.h>
enum SX1280JamMode { JAM_CW=0, JAM_DATA_FLOOD=1, JAM_CHANNEL_HOP=2 };
void sx1280_jammer_menu();
void sx1280_jammer_run(int mode, float startFreq, float endFreq);
#endif
