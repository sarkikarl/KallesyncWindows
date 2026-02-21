#pragma once

#include <windows.h>
#include <vector>
#include <cstdint>



HWND GetViewportHWND();

bool InitializeViewport(HINSTANCE hInstance, int nCmdShow);
bool ProcessMessages();
void renderFrame(const std::vector<uint8_t>& rawFrame);
void CleanupDirectX();