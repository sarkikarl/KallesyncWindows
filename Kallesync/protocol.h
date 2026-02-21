#pragma once
#include <cstdint>

#pragma pack(push, 1)

//=============================================================================
// DATA STRUCTS - used in SwapBuffers AND sent over wire
//=============================================================================

struct KbmData {
    int32_t mouseX, mouseY;
    int16_t wheelDelta;
    bool keys[140];
    bool buttons[5];
};

struct AudioData {
    bool isPlaying;
    uint32_t underruns;
    uint32_t latencyMs;
};

struct CaptureData {
    uint32_t fps;
    uint32_t droppedFrames;
    bool encoding;
};

struct LinuxData {
    uint32_t compQuality;
    bool audioOn;
    bool captureOn;
    bool kbmOn;
};

struct CursorInfo {
    uint32_t serial;
    int32_t width, height;
    int32_t xhot, yhot;
    uint32_t pixels[24 * 24];
};

//=============================================================================
// WIRE PACKETS
//=============================================================================

struct ToLinux {
    KbmData kbm;
    AudioData audio;
    CaptureData capture;
};

struct FromLinux {
    LinuxData config;
    //CursorInfo cursor;
};

#pragma pack(pop)