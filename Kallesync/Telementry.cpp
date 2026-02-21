#include "udpClient.h"
#include "udpServer.h"
#include <windows.h>
#include "Telementry.h"
#include "threadStructures.h"
#include "swapPoint.h"
#include "kbm.h"

  // First, before anything else

ThreadSignals* g_signals = nullptr;
ThreadSignals* g_copy = nullptr;
SRWLOCK g_signalsLock = SRWLOCK_INIT;

void GetSignalsState() {

    AcquireSRWLockShared(&g_signalsLock);
    swapPtrs(g_signals, g_copy);
    ReleaseSRWLockShared(&g_signalsLock);
    return;
}



void printCursorAscii(const CursorInfo& cursor) {
    for (int y = 0; y < cursor.height; y++) {
        for (int x = 0; x < cursor.width; x++) {
            unsigned long pixel = cursor.pixels[y * cursor.width + x];
            unsigned char alpha = (pixel >> 24) & 0xFF;
            std::cout << (alpha > 32 ? '#' : ' ');
        }
        std::cout << '\n';
    }
}


void Telemain(std::string Linip, int port, int port1) {
    udpServer Server = udpServerStart("any", 55080, 1024);
    udpClient Client = udpClientStart("192.168.1.69", 55079);
    KbmState kbm = GetKbmState();
    TeleData tmp;
    while (true) {
        tmp.kbmstate = GetKbmState();
        udpSend(reinterpret_cast<const char*>(&tmp), sizeof(tmp), Client);
        while(!udpServerGet(Server)) {
            //std::cout << "waiting for lin\n";
        }
        std::memcpy(&tmp, &Server.buf, Server.bytesReceived);
        //std::cout << Server.buf << "\n";
        //std::cout << "iterations\n";
        
        
    }
    
}