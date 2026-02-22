#include "kbm.h"
#include "config.h"
#include "threadStructures.h"
#include "VeiwportTracking.h"
#include <iostream>
#include <unordered_map>
#include <Windows.h>
#include <atomic>
//=============================================================================
// GLOBALS
//=============================================================================

static HHOOK g_keyboardHook = NULL;
static HHOOK g_mouseHook = NULL;
static POINT lastPosition = { 0, 0 };
static DWORD lastSwapTime = 0;
const DWORD SWAP_INTERVAL_MS = 8;//8;  // ~120Hz publish rate
static std::atomic<int32_t> g_pendingWheelDelta{ 0 };
//=============================================================================
// KEY MAPPING
//=============================================================================

static std::unordered_map<int, int> winToLinuxKeyMap = {
    // Letters (A-Z)
    {'A', 30}, {'B', 48}, {'C', 46}, {'D', 32}, {'E', 18}, {'F', 33}, {'G', 34}, {'H', 35},
    {'I', 23}, {'J', 36}, {'K', 37}, {'L', 38}, {'M', 50}, {'N', 49}, {'O', 24}, {'P', 25},
    {'Q', 16}, {'R', 19}, {'S', 31}, {'T', 20}, {'U', 22}, {'V', 47}, {'W', 17}, {'X', 45},
    {'Y', 21}, {'Z', 44},

    // Numbers (0-9)
    {'0', 11}, {'1', 2}, {'2', 3}, {'3', 4}, {'4', 5}, {'5', 6}, {'6', 7}, {'7', 8}, {'8', 9}, {'9', 10},

    // Function keys (F1-F12)
    {VK_F1, 59}, {VK_F2, 60}, {VK_F3, 61}, {VK_F4, 62}, {VK_F5, 63}, {VK_F6, 64},
    {VK_F7, 65}, {VK_F8, 66}, {VK_F9, 67}, {VK_F10, 68}, {VK_F11, 87}, {VK_F12, 88},

    // Modifier keys
    {VK_LCONTROL, 29}, {VK_RCONTROL, 97}, {VK_LSHIFT, 42}, {VK_RSHIFT, 54},
    {VK_LMENU, 56}, {VK_RMENU, 100}, {VK_LWIN, 125}, {VK_RWIN, 126},

    // Navigation keys
    {VK_UP, 103}, {VK_DOWN, 108}, {VK_LEFT, 105}, {VK_RIGHT, 106},
    {VK_HOME, 102}, {VK_END, 107}, {VK_PRIOR, 104}, {VK_NEXT, 109},

    // Special keys
    {VK_ESCAPE, 1}, {VK_BACK, 14}, {VK_TAB, 15}, {VK_RETURN, 28}, {VK_SPACE, 57},
    {VK_CAPITAL, 58}, {VK_INSERT, 110}, {VK_DELETE, 111},

    // System keys
    {VK_APPS, 139}, {VK_SNAPSHOT, 99}, {VK_SCROLL, 70}, {VK_PAUSE, 119},

    // OEM keys
    {VK_OEM_1, 39}, {VK_OEM_2, 53}, {VK_OEM_3, 41}, {VK_OEM_4, 26},
    {VK_OEM_5, 43}, {VK_OEM_6, 27}, {VK_OEM_7, 40}, {VK_OEM_COMMA, 51},
    {VK_OEM_PERIOD, 52}, {VK_OEM_MINUS, 12}, {VK_OEM_PLUS, 13},

    // Direct VK codes
    {8, 14}, {9, 15}, {13, 28}, {19, 119}, {20, 58}, {27, 1}, {32, 57},
    {33, 104}, {34, 109}, {35, 107}, {36, 102}, {37, 105}, {38, 103},
    {39, 106}, {40, 108}, {44, 99}, {45, 110}, {46, 111},
    {91, 125}, {93, 139},
    {112, 59}, {113, 60}, {114, 61}, {115, 62}, {116, 63}, {117, 64},
    {118, 65}, {119, 66}, {120, 67}, {121, 68}, {122, 87}, {123, 88},
    {145, 70}, {160, 42}, {161, 54}, {162, 29}, {163, 97}, {164, 56}, {165, 100},
    {186, 39}, {187, 13}, {188, 51}, {189, 12}, {190, 52},
    {191, 53}, {192, 41}, {219, 26}, {220, 43}, {221, 27}, {222, 40}
};

static int GetLinuxKeyCode(int winVkCode) {
    auto it = winToLinuxKeyMap.find(winVkCode);
    return (it != winToLinuxKeyMap.end()) ? it->second : -1;
}

//=============================================================================
// SWAP HELPER - call periodically to publish
//=============================================================================

