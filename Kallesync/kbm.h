#pragma once
#include <cstdint>

struct KbmState {
    int32_t mouseX;
    int32_t mouseY;
    short wheelDelta;
    bool keyStates[140];      // indexed by Linux keycode 0-139
    bool mouseButtons[5];     // indexed by (code - 6901) for 6901-6905
};

void StartKbmHook(int x, int y);

void StopKbmHook();

KbmState GetKbmState();