// config.h
#pragma once
#include <cstdint>

struct AppConfig {
    char linuxIP[16];
    uint16_t kbmSendPort;
    uint16_t kbmRecvPort;
    uint16_t audioPort;
    uint16_t displayPort;
    int32_t linuxWidth;
    int32_t linuxHeight;
};

extern const AppConfig* g_config;

void InitConfig();