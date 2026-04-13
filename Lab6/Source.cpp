// Lab6_Lighting.cpp
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>

#ifndef MAKEFOURCC
#define MAKEFOURCC(ch0, ch1, ch2, ch3)  \
    ((DWORD)(BYTE)(ch0) | ((DWORD)(BYTE)(ch1) << 8) |  \
    ((DWORD)(BYTE)(ch2) << 16) | ((DWORD)(BYTE)(ch3) << 24))
#endif

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

using namespace DirectX;

std::wstring GetExePath()
{
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring path(exePath);
    size_t pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
        path = path.substr(0, pos + 1);
    return path;
}

// ------------------------------------------------------------------
// Структуры для работы с DDS
// ------------------------------------------------------------------
struct DDS_PIXELFORMAT
{
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwFourCC;
    DWORD dwRGBBitCount;
    DWORD dwRBitMask;
    DWORD dwGBitMask;
    DWORD dwBBitMask;
    DWORD dwABitMask;
};

struct DDS_HEADER
{
    DWORD dwSize;
    DWORD dwHeaderFlags;
    DWORD dwHeight;
    DWORD dwWidth;
    DWORD dwPitchOrLinearSize;
    DWORD dwDepth;
    DWORD dwMipMapCount;
    DWORD dwReserved1[11];
    DDS_PIXELFORMAT ddspf;
    DWORD dwSurfaceFlags;
    DWORD dwCubemapFlags;
    DWORD dwReserved2[3];
};

#define DDS_MAGIC 0x20534444
#define DDS_HEADER_FLAGS_TEXTURE 0x00001007
#define DDS_SURFACE_FLAGS_MIPMAP 0x00400000
#define DDS_FOURCC 0x00000004
#define DDS_RGB 0x00000040
#define DDS_RGBA 0x00000041

#define FOURCC_DXT1 MAKEFOURCC('D','X','T','1')
#define FOURCC_DXT3 MAKEFOURCC('D','X','T','3')
#define FOURCC_DXT5 MAKEFOURCC('D','X','T','5')

UINT32 DivUp(UINT32 a, UINT32 b) { return (a + b - 1) / b; }

UINT32 GetBytesPerBlock(DXGI_FORMAT fmt)
{
    switch (fmt)
    {
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC4_UNORM: return 8;
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC5_UNORM: return 16;
    default: return 0;
    }
}

std::string WCSToMBS(const std::wstring& wstr)
{
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

struct TextureDesc
{
    UINT32 pitch = 0;
    UINT32 mipmapsCount = 0;
    DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
    UINT32 width = 0;
    UINT32 height = 0;
    void* pData = nullptr;
};

bool LoadDDS(const wchar_t* filename, TextureDesc& desc, std::vector<void*>* pMipData = nullptr, std::vector<UINT32>* pMipPitches = nullptr)
{
    HANDLE hFile = CreateFileW(filename, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD dwMagic, dwBytesRead;
    ReadFile(hFile, &dwMagic, sizeof(DWORD), &dwBytesRead, NULL);
    if (dwMagic != DDS_MAGIC) { CloseHandle(hFile); return false; }

    DDS_HEADER header;
    ReadFile(hFile, &header, sizeof(DDS_HEADER), &dwBytesRead, NULL);

    desc.width = header.dwWidth;
    desc.height = header.dwHeight;
    desc.mipmapsCount = (header.dwSurfaceFlags & DDS_SURFACE_FLAGS_MIPMAP) ? header.dwMipMapCount : 1;

    if (header.ddspf.dwFlags & DDS_FOURCC)
    {
        switch (header.ddspf.dwFourCC)
        {
        case FOURCC_DXT1: desc.fmt = DXGI_FORMAT_BC1_UNORM; break;
        case FOURCC_DXT3: desc.fmt = DXGI_FORMAT_BC2_UNORM; break;
        case FOURCC_DXT5: desc.fmt = DXGI_FORMAT_BC3_UNORM; break;
        default: desc.fmt = DXGI_FORMAT_UNKNOWN; break;
        }
    }
    else if (header.ddspf.dwFlags & DDS_RGB)
    {
        desc.fmt = DXGI_FORMAT_UNKNOWN;
    }

    if (desc.fmt == DXGI_FORMAT_UNKNOWN) { CloseHandle(hFile); return false; }

    //общий размер данных всех мипов
    UINT32 totalSize = 0;
    UINT32 w = desc.width, h = desc.height;
    for (UINT32 mip = 0; mip < desc.mipmapsCount; ++mip)
    {
        UINT32 blockW = DivUp(w, 4u);
        UINT32 blockH = DivUp(h, 4u);
        UINT32 pitch = blockW * GetBytesPerBlock(desc.fmt);
        UINT32 mipSize = pitch * blockH;
        if (pMipPitches) pMipPitches->push_back(pitch);
        totalSize += mipSize;
        w = max(w / 2, 1u);
        h = max(h / 2, 1u);
    }

    // Читаем все мипы
    void* pAllData = malloc(totalSize);
    if (!pAllData) { CloseHandle(hFile); return false; }
    ReadFile(hFile, pAllData, totalSize, &dwBytesRead, NULL);
    CloseHandle(hFile);

    if (pMipData)
    {
        BYTE* pCurrent = (BYTE*)pAllData;
        w = desc.width; h = desc.height;
        for (UINT32 mip = 0; mip < desc.mipmapsCount; ++mip)
        {
            UINT32 blockW = DivUp(w, 4u);
            UINT32 blockH = DivUp(h, 4u);
            UINT32 pitch = blockW * GetBytesPerBlock(desc.fmt);
            UINT32 mipSize = pitch * blockH;
            pMipData->push_back(pCurrent);
            pCurrent += mipSize;
            w = max(w / 2, 1u);
            h = max(h / 2, 1u);
        }
    }

    desc.pData = pAllData;
    return true;
}

// ------------------------------------------------------------------
// Новый тип вершины (позиция, нормаль, касательная, UV)
// ------------------------------------------------------------------
struct TextureNormalTangentVertex
{
    XMFLOAT3 pos;
    XMFLOAT3 normal;
    XMFLOAT3 tangent;
    XMFLOAT2 uv;
};

// ------------------------------------------------------------------
// Глобальные переменные
// ------------------------------------------------------------------
HWND g_hWnd = nullptr;

ID3D11Device* g_pDevice = nullptr;
ID3D11DeviceContext* g_pDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_pBackBufferRTV = nullptr;
ID3D11DepthStencilView* g_pDepthStencilView = nullptr;

// Геометрия
ID3D11Buffer* g_pVertexBuffer = nullptr;      // куб
ID3D11Buffer* g_pIndexBuffer = nullptr;
ID3D11Buffer* g_pSkyboxVertexBuffer = nullptr;
ID3D11Buffer* g_pSkyboxIndexBuffer = nullptr;

// Шейдеры для куба
ID3D11VertexShader* g_pVertexShader = nullptr;
ID3D11PixelShader* g_pPixelShader = nullptr;
ID3D11InputLayout* g_pInputLayout = nullptr;

// Шейдеры для skybox
ID3D11VertexShader* g_pSkyboxVS = nullptr;
ID3D11PixelShader* g_pSkyboxPS = nullptr;
ID3D11InputLayout* g_pSkyboxInputLayout = nullptr;

// Константные буферы
struct ModelBuffer { XMMATRIX model; };
struct ViewProjBuffer { XMMATRIX vp; };
struct SceneBuffer
{
    XMMATRIX vp;
    XMFLOAT4 cameraPos;
    XMFLOAT4 lightCount;
    struct Light { XMFLOAT4 pos; XMFLOAT4 color; } lights[10];
    XMFLOAT4 ambientColor;
};
ID3D11Buffer* g_pModelBuffer1 = nullptr;
ID3D11Buffer* g_pModelBuffer2 = nullptr;
ID3D11Buffer* g_pViewProjBuffer = nullptr;
ID3D11Buffer* g_pSceneBuffer = nullptr;

// Текстурные ресурсы
ID3D11Texture2D* g_pTexture = nullptr;
ID3D11ShaderResourceView* g_pTextureView = nullptr;
ID3D11Texture2D* g_pNormalMapTexture = nullptr;
ID3D11ShaderResourceView* g_pNormalMapView = nullptr;
ID3D11SamplerState* g_pSampler = nullptr;

// Cubemap для skybox
ID3D11Texture2D* g_pCubemapTexture = nullptr;
ID3D11ShaderResourceView* g_pCubemapView = nullptr;

// Состояния для прозрачных объектов
ID3D11BlendState* g_pBlendState = nullptr;
ID3D11DepthStencilState* g_pDepthNoWrite = nullptr;
ID3D11RasterizerState* g_pRSCullNone = nullptr;

UINT g_ClientWidth = 1280;
UINT g_ClientHeight = 720;

// Управление камерой
float g_CameraYaw = 0.0f;
float g_CameraPitch = 0.3f;
float g_CameraDist = 5.0f;
bool g_KeyLeft = false, g_KeyRight = false, g_KeyUp = false, g_KeyDown = false;
double g_LastTime = 0.0;

#define SAFE_RELEASE(p) if (p) { (p)->Release(); (p) = nullptr; }

inline HRESULT SetResourceName(ID3D11DeviceChild* pResource, const std::string& name)
{
    return pResource->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)name.length(), name.c_str());
}

