// Lab04_Texturing.cpp
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

// Константы DDS
#define DDS_MAGIC 0x20534444  // "DDS "
#define DDS_HEADER_FLAGS_TEXTURE 0x00001007
#define DDS_SURFACE_FLAGS_MIPMAP 0x00400000
#define DDS_FOURCC 0x00000004
#define DDS_RGB 0x00000040
#define DDS_RGBA 0x00000041

#define FOURCC_DXT1 MAKEFOURCC('D','X','T','1')
#define FOURCC_DXT3 MAKEFOURCC('D','X','T','3')
#define FOURCC_DXT5 MAKEFOURCC('D','X','T','5')

// ------------------------------------------------------------------
// Вспомогательные функции для работы с DDS
// ------------------------------------------------------------------

UINT32 DivUp(UINT32 a, UINT32 b)
{
    return (a + b - 1) / b;
}

UINT32 GetBytesPerBlock(DXGI_FORMAT fmt)
{
    switch (fmt)
    {
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC4_UNORM:
        return 8;  // DXT1, BC4
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC5_UNORM:
        return 16; // DXT3, DXT5, BC5
    default:
        return 0; 
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

// Структура для хранения загруженной текстуры
struct TextureDesc
{
    UINT32 pitch = 0;
    UINT32 mipmapsCount = 0;
    DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
    UINT32 width = 0;
    UINT32 height = 0;
    void* pData = nullptr;
};

// Загрузка DDS файла
bool LoadDDS(const wchar_t* filename, TextureDesc& desc)
{
    HANDLE hFile = CreateFileW(filename, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        OutputDebugStringA("Failed to open DDS file\n");
        return false;
    }

    DWORD dwMagic;
    DWORD dwBytesRead;
    ReadFile(hFile, &dwMagic, sizeof(DWORD), &dwBytesRead, NULL);
    if (dwMagic != DDS_MAGIC)
    {
        CloseHandle(hFile);
        OutputDebugStringA("Invalid DDS file (magic number)\n");
        return false;
    }

    DDS_HEADER header;
    ReadFile(hFile, &header, sizeof(DDS_HEADER), &dwBytesRead, NULL);

    desc.width = header.dwWidth;
    desc.height = header.dwHeight;
    desc.mipmapsCount = (header.dwSurfaceFlags & DDS_SURFACE_FLAGS_MIPMAP) ? header.dwMipMapCount : 1;

    // Определение формата
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
        // Для простоты считаем, что все текстуры сжатые
        desc.fmt = DXGI_FORMAT_UNKNOWN;
    }

    if (desc.fmt == DXGI_FORMAT_UNKNOWN)
    {
        CloseHandle(hFile);
        OutputDebugStringA("Unsupported DDS format\n");
        return false;
    }

    // Вычисляем размер данных
    UINT32 blockWidth = DivUp(desc.width, 4u);
    UINT32 blockHeight = DivUp(desc.height, 4u);
    UINT32 pitch = blockWidth * GetBytesPerBlock(desc.fmt);
    UINT32 dataSize = pitch * blockHeight;

    desc.pData = malloc(dataSize);
    if (!desc.pData)
    {
        CloseHandle(hFile);
        return false;
    }
    ReadFile(hFile, desc.pData, dataSize, &dwBytesRead, NULL);

    CloseHandle(hFile);
    return true;
}

// Загрузка DDS для cubemap
bool LoadDDS(const wchar_t* filename, TextureDesc& desc, bool /*cubemapFace*/)
{
    return LoadDDS(filename, desc); // та же логика
}

// ------------------------------------------------------------------
// Глобальные переменные
// ------------------------------------------------------------------

HWND g_hWnd = nullptr;

ID3D11Device* g_pDevice = nullptr;
ID3D11DeviceContext* g_pDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_pBackBufferRTV = nullptr;
ID3D11DepthStencilView* g_pDepthStencilView = nullptr;

// Геометрия текстурированного куба (24 вершины, 36 индексов)
struct TextureVertex
{
    float x, y, z;
    float u, v;
};

ID3D11Buffer* g_pVertexBuffer = nullptr;
ID3D11Buffer* g_pIndexBuffer = nullptr;

// Шейдеры для куба
ID3D11VertexShader* g_pVertexShader = nullptr;
ID3D11PixelShader* g_pPixelShader = nullptr;
ID3D11InputLayout* g_pInputLayout = nullptr;

// Шейдеры для skybox
ID3D11VertexShader* g_pSkyboxVS = nullptr;
ID3D11PixelShader* g_pSkyboxPS = nullptr;
ID3D11InputLayout* g_pSkyboxInputLayout = nullptr;
ID3D11Buffer* g_pSkyboxVertexBuffer = nullptr;
ID3D11Buffer* g_pSkyboxIndexBuffer = nullptr;

// Константные буферы
struct ModelBuffer
{
    XMMATRIX model;
};
struct ViewProjBuffer
{
    XMMATRIX vp;
};
ID3D11Buffer* g_pModelBuffer = nullptr;
ID3D11Buffer* g_pViewProjBuffer = nullptr;

// Текстурные ресурсы
ID3D11Texture2D* g_pTexture = nullptr;
ID3D11ShaderResourceView* g_pTextureView = nullptr;
ID3D11SamplerState* g_pSampler = nullptr;

// Cubemap для skybox
ID3D11Texture2D* g_pCubemapTexture = nullptr;
ID3D11ShaderResourceView* g_pCubemapView = nullptr;

UINT g_ClientWidth = 1280;
UINT g_ClientHeight = 720;

// Управление камерой
float g_CameraYaw = 0.0f;
float g_CameraPitch = 0.3f;
float g_CameraDist = 3.0f;
bool g_KeyLeft = false, g_KeyRight = false, g_KeyUp = false, g_KeyDown = false;

// Время
double g_LastTime = 0.0;

// ------------------------------------------------------------------
// Макросы и утилиты
// ------------------------------------------------------------------

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
void CreateCubeResources();      // создание буферов для куба и skybox
void CompileShaders();           // компиляция всех шейдеров
void LoadTextures();             // загрузка DDS текстур
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
    wc.lpszClassName = L"DX11Lab05Class";

    if (!RegisterClassExW(&wc))
    {
        MessageBoxW(nullptr, L"RegisterClassEx failed", L"Error", MB_OK | MB_ICONERROR);
        return 0;
    }

    RECT rc = { 0, 0, (LONG)g_ClientWidth, (LONG)g_ClientHeight };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    int winWidth = rc.right - rc.left;
    int winHeight = rc.bottom - rc.top;

    g_hWnd = CreateWindowW(wc.lpszClassName, L"Lab 05 - Texturing",
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

    // Константные буферы
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(ModelBuffer);
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    HRESULT hr = g_pDevice->CreateBuffer(&desc, nullptr, &g_pModelBuffer);
    assert(SUCCEEDED(hr));
    SetResourceName(g_pModelBuffer, "ModelBuffer");

    desc.ByteWidth = sizeof(ViewProjBuffer);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = g_pDevice->CreateBuffer(&desc, nullptr, &g_pViewProjBuffer);
    assert(SUCCEEDED(hr));
    SetResourceName(g_pViewProjBuffer, "ViewProjBuffer");

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

    // Создание Depth/Stencil текстуры
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = g_ClientWidth;
    depthDesc.Height = g_ClientHeight;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // 24-bit depth, 8-bit stencil
    depthDesc.SampleDesc.Count = 1;
    depthDesc.SampleDesc.Quality = 0;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    ID3D11Texture2D* pDepthStencil = nullptr;
    hr = g_pDevice->CreateTexture2D(&depthDesc, nullptr, &pDepthStencil);
    if (FAILED(hr)) return false;

    // Создание Depth/Stencil View
    hr = g_pDevice->CreateDepthStencilView(pDepthStencil, nullptr, &g_pDepthStencilView);
    pDepthStencil->Release();
    if (FAILED(hr)) return false;

    hr = g_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pBackBufferRTV);
    pBackBuffer->Release();
    if (FAILED(hr)) return false;

    return true;
}

