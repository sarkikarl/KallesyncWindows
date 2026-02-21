#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "viewport.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <directxmath.h>
#include <iostream>
#include <random>
#include <cmath>
#include "udpViewPort.h"
#include "VeiwportTracking.h"
#include "kbm.h"
#include <thread>
#include "audio.h"
#include "Telementry.h"
#include "config.h"
/// <summary>
/// currently whitish bar stopped after resetting scaling on ubuntu aint sure what happened
/// </summary>

using namespace DirectX;

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

// Frame dimensions
const int RAW_WIDTH = 1920;
const int RAW_HEIGHT = 1080;

// Window state
RECT windowRect;
int screenWidth = 1600;
int screenHeight = 900;
bool fullscreenMode = false;

// Window and DirectX globals
HWND hWnd = nullptr;
ID3D11Device* pDevice = nullptr;
ID3D11DeviceContext* pDeviceContext = nullptr;
IDXGISwapChain1* pSwapChain = nullptr;
ID3D11RenderTargetView* pRenderTargetView = nullptr;
ID3D11Texture2D* pStagingTexture = nullptr;
ID3D11Texture2D* pRenderTexture = nullptr;
ID3D11ShaderResourceView* pRenderSRV = nullptr;

// Shader resources
ID3D11VertexShader* pVertexShader = nullptr;
ID3D11PixelShader* pPixelShader = nullptr;
ID3D11InputLayout* pInputLayout = nullptr;
ID3D11Buffer* pVertexBuffer = nullptr;
ID3D11SamplerState* pSamplerState = nullptr;

// For simulation
std::random_device rd;
std::mt19937 gen(rd());
std::uniform_int_distribution<> colorDist(0, 255);

std::atomic<bool> audio_running(true);

// Vertex structure
struct Vertex {
    XMFLOAT4 position;
    XMFLOAT2 texture;
};

// Function prototypes
bool InitializeWindow(HINSTANCE hInstance, int nCmdShow);
bool InitializeDirectX();
bool InitializeTextures();
bool InitializeShaders();
bool CreateFullscreenQuad();
std::vector<uint8_t> generateSimulationData();
void ToggleFullscreen();

LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

bool InitializeWindow(HINSTANCE hInstance, int nCmdShow) {
    WNDCLASSEX wcex = { 0 };
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WindowProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.lpszClassName = L"RawScreenViewerClass";

    if (!RegisterClassEx(&wcex)) {
        return false;
    }

    screenWidth = GetSystemMetrics(SM_CXSCREEN);
    screenHeight = GetSystemMetrics(SM_CYSCREEN);

    RECT rc = { 0, 0, 800, 600 };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    hWnd = CreateWindow(
        L"RawScreenViewerClass",
        L"Raw Screen Data Viewer",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        NULL,
        NULL,
        hInstance,
        NULL
    );

    if (!hWnd) {
        return false;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    return true;
}

HWND GetViewportHWND() { return hWnd; }//for mouse tracking with correlation to veiwport

bool InitializeDirectX() {
    HRESULT hr;

    RECT rc;
    GetClientRect(hWnd, &rc);
    UINT width = rc.right - rc.left;
    UINT height = rc.bottom - rc.top;

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = { 0 };
    swapChainDesc.Width = width;
    swapChainDesc.Height = height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
    UINT createDeviceFlags = 0;

#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    IDXGIFactory2* pFactory = nullptr;
    hr = CreateDXGIFactory1(__uuidof(IDXGIFactory2), (void**)&pFactory);
    if (FAILED(hr)) return false;

    hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createDeviceFlags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &pDevice,
        nullptr,
        &pDeviceContext
    );

    if (FAILED(hr)) return false;

    hr = pFactory->CreateSwapChainForHwnd(
        pDevice,
        hWnd,
        &swapChainDesc,
        nullptr,
        nullptr,
        &pSwapChain
    );

    pFactory->Release();

    if (FAILED(hr)) return false;

    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    if (FAILED(hr)) return false;

    hr = pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &pRenderTargetView);
    pBackBuffer->Release();
    if (FAILED(hr)) return false;

    pDeviceContext->OMSetRenderTargets(1, &pRenderTargetView, nullptr);

    D3D11_VIEWPORT viewport;
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    pDeviceContext->RSSetViewports(1, &viewport);

    return true;
}