// ------------------------------------------------------------------
// Прототипы
// ------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
bool InitDirectX();
void CreateCubeResources();
void CompileShaders();
void LoadTextures();
void CleanupDirectX();
void RenderFrame();
void OnResize(UINT newWidth, UINT newHeight);
void UpdateCamera(double deltaTime);

// ------------------------------------------------------------------
// WinMain
// ------------------------------------------------------------------
int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int nCmdShow)
{
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"DX11Lab09Class";

    if (!RegisterClassExW(&wc))
    {
        MessageBoxW(nullptr, L"RegisterClassEx failed", L"Error", MB_OK | MB_ICONERROR);
        return 0;
    }

    RECT rc = { 0, 0, (LONG)g_ClientWidth, (LONG)g_ClientHeight };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    int winWidth = rc.right - rc.left;
    int winHeight = rc.bottom - rc.top;

    g_hWnd = CreateWindowW(wc.lpszClassName, L"Lab 09 - Lighting",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        winWidth, winHeight, nullptr, nullptr, hInstance, nullptr);
    if (!g_hWnd)
    {
        MessageBoxW(nullptr, L"CreateWindow failed", L"Error", MB_OK | MB_ICONERROR);
        return 0;
    }

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    if (!InitDirectX())
    {
        CleanupDirectX();
        DestroyWindow(g_hWnd);
        return -1;
    }

    CreateCubeResources();
    CompileShaders();
    LoadTextures();

    // Создание константных буферов
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(ModelBuffer);
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    HRESULT hr = g_pDevice->CreateBuffer(&desc, nullptr, &g_pModelBuffer1);
    assert(SUCCEEDED(hr));
    hr = g_pDevice->CreateBuffer(&desc, nullptr, &g_pModelBuffer2);
    assert(SUCCEEDED(hr));

    desc.ByteWidth = sizeof(ViewProjBuffer);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = g_pDevice->CreateBuffer(&desc, nullptr, &g_pViewProjBuffer);
    assert(SUCCEEDED(hr));

    desc.ByteWidth = sizeof(SceneBuffer);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    hr = g_pDevice->CreateBuffer(&desc, nullptr, &g_pSceneBuffer);
    assert(SUCCEEDED(hr));

    SetResourceName(g_pModelBuffer1, "ModelBuffer1");
    SetResourceName(g_pModelBuffer2, "ModelBuffer2");
    SetResourceName(g_pViewProjBuffer, "ViewProjBuffer");
    SetResourceName(g_pSceneBuffer, "SceneBuffer");

    g_LastTime = (double)GetTickCount64() / 1000.0;

    MSG msg = {};
    bool done = false;
    while (!done)
    {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                done = true;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!done)
            RenderFrame();
    }

    CleanupDirectX();
    return (int)msg.wParam;
}

// ------------------------------------------------------------------
// Оконная процедура
// ------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_SIZE:
        if (g_pSwapChain && wParam != SIZE_MINIMIZED)
        {
            UINT newW = LOWORD(lParam);
            UINT newH = HIWORD(lParam);
            if (newW > 0 && newH > 0)
                OnResize(newW, newH);
        }
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_LEFT) g_KeyLeft = true;
        if (wParam == VK_RIGHT) g_KeyRight = true;
        if (wParam == VK_UP) g_KeyUp = true;
        if (wParam == VK_DOWN) g_KeyDown = true;
        return 0;
    case WM_KEYUP:
        if (wParam == VK_LEFT) g_KeyLeft = false;
        if (wParam == VK_RIGHT) g_KeyRight = false;
        if (wParam == VK_UP) g_KeyUp = false;
        if (wParam == VK_DOWN) g_KeyDown = false;
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, message, wParam, lParam);
}

