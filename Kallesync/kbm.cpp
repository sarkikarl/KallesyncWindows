#include "kbm.h"
#include "VeiwportTracking.h"
#include <iostream>
#include <unordered_map>
#include <Windows.h>

HHOOK g_keyboardHook = NULL;
HHOOK g_mouseHook = NULL;
POINT lastPosition;
POINT Linxy;
DWORD lastMouseSendTime = 0;
const DWORD MOUSE_SEND_INTERVAL_MS = 8; // ~120Hz, adjust as needed

// Global state and lock
KbmState g_kbmState = {};
SRWLOCK g_kbmLock = SRWLOCK_INIT;

// Linux keycodes for mouse wheel
const int LINUX_BTN_WHEEL_UP = 999;
const int LINUX_BTN_WHEEL_DOWN = 109;

class KeyStateMonitor {
private:
    std::atomic<bool> KeyMonitor{ false };

    // Updated and comprehensive Windows to Linux key mapping
    std::unordered_map<int, int> winToLinuxKeyMap = {
        // Letters (A-Z) - Original
        {'A', 30}, {'B', 48}, {'C', 46}, {'D', 32}, {'E', 18}, {'F', 33}, {'G', 34}, {'H', 35},
        {'I', 23}, {'J', 36}, {'K', 37}, {'L', 38}, {'M', 50}, {'N', 49}, {'O', 24}, {'P', 25},
        {'Q', 16}, {'R', 19}, {'S', 31}, {'T', 20}, {'U', 22}, {'V', 47}, {'W', 17}, {'X', 45},
        {'Y', 21}, {'Z', 44},

        // Numbers (0-9) - Original
        {'0', 11}, {'1', 2}, {'2', 3}, {'3', 4}, {'4', 5}, {'5', 6}, {'6', 7}, {'7', 8}, {'8', 9}, {'9', 10},

        // Function keys (F1-F12) - Original
        {VK_F1, 59}, {VK_F2, 60}, {VK_F3, 61}, {VK_F4, 62}, {VK_F5, 63}, {VK_F6, 64},
        {VK_F7, 65}, {VK_F8, 66}, {VK_F9, 67}, {VK_F10, 68}, {VK_F11, 87}, {VK_F12, 88},

        // Modifier keys - Original
        {VK_LCONTROL, 29}, {VK_RCONTROL, 97}, {VK_LSHIFT, 42}, {VK_RSHIFT, 54},
        {VK_LMENU, 56}, {VK_RMENU, 100}, {VK_LWIN, 125}, {VK_RWIN, 126},

        // Navigation keys - Original
        {VK_UP, 103}, {VK_DOWN, 108}, {VK_LEFT, 105}, {VK_RIGHT, 106},
        {VK_HOME, 102}, {VK_END, 107}, {VK_PRIOR, 104}, {VK_NEXT, 109},

        // Special keys - Original
        {VK_ESCAPE, 1}, {VK_BACK, 14}, {VK_TAB, 15}, {VK_RETURN, 28}, {VK_SPACE, 57},
        {VK_CAPITAL, 58}, {VK_INSERT, 110}, {VK_DELETE, 111},

        // Mouse buttons - Original
        {VK_LBUTTON, 6901}, {VK_MBUTTON, 6902}, {VK_RBUTTON, 6903},

        // === NEW KEYS FROM LOG FILE ===

        // System control keys
        {VK_APPS, 139},         // VK_CODE: 93 - Applications/Context menu key
        {VK_SNAPSHOT, 99},      // VK_CODE: 44 - Print Screen
        {VK_SCROLL, 70},        // VK_CODE: 145 - Scroll Lock (was UNKNOWN_145 in log)
        {VK_PAUSE, 119},        // VK_CODE: 19 - Pause/Break

        // OEM/Symbol keys (punctuation and symbols)
        {VK_OEM_1, 39},         // VK_CODE: 186 - Semicolon (;:)
        {VK_OEM_2, 53},         // VK_CODE: 191 - Forward slash (/?)
        {VK_OEM_3, 41},         // VK_CODE: 192 - Backtick (`~)
        {VK_OEM_4, 26},         // VK_CODE: 219 - Left bracket ([{)
        {VK_OEM_5, 43},         // VK_CODE: 220 - Backslash (\|)
        {VK_OEM_6, 27},         // VK_CODE: 221 - Right bracket (]})
        {VK_OEM_7, 40},         // VK_CODE: 222 - Quote ('")
        {VK_OEM_COMMA, 51},     // VK_CODE: 188 - Comma (,<)
        {VK_OEM_PERIOD, 52},    // VK_CODE: 190 - Period (.>)  
        {VK_OEM_MINUS, 12},     // VK_CODE: 189 - Minus (-_)
        {VK_OEM_PLUS, 13},      // VK_CODE: 187 - Equals/Plus (=+)

        // Direct VK code mappings for specific codes found in log
        {8, 14},                // VK_BACK - Backspace
        {9, 15},                // VK_TAB - Tab
        {13, 28},               // VK_RETURN - Enter
        {19, 119},              // VK_PAUSE - Pause
        {20, 58},               // VK_CAPITAL - Caps Lock
        {27, 1},                // VK_ESCAPE - Escape
        {32, 57},               // VK_SPACE - Spacebar
        {33, 104},              // VK_PRIOR - Page Up
        {34, 109},              // VK_NEXT - Page Down
        {35, 107},              // VK_END - End
        {36, 102},              // VK_HOME - Home
        {37, 105},              // VK_LEFT - Left Arrow
        {38, 103},              // VK_UP - Up Arrow
        {39, 106},              // VK_RIGHT - Right Arrow
        {40, 108},              // VK_DOWN - Down Arrow
        {44, 99},               // VK_SNAPSHOT - Print Screen
        {45, 110},              // VK_INSERT - Insert
        {46, 111},              // VK_DELETE - Delete
        {69, 18},               // VK_E - E key
        {81, 16},               // VK_Q - Q key
        {87, 17},               // VK_W - W key
        {91, 125},              // VK_LWIN - Left Windows key
        {93, 139},              // VK_APPS - Applications key
        {112, 59},              // VK_F1 - F1
        {113, 60},              // VK_F2 - F2
        {114, 61},              // VK_F3 - F3
        {115, 62},              // VK_F4 - F4
        {116, 63},              // VK_F5 - F5
        {117, 64},              // VK_F6 - F6
        {118, 65},              // VK_F7 - F7
        {119, 66},              // VK_F8 - F8
        {120, 67},              // VK_F9 - F9
        {121, 68},              // VK_F10 - F10
        {122, 87},              // VK_F11 - F11
        {123, 88},              // VK_F12 - F12
        {145, 70},              // VK_SCROLL - Scroll Lock
        {160, 42},              // VK_LSHIFT - Left Shift
        {161, 54},              // VK_RSHIFT - Right Shift
        {162, 29},              // VK_LCONTROL - Left Control
        {163, 97},              // VK_RCONTROL - Right Control
        {164, 56},              // VK_LMENU - Left Alt
        {165, 100},             // VK_RMENU - Right Alt
        {186, 39},              // VK_OEM_1 - Semicolon
        {187, 13},              // VK_OEM_PLUS - Equals/Plus
        {188, 51},              // VK_OEM_COMMA - Comma
        {189, 12},              // VK_OEM_MINUS - Minus
        {190, 52},              // VK_OEM_PERIOD - Period
        {191, 53},              // VK_OEM_2 - Forward slash
        {192, 41},              // VK_OEM_3 - Backtick
        {219, 26},              // VK_OEM_4 - Left bracket
        {220, 43},              // VK_OEM_5 - Backslash
        {221, 27},              // VK_OEM_6 - Right bracket
        {222, 40}               // VK_OEM_7 - Quote
    };