bool InitializeTextures() {
    HRESULT hr;

    D3D11_TEXTURE2D_DESC textureDesc = {};
    textureDesc.Width = RAW_WIDTH;
    textureDesc.Height = RAW_HEIGHT;
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.SampleDesc.Quality = 0;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    textureDesc.CPUAccessFlags = 0;
    textureDesc.MiscFlags = 0;

    hr = pDevice->CreateTexture2D(&textureDesc, nullptr, &pRenderTexture);
    if (FAILED(hr)) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = textureDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;

    hr = pDevice->CreateShaderResourceView(pRenderTexture, &srvDesc, &pRenderSRV);
    if (FAILED(hr)) return false;

    textureDesc.Usage = D3D11_USAGE_STAGING;
    textureDesc.BindFlags = 0;
    textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = pDevice->CreateTexture2D(&textureDesc, nullptr, &pStagingTexture);
    if (FAILED(hr)) return false;

    return true;
}

bool InitializeShaders() {
    HRESULT hr;

    const char* vsCode = R"(
        struct VS_INPUT {
            float4 Pos : POSITION;
            float2 Tex : TEXCOORD0;
        };
        
        struct PS_INPUT {
            float4 Pos : SV_POSITION;
            float2 Tex : TEXCOORD0;
        };
        
        PS_INPUT main(VS_INPUT input) {
            PS_INPUT output = (PS_INPUT)0;
            output.Pos = input.Pos;
            output.Tex = input.Tex;
            return output;
        }
    )";

    const char* psCode = R"(
        Texture2D tex2D;
        SamplerState samplerState;
        
        struct PS_INPUT {
            float4 Pos : SV_POSITION;
            float2 Tex : TEXCOORD0;
        };
        
        float4 main(PS_INPUT input) : SV_Target {
            return tex2D.Sample(samplerState, input.Tex);
        }
    )";

    ID3DBlob* pVSBlob = nullptr;
    ID3DBlob* pErrorBlob = nullptr;

    hr = D3DCompile(vsCode, strlen(vsCode), "VS", nullptr, nullptr, "main", "vs_4_0", 0, 0, &pVSBlob, &pErrorBlob);
    if (FAILED(hr)) {
        if (pErrorBlob) {
            OutputDebugStringA((char*)pErrorBlob->GetBufferPointer());
            pErrorBlob->Release();
        }
        return false;
    }

    hr = pDevice->CreateVertexShader(pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), nullptr, &pVertexShader);
    if (FAILED(hr)) {
        pVSBlob->Release();
        return false;
    }

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };

    hr = pDevice->CreateInputLayout(layout, ARRAYSIZE(layout), pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), &pInputLayout);
    pVSBlob->Release();
    if (FAILED(hr)) return false;

    ID3DBlob* pPSBlob = nullptr;
    hr = D3DCompile(psCode, strlen(psCode), "PS", nullptr, nullptr, "main", "ps_4_0", 0, 0, &pPSBlob, &pErrorBlob);
    if (FAILED(hr)) {
        if (pErrorBlob) {
            OutputDebugStringA((char*)pErrorBlob->GetBufferPointer());
            pErrorBlob->Release();
        }
        return false;
    }

    hr = pDevice->CreatePixelShader(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), nullptr, &pPixelShader);
    pPSBlob->Release();
    if (FAILED(hr)) return false;

    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDesc.MinLOD = 0;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

    hr = pDevice->CreateSamplerState(&samplerDesc, &pSamplerState);
    if (FAILED(hr)) return false;

    return true;
}

bool CreateFullscreenQuad() {
    HRESULT hr;

    Vertex vertices[] = {
        { XMFLOAT4(-1.0f, -1.0f, 0.0f, 1.0f), XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT4(-1.0f,  1.0f, 0.0f, 1.0f), XMFLOAT2(0.0f, 0.0f) },
        { XMFLOAT4(1.0f, -1.0f, 0.0f, 1.0f), XMFLOAT2(1.0f, 1.0f) },
        { XMFLOAT4(1.0f,  1.0f, 0.0f, 1.0f), XMFLOAT2(1.0f, 0.0f) }
    };

    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.ByteWidth = sizeof(Vertex) * 4;
    bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bufferDesc.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = vertices;

    hr = pDevice->CreateBuffer(&bufferDesc, &initData, &pVertexBuffer);
    if (FAILED(hr)) return false;

    return true;
}

