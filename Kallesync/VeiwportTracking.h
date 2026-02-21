#pragma once


#include <atomic>
#include <cstdint>
#include <windows.h>
#include <iostream>


struct ViewportBounds {
    int32_t x;       // Virtual screen X of client area top-left
    int32_t y;       // Virtual screen Y of client area top-left
    int32_t width;   // Client area width
    int32_t height;  // Client area height
};

struct alignas(8) DPIInfo {
    double scalingFactor{ 1.0 };
};

DPIInfo getDpi();

// Start the tracking thread. Call after viewport is initialized.
void StartViewportTracker(HWND hwnd);

// Stop the tracking thread. Call before shutdown.
void StopViewportTracker();

// Get current viewport bounds. Lock-free read.
ViewportBounds GetViewportBounds();



bool IsViewportActive();