static void TrySwap() {
    DWORD now = GetTickCount64();
    if (now - lastSwapTime >= SWAP_INTERVAL_MS) {
        // Grab accumulated wheel delta before swap
        g_kbm.back()->wheelDelta = g_pendingWheelDelta.exchange(0);

        g_kbm.swap();
        *g_kbm.back() = *g_kbm.front();
        g_kbm.back()->wheelDelta = 0;  // Don't carry over

        lastSwapTime = now;
    }
}

//=============================================================================
// KEYBOARD HOOK
//=============================================================================

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && IsViewportActive()) {
        KBDLLHOOKSTRUCT* pKey = (KBDLLHOOKSTRUCT*)lParam;
        bool keyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);

        if (pKey->vkCode == VK_F11) {
            return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);  // pass through
        }

        int linuxCode = GetLinuxKeyCode(pKey->vkCode);
        if (linuxCode >= 0 && linuxCode < 140) {
            g_kbm.back()->keys[linuxCode] = keyDown;
            TrySwap();
            return 1;  // block input
        }
    }
    return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
}

//=============================================================================
// MOUSE HOOK
//=============================================================================

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && IsViewportActive()) {
        MSLLHOOKSTRUCT* pMouse = (MSLLHOOKSTRUCT*)lParam;
        KbmData* kbm = g_kbm.back();
        // Handle buttons
        int btnIdx = -1;
        bool btnDown = false;

        switch (wParam) {
        case WM_LBUTTONDOWN: btnIdx = 0; btnDown = true;  break;
        case WM_LBUTTONUP:   btnIdx = 0; btnDown = false; break;
        case WM_RBUTTONDOWN: btnIdx = 2; btnDown = true;  break;
        case WM_RBUTTONUP:   btnIdx = 2; btnDown = false; break;
        case WM_MBUTTONDOWN: btnIdx = 1; btnDown = true;  break;
        case WM_MBUTTONUP:   btnIdx = 1; btnDown = false; break;
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP: {
            WORD xBtn = HIWORD(pMouse->mouseData);
            btnIdx = (xBtn == XBUTTON1) ? 3 : 4;
            btnDown = (wParam == WM_XBUTTONDOWN);
            break;
        }
        case WM_MOUSEWHEEL:
            g_pendingWheelDelta.fetch_add(GET_WHEEL_DELTA_WPARAM(pMouse->mouseData));
            break;
        }

        if (btnIdx >= 0 && btnIdx < 5) {
            kbm->buttons[btnIdx] = btnDown;
        }

        // Handle mouse movement
        if (pMouse->pt.x != lastPosition.x || pMouse->pt.y != lastPosition.y) {
            lastPosition = pMouse->pt;

            ViewportBounds bounds = GetViewportBounds();
            DPIInfo dpi = getDpi();

            int32_t physW = static_cast<int32_t>(bounds.width * dpi.scalingFactor);
            int32_t physH = static_cast<int32_t>(bounds.height * dpi.scalingFactor);

            int32_t boundR = bounds.x + physW;
            int32_t boundB = bounds.y + physH;

            bool inBounds = (pMouse->pt.x >= bounds.x && pMouse->pt.x <= boundR &&
                pMouse->pt.y >= bounds.y && pMouse->pt.y <= boundB);

            if (inBounds) {
                int32_t relX = pMouse->pt.x - bounds.x;
                int32_t relY = pMouse->pt.y - bounds.y;

                double scaleX = static_cast<double>(g_config->linuxWidth) / physW;
                double scaleY = static_cast<double>(g_config->linuxHeight) / physH;

                kbm->mouseX = static_cast<int32_t>(relX * scaleX);
                kbm->mouseY = static_cast<int32_t>(relY * scaleY);
            }
        }

        TrySwap();
    }
    return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
}

//=============================================================================
// INSTALL / UNINSTALL
//=============================================================================

static bool InstallHooks() {
    g_keyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc,
        GetModuleHandle(NULL), 0);
    if (!g_keyboardHook) {
        std::cerr << "Failed to install keyboard hook: " << GetLastError() << std::endl;
        return false;
    }

    g_mouseHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc,
        GetModuleHandle(NULL), 0);
    if (!g_mouseHook) {
        std::cerr << "Failed to install mouse hook: " << GetLastError() << std::endl;
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = NULL;
        return false;
    }

    return true;
}

void StopKbmHook() {
    if (g_keyboardHook) {
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = NULL;
    }
    if (g_mouseHook) {
        UnhookWindowsHookEx(g_mouseHook);
        g_mouseHook = NULL;
    }
}

void StartKbmHook() {
    lastPosition = { 0, 0 };
    lastSwapTime = 0;

    std::cout << "Installing hooks..." << std::endl;
    if (!InstallHooks()) {
        return;
    }
    std::cout << "Hooks installed." << std::endl;

    // Message pump - required for low-level hooks
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    StopKbmHook();
}