    // Pre-allocated message buffer for performance


public:

    int getLinuxKeyCode(int winVkCode) {
        auto it = winToLinuxKeyMap.find(winVkCode);
        return (it != winToLinuxKeyMap.end()) ? it->second : -1;
    }
};

KeyStateMonitor g_KeyClass;

KbmState GetKbmState() {
    KbmState copy;
    AcquireSRWLockShared(&g_kbmLock);
    copy = g_kbmState;
    g_kbmState.wheelDelta = 0;
    ReleaseSRWLockShared(&g_kbmLock);
    return copy;
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* pKey = (KBDLLHOOKSTRUCT*)lParam;

        bool keyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        bool keyUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

        // Your logic here: pKey->vkCode is the virtual key code
        // pKey->scanCode is the hardware scan code
        // pKey->flags contains extended key info, injected flag, etc.

        if (IsViewportActive() == true) {
            int linuxKeyCode = g_KeyClass.getLinuxKeyCode(pKey->vkCode);
            if (linuxKeyCode >= 0 && linuxKeyCode < 140) {
                AcquireSRWLockExclusive(&g_kbmLock);
                g_kbmState.keyStates[linuxKeyCode] = keyDown;
                ReleaseSRWLockExclusive(&g_kbmLock);
                return 1;
            }
            return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
        }
        return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
    }
    return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
}

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        MSLLHOOKSTRUCT* pMouse = (MSLLHOOKSTRUCT*)lParam;

        // Your logic here: pMouse->pt.x and pMouse->pt.y are screen coordinates
        // wParam tells you the event type (WM_MOUSEMOVE, WM_LBUTTONDOWN, etc.)

        if (IsViewportActive() == true) {
            const char* eventType = "MOVE";
            int linuxKeyCode = -1;
            bool isDown = false;
            SHORT wheelDelta = 0;

            switch (wParam) {
            case WM_LBUTTONDOWN:  eventType = "L_DOWN"; linuxKeyCode = 6901; isDown = true; break;
            case WM_LBUTTONUP:    eventType = "L_UP"; linuxKeyCode = 6901; isDown = false; break;
            case WM_RBUTTONDOWN:  eventType = "R_DOWN"; linuxKeyCode = 6903; isDown = true; break;
            case WM_RBUTTONUP:    eventType = "R_UP"; linuxKeyCode = 6903; isDown = false; break;
            case WM_MBUTTONDOWN:  eventType = "M_DOWN"; linuxKeyCode = 6902; isDown = true; break;
            case WM_MBUTTONUP:    eventType = "M_UP"; linuxKeyCode = 6902; isDown = false;  break;
            case WM_MOUSEWHEEL:   eventType = "WHEEL"; wheelDelta = GET_WHEEL_DELTA_WPARAM(pMouse->mouseData);
                wheelDelta;
                break;
            case WM_MOUSEHWHEEL:  eventType = "HWHEEL"; wheelDelta = GET_WHEEL_DELTA_WPARAM(pMouse->mouseData);
                break;
            case WM_XBUTTONDOWN:  eventType = "X_DOWN"; {
                WORD xButton = HIWORD(pMouse->mouseData); if (xButton == XBUTTON1) {
                    linuxKeyCode = 6904;
                    isDown = true;
                }
                else if (xButton == XBUTTON2) {
                    linuxKeyCode = 6905;
                    isDown = true;
                }
            }break;
            case WM_XBUTTONUP:    eventType = "X_UP"; {
                WORD xButton = HIWORD(pMouse->mouseData); if (xButton == XBUTTON1) {
                    linuxKeyCode = 6904;
                    isDown = false;
                }
                else if (xButton == XBUTTON2) {
                    linuxKeyCode = 6905;
                    isDown = false;
                }
            }   break;
            }

            // Only process if mouse actually moved
            if (pMouse->pt.x != lastPosition.x || pMouse->pt.y != lastPosition.y) {
                lastPosition.x = pMouse->pt.x;
                lastPosition.y = pMouse->pt.y;
                // Check if mouse is inside viewport
                ViewportBounds bounds = GetViewportBounds();
                DPIInfo WinScale = getDpi();

                // Scale logical dimensions to physical
                int32_t physicalWidth = static_cast<int32_t>(bounds.width * WinScale.scalingFactor);
                int32_t physicalHeight = static_cast<int32_t>(bounds.height * WinScale.scalingFactor);

                int32_t bounds_totalWidth = bounds.x + physicalWidth;
                int32_t bounds_totalHeight = bounds.y + physicalHeight;

                bool isWithinBounds =
                    (pMouse->pt.x >= bounds.x && pMouse->pt.x <= bounds_totalWidth) &&
                    (pMouse->pt.y >= bounds.y && pMouse->pt.y <= bounds_totalHeight);

                if (isWithinBounds) {
                    DWORD now = GetTickCount64();
                    if (now - lastMouseSendTime >= MOUSE_SEND_INTERVAL_MS) {
                        int32_t relativeX = pMouse->pt.x - bounds.x;
                        int32_t relativeY = pMouse->pt.y - bounds.y;

                        // Use physical dimensions for scaling
                        double scaleX = static_cast<double>(Linxy.x) / physicalWidth;
                        double scaleY = static_cast<double>(Linxy.y) / physicalHeight;

                        int32_t scaledX = static_cast<int32_t>(relativeX * scaleX);
                        int32_t scaledY = static_cast<int32_t>(relativeY * scaleY);

                        AcquireSRWLockExclusive(&g_kbmLock);
                        g_kbmState.mouseX = scaledX;
                        g_kbmState.mouseY = scaledY;
                        ReleaseSRWLockExclusive(&g_kbmLock);

                        lastMouseSendTime = now;
                    }
                }
            }

            // Handle wheel delta
            if (wheelDelta != 0) {
                AcquireSRWLockExclusive(&g_kbmLock);
                g_kbmState.wheelDelta = wheelDelta;
                ReleaseSRWLockExclusive(&g_kbmLock);
            }

            // Handle mouse buttons
            if (linuxKeyCode >= 6901 && linuxKeyCode <= 6905) {
                int buttonIndex = linuxKeyCode - 6901;
                AcquireSRWLockExclusive(&g_kbmLock);
                g_kbmState.mouseButtons[buttonIndex] = isDown;
                ReleaseSRWLockExclusive(&g_kbmLock);
            }

            return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
        }
        // Return 1 to block the mouse event
        // Return CallNextHookEx(...) to let it pass through
        return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
    }
    return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
}