// ------------------------------------------------------------------
// Инициализация DirectX 
// ------------------------------------------------------------------
bool InitDirectX()
{
    HRESULT hr;

    IDXGIFactory* pFactory = nullptr;
    hr = CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&pFactory);
    if (FAILED(hr)) return false;

    IDXGIAdapter* pSelectedAdapter = nullptr;
    UINT idx = 0;
    while (true)
    {
        IDXGIAdapter* pAdapter = nullptr;
        hr = pFactory->EnumAdapters(idx, &pAdapter);
        if (FAILED(hr) || pAdapter == nullptr) break;

        DXGI_ADAPTER_DESC desc;
        pAdapter->GetDesc(&desc);
        if (wcscmp(desc.Description, L"Microsoft Basic Render Driver") != 0)
        {
            pSelectedAdapter = pAdapter;
            break;
        }
        pAdapter->Release();
        idx++;
    }

    if (!pSelectedAdapter)
    {
        pFactory->Release();
        return false;
    }

    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL obtainedLevel;

    hr = D3D11CreateDevice(pSelectedAdapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
        flags, levels, 1, D3D11_SDK_VERSION,
        &g_pDevice, &obtainedLevel, &g_pDeviceContext);
    pSelectedAdapter->Release();

    if (FAILED(hr) || obtainedLevel != D3D_FEATURE_LEVEL_11_0)
    {
        pFactory->Release();
        return false;
    }

    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 2;
    scd.BufferDesc.Width = g_ClientWidth;
    scd.BufferDesc.Height = g_ClientHeight;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 0;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    scd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = g_hWnd;
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.Flags = 0;

    hr = pFactory->CreateSwapChain(g_pDevice, &scd, &g_pSwapChain);
    pFactory->Release();
    if (FAILED(hr)) return false;

    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
    if (FAILED(hr)) return false;

    // Depth buffer D32_FLOAT
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = g_ClientWidth;
    depthDesc.Height = g_ClientHeight;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.SampleDesc.Quality = 0;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    ID3D11Texture2D* pDepthStencil = nullptr;
    hr = g_pDevice->CreateTexture2D(&depthDesc, nullptr, &pDepthStencil);
    if (FAILED(hr)) return false;

    hr = g_pDevice->CreateDepthStencilView(pDepthStencil, nullptr, &g_pDepthStencilView);
    pDepthStencil->Release();
    if (FAILED(hr)) return false;

    hr = g_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pBackBufferRTV);
    pBackBuffer->Release();
    if (FAILED(hr)) return false;

    return true;
}

// ------------------------------------------------------------------
// Создание геометрии (куб + skybox)
// ------------------------------------------------------------------
void CreateCubeResources()
{
    // 24 вершины с нормалями и касательными
    const TextureNormalTangentVertex cubeVertices[] = {
        // Задняя грань (z = -0.5)
        { XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT3(0,0,-1), XMFLOAT3(1,0,0), XMFLOAT2(0,1) },
        { XMFLOAT3(0.5f, -0.5f, -0.5f), XMFLOAT3(0,0,-1), XMFLOAT3(1,0,0), XMFLOAT2(1,1) },
        { XMFLOAT3(0.5f,  0.5f, -0.5f), XMFLOAT3(0,0,-1), XMFLOAT3(1,0,0), XMFLOAT2(1,0) },
        { XMFLOAT3(-0.5f,  0.5f, -0.5f), XMFLOAT3(0,0,-1), XMFLOAT3(1,0,0), XMFLOAT2(0,0) },
        // Передняя грань (z = 0.5)
        { XMFLOAT3(-0.5f, -0.5f,  0.5f), XMFLOAT3(0,0, 1), XMFLOAT3(1,0,0), XMFLOAT2(0,1) },
        { XMFLOAT3(0.5f, -0.5f,  0.5f), XMFLOAT3(0,0, 1), XMFLOAT3(1,0,0), XMFLOAT2(1,1) },
        { XMFLOAT3(0.5f,  0.5f,  0.5f), XMFLOAT3(0,0, 1), XMFLOAT3(1,0,0), XMFLOAT2(1,0) },
        { XMFLOAT3(-0.5f,  0.5f,  0.5f), XMFLOAT3(0,0, 1), XMFLOAT3(1,0,0), XMFLOAT2(0,0) },
        // Левая грань (x = -0.5)
        { XMFLOAT3(-0.5f, -0.5f,  0.5f), XMFLOAT3(-1,0,0), XMFLOAT3(0,0,1), XMFLOAT2(0,1) },
        { XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT3(-1,0,0), XMFLOAT3(0,0,1), XMFLOAT2(1,1) },
        { XMFLOAT3(-0.5f,  0.5f, -0.5f), XMFLOAT3(-1,0,0), XMFLOAT3(0,0,1), XMFLOAT2(1,0) },
        { XMFLOAT3(-0.5f,  0.5f,  0.5f), XMFLOAT3(-1,0,0), XMFLOAT3(0,0,1), XMFLOAT2(0,0) },
        // Правая грань (x = 0.5)
        { XMFLOAT3(0.5f, -0.5f, -0.5f), XMFLOAT3(1,0,0), XMFLOAT3(0,0,1), XMFLOAT2(0,1) },
        { XMFLOAT3(0.5f, -0.5f,  0.5f), XMFLOAT3(1,0,0), XMFLOAT3(0,0,1), XMFLOAT2(1,1) },
        { XMFLOAT3(0.5f,  0.5f,  0.5f), XMFLOAT3(1,0,0), XMFLOAT3(0,0,1), XMFLOAT2(1,0) },
        { XMFLOAT3(0.5f,  0.5f, -0.5f), XMFLOAT3(1,0,0), XMFLOAT3(0,0,1), XMFLOAT2(0,0) },
        // Верхняя грань (y = 0.5)
        { XMFLOAT3(-0.5f,  0.5f, -0.5f), XMFLOAT3(0,1,0), XMFLOAT3(1,0,0), XMFLOAT2(0,1) },
        { XMFLOAT3(0.5f,  0.5f, -0.5f), XMFLOAT3(0,1,0), XMFLOAT3(1,0,0), XMFLOAT2(1,1) },
        { XMFLOAT3(0.5f,  0.5f,  0.5f), XMFLOAT3(0,1,0), XMFLOAT3(1,0,0), XMFLOAT2(1,0) },
        { XMFLOAT3(-0.5f,  0.5f,  0.5f), XMFLOAT3(0,1,0), XMFLOAT3(1,0,0), XMFLOAT2(0,0) },
        // Нижняя грань (y = -0.5)
        { XMFLOAT3(-0.5f, -0.5f,  0.5f), XMFLOAT3(0,-1,0), XMFLOAT3(1,0,0), XMFLOAT2(0,1) },
        { XMFLOAT3(0.5f, -0.5f,  0.5f), XMFLOAT3(0,-1,0), XMFLOAT3(1,0,0), XMFLOAT2(1,1) },
        { XMFLOAT3(0.5f, -0.5f, -0.5f), XMFLOAT3(0,-1,0), XMFLOAT3(1,0,0), XMFLOAT2(1,0) },
        { XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT3(0,-1,0), XMFLOAT3(1,0,0), XMFLOAT2(0,0) }
    };

    const USHORT cubeIndices[] = {
         0,  2,  1,  0,  3,  2,  // задняя
         4,  5,  6,  4,  6,  7,  // передняя
         8, 10,  9,  8, 11, 10,  // левая
        12, 14, 13, 12, 15, 14,  // правая
        16, 18, 17, 16, 19, 18,  // верхняя
        20, 22, 21, 20, 23, 22   // нижняя
    };

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(cubeVertices);
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA data = { cubeVertices };
    HRESULT hr = g_pDevice->CreateBuffer(&desc, &data, &g_pVertexBuffer);
    if (FAILED(hr)) { OutputDebugStringA("CreateBuffer failed\n"); return; }
    SetResourceName(g_pVertexBuffer, "CubeVertexBuffer");

    desc.ByteWidth = sizeof(cubeIndices);
    desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    data.pSysMem = cubeIndices;
    hr = g_pDevice->CreateBuffer(&desc, &data, &g_pIndexBuffer);
    if (FAILED(hr)) { OutputDebugStringA("CreateBuffer failed\n"); return; }
    SetResourceName(g_pIndexBuffer, "CubeIndexBuffer");

    // Skybox (куб размером 10)
    struct SkyboxVertex { XMFLOAT3 pos; XMFLOAT2 uv; };
    const SkyboxVertex skyboxVertices[] = {
        { XMFLOAT3(-10, -10, -10), XMFLOAT2(0,0) }, { XMFLOAT3(10, -10, -10), XMFLOAT2(0,0) },
        { XMFLOAT3(10,  10, -10), XMFLOAT2(0,0) }, { XMFLOAT3(-10,  10, -10), XMFLOAT2(0,0) },
        { XMFLOAT3(-10, -10,  10), XMFLOAT2(0,0) }, { XMFLOAT3(10, -10,  10), XMFLOAT2(0,0) },
        { XMFLOAT3(10,  10,  10), XMFLOAT2(0,0) }, { XMFLOAT3(-10,  10,  10), XMFLOAT2(0,0) }
    };
    const USHORT skyboxIndices[] = {
        0,2,1, 0,3,2,  4,5,6, 4,6,7,  0,7,3, 0,4,7,
        1,2,6, 1,6,5,  3,7,6, 3,6,2,  0,1,5, 0,5,4
    };

    desc.ByteWidth = sizeof(skyboxVertices);
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    data.pSysMem = skyboxVertices;
    hr = g_pDevice->CreateBuffer(&desc, &data, &g_pSkyboxVertexBuffer);
    if (FAILED(hr)) { OutputDebugStringA("CreateBuffer failed\n"); return; }
    SetResourceName(g_pSkyboxVertexBuffer, "SkyboxVertexBuffer");

    desc.ByteWidth = sizeof(skyboxIndices);
    desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    data.pSysMem = skyboxIndices;
    hr = g_pDevice->CreateBuffer(&desc, &data, &g_pSkyboxIndexBuffer);
    if (FAILED(hr)) { OutputDebugStringA("CreateBuffer failed\n"); return; }
    SetResourceName(g_pSkyboxIndexBuffer, "SkyboxIndexBuffer");
}