void renderFrame(const std::vector<uint8_t>& rawFrame) {
    if (!pStagingTexture || !pRenderTexture) return;

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    HRESULT hr = pDeviceContext->Map(pStagingTexture, 0, D3D11_MAP_WRITE, 0, &mappedResource);
    if (SUCCEEDED(hr)) {
        const size_t srcRowPitch = RAW_WIDTH * 4;
        std::cout << "srcRowPitch=" << srcRowPitch
            << " dstRowPitch=" << mappedResource.RowPitch << std::endl;
        for (int y = 0; y < RAW_HEIGHT; y++) {
            memcpy(
                static_cast<uint8_t*>(mappedResource.pData) + y * mappedResource.RowPitch,
                rawFrame.data() + y * srcRowPitch,
                srcRowPitch
            );
        }

        pDeviceContext->Unmap(pStagingTexture, 0);
    }

    pDeviceContext->CopyResource(pRenderTexture, pStagingTexture);

    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    pDeviceContext->ClearRenderTargetView(pRenderTargetView, clearColor);
    pDeviceContext->OMSetRenderTargets(1, &pRenderTargetView, nullptr);

    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    pDeviceContext->IASetVertexBuffers(0, 1, &pVertexBuffer, &stride, &offset);
    pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    pDeviceContext->IASetInputLayout(pInputLayout);
    pDeviceContext->VSSetShader(pVertexShader, nullptr, 0);
    pDeviceContext->PSSetShader(pPixelShader, nullptr, 0);
    pDeviceContext->PSSetShaderResources(0, 1, &pRenderSRV);
    pDeviceContext->PSSetSamplers(0, 1, &pSamplerState);

    pDeviceContext->Draw(4, 0);

    pSwapChain->Present(1, 0);
}

std::vector<uint8_t> generateSimulationData() {
    static int frameCount = 0;
    frameCount++;

    std::vector<uint8_t> frame(RAW_WIDTH * RAW_HEIGHT * 4);

    int patternType = (frameCount / 100) % 4;

    for (int y = 0; y < RAW_HEIGHT; y++) {
        for (int x = 0; x < RAW_WIDTH; x++) {
            uint8_t r, g, b;

            switch (patternType) {
            case 0:
                r = static_cast<uint8_t>((x * 255) / RAW_WIDTH);
                g = static_cast<uint8_t>((y * 255) / RAW_HEIGHT);
                b = static_cast<uint8_t>(((x + y + frameCount) * 255) / (RAW_WIDTH + RAW_HEIGHT));
                break;

            case 1:
            {
                float time = frameCount * 0.03f;
                float sinX = sin((x + time * 50) * 0.01f);
                float sinY = sin((y + time * 30) * 0.01f);
                r = static_cast<uint8_t>(127.5f + 127.5f * sinX);
                g = static_cast<uint8_t>(127.5f + 127.5f * sinY);
                b = static_cast<uint8_t>(127.5f + 127.5f * sin(time * 5));
            }
            break;

            case 2:
            {
                int centerX = RAW_WIDTH / 2;
                int centerY = RAW_HEIGHT / 2;
                float dx = static_cast<float>(x - centerX);
                float dy = static_cast<float>(y - centerY);
                float distance = sqrt(dx * dx + dy * dy);
                float angle = atan2(dy, dx);
                float spiral = (distance * 0.1f + angle * 10 + frameCount * 0.05f);
                r = static_cast<uint8_t>(127.5f + 127.5f * sin(spiral));
                g = static_cast<uint8_t>(127.5f + 127.5f * sin(spiral + 2.1f));
                b = static_cast<uint8_t>(127.5f + 127.5f * sin(spiral + 4.2f));
            }
            break;

            case 3:
            default:
                r = static_cast<uint8_t>(colorDist(gen));
                g = static_cast<uint8_t>(colorDist(gen));
                b = static_cast<uint8_t>(colorDist(gen));
                break;
            }

            int idx = (y * RAW_WIDTH + x) * 4;
            frame[idx + 0] = r;
            frame[idx + 1] = g;
            frame[idx + 2] = b;
            frame[idx + 3] = 255;
        }
    }

    return frame;
}

