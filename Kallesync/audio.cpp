#include "Audio.h"
#include <iostream>
#include <ws2tcpip.h>
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>

#pragma comment (lib, "ws2_32.lib")

using namespace std;

constexpr uint32_t SAMPLE_RATE = 48000;
constexpr uint16_t CHANNELS = 2;
constexpr uint16_t BITS_PER_SAMPLE = 16;
constexpr size_t FRAMES_PER_PACKET = 240;
constexpr size_t BYTES_PER_FRAME = CHANNELS * (BITS_PER_SAMPLE / 8);
constexpr size_t BUFFER_SIZE = FRAMES_PER_PACKET * BYTES_PER_FRAME;

void audio(std::atomic<bool>* running, uint16_t port) {
    WSADATA data;
    WORD version = MAKEWORD(2, 2);
    int wsOK = WSAStartup(version, &data);
    if (wsOK != 0) {
        throw std::runtime_error("audio: WSAStartup failed with error " + std::to_string(wsOK));
    }

    SOCKET in = socket(AF_INET, SOCK_DGRAM, 0);
    if (in == INVALID_SOCKET) {
        WSACleanup();
        throw std::runtime_error("audio: socket creation failed with error " + std::to_string(WSAGetLastError()));
    }

    sockaddr_in serverHint;
    serverHint.sin_addr.S_un.S_addr = ADDR_ANY;
    serverHint.sin_family = AF_INET;
    serverHint.sin_port = htons(port);
    if (bind(in, (sockaddr*)&serverHint, sizeof(serverHint)) == SOCKET_ERROR) {
        closesocket(in);
        WSACleanup();
        throw std::runtime_error("audio: bind failed with error " + std::to_string(WSAGetLastError()));
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        closesocket(in);
        WSACleanup();
        throw std::runtime_error("audio: CoInitializeEx failed with HRESULT " + std::to_string(hr));
    }

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* audioClient = nullptr;
    IAudioRenderClient* renderClient = nullptr;

    hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), (void**)&enumerator
    );
    if (FAILED(hr)) {
        CoUninitialize();
        closesocket(in);
        WSACleanup();
        throw std::runtime_error("audio: CoCreateInstance MMDeviceEnumerator failed with HRESULT " + std::to_string(hr));
    }

    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(hr)) {
        enumerator->Release();
        CoUninitialize();
        closesocket(in);
        WSACleanup();
        throw std::runtime_error("audio: GetDefaultAudioEndpoint failed with HRESULT " + std::to_string(hr));
    }

    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&audioClient);
    if (FAILED(hr)) {
        device->Release();
        enumerator->Release();
        CoUninitialize();
        closesocket(in);
        WSACleanup();
        throw std::runtime_error("audio: Activate IAudioClient failed with HRESULT " + std::to_string(hr));
    }

    WAVEFORMATEX format = {};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = CHANNELS;
    format.nSamplesPerSec = SAMPLE_RATE;
    format.wBitsPerSample = BITS_PER_SAMPLE;
    format.nBlockAlign = CHANNELS * (BITS_PER_SAMPLE / 8);
    format.nAvgBytesPerSec = SAMPLE_RATE * format.nBlockAlign;

    REFERENCE_TIME bufferDuration = 100000;

    hr = audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
        bufferDuration,
        0,
        &format,
        nullptr
    );
    if (FAILED(hr)) {
        audioClient->Release();
        device->Release();
        enumerator->Release();
        CoUninitialize();
        closesocket(in);
        WSACleanup();
        throw std::runtime_error("audio: AudioClient Initialize failed with HRESULT " + std::to_string(hr));
    }

    hr = audioClient->GetService(__uuidof(IAudioRenderClient), (void**)&renderClient);
    if (FAILED(hr)) {
        audioClient->Release();
        device->Release();
        enumerator->Release();
        CoUninitialize();
        closesocket(in);
        WSACleanup();
        throw std::runtime_error("audio: GetService IAudioRenderClient failed with HRESULT " + std::to_string(hr));
    }

    UINT32 bufferFrameCount;
    audioClient->GetBufferSize(&bufferFrameCount);

    audioClient->Start();

    int16_t recvBuffer[FRAMES_PER_PACKET * CHANNELS];

    while (running->load()) {
        ZeroMemory(recvBuffer, BUFFER_SIZE);

        int bytesIn = recvfrom(in, reinterpret_cast<char*>(recvBuffer), BUFFER_SIZE, 0, nullptr, nullptr);
        if (bytesIn == SOCKET_ERROR) {
            continue;
        }

        UINT32 paddingFrames;
        audioClient->GetCurrentPadding(&paddingFrames);

        UINT32 availableFrames = bufferFrameCount - paddingFrames;

        if (availableFrames >= FRAMES_PER_PACKET) {
            BYTE* data;
            renderClient->GetBuffer(FRAMES_PER_PACKET, &data);
            memcpy(data, recvBuffer, BUFFER_SIZE);
            renderClient->ReleaseBuffer(FRAMES_PER_PACKET, 0);
        }
        else {
            Sleep(1);
        }
    }

    audioClient->Stop();
    renderClient->Release();
    audioClient->Release();
    device->Release();
    enumerator->Release();
    CoUninitialize();
    closesocket(in);
    WSACleanup();
}