bool InstallHooks() {

    g_keyboardHook = SetWindowsHookEx(
        WH_KEYBOARD_LL,
        LowLevelKeyboardProc,
        GetModuleHandle(NULL),
        0
    );

    if (!g_keyboardHook) {
        std::cerr << "Failed to install keyboard hook. Error: " << GetLastError() << std::endl;
        return false;
    }

    g_mouseHook = SetWindowsHookEx(
        WH_MOUSE_LL,
        LowLevelMouseProc,
        GetModuleHandle(NULL),
        0
    );

    if (!g_mouseHook) {
        std::cerr << "Failed to install mouse hook. Error: " << GetLastError() << std::endl;
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

void StartKbmHook(int tmpx, int tmpy) {
    Linxy.x = tmpx;
    Linxy.y = tmpy;
    lastPosition.x = 0;
    lastPosition.y = 0;

    // Zero out the state
    memset(&g_kbmState, 0, sizeof(g_kbmState));

    static KeyStateMonitor keyMonitor;
    std::cout << "Installing hooks..." << std::endl;
    if (!InstallHooks()) {
        return;
    }

    std::cout << "Hooks installed. Press Ctrl+C in console to exit." << std::endl;
    std::cout << "All input is being captured (except Ctrl+Alt+Del)." << std::endl;

    // Message pump - required for low-level hooks to work
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    StopKbmHook();
    return;
}