// ------------------------------------------------------------------
// Компиляция шейдеров
// ------------------------------------------------------------------
void CompileShaders()
{
    // Вершинный шейдер
    const char* vsCode = R"(
        cbuffer ModelBuffer : register(b0) { float4x4 model; }
        cbuffer ViewProjBuffer : register(b1) { float4x4 vp; }
        struct VSInput {
            float3 pos : POSITION;
            float3 normal : NORMAL;
            float3 tangent : TANGENT;
            float2 uv : TEXCOORD;
        };
        struct VSOutput {
            float4 pos : SV_Position;
            float3 worldPos : TEXCOORD0;
            float3 worldNormal : NORMAL;
            float3 worldTangent : TANGENT;
            float2 uv : TEXCOORD1;
        };
        VSOutput vs(VSInput v) {
            VSOutput o;
            float4 worldPos = mul(float4(v.pos, 1.0), model);
            o.pos = mul(worldPos, vp);
            o.worldPos = worldPos.xyz;
            float3x3 normalMatrix = (float3x3)model;
            o.worldNormal = normalize(mul(v.normal, normalMatrix));
            o.worldTangent = normalize(mul(v.tangent, normalMatrix));
            o.uv = v.uv;
            return o;
        }
    )";

    // Пиксельный шейдер с освещением и normal map
    const char* psCode = R"(
        Texture2D colorTexture : register(t0);
        Texture2D normalMap : register(t1);
        SamplerState colorSampler : register(s0);
        cbuffer SceneBuffer : register(b2) {
            float4x4 vp;
            float4 cameraPos;
            float4 lightCount;
            struct Light { float4 pos; float4 color; } lights[10];
            float4 ambientColor;
        };
        struct VSOutput {
            float4 pos : SV_Position;
            float3 worldPos : TEXCOORD0;
            float3 worldNormal : NORMAL;
            float3 worldTangent : TANGENT;
            float2 uv : TEXCOORD1;
        };
        float4 ps(VSOutput p) : SV_Target0 {
            float4 texColor = colorTexture.Sample(colorSampler, p.uv);
            float3 tangentNormal = normalMap.Sample(colorSampler, p.uv).xyz * 2.0 - 1.0;
            float3 N = normalize(p.worldNormal);
            float3 T = normalize(p.worldTangent);
            float3 B = cross(N, T);
            float3x3 TBN = float3x3(T, B, N);
            float3 worldNormal = normalize(mul(tangentNormal, TBN));
            float3 finalColor = ambientColor.xyz * texColor.xyz;
            for (int i = 0; i < lightCount.x; ++i) {
                float3 L = lights[i].pos.xyz - p.worldPos.xyz;
                float dist = length(L);
                L = L / dist;
                float atten = 1.0 / (1.0 + 0.1 * dist + 0.01 * dist * dist);
                float diff = max(dot(worldNormal, L), 0.0);
                finalColor += texColor.xyz * diff * atten * lights[i].color.xyz;
                float3 V = normalize(cameraPos.xyz - p.worldPos.xyz);
                float3 R = reflect(-L, worldNormal);
                float spec = pow(max(dot(V, R), 0.0), 32.0);
                finalColor += spec * atten * lights[i].color.xyz;
            }
            return float4(finalColor, 1.0);
        }
    )";

    // Шейдеры skybox
    const char* skyboxVS = R"(
        cbuffer ViewProjBuffer : register(b1) { float4x4 vp; }
        struct VSInput { float3 pos : POSITION; float2 uv : TEXCOORD; };
        struct VSOutput { float4 pos : SV_Position; float3 localPos : TEXCOORD; };
        VSOutput vs(VSInput v) {
            VSOutput o;
            o.pos = mul(float4(v.pos, 1.0), vp);
            o.localPos = v.pos;
            return o;
        }
    )";

    const char* skyboxPS = R"(
        TextureCube skyboxTexture : register(t1);
        SamplerState skyboxSampler : register(s1);
        struct VSOutput { float4 pos : SV_Position; float3 localPos : TEXCOORD; };
        float4 ps(VSOutput p) : SV_Target0 {
            return skyboxTexture.Sample(skyboxSampler, p.localPos);
        }
    )";

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ID3DBlob* pVsBlob = nullptr, * pPsBlob = nullptr, * pErrorBlob = nullptr;

    // Компиляция шейдеров куба
    HRESULT hr = D3DCompile(vsCode, strlen(vsCode), nullptr, nullptr, nullptr, "vs", "vs_5_0", flags, 0, &pVsBlob, &pErrorBlob);
    if (FAILED(hr)) { if (pErrorBlob) OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer()); return; }
    hr = g_pDevice->CreateVertexShader(pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), nullptr, &g_pVertexShader);
    if (FAILED(hr)) { OutputDebugStringA("CreateVertexShader failed\n"); return; }
    SetResourceName(g_pVertexShader, "CubeVS");

    hr = D3DCompile(psCode, strlen(psCode), nullptr, nullptr, nullptr, "ps", "ps_5_0", flags, 0, &pPsBlob, &pErrorBlob);
    if (FAILED(hr)) { if (pErrorBlob) OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer()); return; }
    hr = g_pDevice->CreatePixelShader(pPsBlob->GetBufferPointer(), pPsBlob->GetBufferSize(), nullptr, &g_pPixelShader);
    if (FAILED(hr)) { OutputDebugStringA("CreatePixelShader failed\n"); return; }
    SetResourceName(g_pPixelShader, "CubePS");

    // Input layout для вершин с нормалью и касательной
    D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    hr = g_pDevice->CreateInputLayout(layoutDesc, 4, pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), &g_pInputLayout);
    if (FAILED(hr)) { OutputDebugStringA("CreateInputLayout failed\n"); return; }
    SetResourceName(g_pInputLayout, "CubeInputLayout");
    SAFE_RELEASE(pVsBlob);
    SAFE_RELEASE(pPsBlob);
    SAFE_RELEASE(pErrorBlob);

    // Компиляция шейдеров skybox
    hr = D3DCompile(skyboxVS, strlen(skyboxVS), nullptr, nullptr, nullptr, "vs", "vs_5_0", flags, 0, &pVsBlob, &pErrorBlob);
    if (FAILED(hr)) { if (pErrorBlob) OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer()); return; }
    hr = g_pDevice->CreateVertexShader(pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), nullptr, &g_pSkyboxVS);
    if (FAILED(hr)) { OutputDebugStringA("CreateVertexShader failed\n"); return; }
    SetResourceName(g_pSkyboxVS, "SkyboxVS");

    hr = D3DCompile(skyboxPS, strlen(skyboxPS), nullptr, nullptr, nullptr, "ps", "ps_5_0", flags, 0, &pPsBlob, &pErrorBlob);
    if (FAILED(hr)) { if (pErrorBlob) OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer()); return; }
    hr = g_pDevice->CreatePixelShader(pPsBlob->GetBufferPointer(), pPsBlob->GetBufferSize(), nullptr, &g_pSkyboxPS);
    if (FAILED(hr)) { OutputDebugStringA("CreatePixelShader failed\n"); return; }
    SetResourceName(g_pSkyboxPS, "SkyboxPS");

    D3D11_INPUT_ELEMENT_DESC layoutSky[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    hr = g_pDevice->CreateInputLayout(layoutSky, 2, pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), &g_pSkyboxInputLayout);
    if (FAILED(hr)) { OutputDebugStringA("CreateInputLayout failed\n"); return; }
    SetResourceName(g_pSkyboxInputLayout, "SkyboxInputLayout");
    SAFE_RELEASE(pVsBlob);
    SAFE_RELEASE(pPsBlob);
    SAFE_RELEASE(pErrorBlob);
}

