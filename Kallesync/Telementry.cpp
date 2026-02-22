#include "Telementry.h"
#include "config.h"
#include "threadStructures.h"  // includes protocol.h
#include "udpClient.h"
#include "udpServer.h"
#include <cstring>
#include <chrono>
#include <thread>

void Telemain() {
    udpServer server = udpServerStart("any", g_config->kbmRecvPort, 1024);
    udpClient client = udpClientStart(g_config->linuxIP, g_config->kbmSendPort);

    ToLinux outgoing;
    FromLinux incoming;

    while (true) {
        // GATHER
        std::this_thread::sleep_for(std::chrono::milliseconds(14));
        memcpy(&outgoing.kbm, g_kbm.front(), sizeof(KbmData));
        memcpy(&outgoing.audio, g_audio.front(), sizeof(AudioData));
        memcpy(&outgoing.capture, g_capture.front(), sizeof(CaptureData));
        

        // SEND
        udpSend(reinterpret_cast<const char*>(&outgoing), sizeof(outgoing), client);

        // RECEIVE
        while (!udpServerGet(server)) {}
        memcpy(&incoming, server.buf, sizeof(FromLinux));

        // BROADCAST
        LinuxData* cfg = g_linux.back();
        *cfg = incoming.config;
        g_linux.swap();
    }
}