// ------------------------------------------------------------------
// Создание вершинных/индексных буферов для куба и skybox
// ------------------------------------------------------------------

void CreateCubeResources()
{
    // -------------------- Текстурированный куб (24 вершины) --------------------
    const TextureVertex cubeVertices[] = {
        // Bottom face
        // Задняя грань (z = -0.5)
        {-0.5f, -0.5f, -0.5f, 0.0f, 1.0f}, // 0
        { 0.5f, -0.5f, -0.5f, 1.0f, 1.0f}, // 1
        { 0.5f,  0.5f, -0.5f, 1.0f, 0.0f}, // 2
        {-0.5f,  0.5f, -0.5f, 0.0f, 0.0f}, // 3

        // Передняя грань (z = 0.5)
        {-0.5f, -0.5f,  0.5f, 0.0f, 1.0f}, // 4
        { 0.5f, -0.5f,  0.5f, 1.0f, 1.0f}, // 5
        { 0.5f,  0.5f,  0.5f, 1.0f, 0.0f}, // 6
        {-0.5f,  0.5f,  0.5f, 0.0f, 0.0f}, // 7

        // Левая грань (x = -0.5)
        {-0.5f, -0.5f,  0.5f, 0.0f, 1.0f}, // 8
        {-0.5f, -0.5f, -0.5f, 1.0f, 1.0f}, // 9
        {-0.5f,  0.5f, -0.5f, 1.0f, 0.0f}, // 10
        {-0.5f,  0.5f,  0.5f, 0.0f, 0.0f}, // 11

        // Правая грань (x = 0.5)
        { 0.5f, -0.5f, -0.5f, 0.0f, 1.0f}, // 12
        { 0.5f, -0.5f,  0.5f, 1.0f, 1.0f}, // 13
        { 0.5f,  0.5f,  0.5f, 1.0f, 0.0f}, // 14
        { 0.5f,  0.5f, -0.5f, 0.0f, 0.0f}, // 15

        // Верхняя грань (y = 0.5)
        {-0.5f,  0.5f, -0.5f, 0.0f, 1.0f}, // 16
        { 0.5f,  0.5f, -0.5f, 1.0f, 1.0f}, // 17
        { 0.5f,  0.5f,  0.5f, 1.0f, 0.0f}, // 18
        {-0.5f,  0.5f,  0.5f, 0.0f, 0.0f}, // 19

        // Нижняя грань (y = -0.5)
        {-0.5f, -0.5f,  0.5f, 0.0f, 1.0f}, // 20
        { 0.5f, -0.5f,  0.5f, 1.0f, 1.0f}, // 21
        { 0.5f, -0.5f, -0.5f, 1.0f, 0.0f}, // 22
        {-0.5f, -0.5f, -0.5f, 0.0f, 0.0f}  // 23
    };

    const USHORT cubeIndices[] = {
        // Задняя грань (0,1,2,3)
        0, 2, 1,  0, 3, 2,
        // Передняя грань (4,5,6,7)
        4, 5, 6,  4, 6, 7,
        // Левая грань (8,9,10,11)
        8, 10, 9,  8, 11, 10,
        // Правая грань (12,13,14,15)
        12, 14, 13,  12, 15, 14,
        // Верхняя грань (16,17,18,19)
        16, 18, 17,  16, 19, 18,
        // Нижняя грань (20,21,22,23)
        20, 22, 21,  20, 23, 22
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

    // -------------------- Skybox --------------------
    const TextureVertex skyboxVertices[] = {
        // просто куб размером 10
        {-10, -10, -10, 0,0}, { 10, -10, -10, 0,0}, { 10,  10, -10, 0,0}, {-10,  10, -10, 0,0},
        {-10, -10,  10, 0,0}, { 10, -10,  10, 0,0}, { 10,  10,  10, 0,0}, {-10,  10,  10, 0,0}
    };
    const USHORT skyboxIndices[] = {
        0,2,1, 0,3,2,
        4,5,6, 4,6,7,
        0,7,3, 0,4,7, 

    };
    

    const USHORT skyboxFullIndices[] = {
        0,2,1, 0,3,2,  4,5,6, 4,6,7,  0,7,3, 0,4,7,  1,2,6, 1,6,5,  3,7,6, 3,6,2,  0,1,5, 0,5,4
    };

    desc.ByteWidth = sizeof(skyboxVertices);
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    data.pSysMem = skyboxVertices;
    hr = g_pDevice->CreateBuffer(&desc, &data, &g_pSkyboxVertexBuffer);
    if (FAILED(hr)) { OutputDebugStringA("CreateBuffer failed\n"); return; }
    SetResourceName(g_pSkyboxVertexBuffer, "SkyboxVertexBuffer");

    desc.ByteWidth = sizeof(skyboxFullIndices);
    desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    data.pSysMem = skyboxFullIndices;
    hr = g_pDevice->CreateBuffer(&desc, &data, &g_pSkyboxIndexBuffer);
    if (FAILED(hr)) { OutputDebugStringA("CreateBuffer failed\n"); return; }
    SetResourceName(g_pSkyboxIndexBuffer, "SkyboxIndexBuffer");
}

// ------------------------------------------------------------------
// Компиляция шейдеров
// ------------------------------------------------------------------

void CompileShaders()
{
    // ---------------- Шейдеры для куба ----------------
    const char* cubeVS = R"(
        cbuffer ModelBuffer : register(b0)
        {
            float4x4 model;
        }
        cbuffer ViewProjBuffer : register(b1)
        {
            float4x4 vp;
        }
        struct VSInput
        {
            float3 pos : POSITION;
            float2 uv : TEXCOORD;
        };
        struct VSOutput
        {
            float4 pos : SV_Position;
            float2 uv : TEXCOORD;
        };
        VSOutput vs(VSInput vertex)
        {
            VSOutput result;
            float4 worldPos = mul(float4(vertex.pos, 1.0), model);
            result.pos = mul(worldPos, vp);
            result.uv = vertex.uv;
            return result;
        }
    )";

    const char* cubePS = R"(
        Texture2D colorTexture : register(t0);
        SamplerState colorSampler : register(s0);
        struct VSOutput
        {
            float4 pos : SV_Position;
            float2 uv : TEXCOORD;
        };
        float4 ps(VSOutput pixel) : SV_Target0
        {
            return colorTexture.Sample(colorSampler, pixel.uv);
        }
    )";

    // ---------------- Шейдеры для skybox ----------------
    const char* skyboxVS = R"(
        cbuffer ViewProjBuffer : register(b1)
        {
            float4x4 vp;
        }
        struct VSInput
        {
            float3 pos : POSITION;
            float2 uv : TEXCOORD; // не используется
        };
        struct VSOutput
        {
            float4 pos : SV_Position;
            float3 localPos : TEXCOORD;
        };
        VSOutput vs(VSInput vertex)
        {
            VSOutput result;
            // Чтобы куб всегда был вокруг камеры, передаём позицию без трансляции модели,
            // но с учётом поворотов камеры. В простом случае можно просто удалить трансляцию из view.
            // Используем матрицу vp без трансляции камеры? Лучше передавать отдельно матрицу вида без перемещения.
            // Упростим: будем использовать ту же vp, но с обнулённой трансляцией? Или просто умножим позицию на vp,
            // а камера находится в центре куба, поэтому смещение не нужно.
            result.pos = mul(float4(vertex.pos, 1.0), vp);
            result.localPos = vertex.pos;
            return result;
        }
    )";

    const char* skyboxPS = R"(
        TextureCube skyboxTexture : register(t1);
        SamplerState skyboxSampler : register(s1);
        struct VSOutput
        {
            float4 pos : SV_Position;
            float3 localPos : TEXCOORD;
        };
        float4 ps(VSOutput pixel) : SV_Target0
        {
            return skyboxTexture.Sample(skyboxSampler, pixel.localPos); //
            //return float4(0, 1, 0, 1);
        }
    )";

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ID3DBlob* pVsBlob = nullptr, * pPsBlob = nullptr, * pErrorBlob = nullptr;
    HRESULT hr;

    // Компиляция шейдеров куба
    hr = D3DCompile(cubeVS, strlen(cubeVS), nullptr, nullptr, nullptr, "vs", "vs_5_0", flags, 0, &pVsBlob, &pErrorBlob);
    if (FAILED(hr)) { OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer()); return; }
    hr = g_pDevice->CreateVertexShader(pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), nullptr, &g_pVertexShader);
    if (FAILED(hr)) { OutputDebugStringA("CreateVertexShader failed\n"); return; }
    SetResourceName(g_pVertexShader, "CubeVS");

    hr = D3DCompile(cubePS, strlen(cubePS), nullptr, nullptr, nullptr, "ps", "ps_5_0", flags, 0, &pPsBlob, &pErrorBlob);
    if (FAILED(hr)) { OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer()); return; }
    hr = g_pDevice->CreatePixelShader(pPsBlob->GetBufferPointer(), pPsBlob->GetBufferSize(), nullptr, &g_pPixelShader);
    if (FAILED(hr)) { OutputDebugStringA("CreatePixelShader failed\n"); return; }
    SetResourceName(g_pPixelShader, "CubePS");

    D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    hr = g_pDevice->CreateInputLayout(layoutDesc, 2, pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), &g_pInputLayout);
    if (FAILED(hr)) { OutputDebugStringA("CreateInputLayout failed\n"); return; }
    SetResourceName(g_pInputLayout, "CubeInputLayout");

    SAFE_RELEASE(pVsBlob);
    SAFE_RELEASE(pPsBlob);
    SAFE_RELEASE(pErrorBlob);

    // Компиляция шейдеров skybox
    hr = D3DCompile(skyboxVS, strlen(skyboxVS), nullptr, nullptr, nullptr, "vs", "vs_5_0", flags, 0, &pVsBlob, &pErrorBlob);
    if (FAILED(hr)) { OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer()); return; }
    hr = g_pDevice->CreateVertexShader(pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), nullptr, &g_pSkyboxVS);
    if (FAILED(hr)) { OutputDebugStringA("CreateVertexShader failed\n"); return; }
    SetResourceName(g_pSkyboxVS, "SkyboxVS");



    hr = D3DCompile(skyboxPS, strlen(skyboxPS), nullptr, nullptr, nullptr, "ps", "ps_5_0", flags, 0, &pPsBlob, &pErrorBlob);
    if (FAILED(hr)) { OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer()); return; }
    hr = g_pDevice->CreatePixelShader(pPsBlob->GetBufferPointer(), pPsBlob->GetBufferSize(), nullptr, &g_pSkyboxPS);
    if (FAILED(hr)) { OutputDebugStringA("CreatePixelShader failed\n"); return; }
    SetResourceName(g_pSkyboxPS, "SkyboxPS");



    // Для skybox используем тот же layout (позиция + текстурные координаты, хотя последние не используются)
    hr = g_pDevice->CreateInputLayout(layoutDesc, 2, pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), &g_pSkyboxInputLayout);
    if (FAILED(hr)) { OutputDebugStringA("CreateInputLayout failed\n"); return; }
    SetResourceName(g_pSkyboxInputLayout, "SkyboxInputLayout");



    SAFE_RELEASE(pVsBlob);
    SAFE_RELEASE(pPsBlob);
    SAFE_RELEASE(pErrorBlob);
}

