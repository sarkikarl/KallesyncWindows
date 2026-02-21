// config.cpp
#include "config.h"
#include <cstring>

static AppConfig s_config;
const AppConfig* g_config = nullptr;

void InitConfig() {
    strcpy_s(s_config.linuxIP, "192.168.1.69");
    s_config.kbmSendPort = 55079;
    s_config.kbmRecvPort = 55080;
    s_config.audioPort = 55069;
    s_config.displayPort = 55059;
    s_config.linuxWidth = 1920;
    s_config.linuxHeight = 1080;

    g_config = &s_config;
}