#pragma once
#include "threadStructures.h"
#include <cstdint>
#include <string>
#include "kbm.h"

extern ThreadSignals* g_signals;
extern SRWLOCK g_signalsLock;

void GetSignalsState();



struct CursorInfo {
    uint32_t serial;
    int32_t width, height;
    int32_t xhot, yhot;
    uint32_t pixels[24 * 24];  // fixed 576 elements
};

inline uint32_t ipToInt(const std::string& ip) {
    uint32_t a, b, c, d;
    sscanf_s(ip.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d);
    return (a << 24) | (b << 16) | (c << 8) | d;
}

inline std::string IntToIp(uint32_t ip) {
    return std::to_string((ip >> 24) & 0xFF) + "." +
        std::to_string((ip >> 16) & 0xFF) + "." +
        std::to_string((ip >> 8) & 0xFF) + "." +
        std::to_string(ip & 0xFF);
}


struct ThreadVars {
    bool isRunning = false;
    uint16_t port;
};
struct twoway {
    uint16_t LinPort;
    uint16_t WinPort;
};

struct address {
    uint32_t LocalIP = ipToInt("192.168.1.69");
    uint32_t ClientIP = ipToInt("192.168.1.101");
};


struct controlData {
    ThreadVars Audio = { 0,0 };
    ThreadVars Capture = { 0,0 };
    twoway telementry = { 0,0 };
    twoway handshake = { 0,0 };
    uint32_t FrameCompQuality = 95;//need to be clamped from 30 - 100 << FrameCompQuality = std::clamp(FrameCompQuality, 30, 100);
};

struct kData {
    address addr;
    controlData Cdata;
    bool StartMain = true;
    CursorInfo cursor;
};
struct TeleData {
    controlData controls;
    KbmState kbmstate;
};


void Telemain(std::string ip, int port, int port1);