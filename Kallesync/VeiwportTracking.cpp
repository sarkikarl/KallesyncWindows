#include "VeiwportTracking.h"
#include <thread>
#include <chrono>
#include <iostream>

namespace {
    std::atomic<bool> g_running{ false };
    std::thread g_tracker_thread;
    HWND g_hwnd = nullptr;

    // Packed into 64-bit for atomic load/store
    struct alignas(8) PackedBounds {
        int16_t x;
        int16_t y;
        int16_t width;
        int16_t height;
    };
    static_assert(sizeof(PackedBounds) == 8, "PackedBounds must be 8 bytes");

    std::atomic<DPIInfo> g_dpiInfo;

    void UpdateDPIScaling(HWND hwnd) {
        if (hwnd) {
            unsigned int dpi = GetDpiForWindow(hwnd);
            DPIInfo newInfo;
            newInfo.scalingFactor = static_cast<double>(dpi) / 96.0;
            g_dpiInfo.store(newInfo, std::memory_order_release);
        }
    }

    std::atomic<bool> g_is_active{ false };

    std::atomic<uint64_t> g_bounds{ 0 };

    void TrackerLoop() {
        while (g_running.load(std::memory_order_relaxed)) {
            if (g_hwnd && IsWindow(g_hwnd)) {
                RECT client;
                if (GetClientRect(g_hwnd, &client)) {
                    POINT top_left = { client.left, client.top };
                    if (ClientToScreen(g_hwnd, &top_left)) {
                        PackedBounds pb;
                        pb.x = static_cast<int16_t>(top_left.x);
                        pb.y = static_cast<int16_t>(top_left.y);
                        pb.width = static_cast<int16_t>(client.right - client.left);
                        pb.height = static_cast<int16_t>(client.bottom - client.top);
                        
                        
                        std::cout << "cords(X:" << pb.x << " Y:" << pb.y << " Width:" << pb.width << " Height:" << pb.height << ")\n";
                        uint64_t packed;
                        std::memcpy(&packed, &pb, sizeof(packed));
                        g_bounds.store(packed, std::memory_order_release);
                        g_is_active.store(GetForegroundWindow() == g_hwnd, std::memory_order_release);
                        UpdateDPIScaling(g_hwnd);
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
}


void StartViewportTracker(HWND hwnd) {
    if (g_running.load(std::memory_order_relaxed)) {
        return; // Already running
    }
    g_hwnd = hwnd;
    g_running.store(true, std::memory_order_relaxed);
    g_tracker_thread = std::thread(TrackerLoop);
}

void StopViewportTracker() {
    g_running.store(false, std::memory_order_relaxed);
    if (g_tracker_thread.joinable()) {
        g_tracker_thread.join();
    }
}

ViewportBounds GetViewportBounds() {
    uint64_t packed = g_bounds.load(std::memory_order_acquire);
    PackedBounds pb;
    std::memcpy(&pb, &packed, sizeof(pb));

    ViewportBounds vb;
    vb.x = pb.x;
    vb.y = pb.y;
    vb.width = pb.width;
    vb.height = pb.height;
    return vb;
}

DPIInfo getDpi() {
    return g_dpiInfo.load(std::memory_order_acquire);
}


bool IsViewportActive() {
    return g_is_active.load(std::memory_order_acquire);
}