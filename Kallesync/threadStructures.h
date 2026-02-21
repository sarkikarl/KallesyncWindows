#pragma once
#include <atomic>
#include <cstdint>

//=============================================================================
// SWAP BUFFER - internal, not sent over wire
//=============================================================================

template<typename T>
struct SwapBuffer {
    T buf[2];
    std::atomic<int> idx{ 0 };

    T* back() { return &buf[1 - idx.load()]; }
    void swap() { idx.store(1 - idx.load()); }
    const T* front() { return &buf[idx.load()]; }
};

//=============================================================================
// Include wire formats
//=============================================================================

#include "protocol.h"

//=============================================================================
// GLOBALS
//=============================================================================

extern SwapBuffer<KbmData> g_kbm;
extern SwapBuffer<AudioData> g_audio;
extern SwapBuffer<CaptureData> g_capture;
extern SwapBuffer<LinuxData> g_linux;

void InitThreadStructures();