// ------------------------------------------------------------------
// Загрузка текстур DDS
// ------------------------------------------------------------------

void LoadTextures()
{
    HRESULT hr = S_OK;

    // -------------------- Текстура для куба --------------------
    const std::wstring cubeTexName = L"Textures/brick.dds";
    TextureDesc texDesc;
    if (!LoadDDS(cubeTexName.c_str(), texDesc))
    {
        MessageBoxA(NULL, "Failed to load brick.dds", "Error", MB_OK);
        return;
    }

    //char buf[256];
    //sprintf_s(buf, "Loaded texture: %dx%d format: %d mips: %d",
    //    texDesc.width, texDesc.height, texDesc.fmt, texDesc.mipmapsCount);
    //MessageBoxA(NULL, buf, "Debug", MB_OK);

    // ИСПРАВЛЕНИЕ: Используем ТОЛЬКО первый MIP-уровень для простоты
    D3D11_TEXTURE2D_DESC tex2DDesc = {};
    tex2DDesc.Width = texDesc.width;
    tex2DDesc.Height = texDesc.height;
    tex2DDesc.MipLevels = 1;  // ВАЖНО: используем 1, а не texDesc.mipmapsCount
    tex2DDesc.ArraySize = 1;
    tex2DDesc.Format = texDesc.fmt;
    tex2DDesc.SampleDesc.Count = 1;
    tex2DDesc.SampleDesc.Quality = 0;  // Добавили Quality
    tex2DDesc.Usage = D3D11_USAGE_IMMUTABLE;
    tex2DDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    tex2DDesc.CPUAccessFlags = 0;
    tex2DDesc.MiscFlags = 0;

    UINT32 blockWidth = DivUp(texDesc.width, 4u);
    UINT32 blockHeight = DivUp(texDesc.height, 4u);
    UINT32 pitch = blockWidth * GetBytesPerBlock(texDesc.fmt);

    // Данные только для первого MIP-уровня
    D3D11_SUBRESOURCE_DATA texData = {};
    texData.pSysMem = texDesc.pData;
    texData.SysMemPitch = pitch;


    // Добавьте после заполнения tex2DDesc и перед CreateTexture2D:

    char checkBuf[256];
    sprintf_s(checkBuf, "Width: %d, Height: %d, Width%%4: %d, Height%%4: %d",
        tex2DDesc.Width, tex2DDesc.Height,
        tex2DDesc.Width % 4, tex2DDesc.Height % 4);
    //MessageBoxA(NULL, checkBuf, "Debug", MB_OK);

    // Проверка кратности 4
    //if ((tex2DDesc.Width % 4 != 0) || (tex2DDesc.Height % 4 != 0))
    //{
    //    MessageBoxA(NULL, "WARNING: Texture dimensions not multiple of 4!", "Error", MB_OK);
    //}


    hr = g_pDevice->CreateTexture2D(&tex2DDesc, &texData, &g_pTexture);

    //if (FAILED(hr) || !g_pTexture)
    //{
    //    char err[128];
    //    sprintf_s(err, "CreateTexture2D failed: 0x%08X", hr);
    //    MessageBoxA(NULL, err, "Error", MB_OK);
    //    free(texDesc.pData);
    //    return;
    //}
    //MessageBoxA(NULL, "Texture created OK", "Debug", MB_OK);
    SetResourceName(g_pTexture, "CubeTexture");

    // Создание SRV
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.fmt;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;

    hr = g_pDevice->CreateShaderResourceView(g_pTexture, &srvDesc, &g_pTextureView);

    //if (FAILED(hr) || !g_pTextureView)
    //{
    //    char err[128];
    //    sprintf_s(err, "CreateSRV failed: 0x%08X", hr);
    //    MessageBoxA(NULL, err, "Error", MB_OK);
    //    free(texDesc.pData);
    //    return;
    //}
    //MessageBoxA(NULL, "TextureView created OK", "Debug", MB_OK);
    //    SetResourceName(g_pTextureView, "CubeSRV");

    free(texDesc.pData);

    // -------------------- Создание сэмплера --------------------
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

    //if (FAILED(hr) || !g_pSampler)
    //{
    //    char err[128];
    //    sprintf_s(err, "CreateSampler failed: 0x%08X", hr);
    //    MessageBoxA(NULL, err, "Error", MB_OK);
    //    return;
    //}
    //MessageBoxA(NULL, "Sampler created OK", "Debug", MB_OK);
    //    SetResourceName(g_pSampler, "Sampler");

    // -------------------- Cubemap для skybox (оставляем как есть) --------------------
    const std::wstring faceNames[6] = {
        L"Textures/Skybox/posx.dds",
        L"Textures/Skybox/negx.dds",
        L"Textures/Skybox/posy.dds",
        L"Textures/Skybox/negy.dds",
        L"Textures/Skybox/posz.dds",
        L"Textures/Skybox/negz.dds"
    };

    TextureDesc faceDescs[6];
    bool allOk = true;
    for (int i = 0; i < 6; ++i)
    {
        if (!LoadDDS(faceNames[i].c_str(), faceDescs[i]))
        {
            char err[256];
            sprintf_s(err, "Failed to load: %S", faceNames[i].c_str());
            //MessageBoxA(NULL, err, "Error", MB_OK);
            allOk = false;
            break;
        }
        else
        {
            char ok[256];
            sprintf_s(ok, "Loaded: %S, %dx%d", faceNames[i].c_str(),
                faceDescs[i].width, faceDescs[i].height);
            //MessageBoxA(NULL, ok, "Debug", MB_OK);
        }
    }

    if (!allOk)
    {
        MessageBoxA(NULL, "Failed to load cubemap faces", "Error", MB_OK);
        return;
    }

    // Проверка совпадения форматов
    for (int i = 1; i < 6; ++i)
    {
        if (faceDescs[i].fmt != faceDescs[0].fmt ||
            faceDescs[i].width != faceDescs[0].width ||
            faceDescs[i].height != faceDescs[0].height)
        {
            MessageBoxA(NULL, "Cubemap faces must have same format and size", "Error", MB_OK);
            return;
        }
    }

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

    blockWidth = DivUp(cubeDesc.Width, 4u);
    blockHeight = DivUp(cubeDesc.Height, 4u);
    pitch = blockWidth * GetBytesPerBlock(cubeDesc.Format);

    D3D11_SUBRESOURCE_DATA initData[6];
    for (int i = 0; i < 6; ++i)
    {
        initData[i].pSysMem = faceDescs[i].pData;
        initData[i].SysMemPitch = pitch;
        initData[i].SysMemSlicePitch = 0;
    }

    hr = g_pDevice->CreateTexture2D(&cubeDesc, initData, &g_pCubemapTexture);

    // Очистка данных граней
    for (int i = 0; i < 6; ++i)
        free(faceDescs[i].pData);

    //if (FAILED(hr) || !g_pCubemapTexture)
    //{
    //    char err[128];
    //    sprintf_s(err, "CreateCubemapTexture failed: 0x%08X", hr);
    //    MessageBoxA(NULL, err, "Error", MB_OK);
    //    return;
    //}
    //MessageBoxA(NULL, "Cubemap texture created OK", "Debug", MB_OK);
    //    SetResourceName(g_pCubemapTexture, "CubemapTexture");

    // Создание SRV для cubemap
    D3D11_SHADER_RESOURCE_VIEW_DESC cubeSRVDesc = {};
    cubeSRVDesc.Format = cubeDesc.Format;
    cubeSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    cubeSRVDesc.TextureCube.MipLevels = 1;
    cubeSRVDesc.TextureCube.MostDetailedMip = 0;

    hr = g_pDevice->CreateShaderResourceView(g_pCubemapTexture, &cubeSRVDesc, &g_pCubemapView);

    //if (FAILED(hr) || !g_pCubemapView)
    //{
    //    char err[128];
    //    sprintf_s(err, "CreateCubemapSRV failed: 0x%08X", hr);
    //    MessageBoxA(NULL, err, "Error", MB_OK);
    //    return;
    //}
    //MessageBoxA(NULL, "Cubemap view created OK", "Debug", MB_OK);
    //    SetResourceName(g_pCubemapView, "CubemapSRV");
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

    // Очистка back buffer
    g_pDeviceContext->ClearState();
    ID3D11RenderTargetView* views[] = { g_pBackBufferRTV };
    g_pDeviceContext->OMSetRenderTargets(1, views, g_pDepthStencilView);

    const FLOAT clearColor[4] = { 0.25f, 0.25f, 0.25f, 1.0f };
    g_pDeviceContext->ClearRenderTargetView(g_pBackBufferRTV, clearColor);
    g_pDeviceContext->ClearDepthStencilView(g_pDepthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    D3D11_VIEWPORT viewport = { 0, 0, (FLOAT)g_ClientWidth, (FLOAT)g_ClientHeight, 0.0f, 1.0f };
    g_pDeviceContext->RSSetViewports(1, &viewport);

    // Матрицы
    float angle = (float)currentTime * 0.5f;
    XMMATRIX model = XMMatrixRotationY(angle);

    float camX = g_CameraDist * sin(g_CameraYaw) * cos(g_CameraPitch);
    float camY = g_CameraDist * sin(g_CameraPitch);
    float camZ = g_CameraDist * cos(g_CameraYaw) * cos(g_CameraPitch);
    XMVECTOR eye = XMVectorSet(camX, camY, camZ, 0);
    XMVECTOR target = XMVectorZero();
    XMVECTOR up = XMVectorSet(0, 1, 0, 0);
    XMMATRIX view = XMMatrixLookAtLH(eye, target, up);

    float aspect = (float)g_ClientWidth / (float)g_ClientHeight;
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PI / 3, aspect, 0.1f, 100.0f);
    XMMATRIX vp = XMMatrixMultiply(view, proj);

    // Обновление буфера вида-проекции (основной)
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = g_pDeviceContext->Map(g_pViewProjBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr))
    {
        ViewProjBuffer* pVP = (ViewProjBuffer*)mapped.pData;
        XMStoreFloat4x4((XMFLOAT4X4*)&pVP->vp, XMMatrixTranspose(vp));
        g_pDeviceContext->Unmap(g_pViewProjBuffer, 0);
    }

    // Установка сэмплера
    ID3D11SamplerState* samplers[] = { g_pSampler };
    g_pDeviceContext->PSSetSamplers(0, 1, samplers);
    g_pDeviceContext->PSSetSamplers(1, 1, samplers);

    //Рисуем skybox в фон =====
    
    // Матрица вида без трансляции (только поворот камеры)
    XMMATRIX viewNoTranslate = view;
    viewNoTranslate.r[3] = XMVectorSet(0, 0, 0, 1);
    XMMATRIX vpSky = XMMatrixMultiply(viewNoTranslate, proj);

    // Обновляем буфер для skybox
    D3D11_MAPPED_SUBRESOURCE mappedSky;
    hr = g_pDeviceContext->Map(g_pViewProjBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSky);
    if (SUCCEEDED(hr))
    {
        ViewProjBuffer* pVP = (ViewProjBuffer*)mappedSky.pData;
        XMStoreFloat4x4((XMFLOAT4X4*)&pVP->vp, XMMatrixTranspose(vpSky));
        g_pDeviceContext->Unmap(g_pViewProjBuffer, 0);
    }

    // Создание depth stencil state
    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = TRUE;
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;  
    dsDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    dsDesc.StencilEnable = FALSE;
    ID3D11DepthStencilState* pDSSkybox = nullptr;
    g_pDevice->CreateDepthStencilState(&dsDesc, &pDSSkybox);

    // Растеризатор без отсечения задних граней
    D3D11_RASTERIZER_DESC rsDesc = {};
    rsDesc.FillMode = D3D11_FILL_SOLID;
    rsDesc.CullMode = D3D11_CULL_NONE;
    rsDesc.FrontCounterClockwise = FALSE;
    ID3D11RasterizerState* pRSCullNone = nullptr;
    g_pDevice->CreateRasterizerState(&rsDesc, &pRSCullNone);
    g_pDeviceContext->RSSetState(pRSCullNone);
    g_pDeviceContext->OMSetDepthStencilState(pDSSkybox, 0);

    // Шейдеры и геометрия skybox
    g_pDeviceContext->VSSetShader(g_pSkyboxVS, nullptr, 0);
    g_pDeviceContext->PSSetShader(g_pSkyboxPS, nullptr, 0);
    g_pDeviceContext->IASetInputLayout(g_pSkyboxInputLayout);
    UINT stride = sizeof(TextureVertex);
    UINT offset = 0;
    ID3D11Buffer* vbSky[] = { g_pSkyboxVertexBuffer };
    g_pDeviceContext->IASetVertexBuffers(0, 1, vbSky, &stride, &offset);
    g_pDeviceContext->IASetIndexBuffer(g_pSkyboxIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
    g_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    
    // Cubemap в слот t1
    ID3D11ShaderResourceView* skySRV[] = { g_pCubemapView };
    g_pDeviceContext->PSSetShaderResources(1, 1, skySRV);
    
    // Константные буферы для skybox
    ID3D11Buffer* cbsSky[] = { nullptr, g_pViewProjBuffer };
    g_pDeviceContext->VSSetConstantBuffers(0, 2, cbsSky);
    
    // Рисуем skybox
    g_pDeviceContext->DrawIndexed(36, 0, 0);

    SAFE_RELEASE(pRSCullNone);
    SAFE_RELEASE(pDSSkybox);

    // Рисуем куб поверх skybox =====
    
    // Восстанавливаем обычный depth stencil state
    D3D11_DEPTH_STENCIL_DESC dsDescCube = {};
    dsDescCube.DepthEnable = TRUE;
    dsDescCube.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;  // Пишем в depth
    dsDescCube.DepthFunc = D3D11_COMPARISON_LESS;
    dsDescCube.StencilEnable = FALSE;
    ID3D11DepthStencilState* pDSCube = nullptr;
    g_pDevice->CreateDepthStencilState(&dsDescCube, &pDSCube);
    g_pDeviceContext->OMSetDepthStencilState(pDSCube, 0);

    // Восстанавливаем обычный растеризатор
    D3D11_RASTERIZER_DESC rsDescCube = {};
    rsDescCube.FillMode = D3D11_FILL_SOLID;
    rsDescCube.CullMode = D3D11_CULL_BACK;  // Отсечение задних граней
    rsDescCube.FrontCounterClockwise = FALSE;
    ID3D11RasterizerState* pRSCube = nullptr;
    g_pDevice->CreateRasterizerState(&rsDescCube, &pRSCube);
    g_pDeviceContext->RSSetState(pRSCube);

    // Обновляем буфер для куба
    hr = g_pDeviceContext->Map(g_pViewProjBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr))
    {
        ViewProjBuffer* pVP = (ViewProjBuffer*)mapped.pData;
        XMStoreFloat4x4((XMFLOAT4X4*)&pVP->vp, XMMatrixTranspose(vp));
        g_pDeviceContext->Unmap(g_pViewProjBuffer, 0);
    }

    // Обновление буфера модели
    ModelBuffer modelData;
    XMStoreFloat4x4((XMFLOAT4X4*)&modelData.model, XMMatrixTranspose(model));
    g_pDeviceContext->UpdateSubresource(g_pModelBuffer, 0, nullptr, &modelData, 0, 0);

    // Шейдеры и геометрия куба
    g_pDeviceContext->VSSetShader(g_pVertexShader, nullptr, 0);
    g_pDeviceContext->PSSetShader(g_pPixelShader, nullptr, 0);
    g_pDeviceContext->IASetInputLayout(g_pInputLayout);
    ID3D11Buffer* vbCube[] = { g_pVertexBuffer };
    g_pDeviceContext->IASetVertexBuffers(0, 1, vbCube, &stride, &offset);
    g_pDeviceContext->IASetIndexBuffer(g_pIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
    
    // Текстура куба в слот t0
    ID3D11ShaderResourceView* cubeSRV[] = { g_pTextureView };
    g_pDeviceContext->PSSetShaderResources(0, 1, cubeSRV);
    
    // Константные буферы для куба
    ID3D11Buffer* cbsCube[] = { g_pModelBuffer, g_pViewProjBuffer };
    g_pDeviceContext->VSSetConstantBuffers(0, 2, cbsCube);
    
    // Рисуем куб
    g_pDeviceContext->DrawIndexed(36, 0, 0);

    SAFE_RELEASE(pRSCube);
    SAFE_RELEASE(pDSCube);

    g_pSwapChain->Present(1, 0);
}

// ------------------------------------------------------------------
// Обработка изменения размера окна
// ------------------------------------------------------------------

void OnResize(UINT newWidth, UINT newHeight)
{
    if (!g_pSwapChain || !g_pDevice || !g_pDeviceContext)
        return;

    g_pDeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
    SAFE_RELEASE(g_pBackBufferRTV);

    HRESULT hr = g_pSwapChain->ResizeBuffers(2, newWidth, newHeight, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) { OutputDebugStringA("ResizeBuffers failed\n"); return; }

    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
    if (FAILED(hr)) { OutputDebugStringA("GetBuffer failed\n"); return; }

    hr = g_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pBackBufferRTV);
    pBackBuffer->Release();
    if (FAILED(hr)) { OutputDebugStringA("CreateRenderTargetView failed\n"); return; }

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

    SAFE_RELEASE(g_pSampler);
    SAFE_RELEASE(g_pTextureView);
    SAFE_RELEASE(g_pTexture);
    SAFE_RELEASE(g_pCubemapView);
    SAFE_RELEASE(g_pCubemapTexture);

    SAFE_RELEASE(g_pModelBuffer);
    SAFE_RELEASE(g_pViewProjBuffer);
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