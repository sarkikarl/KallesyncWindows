#pragma once

#include "resource.h"
#include <windows.h>
#include <vector>
#include <cstdint>

bool InitializeViewport(HINSTANCE hInstance, int nCmdShow);
bool ProcessMessages();
void renderFrame(const std::vector<uint8_t>& rawFrame);
void CleanupDirectX();