bool InitializeViewport(HINSTANCE hInstance, int nCmdShow) {
    if (!InitializeWindow(hInstance, nCmdShow)) return false;
    if (!InitializeDirectX()) return false;
    if (!InitializeShaders()) return false;
    if (!CreateFullscreenQuad()) return false;
    if (!InitializeTextures()) return false;
    return true;
}

void CleanupDirectX() {
    if (pRenderSRV) pRenderSRV->Release();
    if (pStagingTexture) pStagingTexture->Release();
    if (pRenderTexture) pRenderTexture->Release();
    if (pRenderTargetView) pRenderTargetView->Release();
    if (pSwapChain) pSwapChain->Release();
    if (pSamplerState) pSamplerState->Release();
    if (pVertexBuffer) pVertexBuffer->Release();
    if (pInputLayout) pInputLayout->Release();
    if (pPixelShader) pPixelShader->Release();
    if (pVertexShader) pVertexShader->Release();
    if (pDeviceContext) pDeviceContext->Release();
    if (pDevice) pDevice->Release();
}

void ToggleFullscreen() {
    fullscreenMode = !fullscreenMode;

    if (fullscreenMode) {
        GetWindowRect(hWnd, &windowRect);
        SetWindowLong(hWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(hWnd, HWND_TOP, 0, 0, screenWidth, screenHeight, SWP_FRAMECHANGED);
    }
    else {
        SetWindowLong(hWnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPos(hWnd, HWND_TOP,
            windowRect.left, windowRect.top,
            windowRect.right - windowRect.left,
            windowRect.bottom - windowRect.top,
            SWP_FRAMECHANGED);
    }
}

bool ProcessMessages() {
    MSG msg = { 0 };
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return true;
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_KEYDOWN:
        if (wParam == VK_F11 || (wParam == VK_RETURN && (GetKeyState(VK_MENU) & 0x8000))) {
            ToggleFullscreen();
        }
        else if (wParam == VK_ESCAPE && fullscreenMode) {
            ToggleFullscreen();
        }
        return 0;

    case WM_SIZE:
        if (pSwapChain && pDevice && pRenderTargetView) {
            pDeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
            pRenderTargetView->Release();

            HRESULT hr = pSwapChain->ResizeBuffers(
                0,
                LOWORD(lParam), HIWORD(lParam),
                DXGI_FORMAT_UNKNOWN,
                0
            );

            if (SUCCEEDED(hr)) {
                ID3D11Texture2D* pBackBuffer = nullptr;
                hr = pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);

                if (SUCCEEDED(hr)) {
                    hr = pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &pRenderTargetView);
                    pBackBuffer->Release();

                    if (SUCCEEDED(hr)) {
                        pDeviceContext->OMSetRenderTargets(1, &pRenderTargetView, nullptr);

                        D3D11_VIEWPORT viewport;
                        viewport.Width = static_cast<float>(LOWORD(lParam));
                        viewport.Height = static_cast<float>(HIWORD(lParam));
                        viewport.MinDepth = 0.0f;
                        viewport.MaxDepth = 1.0f;
                        viewport.TopLeftX = 0;
                        viewport.TopLeftY = 0;
                        pDeviceContext->RSSetViewports(1, &viewport);
                    }
                }
            }
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    return 0;
}

int WINAPI WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    AllocConsole();
    FILE* fp;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);
    if (!InitializeViewport(hInstance, nCmdShow)) {
        MessageBox(NULL, L"Viewport initialization failed", L"Error", MB_OK);
        CleanupDirectX();
        return 1;
    }
    InitConfig();
    
    UdpVeiwPort();  //Starts IOCP workers, returns immediately

    StartViewportTracker(GetViewportHWND());//for mouse tracking start before KbmHook

    std::thread KBMT(StartKbmHook);
    KBMT.detach();
    std::thread audio_t(audio);
    audio_t.detach();
    std::thread tele_t(Telemain);
    tele_t.detach();
    std::cout << "pass tele thread port: " << "port1\n";
   

    for (;;) {
        if (!ProcessMessages()) break;
        ConsumePackets();  // Process packets on main thread -> renderFrame on main thread
    }

    StopViewportTracker();
    StopKbmHook();

    CleanupDirectX();
    //audio_running.store(false);

    // wait for thread to finish
    return 0;
}