// ------------------------------------------------------------------
// Загрузка текстур
// ------------------------------------------------------------------
void LoadTextures()
{
    // Получаем путь к исполняемому файлу и поднимаемся на уровень выше
    std::wstring basePath = GetExePath() + L"..\\..\\textures\\";
    std::wstring colorTexPath = basePath + L"brick.dds";
    std::wstring normalTexPath = basePath + L"brick_normal.dds";

    // Загрузка color map

    TextureDesc texDesc;
    std::vector<void*> mipData;
    std::vector<UINT32> mipPitches;
    if (!LoadDDS(colorTexPath.c_str(), texDesc, &mipData, &mipPitches))
    {
        MessageBoxA(NULL, "Failed to load color texture", "Error", MB_OK);
        return;
    }

    D3D11_TEXTURE2D_DESC tex2DDesc = {};
    tex2DDesc.Width = texDesc.width;
    tex2DDesc.Height = texDesc.height;
    tex2DDesc.MipLevels = texDesc.mipmapsCount;   //число мипов из файла
    tex2DDesc.ArraySize = 1;
    tex2DDesc.Format = texDesc.fmt;
    tex2DDesc.SampleDesc.Count = 1;
    tex2DDesc.SampleDesc.Quality = 0;
    tex2DDesc.Usage = D3D11_USAGE_IMMUTABLE;
    tex2DDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    std::vector<D3D11_SUBRESOURCE_DATA> initData(texDesc.mipmapsCount);
    for (UINT32 i = 0; i < texDesc.mipmapsCount; ++i)
    {
        initData[i].pSysMem = mipData[i];
        initData[i].SysMemPitch = mipPitches[i];
        initData[i].SysMemSlicePitch = 0;
    }

    HRESULT hr = g_pDevice->CreateTexture2D(&tex2DDesc, initData.data(), &g_pTexture);
    free(texDesc.pData);
    if (FAILED(hr)) return;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.fmt;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = texDesc.mipmapsCount;
    hr = g_pDevice->CreateShaderResourceView(g_pTexture, &srvDesc, &g_pTextureView);
    if (FAILED(hr)) return;

    TextureDesc normalDesc;
    std::vector<void*> normalMipData;
    std::vector<UINT32> normalMipPitches;
    if (LoadDDS(normalTexPath.c_str(), normalDesc, &normalMipData, &normalMipPitches))
    {
        D3D11_TEXTURE2D_DESC nTexDesc = {};
        nTexDesc.Width = normalDesc.width;
        nTexDesc.Height = normalDesc.height;
        nTexDesc.MipLevels = normalDesc.mipmapsCount;
        nTexDesc.ArraySize = 1;
        nTexDesc.Format = normalDesc.fmt;
        nTexDesc.SampleDesc.Count = 1;
        nTexDesc.SampleDesc.Quality = 0;
        nTexDesc.Usage = D3D11_USAGE_IMMUTABLE;
        nTexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        std::vector<D3D11_SUBRESOURCE_DATA> nInitData(normalDesc.mipmapsCount);
        for (UINT32 i = 0; i < normalDesc.mipmapsCount; ++i)
        {
            nInitData[i].pSysMem = normalMipData[i];
            nInitData[i].SysMemPitch = normalMipPitches[i];
            nInitData[i].SysMemSlicePitch = 0;
        }

        ID3D11Texture2D* pNormalTex = nullptr;
        hr = g_pDevice->CreateTexture2D(&nTexDesc, nInitData.data(), &pNormalTex);
        if (SUCCEEDED(hr))
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC nSrv = {};
            nSrv.Format = normalDesc.fmt;
            nSrv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            nSrv.Texture2D.MipLevels = normalDesc.mipmapsCount;
            hr = g_pDevice->CreateShaderResourceView(pNormalTex, &nSrv, &g_pNormalMapView);
            pNormalTex->Release();
        }
        free(normalDesc.pData);
    }
    else
    {
        OutputDebugStringA("Normal map not loaded, using flat normals\n");
    }

    // Создание сэмплера
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_ANISOTROPIC;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.MinLOD = -FLT_MAX;
    sampDesc.MaxLOD = FLT_MAX;
    sampDesc.MipLODBias = 0.0f;
    sampDesc.MaxAnisotropy = 16;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.BorderColor[0] = sampDesc.BorderColor[1] = sampDesc.BorderColor[2] = sampDesc.BorderColor[3] = 1.0f;
    hr = g_pDevice->CreateSamplerState(&sampDesc, &g_pSampler);
    if (FAILED(hr)) return;

    // Состояние для прозрачных объектов
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    g_pDevice->CreateBlendState(&blendDesc, &g_pBlendState);

    // Depth state без записи
    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = TRUE;
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dsDesc.DepthFunc = D3D11_COMPARISON_LESS;
    dsDesc.StencilEnable = FALSE;
    g_pDevice->CreateDepthStencilState(&dsDesc, &g_pDepthNoWrite);

    // Растеризатор без отсечения (для прозрачных и skybox)
    D3D11_RASTERIZER_DESC rsDesc = {};
    rsDesc.FillMode = D3D11_FILL_SOLID;
    rsDesc.CullMode = D3D11_CULL_NONE;
    rsDesc.FrontCounterClockwise = FALSE;
    g_pDevice->CreateRasterizerState(&rsDesc, &g_pRSCullNone);

    // Загрузка cubemap (skybox)
    std::wstring skyboxPath = basePath + L"skybox\\";
    std::wstring faceNames[6] = {
        skyboxPath + L"posx.dds", skyboxPath + L"negx.dds",
        skyboxPath + L"posy.dds", skyboxPath + L"negy.dds",
        skyboxPath + L"posz.dds", skyboxPath + L"negz.dds"
    };

    TextureDesc faceDescs[6];
    bool allOk = true;
    for (int i = 0; i < 6 && allOk; ++i)
        if (!LoadDDS(faceNames[i].c_str(), faceDescs[i]))
            allOk = false;

    if (allOk)
    {
        // Проверка идентичности
        for (int i = 1; i < 6; ++i)
        {
            if (faceDescs[i].fmt != faceDescs[0].fmt ||
                faceDescs[i].width != faceDescs[0].width ||
                faceDescs[i].height != faceDescs[0].height)
            {
                allOk = false;
                break;
            }
        }
    }

    if (allOk)
    {
        D3D11_TEXTURE2D_DESC cubeDesc = {};
        cubeDesc.Width = faceDescs[0].width;
        cubeDesc.Height = faceDescs[0].height;
        cubeDesc.MipLevels = 1;
        cubeDesc.ArraySize = 6;
        cubeDesc.Format = faceDescs[0].fmt;
        cubeDesc.SampleDesc.Count = 1;
        cubeDesc.SampleDesc.Quality = 0;
        cubeDesc.Usage = D3D11_USAGE_IMMUTABLE;
        cubeDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        cubeDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

        UINT32 blockWidth = DivUp(cubeDesc.Width, 4u);
        UINT32 blockHeight = DivUp(cubeDesc.Height, 4u);
        UINT32 pitch = blockWidth * GetBytesPerBlock(cubeDesc.Format);

        D3D11_SUBRESOURCE_DATA initData[6];
        for (int i = 0; i < 6; ++i)
        {
            initData[i].pSysMem = faceDescs[i].pData;
            initData[i].SysMemPitch = pitch;
            initData[i].SysMemSlicePitch = 0;
        }

        ID3D11Texture2D* pCubemapTex = nullptr;
        hr = g_pDevice->CreateTexture2D(&cubeDesc, initData, &pCubemapTex);
        for (int i = 0; i < 6; ++i) free(faceDescs[i].pData);
        if (SUCCEEDED(hr))
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC cubeSRVDesc = {};
            cubeSRVDesc.Format = cubeDesc.Format;
            cubeSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
            cubeSRVDesc.TextureCube.MipLevels = 1;
            cubeSRVDesc.TextureCube.MostDetailedMip = 0;
            hr = g_pDevice->CreateShaderResourceView(pCubemapTex, &cubeSRVDesc, &g_pCubemapView);
            pCubemapTex->Release();
        }
    }
    else
    {
        OutputDebugStringA("Failed to load cubemap faces\n");
    }
}

