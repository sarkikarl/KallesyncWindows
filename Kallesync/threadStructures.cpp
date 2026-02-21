// threadStructures.cpp
#include "threadStructures.h"
#include <cstring>

SwapBuffer<KbmData> g_kbm;
SwapBuffer<AudioData> g_audio;
SwapBuffer<CaptureData> g_capture;
SwapBuffer<LinuxData> g_linux;

void InitThreadStructures() {
    memset(&g_kbm.buf[0], 0, sizeof(KbmData));
    memset(&g_kbm.buf[1], 0, sizeof(KbmData));

    memset(&g_audio.buf[0], 0, sizeof(AudioData));
    memset(&g_audio.buf[1], 0, sizeof(AudioData));

    memset(&g_capture.buf[0], 0, sizeof(CaptureData));
    memset(&g_capture.buf[1], 0, sizeof(CaptureData));

    // Defaults - threads start running
    g_linux.buf[0].compQuality = 95;
    g_linux.buf[0].audioOn = true;
    g_linux.buf[0].captureOn = true;
    g_linux.buf[0].kbmOn = true;

    g_linux.buf[1] = g_linux.buf[0];
}