// ------------------------------------------------------------------
// Обновление камеры
// ------------------------------------------------------------------
void UpdateCamera(double deltaTime)
{
    float speed = 1.0f;
    if (g_KeyLeft)  g_CameraYaw -= speed * (float)deltaTime;
    if (g_KeyRight) g_CameraYaw += speed * (float)deltaTime;
    if (g_KeyUp)    g_CameraPitch += speed * (float)deltaTime;
    if (g_KeyDown)  g_CameraPitch -= speed * (float)deltaTime;
    if (g_CameraPitch > 1.5f) g_CameraPitch = 1.5f;
    if (g_CameraPitch < -1.5f) g_CameraPitch = -1.5f;
}

// ------------------------------------------------------------------
// Рендер 
// ------------------------------------------------------------------
void RenderFrame()
{
    if (!g_pDeviceContext || !g_pBackBufferRTV || !g_pSwapChain)
        return;

    double currentTime = (double)GetTickCount64() / 1000.0;
    double deltaTime = currentTime - g_LastTime;
    g_LastTime = currentTime;

    UpdateCamera(deltaTime);

    g_pDeviceContext->ClearState();
    g_pDeviceContext->OMSetRenderTargets(1, &g_pBackBufferRTV, g_pDepthStencilView);
    const FLOAT clearColor[4] = { 0.25f, 0.25f, 0.25f, 1.0f };
    g_pDeviceContext->ClearRenderTargetView(g_pBackBufferRTV, clearColor);
    g_pDeviceContext->ClearDepthStencilView(g_pDepthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0);

    D3D11_VIEWPORT viewport = { 0, 0, (FLOAT)g_ClientWidth, (FLOAT)g_ClientHeight, 0.0f, 1.0f };
    g_pDeviceContext->RSSetViewports(1, &viewport);

    // Матрицы камеры
    float camX = g_CameraDist * sin(g_CameraYaw) * cos(g_CameraPitch);
    float camY = g_CameraDist * sin(g_CameraPitch);
    float camZ = g_CameraDist * cos(g_CameraYaw) * cos(g_CameraPitch);
    XMVECTOR eye = XMVectorSet(camX, camY, camZ, 0.0f);
    XMVECTOR at = XMVectorZero();
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMMATRIX view = XMMatrixLookAtLH(eye, at, up);
    float aspect = (float)g_ClientWidth / (float)g_ClientHeight;
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PI / 3.0f, aspect, 0.1f, 100.0f);
    XMMATRIX vp = view * proj;

    // --- Skybox (отдельная матрица без трансляции) ---
    {
        XMMATRIX viewNoTrans = view;
        viewNoTrans.r[3] = XMVectorSet(0, 0, 0, 1);
        XMMATRIX vpSky = viewNoTrans * proj;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(g_pDeviceContext->Map(g_pViewProjBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            ViewProjBuffer* pData = (ViewProjBuffer*)mapped.pData;
            XMStoreFloat4x4((XMFLOAT4X4*)&pData->vp, XMMatrixTranspose(vpSky));
            g_pDeviceContext->Unmap(g_pViewProjBuffer, 0);
        }

        // Состояния для skybox
        D3D11_DEPTH_STENCIL_DESC dsSky = {};
        dsSky.DepthEnable = TRUE;
        dsSky.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        dsSky.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        ID3D11DepthStencilState* pDSSky = nullptr;
        g_pDevice->CreateDepthStencilState(&dsSky, &pDSSky);
        g_pDeviceContext->OMSetDepthStencilState(pDSSky, 0);
        g_pDeviceContext->RSSetState(g_pRSCullNone);
        SAFE_RELEASE(pDSSky);

        g_pDeviceContext->VSSetShader(g_pSkyboxVS, nullptr, 0);
        g_pDeviceContext->PSSetShader(g_pSkyboxPS, nullptr, 0);
        g_pDeviceContext->IASetInputLayout(g_pSkyboxInputLayout);
        // Skybox vertex consists of XMFLOAT3 pos (12 bytes) + XMFLOAT2 uv (8 bytes) = 20 bytes
        UINT stride = sizeof(XMFLOAT3) + sizeof(XMFLOAT2); // pos + uv
        UINT offset = 0;
        ID3D11Buffer* vbSky[] = { g_pSkyboxVertexBuffer };
        g_pDeviceContext->IASetVertexBuffers(0, 1, vbSky, &stride, &offset);
        g_pDeviceContext->IASetIndexBuffer(g_pSkyboxIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
        g_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11Buffer* cbsSky[] = { nullptr, g_pViewProjBuffer };
        g_pDeviceContext->VSSetConstantBuffers(0, 2, cbsSky);
        ID3D11ShaderResourceView* skySRV[] = { g_pCubemapView };
        g_pDeviceContext->PSSetShaderResources(1, 1, skySRV);
        ID3D11SamplerState* samplers[] = { g_pSampler };
        g_pDeviceContext->PSSetSamplers(1, 1, samplers);

        g_pDeviceContext->DrawIndexed(36, 0, 0);

        g_pDeviceContext->RSSetState(nullptr);
        g_pDeviceContext->OMSetDepthStencilState(nullptr, 0);
    }

    // Обновляем ViewProjBuffer для основных объектов
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(g_pDeviceContext->Map(g_pViewProjBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        ViewProjBuffer* pData = (ViewProjBuffer*)mapped.pData;
        XMStoreFloat4x4((XMFLOAT4X4*)&pData->vp, XMMatrixTranspose(vp));
        g_pDeviceContext->Unmap(g_pViewProjBuffer, 0);
    }

    // Обновляем SceneBuffer (источники света)
    if (SUCCEEDED(g_pDeviceContext->Map(g_pSceneBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        SceneBuffer* pScene = (SceneBuffer*)mapped.pData;
        XMStoreFloat4x4((XMFLOAT4X4*)&pScene->vp, XMMatrixTranspose(vp));
        pScene->cameraPos = XMFLOAT4(camX, camY, camZ, 1.0f);
        pScene->lightCount.x = 2;
        // Два источника света
        pScene->lights[0].pos = XMFLOAT4(0.0f, 2.0f, 0.0f, 1.0f);
        pScene->lights[0].color = XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f);
        pScene->lights[1].pos = XMFLOAT4(0.0f, 2.0f, 2.5f, 1.0f);
        pScene->lights[1].color = XMFLOAT4(0.6f, 0.8f, 1.0f, 1.0f);
        pScene->ambientColor = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
        g_pDeviceContext->Unmap(g_pSceneBuffer, 0);
    }

    // --- Два непрозрачных куба ---
    float angle = (float)currentTime * 0.5f;
    XMMATRIX model1 = XMMatrixRotationY(angle) * XMMatrixTranslation(0.0f, 0.0f, 0.0f);
    XMMATRIX model2 = XMMatrixRotationY(angle + XM_PI) * XMMatrixTranslation(0.0f, 0.0f, 2.0f);

    ModelBuffer modelData;
    XMStoreFloat4x4((XMFLOAT4X4*)&modelData.model, XMMatrixTranspose(model1));
    g_pDeviceContext->UpdateSubresource(g_pModelBuffer1, 0, nullptr, &modelData, 0, 0);
    XMStoreFloat4x4((XMFLOAT4X4*)&modelData.model, XMMatrixTranspose(model2));
    g_pDeviceContext->UpdateSubresource(g_pModelBuffer2, 0, nullptr, &modelData, 0, 0);

    ID3D11Buffer* cbsCube[] = { g_pModelBuffer1, g_pViewProjBuffer, g_pSceneBuffer };
    g_pDeviceContext->VSSetConstantBuffers(0, 3, cbsCube);
    g_pDeviceContext->PSSetConstantBuffers(2, 1, &g_pSceneBuffer);

    UINT stride = sizeof(TextureNormalTangentVertex);
    UINT offset = 0;
    ID3D11Buffer* vbCube[] = { g_pVertexBuffer };
    g_pDeviceContext->IASetVertexBuffers(0, 1, vbCube, &stride, &offset);
    g_pDeviceContext->IASetIndexBuffer(g_pIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
    g_pDeviceContext->IASetInputLayout(g_pInputLayout);
    g_pDeviceContext->VSSetShader(g_pVertexShader, nullptr, 0);
    g_pDeviceContext->PSSetShader(g_pPixelShader, nullptr, 0);
    ID3D11SamplerState* samplers[] = { g_pSampler };
    g_pDeviceContext->PSSetSamplers(0, 1, samplers);
    ID3D11ShaderResourceView* cubeSRV[] = { g_pTextureView, g_pNormalMapView };
    g_pDeviceContext->PSSetShaderResources(0, 2, cubeSRV);

    g_pDeviceContext->DrawIndexed(36, 0, 0); // первый куб

    cbsCube[0] = g_pModelBuffer2;
    g_pDeviceContext->VSSetConstantBuffers(0, 3, cbsCube);
    g_pDeviceContext->DrawIndexed(36, 0, 0); // второй куб

    g_pSwapChain->Present(1, 0);
}

// ------------------------------------------------------------------
// Обработка изменения размера окна (без изменений)
// ------------------------------------------------------------------
void OnResize(UINT newWidth, UINT newHeight)
{
    if (!g_pSwapChain || !g_pDevice || !g_pDeviceContext)
        return;

    g_pDeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
    SAFE_RELEASE(g_pBackBufferRTV);
    SAFE_RELEASE(g_pDepthStencilView);

    HRESULT hr = g_pSwapChain->ResizeBuffers(2, newWidth, newHeight, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) { OutputDebugStringA("ResizeBuffers failed\n"); return; }

    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
    if (FAILED(hr)) return;

    hr = g_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pBackBufferRTV);
    pBackBuffer->Release();
    if (FAILED(hr)) return;

    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = newWidth;
    depthDesc.Height = newHeight;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.SampleDesc.Quality = 0;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    ID3D11Texture2D* pDepthStencil = nullptr;
    hr = g_pDevice->CreateTexture2D(&depthDesc, nullptr, &pDepthStencil);
    if (FAILED(hr)) return;

    hr = g_pDevice->CreateDepthStencilView(pDepthStencil, nullptr, &g_pDepthStencilView);
    pDepthStencil->Release();
    if (FAILED(hr)) return;

    g_ClientWidth = newWidth;
    g_ClientHeight = newHeight;
}

// ------------------------------------------------------------------
// Очистка ресурсов
// ------------------------------------------------------------------
void CleanupDirectX()
{
    if (g_pDeviceContext)
        g_pDeviceContext->ClearState();

    SAFE_RELEASE(g_pModelBuffer1);
    SAFE_RELEASE(g_pModelBuffer2);
    SAFE_RELEASE(g_pViewProjBuffer);
    SAFE_RELEASE(g_pSceneBuffer);
    SAFE_RELEASE(g_pInputLayout);
    SAFE_RELEASE(g_pVertexShader);
    SAFE_RELEASE(g_pPixelShader);
    SAFE_RELEASE(g_pSkyboxInputLayout);
    SAFE_RELEASE(g_pSkyboxVS);
    SAFE_RELEASE(g_pSkyboxPS);
    SAFE_RELEASE(g_pIndexBuffer);
    SAFE_RELEASE(g_pVertexBuffer);
    SAFE_RELEASE(g_pSkyboxIndexBuffer);
    SAFE_RELEASE(g_pSkyboxVertexBuffer);
    SAFE_RELEASE(g_pBackBufferRTV);
    SAFE_RELEASE(g_pSwapChain);
    SAFE_RELEASE(g_pDepthStencilView);
    SAFE_RELEASE(g_pTextureView);
    SAFE_RELEASE(g_pTexture);
    SAFE_RELEASE(g_pNormalMapView);
    SAFE_RELEASE(g_pNormalMapTexture);
    SAFE_RELEASE(g_pCubemapView);
    SAFE_RELEASE(g_pCubemapTexture);
    SAFE_RELEASE(g_pSampler);
    SAFE_RELEASE(g_pBlendState);
    SAFE_RELEASE(g_pDepthNoWrite);
    SAFE_RELEASE(g_pRSCullNone);

#ifdef _DEBUG
    if (g_pDevice)
    {
        ID3D11Debug* pDebug = nullptr;
        if (SUCCEEDED(g_pDevice->QueryInterface(__uuidof(ID3D11Debug), (void**)&pDebug)))
        {
            pDebug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL | D3D11_RLDO_IGNORE_INTERNAL);
            pDebug->Release();
        }
    }
#endif

    SAFE_RELEASE(g_pDeviceContext);
    SAFE_RELEASE(g_pDevice);
}