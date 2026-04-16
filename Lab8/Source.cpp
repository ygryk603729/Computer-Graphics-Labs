// Lab7_InstancingAndPostprocess
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
#include <cstring>

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

const UINT MAX_INSTANCES = 20;
const UINT NUM_TEXTURES = 2;
const std::wstring TEXTURE_NAMES[] = { L"brick.dds", L"Kitty.dds" };

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
struct DDS_PIXELFORMAT { DWORD dwSize, dwFlags, dwFourCC, dwRGBBitCount, dwRBitMask, dwGBitMask, dwBBitMask, dwABitMask; };
struct DDS_HEADER { DWORD dwSize, dwHeaderFlags, dwHeight, dwWidth, dwPitchOrLinearSize, dwDepth, dwMipMapCount, dwReserved1[11]; DDS_PIXELFORMAT ddspf; DWORD dwSurfaceFlags, dwCubemapFlags, dwReserved2[3]; };
#define DDS_MAGIC 0x20534444
#define DDS_HEADER_FLAGS_TEXTURE 0x00001007
#define DDS_SURFACE_FLAGS_MIPMAP 0x00400000
#define DDS_FOURCC 0x00000004
#define DDS_RGB 0x00000040
#define FOURCC_DXT1 MAKEFOURCC('D','X','T','1')
#define FOURCC_DXT3 MAKEFOURCC('D','X','T','3')
#define FOURCC_DXT5 MAKEFOURCC('D','X','T','5')

UINT32 DivUp(UINT32 a, UINT32 b) { return (a + b - 1) / b; }
UINT32 GetBytesPerBlock(DXGI_FORMAT fmt)
{
    switch (fmt) {
    case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC4_UNORM: return 8;
    case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC5_UNORM: return 16;
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

struct TextureDesc { UINT32 pitch = 0, mipmapsCount = 0; DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN; UINT32 width = 0, height = 0; void* pData = nullptr; };

bool LoadDDS(const wchar_t* filename, TextureDesc& desc, std::vector<void*>* pMipData = nullptr, std::vector<UINT32>* pMipPitches = nullptr)
{
    HANDLE hFile = CreateFileW(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
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

    // общий размер данных всех мипов
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

    // все мипы за один раз
    void* pAllData = malloc(totalSize);
    if (!pAllData) { CloseHandle(hFile); return false; }
    ReadFile(hFile, pAllData, totalSize, &dwBytesRead, NULL);
    CloseHandle(hFile);

    // отдельные указатели на каждый мип
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
// Типы вершин
// ------------------------------------------------------------------
struct TextureNormalTangentVertex
{
    XMFLOAT3 pos;
    XMFLOAT3 normal;
    XMFLOAT3 tangent;
    XMFLOAT2 uv;
};
struct TexturedVertex { XMFLOAT3 pos; XMFLOAT2 uv; };

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
ID3D11Buffer* g_pVertexBuffer = nullptr;
ID3D11Buffer* g_pIndexBuffer = nullptr;
ID3D11Buffer* g_pSkyboxVertexBuffer = nullptr;
ID3D11Buffer* g_pSkyboxIndexBuffer = nullptr;

// Шейдеры для куба (оригинальные, для возможного использования)
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

// Состояния
ID3D11RasterizerState* g_pRSCullNone = nullptr;
ID3D11RasterizerState* g_pRSCullBack = nullptr;

UINT g_ClientWidth = 1280;
UINT g_ClientHeight = 720;

// Управление камерой
float g_CameraYaw = 0.0f;
float g_CameraPitch = 0.3f;
float g_CameraDist = 5.0f;
bool g_KeyLeft = false, g_KeyRight = false, g_KeyUp = false, g_KeyDown = false;
double g_LastTime = 0.0;

// ------------------------------------------------------------------
// Instancing
// ------------------------------------------------------------------
struct GeomBuffer
{
    XMMATRIX model;
    XMMATRIX norm;
    XMFLOAT4 shineSpeedTexIdNM; // x=shininess, y=rot speed, z=texture id, w=normal map presence
    XMFLOAT4 angle; // xyz=position, w=current angle
};
ID3D11Buffer* g_pGeomBufferInst = nullptr;      // массив данных экземпляров
ID3D11Buffer* g_pVisibleIdsBuffer = nullptr;   // буфер видимых ID (uint4)
GeomBuffer g_Instances[MAX_INSTANCES];
UINT g_InstanceCount = 0;
XMVECTOR g_LocalAABBMin = XMVectorSet(-0.5f, -0.5f, -0.5f, 1.0f);
XMVECTOR g_LocalAABBMax = XMVectorSet(0.5f, 0.5f, 0.5f, 1.0f);

// Шейдеры для instanced
ID3D11VertexShader* g_pInstancedVS = nullptr;
ID3D11PixelShader* g_pInstancedPS = nullptr;
ID3D11InputLayout* g_pInstancedInputLayout = nullptr;

// Массив текстур
ID3D11ShaderResourceView* g_pTextureArrayView = nullptr;

// ------------------------------------------------------------------
// Постпроцессинг
// ------------------------------------------------------------------
ID3D11Texture2D* g_pColorBuffer = nullptr;
ID3D11RenderTargetView* g_pColorBufferRTV = nullptr;
ID3D11ShaderResourceView* g_pColorBufferSRV = nullptr;
bool g_UseFilter = true;   // включен фильтр (оттенки серого)
ID3D11VertexShader* g_pFilterVS = nullptr;
ID3D11PixelShader* g_pFilterPS = nullptr;

// ------------------------------------------------------------------
// GPU Frustum Culling
// ------------------------------------------------------------------
ID3D11ComputeShader* g_pCullCS = nullptr;
ID3D11Buffer* g_pIndirectArgsUAV = nullptr;
ID3D11Buffer* g_pIndirectArgsDraw = nullptr;
ID3D11UnorderedAccessView* g_pIndirectArgsUAVView = nullptr;
ID3D11Buffer* g_pVisibleIdsStructured = nullptr;
ID3D11UnorderedAccessView* g_pVisibleIdsUAV = nullptr;
ID3D11ShaderResourceView* g_pVisibleIdsSRV = nullptr;
ID3D11Buffer* g_pCullParamsCB = nullptr;
ID3D11Buffer* g_pFrustumPlanesCB = nullptr;

struct CullParams {
    UINT   numInstances;
    UINT   padding[3];
    XMFLOAT4 bbMin[MAX_INSTANCES];
    XMFLOAT4 bbMax[MAX_INSTANCES];
};
CullParams g_cullParams;

ID3D11Query* g_pQueries[10] = {};
UINT         g_curFrame = 0;
UINT         g_lastCompletedFrame = 0;
int          g_gpuVisibleInstances = 0;
bool         g_useGPUculling = true;

// ------------------------------------------------------------------
// Прототипы
// ------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
bool InitDirectX();
void CreateCubeResources();
void CompileShaders();
void LoadTextures();
void LoadTextureArray();
void CreateInstances();
void CleanupDirectX();
void RenderFrame();
void OnResize(UINT newWidth, UINT newHeight);
void UpdateCamera(double deltaTime);
void SetupColorBuffer(UINT width, UINT height);
void BuildFrustumPlanes(const XMMATRIX& vp, XMVECTOR planes[6]);
void TransformAABB(const XMMATRIX& transform, const XMVECTOR& localMin, const XMVECTOR& localMax, XMVECTOR& worldMin, XMVECTOR& worldMax);
bool IsAABBInsideFrustum(const XMVECTOR planes[6], const XMVECTOR& aabbMin, const XMVECTOR& aabbMax);
void UpdateInstanceTransforms(double time);
void CreateGPUResources();


#define SAFE_RELEASE(p) if (p) { (p)->Release(); (p) = nullptr; }
inline HRESULT SetResourceName(ID3D11DeviceChild* pResource, const std::string& name) { return pResource->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)name.length(), name.c_str()); }

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
    wc.lpszClassName = L"DX11Lab10Class";

    if (!RegisterClassExW(&wc)) { MessageBoxW(nullptr, L"RegisterClassEx failed", L"Error", MB_OK | MB_ICONERROR); return 0; }

    RECT rc = { 0, 0, (LONG)g_ClientWidth, (LONG)g_ClientHeight };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    int winWidth = rc.right - rc.left;
    int winHeight = rc.bottom - rc.top;

    g_hWnd = CreateWindowW(wc.lpszClassName, L"Lab 7 - Instancing",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        winWidth, winHeight, nullptr, nullptr, hInstance, nullptr);
    if (!g_hWnd) { MessageBoxW(nullptr, L"CreateWindow failed", L"Error", MB_OK | MB_ICONERROR); return 0; }

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    if (!InitDirectX()) { CleanupDirectX(); DestroyWindow(g_hWnd); return -1; }

    CreateCubeResources();
    CompileShaders();
    LoadTextures();
    LoadTextureArray();
    CreateInstances();
    SetupColorBuffer(g_ClientWidth, g_ClientHeight);
    CreateGPUResources();

    // Создание константных буферов
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(ModelBuffer);
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    HRESULT hr = g_pDevice->CreateBuffer(&desc, nullptr, &g_pModelBuffer1);
    if (FAILED(hr)) { char buf[256]; sprintf_s(buf, "CreateBuffer(ModelBuffer1) failed: 0x%08X", (unsigned)hr); MessageBoxA(NULL, buf, "Error", MB_OK | MB_ICONERROR); CleanupDirectX(); DestroyWindow(g_hWnd); return -1; }
    hr = g_pDevice->CreateBuffer(&desc, nullptr, &g_pModelBuffer2);
    if (FAILED(hr)) { char buf[256]; sprintf_s(buf, "CreateBuffer(ModelBuffer2) failed: 0x%08X", (unsigned)hr); MessageBoxA(NULL, buf, "Error", MB_OK | MB_ICONERROR); CleanupDirectX(); DestroyWindow(g_hWnd); return -1; }

    desc.ByteWidth = sizeof(ViewProjBuffer);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = g_pDevice->CreateBuffer(&desc, nullptr, &g_pViewProjBuffer);
    if (FAILED(hr)) { char buf[256]; sprintf_s(buf, "CreateBuffer(ViewProjBuffer) failed: 0x%08X", (unsigned)hr); MessageBoxA(NULL, buf, "Error", MB_OK | MB_ICONERROR); CleanupDirectX(); DestroyWindow(g_hWnd); return -1; }

    desc.ByteWidth = sizeof(SceneBuffer);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    hr = g_pDevice->CreateBuffer(&desc, nullptr, &g_pSceneBuffer);
    if (FAILED(hr)) { char buf[256]; sprintf_s(buf, "CreateBuffer(SceneBuffer) failed: 0x%08X", (unsigned)hr); MessageBoxA(NULL, buf, "Error", MB_OK | MB_ICONERROR); CleanupDirectX(); DestroyWindow(g_hWnd); return -1; }

    desc.ByteWidth = sizeof(GeomBuffer) * MAX_INSTANCES;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = 0; // ensure no CPU access for DEFAULT usage
    desc.MiscFlags = 0;
    desc.StructureByteStride = 0;
    hr = g_pDevice->CreateBuffer(&desc, nullptr, &g_pGeomBufferInst);
    if (FAILED(hr)) { char buf[256]; sprintf_s(buf, "CreateBuffer(GeomBufferInst) failed: 0x%08X", (unsigned)hr); MessageBoxA(NULL, buf, "Error", MB_OK | MB_ICONERROR); CleanupDirectX(); DestroyWindow(g_hWnd); return -1; }

    desc.ByteWidth = sizeof(XMUINT4) * MAX_INSTANCES;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = g_pDevice->CreateBuffer(&desc, nullptr, &g_pVisibleIdsBuffer);
    if (FAILED(hr)) { char buf[256]; sprintf_s(buf, "CreateBuffer(VisibleIdsBuffer) failed: 0x%08X", (unsigned)hr); MessageBoxA(NULL, buf, "Error", MB_OK | MB_ICONERROR); CleanupDirectX(); DestroyWindow(g_hWnd); return -1; }

    SetResourceName(g_pModelBuffer1, "ModelBuffer1");
    SetResourceName(g_pModelBuffer2, "ModelBuffer2");
    SetResourceName(g_pViewProjBuffer, "ViewProjBuffer");
    SetResourceName(g_pSceneBuffer, "SceneBuffer");
    SetResourceName(g_pGeomBufferInst, "GeomBufferInst");
    SetResourceName(g_pVisibleIdsBuffer, "VisibleIdsBuffer");

    g_LastTime = (double)GetTickCount64() / 1000.0;

    MSG msg = {};
    bool done = false;
    while (!done)
    {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT) done = true;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!done) RenderFrame();
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
            if (newW > 0 && newH > 0) OnResize(newW, newH);
        }
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_LEFT)  g_KeyLeft = true;
        if (wParam == VK_RIGHT) g_KeyRight = true;
        if (wParam == VK_UP)    g_KeyUp = true;
        if (wParam == VK_DOWN)  g_KeyDown = true;
        return 0;
    case WM_KEYUP:
        if (wParam == VK_LEFT)  g_KeyLeft = false;
        if (wParam == VK_RIGHT) g_KeyRight = false;
        if (wParam == VK_UP)    g_KeyUp = false;
        if (wParam == VK_DOWN)  g_KeyDown = false;
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
    IDXGIAdapter* pFirstAdapter = nullptr;
    UINT idx = 0;
    while (true)
    {
        IDXGIAdapter* pAdapter = nullptr;
        hr = pFactory->EnumAdapters(idx, &pAdapter);
        if (FAILED(hr) || pAdapter == nullptr) break;
        if (!pFirstAdapter) pFirstAdapter = pAdapter; // keep first adapter
        DXGI_ADAPTER_DESC desc;
        pAdapter->GetDesc(&desc);
        if (wcscmp(desc.Description, L"Microsoft Basic Render Driver") != 0)
        {
            pSelectedAdapter = pAdapter;
            break;
        }
        // release only if not keeping as first
        if (pAdapter != pFirstAdapter) pAdapter->Release();
        idx++;
    }
    if (!pSelectedAdapter) pSelectedAdapter = pFirstAdapter;
    if (!pSelectedAdapter) { pFactory->Release(); return false; }

    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL obtainedLevel;
    hr = D3D11CreateDevice(pSelectedAdapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, levels, 1, D3D11_SDK_VERSION, &g_pDevice, &obtainedLevel, &g_pDeviceContext);
    pSelectedAdapter->Release();
    if (FAILED(hr) || obtainedLevel != D3D_FEATURE_LEVEL_11_0) { pFactory->Release(); return false; }

    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 2;
    scd.BufferDesc.Width = g_ClientWidth;
    scd.BufferDesc.Height = g_ClientHeight;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 0;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = g_hWnd;
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    hr = pFactory->CreateSwapChain(g_pDevice, &scd, &g_pSwapChain);
    pFactory->Release();
    if (FAILED(hr)) return false;

    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    if (FAILED(hr)) return false;

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
         0,  2,  1,  0,  3,  2,  4,  5,  6,  4,  6,  7,  8, 10,  9,  8, 11, 10,
        12, 14, 13, 12, 15, 14, 16, 18, 17, 16, 19, 18, 20, 22, 21, 20, 23, 22
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

    // Skybox
    const TexturedVertex skyboxVertices[] = {
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
// Компиляция шейдеров (добавлены instanced и фильтр)
// ------------------------------------------------------------------
void CompileShaders()
{
    // Шейдеры для обычного куба
    const char* vsCode = R"(
        cbuffer ModelBuffer : register(b0) { float4x4 model; }
        cbuffer ViewProjBuffer : register(b1) { float4x4 vp; }
        struct VSInput { float3 pos : POSITION; float3 normal : NORMAL; float3 tangent : TANGENT; float2 uv : TEXCOORD; };
        struct VSOutput { float4 pos : SV_Position; float3 worldPos : TEXCOORD0; float3 worldNormal : NORMAL; float3 worldTangent : TANGENT; float2 uv : TEXCOORD1; };
        VSOutput vs(VSInput v) {
            VSOutput o; float4 worldPos = mul(float4(v.pos, 1.0), model); o.pos = mul(worldPos, vp); o.worldPos = worldPos.xyz;
            float3x3 normalMatrix = (float3x3)model; o.worldNormal = normalize(mul(v.normal, normalMatrix)); o.worldTangent = normalize(mul(v.tangent, normalMatrix)); o.uv = v.uv; return o;
        }
    )";
    const char* psCode = R"(
        Texture2D colorTexture : register(t0); Texture2D normalMap : register(t1); SamplerState colorSampler : register(s0);
        cbuffer SceneCB : register(b2) { float4x4 vp; float4 cameraPos; float4 lightCount; struct Light { float4 pos; float4 color; } lights[10]; float4 ambientColor; };
        struct VSOutput { float4 pos : SV_Position; float3 worldPos : TEXCOORD0; float3 worldNormal : NORMAL; float3 worldTangent : TANGENT; float2 uv : TEXCOORD1; };
        float4 ps(VSOutput p) : SV_Target0 {
            float4 texColor = colorTexture.Sample(colorSampler, p.uv);
            float3 tangentNormal = normalMap.Sample(colorSampler, p.uv).xyz * 2.0 - 1.0;
            float3 N = normalize(p.worldNormal); float3 T = normalize(p.worldTangent); float3 B = cross(N, T);
            float3x3 TBN = float3x3(T, B, N); float3 worldNormal = normalize(mul(tangentNormal, TBN));
            float3 finalColor = ambientColor.xyz * texColor.xyz;
            for (int i = 0; i < lightCount.x; ++i) {
                float3 L = lights[i].pos.xyz - p.worldPos.xyz; float dist = length(L); L = L / dist;
                float atten = 1.0 / (1.0 + 0.1*dist + 0.01*dist*dist);
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
    const char* skyboxVS = R"(
        cbuffer ViewProjBuffer : register(b1) { float4x4 vp; }
        struct VSInput { float3 pos : POSITION; float2 uv : TEXCOORD; };
        struct VSOutput { float4 pos : SV_Position; float3 localPos : TEXCOORD; };
        VSOutput vs(VSInput v) { VSOutput o; o.pos = mul(float4(v.pos, 1.0), vp); o.localPos = v.pos; return o; }
    )";
    const char* skyboxPS = R"(
        TextureCube skyboxTexture : register(t1); SamplerState skyboxSampler : register(s1);
        struct VSOutput { float4 pos : SV_Position; float3 localPos : TEXCOORD; };
        float4 ps(VSOutput p) : SV_Target0 { return skyboxTexture.Sample(skyboxSampler, p.localPos); }
    )";

    // Instanced вершинный шейдер
    const char* instancedVS = R"(
        cbuffer GeomBufferInst : register(b1)
        {
            struct GeomBuffer
            {
                float4x4 model;
                float4x4 norm;
                float4 shineSpeedTexIdNM; // x=shininess, y=rot speed, z=tex id, w=normal map presence
                float4 angle;
            } geomBuffer[100];
        };
        cbuffer ViewProjCB : register(b2)
        {
            float4x4 vp;
        };
        StructuredBuffer<uint4> visibleIds : register(t2);
        struct VSInput
        {
            float3 pos    : POSITION;
            float3 tang   : TANGENT;
            float3 norm   : NORMAL;
            float2 uv     : TEXCOORD;
            uint instanceId : SV_InstanceID;
        };
        struct VSOutput
        {
            float4 pos       : SV_Position;
            float4 worldPos  : POSITION;
            float3 tang      : TANGENT;
            float3 norm      : NORMAL;
            float2 uv        : TEXCOORD;
            nointerpolation uint instanceId : INST_ID;
        };
        VSOutput vs(VSInput v)
        {
            VSOutput o;
            uint globalIdx = visibleIds[v.instanceId].x;   
            float4 worldPos = mul(geomBuffer[globalIdx].model, float4(v.pos, 1.0));
            o.pos = mul(worldPos, vp);
            o.worldPos = worldPos;
            o.uv = v.uv;
            o.tang = mul(geomBuffer[globalIdx].norm, float4(v.tang, 0)).xyz;
            o.norm = mul(geomBuffer[globalIdx].norm, float4(v.norm, 0)).xyz;
            o.instanceId = v.instanceId; 
            return o;
        }
    )";

    // Instanced пиксельный шейдер (с поддержкой текстурного массива и normal map)
    const char* instancedPS = R"(
        Texture2DArray colorTexture : register(t0);
        Texture2D normalMapTexture : register(t1);
        SamplerState colorSampler : register(s0);
        cbuffer GeomBufferInst : register(b1)
        {
            struct GeomBuffer
            {
                float4x4 model;
                float4x4 norm;
                float4 shineSpeedTexIdNM;
                float4 angle;
            } geomBuffer[100];
        };
        cbuffer SceneCB : register(b3)
        {
            float4x4 vp;
            float4 cameraPos;
            float4 lightCount;
            struct Light
            {
                float4 pos;
                float4 color;
            } lights[10];
            float4 ambientColor;
        };
        StructuredBuffer<uint4> visibleIds : register(t2);
        struct VSOutput
        {
            float4 pos       : SV_Position;
            float4 worldPos  : POSITION;
            float3 tang      : TANGENT;
            float3 norm      : NORMAL;
            float2 uv        : TEXCOORD;
            nointerpolation uint instanceId : INST_ID;
        };
        float4 ps(VSOutput pixel) : SV_Target0
        {
            uint idx = visibleIds[pixel.instanceId].x;
            uint texId = (uint)geomBuffer[idx].shineSpeedTexIdNM.z;
            float3 color = colorTexture.Sample(colorSampler, float3(pixel.uv, texId)).xyz;
            uint flags = asuint(geomBuffer[idx].shineSpeedTexIdNM.w);
            float3 normal;
            if (flags == 1 && lightCount.y > 0)
            {
                float3 tangentNormal = normalMapTexture.Sample(colorSampler, pixel.uv).xyz * 2.0 - 1.0;
                float3 N = normalize(pixel.norm);
                float3 T = normalize(pixel.tang);
                float3 B = cross(N, T);
                normal = normalize(tangentNormal.x * T + tangentNormal.y * B + tangentNormal.z * N);
            }
            else
            {
                normal = normalize(pixel.norm);
            }
            float shininess = geomBuffer[idx].shineSpeedTexIdNM.x;
            float3 finalColor = ambientColor.xyz * color;
            for (int i = 0; i < lightCount.x; ++i)
            {
                float3 L = lights[i].pos.xyz - pixel.worldPos.xyz;
                float dist = length(L);
                L = L / dist;
                float atten = 1.0 / (1.0 + 0.1 * dist + 0.01 * dist * dist);
                float diff = max(dot(normal, L), 0.0);
                finalColor += color * diff * atten * lights[i].color.xyz;
                float3 V = normalize(cameraPos.xyz - pixel.worldPos.xyz);
                float3 R = reflect(-L, normal);
                float spec = pow(max(dot(V, R), 0.0), shininess);
                finalColor += spec * atten * lights[i].color.xyz;
            }
            return float4(finalColor, 1.0);
        }
    )";

    // Шейдеры для постпроцессинга (оттенки серого)
    const char* filterVS = R"(
        struct VSInput { uint vertexId : SV_VertexID; };
        struct VSOutput { float4 pos : SV_Position; float2 uv : TEXCOORD; };
        VSOutput vs(VSInput v) {
            VSOutput o;
            float4 pos = float4(0,0,0,0);
            switch (v.vertexId) {
                case 0: pos = float4(-1, 1, 0, 1); break;
                case 1: pos = float4( 3, 1, 0, 1); break;
                case 2: pos = float4(-1,-3, 0, 1); break;
            }
            o.pos = pos;
            o.uv = float2(pos.x * 0.5 + 0.5, 0.5 - pos.y * 0.5);
            return o;
        }
    )";
    const char* filterPS = R"(
        Texture2D colorTexture : register(t0);
        SamplerState colorSampler : register(s0);
        struct VSOutput { float4 pos : SV_Position; float2 uv : TEXCOORD; };
        float4 ps(VSOutput i) : SV_Target0 {
            float3 color = colorTexture.Sample(colorSampler, i.uv).rgb;
            float gray = dot(color, float3(0.299, 0.587, 0.114));
            return float4(gray, gray, gray, 1.0);
        }
    )";
    const char* cullCS = R"(
        cbuffer FrustumPlanes : register(b0) {
            float4 planes[6];
        };
        cbuffer CullParams : register(b1) {
            uint   numInstances;
            float4 bbMin[20];
            float4 bbMax[20];
        };
        RWStructuredBuffer<uint> indirectArgs : register(u0);
        RWStructuredBuffer<uint4> visibleIds : register(u1);

        bool IsBoxInside(float4 frustum[6], float3 bmin, float3 bmax) {
            for (int i = 0; i < 6; ++i) {
                float3 n = frustum[i].xyz;
                float3 p;
                p.x = n.x < 0 ? bmin.x : bmax.x;
                p.y = n.y < 0 ? bmin.y : bmax.y;
                p.z = n.z < 0 ? bmin.z : bmax.z;
                if (dot(float4(p, 1.0), frustum[i]) < 0.0f)
                    return false;
            }
            return true;
        }

        [numthreads(64, 1, 1)]
        void cs(uint3 tid : SV_DispatchThreadID) {
            if (tid.x >= numInstances) return;
            if (IsBoxInside(planes, bbMin[tid.x].xyz, bbMax[tid.x].xyz)) {
                uint id;
                InterlockedAdd(indirectArgs[1], 1, id); // InstanceCount
                visibleIds[id] = uint4(tid.x, 0, 0, 0);
            }
        }
    )";

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ID3DBlob* pVsBlob = nullptr, * pPsBlob = nullptr, * pErrorBlob = nullptr;

    // Обычные шейдеры
    D3DCompile(vsCode, strlen(vsCode), nullptr, nullptr, nullptr, "vs", "vs_5_0", flags, 0, &pVsBlob, &pErrorBlob);
    if (pErrorBlob) { OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer()); pErrorBlob->Release(); }
    g_pDevice->CreateVertexShader(pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), nullptr, &g_pVertexShader);
    D3DCompile(psCode, strlen(psCode), nullptr, nullptr, nullptr, "ps", "ps_5_0", flags, 0, &pPsBlob, &pErrorBlob);
    if (pErrorBlob) { OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer()); pErrorBlob->Release(); }
    g_pDevice->CreatePixelShader(pPsBlob->GetBufferPointer(), pPsBlob->GetBufferSize(), nullptr, &g_pPixelShader);
    SAFE_RELEASE(pPsBlob);

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    g_pDevice->CreateInputLayout(layout, 4, pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), &g_pInputLayout);
    SAFE_RELEASE(pVsBlob);

    // Instanced шейдеры
    D3DCompile(instancedVS, strlen(instancedVS), nullptr, nullptr, nullptr, "vs", "vs_5_0", flags, 0, &pVsBlob, &pErrorBlob);
    if (pErrorBlob) { OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer()); pErrorBlob->Release(); }
    g_pDevice->CreateVertexShader(pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), nullptr, &g_pInstancedVS);
    D3DCompile(instancedPS, strlen(instancedPS), nullptr, nullptr, nullptr, "ps", "ps_5_0", flags, 0, &pPsBlob, &pErrorBlob);
    if (pErrorBlob) { OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer()); pErrorBlob->Release(); }
    g_pDevice->CreatePixelShader(pPsBlob->GetBufferPointer(), pPsBlob->GetBufferSize(), nullptr, &g_pInstancedPS);
    SAFE_RELEASE(pPsBlob);

    D3D11_INPUT_ELEMENT_DESC layoutInst[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };

    g_pDevice->CreateInputLayout(layoutInst, 4, pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), &g_pInstancedInputLayout);
    SAFE_RELEASE(pVsBlob);

    // Skybox
    D3DCompile(skyboxVS, strlen(skyboxVS), nullptr, nullptr, nullptr, "vs", "vs_5_0", flags, 0, &pVsBlob, &pErrorBlob);
    if (pErrorBlob) { OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer()); pErrorBlob->Release(); }
    g_pDevice->CreateVertexShader(pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), nullptr, &g_pSkyboxVS);
    D3DCompile(skyboxPS, strlen(skyboxPS), nullptr, nullptr, nullptr, "ps", "ps_5_0", flags, 0, &pPsBlob, &pErrorBlob);
    if (pErrorBlob) { OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer()); pErrorBlob->Release(); }
    g_pDevice->CreatePixelShader(pPsBlob->GetBufferPointer(), pPsBlob->GetBufferSize(), nullptr, &g_pSkyboxPS);
    SAFE_RELEASE(pPsBlob);

    D3D11_INPUT_ELEMENT_DESC layoutSky[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    g_pDevice->CreateInputLayout(layoutSky, 2, pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), &g_pSkyboxInputLayout);
    SAFE_RELEASE(pVsBlob);
    SAFE_RELEASE(pPsBlob);

    // Шейдеры фильтра
    D3DCompile(filterVS, strlen(filterVS), nullptr, nullptr, nullptr, "vs", "vs_5_0", flags, 0, &pVsBlob, &pErrorBlob);
    if (pErrorBlob) { OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer()); pErrorBlob->Release(); }
    if (pVsBlob) { g_pDevice->CreateVertexShader(pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), nullptr, &g_pFilterVS); SAFE_RELEASE(pVsBlob); }
    D3DCompile(filterPS, strlen(filterPS), nullptr, nullptr, nullptr, "ps", "ps_5_0", flags, 0, &pPsBlob, &pErrorBlob);
    if (pErrorBlob) { OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer()); pErrorBlob->Release(); }
    if (pPsBlob) { g_pDevice->CreatePixelShader(pPsBlob->GetBufferPointer(), pPsBlob->GetBufferSize(), nullptr, &g_pFilterPS); SAFE_RELEASE(pPsBlob); }

    ID3DBlob* pCSBlob = nullptr;
    HRESULT hrCS = D3DCompile(cullCS, strlen(cullCS), nullptr, nullptr, nullptr, "cs", "cs_5_0", flags, 0, &pCSBlob, &pErrorBlob);
    if (FAILED(hrCS)) {
        if (pErrorBlob) OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer());
        assert(false);
    }
    hrCS = g_pDevice->CreateComputeShader(pCSBlob->GetBufferPointer(), pCSBlob->GetBufferSize(), nullptr, &g_pCullCS);
    assert(SUCCEEDED(hrCS));
    SAFE_RELEASE(pCSBlob);
    SAFE_RELEASE(pErrorBlob);

}

// ------------------------------------------------------------------
// Загрузка текстур (brick.dds, brick_normal.dds, skybox)
// ------------------------------------------------------------------
void LoadTextures()
{
    HRESULT hr;
    std::wstring basePath = GetExePath() + L"..\\..\\textures\\";
    std::wstring colorTexPath = basePath + L"brick.dds";
    std::wstring normalTexPath = basePath + L"brick_normal.dds";

    // Загрузка color map с мипами
    TextureDesc texDesc;
    std::vector<void*> mipData;
    std::vector<UINT32> mipPitches;
    if (!LoadDDS(colorTexPath.c_str(), texDesc, &mipData, &mipPitches))
    {
        MessageBoxA(NULL, "Failed to load brick.dds", "Error", MB_OK);
        return;
    }

    D3D11_TEXTURE2D_DESC tex2DDesc = {};
    tex2DDesc.Width = texDesc.width;
    tex2DDesc.Height = texDesc.height;
    tex2DDesc.MipLevels = texDesc.mipmapsCount;
    tex2DDesc.ArraySize = 1;
    tex2DDesc.Format = texDesc.fmt;
    tex2DDesc.SampleDesc.Count = 1;
    tex2DDesc.Usage = D3D11_USAGE_IMMUTABLE;
    tex2DDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    std::vector<D3D11_SUBRESOURCE_DATA> initData(texDesc.mipmapsCount);
    for (UINT32 i = 0; i < texDesc.mipmapsCount; ++i)
    {
        initData[i].pSysMem = mipData[i];
        initData[i].SysMemPitch = mipPitches[i];
        initData[i].SysMemSlicePitch = 0;
    }

    ID3D11Texture2D* pTexture = nullptr;
    hr = g_pDevice->CreateTexture2D(&tex2DDesc, initData.data(), &pTexture);
    free(texDesc.pData);
    if (FAILED(hr)) return;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.fmt;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = texDesc.mipmapsCount;
    hr = g_pDevice->CreateShaderResourceView(pTexture, &srvDesc, &g_pTextureView);
    pTexture->Release();
    if (FAILED(hr)) return;

    // Загрузка normal map с мипами
    TextureDesc normalDesc;
    std::vector<void*> normalMipData;
    std::vector<UINT32> normalMipPitches;
    if (LoadDDS(normalTexPath.c_str(), normalDesc, &normalMipData, &normalMipPitches))
    {
        D3D11_TEXTURE2D_DESC normTexDesc = {};
        normTexDesc.Width = normalDesc.width;
        normTexDesc.Height = normalDesc.height;
        normTexDesc.MipLevels = normalDesc.mipmapsCount;
        normTexDesc.ArraySize = 1;
        normTexDesc.Format = normalDesc.fmt;
        normTexDesc.SampleDesc.Count = 1;
        normTexDesc.Usage = D3D11_USAGE_IMMUTABLE;
        normTexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        std::vector<D3D11_SUBRESOURCE_DATA> nInitData(normalDesc.mipmapsCount);
        for (UINT32 i = 0; i < normalDesc.mipmapsCount; ++i)
        {
            nInitData[i].pSysMem = normalMipData[i];
            nInitData[i].SysMemPitch = normalMipPitches[i];
            nInitData[i].SysMemSlicePitch = 0;
        }

        ID3D11Texture2D* pNormalTex = nullptr;
        hr = g_pDevice->CreateTexture2D(&normTexDesc, nInitData.data(), &pNormalTex);
        if (SUCCEEDED(hr))
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC nSrvDesc = {};
            nSrvDesc.Format = normalDesc.fmt;
            nSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            nSrvDesc.Texture2D.MipLevels = normalDesc.mipmapsCount;
            hr = g_pDevice->CreateShaderResourceView(pNormalTex, &nSrvDesc, &g_pNormalMapView);
            pNormalTex->Release();
        }
        free(normalDesc.pData);
    }

    // Создание сэмплера с поддержкой мипов
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
    g_pDevice->CreateSamplerState(&sampDesc, &g_pSampler);

    // Cubemap
    std::wstring skyboxPath = basePath + L"skybox\\";
    std::wstring faceNames[6] = {
        skyboxPath + L"posx.dds", skyboxPath + L"negx.dds", skyboxPath + L"posy.dds",
        skyboxPath + L"negy.dds", skyboxPath + L"posz.dds", skyboxPath + L"negz.dds"
    };

    TextureDesc faceDescs[6];
    bool allOk = true;
    for (int i = 0; i < 6; ++i)
        if (!LoadDDS(faceNames[i].c_str(), faceDescs[i]))
        {
            allOk = false; break;
        }

    if (!allOk) { MessageBoxA(NULL, "Failed to load cubemap faces", "Error", MB_OK); return; }

    D3D11_TEXTURE2D_DESC cubeDesc = {};
    cubeDesc.Width = faceDescs[0].width;
    cubeDesc.Height = faceDescs[0].height;
    cubeDesc.MipLevels = 1;
    cubeDesc.ArraySize = 6;
    cubeDesc.Format = faceDescs[0].fmt;
    cubeDesc.SampleDesc.Count = 1;
    cubeDesc.Usage = D3D11_USAGE_IMMUTABLE;
    cubeDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    cubeDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

    UINT32 blockWidth = DivUp(cubeDesc.Width, 4u);
    UINT32 blockHeight = DivUp(cubeDesc.Height, 4u);
    UINT32 pitch = blockWidth * GetBytesPerBlock(cubeDesc.Format);

    std::vector<D3D11_SUBRESOURCE_DATA> cubeInitData(6);
    for (int i = 0; i < 6; ++i)
    {
        cubeInitData[i].pSysMem = faceDescs[i].pData;
        cubeInitData[i].SysMemPitch = pitch;
        cubeInitData[i].SysMemSlicePitch = 0;
    }

    ID3D11Texture2D* pCubemapTex = nullptr;
    hr = g_pDevice->CreateTexture2D(&cubeDesc, cubeInitData.data(), &pCubemapTex);

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

    // Rasterizer states
    D3D11_RASTERIZER_DESC rsDesc = {};
    rsDesc.FillMode = D3D11_FILL_SOLID;
    rsDesc.CullMode = D3D11_CULL_BACK;
    rsDesc.FrontCounterClockwise = FALSE;
    g_pDevice->CreateRasterizerState(&rsDesc, &g_pRSCullBack);
    rsDesc.CullMode = D3D11_CULL_NONE;
    g_pDevice->CreateRasterizerState(&rsDesc, &g_pRSCullNone);
}

// ------------------------------------------------------------------
// Создание массива текстур для instancing
// ------------------------------------------------------------------
void LoadTextureArray()
{
    std::vector<TextureDesc> texDescs(NUM_TEXTURES);
    std::vector<std::vector<void*>> mipDataPerTex(NUM_TEXTURES);
    std::vector<std::vector<UINT32>> mipPitchesPerTex(NUM_TEXTURES);
    std::vector<bool> loaded(NUM_TEXTURES, false);
    std::wstring basePath = GetExePath() + L"..\\..\\textures\\";

    for (UINT i = 0; i < NUM_TEXTURES; ++i)
    {
        std::wstring candidates[] = {
            basePath + TEXTURE_NAMES[i],
            GetExePath() + TEXTURE_NAMES[i],
            TEXTURE_NAMES[i]
        };
        for (auto& p : candidates)
        {
            if (LoadDDS(p.c_str(), texDescs[i], &mipDataPerTex[i], &mipPitchesPerTex[i]))
            {
                loaded[i] = true;
                break;
            }
        }
    }

    bool anyLoaded = false;
    for (bool b : loaded) if (b) { anyLoaded = true; break; }
    if (!anyLoaded) { MessageBoxA(NULL, "Failed to load textures for array", "Error", MB_OK); return; }

    int firstIdx = -1;
    for (UINT i = 0; i < NUM_TEXTURES; ++i) if (loaded[i]) { firstIdx = (int)i; break; }

    // Если какая-то текстура не загрузилась, дублирую первую успешную (включая мипы)
    for (UINT i = 0; i < NUM_TEXTURES; ++i)
    {
        if (!loaded[i])
        {
            TextureDesc& src = texDescs[firstIdx];
            TextureDesc& dst = texDescs[i];
            dst.width = src.width;
            dst.height = src.height;
            dst.fmt = src.fmt;
            dst.mipmapsCount = src.mipmapsCount;
            // Копируем данные мипов
            UINT totalSize = 0;
            for (UINT32 mip = 0; mip < src.mipmapsCount; ++mip)
            {
                UINT32 w = max(src.width >> mip, 1u);
                UINT32 h = max(src.height >> mip, 1u);
                UINT32 blockW = DivUp(w, 4u);
                UINT32 blockH = DivUp(h, 4u);
                UINT32 mipSize = blockW * GetBytesPerBlock(src.fmt) * blockH;
                totalSize += mipSize;
            }
            dst.pData = malloc(totalSize);
            if (dst.pData && src.pData) memcpy(dst.pData, src.pData, totalSize);
            // Копируем векторы указателей на мипы
            mipDataPerTex[i] = mipDataPerTex[firstIdx];
            mipPitchesPerTex[i] = mipPitchesPerTex[firstIdx];
            loaded[i] = true;
        }
    }

    // Проверка совпадения форматов и размеров (с учётом числа мипов)
    DXGI_FORMAT fmt = texDescs[firstIdx].fmt;
    UINT width = texDescs[firstIdx].width, height = texDescs[firstIdx].height;
    UINT mipCount = texDescs[firstIdx].mipmapsCount;
    for (UINT i = 0; i < NUM_TEXTURES; ++i)
    {
        if (texDescs[i].fmt != fmt || texDescs[i].width != width ||
            texDescs[i].height != height || texDescs[i].mipmapsCount != mipCount)
        {
            for (auto& td : texDescs) if (td.pData) free(td.pData);
            MessageBoxA(NULL, "Textures must have same format, size and mipmap count", "Error", MB_OK);
            return;
        }
    }

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = mipCount;
    texDesc.ArraySize = NUM_TEXTURES;
    texDesc.Format = fmt;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_IMMUTABLE;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    // Подготавливаем SubresourceData для каждого элемента массива и каждого мипа
    UINT totalSubresources = NUM_TEXTURES * mipCount;
    std::vector<D3D11_SUBRESOURCE_DATA> initData(totalSubresources);

    UINT idx = 0;
    for (UINT tex = 0; tex < NUM_TEXTURES; ++tex)
    {
        for (UINT32 mip = 0; mip < mipCount; ++mip)
        {
            initData[idx].pSysMem = mipDataPerTex[tex][mip];
            initData[idx].SysMemPitch = mipPitchesPerTex[tex][mip];
            initData[idx].SysMemSlicePitch = 0;
            ++idx;
        }
    }

    ID3D11Texture2D* pTexArray = nullptr;
    HRESULT hr = g_pDevice->CreateTexture2D(&texDesc, initData.data(), &pTexArray);

    // Освобождаем выделенную память (каждый TextureDesc.pData указывает на начало блока всех мипов)
    for (auto& td : texDescs) if (td.pData) free(td.pData);

    if (FAILED(hr)) return;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = fmt;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Texture2DArray.MipLevels = mipCount;
    srvDesc.Texture2DArray.ArraySize = NUM_TEXTURES;
    srvDesc.Texture2DArray.FirstArraySlice = 0;

    g_pDevice->CreateShaderResourceView(pTexArray, &srvDesc, &g_pTextureArrayView);
    pTexArray->Release();
}

// ------------------------------------------------------------------
// Создание данных для экземпляров (расположение по сфере)
// ------------------------------------------------------------------
void CreateInstances()
{
    g_InstanceCount = MAX_INSTANCES;
    float radius = 3.0f;
    for (UINT i = 0; i < MAX_INSTANCES; ++i)
    {
        // Золотое сечение для равномерного распределения по сфере
        float phi = XM_PI * (3.0f - sqrtf(5.0f));
        float y = 1.0f - (i / (float)(MAX_INSTANCES - 1)) * 2.0f;
        float radiusAtY = sqrtf(1.0f - y * y);
        float theta = i * phi * 2.0f * XM_PI;
        float x = cosf(theta) * radiusAtY;
        float z = sinf(theta) * radiusAtY;
        XMFLOAT3 pos(x * radius, y * radius, z * radius);

        XMMATRIX model = XMMatrixTranslation(pos.x, pos.y, pos.z);
        XMMATRIX norm = XMMatrixTranspose(XMMatrixInverse(nullptr, model));
        g_Instances[i].model = model;
        g_Instances[i].norm = norm;

        int texId = i % NUM_TEXTURES;   // чередуем текстуры
        float shininess = 32.0f;
        float rotSpeed = 0.5f + (rand() % 100) / 100.0f;
        float normalMapPresence = (texId == 0) ? 1.0f : 0.0f; // только первая текстура имеет normal map
        g_Instances[i].shineSpeedTexIdNM = XMFLOAT4(shininess, rotSpeed, (float)texId, normalMapPresence);
        g_Instances[i].angle = XMFLOAT4(pos.x, pos.y, pos.z, 0.0f);
    }
}

void UpdateInstanceTransforms(double time)
{
    for (UINT i = 0; i < g_InstanceCount; ++i)
    {
        float angle = (float)time * g_Instances[i].shineSpeedTexIdNM.y;
        XMMATRIX rot = XMMatrixRotationY(angle);
        XMMATRIX trans = XMMatrixTranslation(g_Instances[i].angle.x, g_Instances[i].angle.y, g_Instances[i].angle.z);
        g_Instances[i].model = rot * trans;
        g_Instances[i].norm = XMMatrixTranspose(XMMatrixInverse(nullptr, g_Instances[i].model));
    }
}

void UpdateAABBBuffer()
{
    XMVECTOR localMin = XMVectorSet(-0.5f, -0.5f, -0.5f, 1.0f);
    XMVECTOR localMax = XMVectorSet(0.5f, 0.5f, 0.5f, 1.0f);
    g_cullParams.numInstances = g_InstanceCount;
    for (UINT i = 0; i < g_InstanceCount; ++i) {
        XMVECTOR worldMin, worldMax;
        TransformAABB(g_Instances[i].model, localMin, localMax, worldMin, worldMax);
        XMStoreFloat4(&g_cullParams.bbMin[i], worldMin);
        XMStoreFloat4(&g_cullParams.bbMax[i], worldMax);
    }
    g_pDeviceContext->UpdateSubresource(g_pCullParamsCB, 0, nullptr, &g_cullParams, 0, 0);
}

void UpdateFrustumPlanesCB(const XMMATRIX& vp)
{
    XMVECTOR planes[6];
    BuildFrustumPlanes(vp, planes);
    XMFLOAT4 planesCPU[6];
    for (int i = 0; i < 6; ++i)
        XMStoreFloat4(&planesCPU[i], planes[i]);
    g_pDeviceContext->UpdateSubresource(g_pFrustumPlanesCB, 0, nullptr, planesCPU, 0, 0);
}

void ReadQueries()
{
    while (g_lastCompletedFrame < g_curFrame) {
        D3D11_QUERY_DATA_PIPELINE_STATISTICS stats;
        HRESULT hr = g_pDeviceContext->GetData(g_pQueries[g_lastCompletedFrame % 10], &stats, sizeof(stats), 0);
        if (hr == S_OK) {
            g_gpuVisibleInstances = (int)(stats.IAPrimitives / 12); // 12 triangles per cube (36 indices / 3)
            ++g_lastCompletedFrame;
        }
        else {
            break;
        }
    }
}

// ------------------------------------------------------------------
// Frustum culling
// ------------------------------------------------------------------
void BuildFrustumPlanes(const XMMATRIX& vp, XMVECTOR planes[6])
{
    XMVECTOR row1 = XMVectorSet(vp.r[0].m128_f32[0], vp.r[1].m128_f32[0], vp.r[2].m128_f32[0], vp.r[3].m128_f32[0]);
    XMVECTOR row2 = XMVectorSet(vp.r[0].m128_f32[1], vp.r[1].m128_f32[1], vp.r[2].m128_f32[1], vp.r[3].m128_f32[1]);
    XMVECTOR row3 = XMVectorSet(vp.r[0].m128_f32[2], vp.r[1].m128_f32[2], vp.r[2].m128_f32[2], vp.r[3].m128_f32[2]);
    XMVECTOR row4 = XMVectorSet(vp.r[0].m128_f32[3], vp.r[1].m128_f32[3], vp.r[2].m128_f32[3], vp.r[3].m128_f32[3]);

    planes[0] = row4 + row1; // left
    planes[1] = row4 - row1; // right
    planes[2] = row4 + row2; // bottom
    planes[3] = row4 - row2; // top
    planes[4] = row4 + row3; // near
    planes[5] = row4 - row3; // far

    for (int i = 0; i < 6; ++i)
    {
        XMVECTOR norm = XMVector3Length(planes[i]);
        planes[i] = planes[i] / norm;
    }
}

void TransformAABB(const XMMATRIX& transform, const XMVECTOR& localMin, const XMVECTOR& localMax, XMVECTOR& worldMin, XMVECTOR& worldMax)
{
    XMVECTOR corners[8];
    corners[0] = XMVectorSet(XMVectorGetX(localMin), XMVectorGetY(localMin), XMVectorGetZ(localMin), 1.0f);
    corners[1] = XMVectorSet(XMVectorGetX(localMax), XMVectorGetY(localMin), XMVectorGetZ(localMin), 1.0f);
    corners[2] = XMVectorSet(XMVectorGetX(localMin), XMVectorGetY(localMax), XMVectorGetZ(localMin), 1.0f);
    corners[3] = XMVectorSet(XMVectorGetX(localMax), XMVectorGetY(localMax), XMVectorGetZ(localMin), 1.0f);
    corners[4] = XMVectorSet(XMVectorGetX(localMin), XMVectorGetY(localMin), XMVectorGetZ(localMax), 1.0f);
    corners[5] = XMVectorSet(XMVectorGetX(localMax), XMVectorGetY(localMin), XMVectorGetZ(localMax), 1.0f);
    corners[6] = XMVectorSet(XMVectorGetX(localMin), XMVectorGetY(localMax), XMVectorGetZ(localMax), 1.0f);
    corners[7] = XMVectorSet(XMVectorGetX(localMax), XMVectorGetY(localMax), XMVectorGetZ(localMax), 1.0f);
    worldMin = XMVectorReplicate(FLT_MAX); worldMax = XMVectorReplicate(-FLT_MAX);
    for (int i = 0; i < 8; ++i)
    {
        XMVECTOR worldCorner = XMVector4Transform(corners[i], transform);
        worldMin = XMVectorMin(worldMin, worldCorner);
        worldMax = XMVectorMax(worldMax, worldCorner);
    }
}

bool IsAABBInsideFrustum(const XMVECTOR planes[6], const XMVECTOR& aabbMin, const XMVECTOR& aabbMax)
{
    for (int i = 0; i < 6; ++i)
    {
        XMVECTOR p = aabbMin;
        if (XMVectorGetX(planes[i]) >= 0) p = XMVectorSetX(p, XMVectorGetX(aabbMax));
        if (XMVectorGetY(planes[i]) >= 0) p = XMVectorSetY(p, XMVectorGetY(aabbMax));
        if (XMVectorGetZ(planes[i]) >= 0) p = XMVectorSetZ(p, XMVectorGetZ(aabbMax));
        if (XMVector4Dot(p, planes[i]).m128_f32[0] < 0) return false;
    }
    return true;
}

// ------------------------------------------------------------------
// Постпроцессинг: текстура для рендера
// ------------------------------------------------------------------
void SetupColorBuffer(UINT width, UINT height)
{
    SAFE_RELEASE(g_pColorBuffer);
    SAFE_RELEASE(g_pColorBufferRTV);
    SAFE_RELEASE(g_pColorBufferSRV);

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.ArraySize = 1;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.Height = height;
    desc.Width = width;
    desc.MipLevels = 1;

    HRESULT hr = g_pDevice->CreateTexture2D(&desc, nullptr, &g_pColorBuffer);
    if (SUCCEEDED(hr)) hr = g_pDevice->CreateRenderTargetView(g_pColorBuffer, nullptr, &g_pColorBufferRTV);
    if (SUCCEEDED(hr)) hr = g_pDevice->CreateShaderResourceView(g_pColorBuffer, nullptr, &g_pColorBufferSRV);
    assert(SUCCEEDED(hr));
}

void CreateGPUResources()
{
    HRESULT hr;

    // Буфер для аргументов косвенной отрисовки (UAV)
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(D3D11_DRAW_INDEXED_INSTANCED_INDIRECT_ARGS);
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = sizeof(UINT);
    hr = g_pDevice->CreateBuffer(&desc, nullptr, &g_pIndirectArgsUAV);
    assert(SUCCEEDED(hr));
    hr = g_pDevice->CreateUnorderedAccessView(g_pIndirectArgsUAV, nullptr, &g_pIndirectArgsUAVView);
    assert(SUCCEEDED(hr));

    // Буфер для аргументов косвенной отрисовки (только для чтения)
    desc.ByteWidth = sizeof(D3D11_DRAW_INDEXED_INSTANCED_INDIRECT_ARGS);
    desc.BindFlags = 0;
    desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
    desc.StructureByteStride = 0;
    hr = g_pDevice->CreateBuffer(&desc, nullptr, &g_pIndirectArgsDraw);
    assert(SUCCEEDED(hr));

    // Structured buffer для видимых ID (UAV и SRV)
    desc.ByteWidth = sizeof(XMUINT4) * MAX_INSTANCES;
    desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = sizeof(XMUINT4);
    hr = g_pDevice->CreateBuffer(&desc, nullptr, &g_pVisibleIdsStructured);
    assert(SUCCEEDED(hr));
    hr = g_pDevice->CreateUnorderedAccessView(g_pVisibleIdsStructured, nullptr, &g_pVisibleIdsUAV);
    assert(SUCCEEDED(hr));
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.NumElements = MAX_INSTANCES;
    hr = g_pDevice->CreateShaderResourceView(g_pVisibleIdsStructured, &srvDesc, &g_pVisibleIdsSRV);
    assert(SUCCEEDED(hr));

    // Константный буфер для параметров culling (AABB)
    desc.ByteWidth = sizeof(CullParams);
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.MiscFlags = 0;
    hr = g_pDevice->CreateBuffer(&desc, nullptr, &g_pCullParamsCB);
    assert(SUCCEEDED(hr));

    // Константный буфер для плоскостей фрустума
    desc.ByteWidth = sizeof(XMFLOAT4) * 6;
    hr = g_pDevice->CreateBuffer(&desc, nullptr, &g_pFrustumPlanesCB);
    assert(SUCCEEDED(hr));

    // Запросы для pipeline statistics
    D3D11_QUERY_DESC qdesc = {};
    qdesc.Query = D3D11_QUERY_PIPELINE_STATISTICS;
    qdesc.MiscFlags = 0;
    for (int i = 0; i < 10; ++i) {
        hr = g_pDevice->CreateQuery(&qdesc, &g_pQueries[i]);
        assert(SUCCEEDED(hr));
    }

    // Инициализация indirect args нулями
    D3D11_DRAW_INDEXED_INSTANCED_INDIRECT_ARGS args = {};
    args.IndexCountPerInstance = 36;
    args.InstanceCount = 0;
    args.StartIndexLocation = 0;
    args.BaseVertexLocation = 0;
    args.StartInstanceLocation = 0;
    g_pDeviceContext->UpdateSubresource(g_pIndirectArgsUAV, 0, nullptr, &args, 0, 0);
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
    if (!g_pDeviceContext || !g_pBackBufferRTV || !g_pSwapChain) return;

    double currentTime = (double)GetTickCount64() / 1000.0;
    double deltaTime = currentTime - g_LastTime;
    g_LastTime = currentTime;
    UpdateCamera(deltaTime);

    // Выбор цели рендера: если фильтр включен, рисуем в текстуру, иначе в back buffer
    ID3D11RenderTargetView* sceneTarget = g_UseFilter ? g_pColorBufferRTV : g_pBackBufferRTV;
    g_pDeviceContext->ClearState();
    g_pDeviceContext->OMSetRenderTargets(1, &sceneTarget, g_pDepthStencilView);
    const FLOAT clearColor[4] = { 0.25f, 0.25f, 0.25f, 1.0f };
    g_pDeviceContext->ClearRenderTargetView(sceneTarget, clearColor);
    g_pDeviceContext->ClearDepthStencilView(g_pDepthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0);

    D3D11_VIEWPORT viewport = { 0, 0, (FLOAT)g_ClientWidth, (FLOAT)g_ClientHeight, 0.0f, 1.0f };
    g_pDeviceContext->RSSetViewports(1, &viewport);
    g_pDeviceContext->RSSetState(g_pRSCullBack);

    // Камера
    float camX = g_CameraDist * sin(g_CameraYaw) * cos(g_CameraPitch);
    float camY = g_CameraDist * sin(g_CameraPitch);
    float camZ = g_CameraDist * cos(g_CameraYaw) * cos(g_CameraPitch);
    XMVECTOR eye = XMVectorSet(camX, camY, camZ, 0.0f);
    XMVECTOR at = XMVectorZero();
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMMATRIX view = XMMatrixLookAtLH(eye, at, up);
    float aspect = (float)g_ClientWidth / (float)g_ClientHeight;
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PI / 3.0f, aspect, 0.1f, 100.0f);
    XMMATRIX viewProj = view * proj;

    // Skybox (с отдельной матрицей без трансляции)
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
        D3D11_DEPTH_STENCIL_DESC dsSky = {};
        dsSky.DepthEnable = TRUE; dsSky.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO; dsSky.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        ID3D11DepthStencilState* pDSSky = nullptr;
        g_pDevice->CreateDepthStencilState(&dsSky, &pDSSky);
        g_pDeviceContext->OMSetDepthStencilState(pDSSky, 0);
        g_pDeviceContext->RSSetState(g_pRSCullNone);
        SAFE_RELEASE(pDSSky);
        g_pDeviceContext->VSSetShader(g_pSkyboxVS, nullptr, 0);
        g_pDeviceContext->PSSetShader(g_pSkyboxPS, nullptr, 0);
        g_pDeviceContext->IASetInputLayout(g_pSkyboxInputLayout);
        UINT stride = sizeof(TexturedVertex);
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
        g_pDeviceContext->RSSetState(g_pRSCullBack);
        g_pDeviceContext->OMSetDepthStencilState(nullptr, 0);
    }

    // Обновляем обычный ViewProjBuffer для сцены
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(g_pDeviceContext->Map(g_pViewProjBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        ViewProjBuffer* pData = (ViewProjBuffer*)mapped.pData;
        XMStoreFloat4x4((XMFLOAT4X4*)&pData->vp, XMMatrixTranspose(viewProj));
        g_pDeviceContext->Unmap(g_pViewProjBuffer, 0);
    }

    // Обновляем SceneBuffer (освещение)
    if (SUCCEEDED(g_pDeviceContext->Map(g_pSceneBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        SceneBuffer* pScene = (SceneBuffer*)mapped.pData;
        XMStoreFloat4x4((XMFLOAT4X4*)&pScene->vp, XMMatrixTranspose(viewProj));
        pScene->cameraPos = XMFLOAT4(camX, camY, camZ, 1.0f);
        pScene->lightCount.x = 2;
        pScene->lights[0].pos = XMFLOAT4(0.0f, 2.0f, 0.0f, 1.0f);
        pScene->lights[0].color = XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f);
        pScene->lights[1].pos = XMFLOAT4(0.0f, 2.0f, 2.5f, 1.0f);
        pScene->lights[1].color = XMFLOAT4(0.6f, 0.8f, 1.0f, 1.0f);
        pScene->ambientColor = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
        g_pDeviceContext->Unmap(g_pSceneBuffer, 0);
    }

    // Обновляем матрицы экземпляров
    UpdateInstanceTransforms(currentTime);
    g_pDeviceContext->UpdateSubresource(g_pGeomBufferInst, 0, nullptr, g_Instances, sizeof(GeomBuffer) * MAX_INSTANCES, 0);

    // Frustum culling
    // Обновление AABB и плоскостей для GPU culling
    UpdateAABBBuffer();
    UpdateFrustumPlanesCB(viewProj);

    // Сброс indirect args
    D3D11_DRAW_INDEXED_INSTANCED_INDIRECT_ARGS args = {};
    args.IndexCountPerInstance = 36;
    args.InstanceCount = 0;
    args.StartIndexLocation = 0;
    args.BaseVertexLocation = 0;
    args.StartInstanceLocation = 0;
    g_pDeviceContext->UpdateSubresource(g_pIndirectArgsUAV, 0, nullptr, &args, 0, 0);

    // Запуск compute shader для culling
    ID3D11Buffer* csCBs[] = { g_pFrustumPlanesCB, g_pCullParamsCB };
    g_pDeviceContext->CSSetConstantBuffers(0, 2, csCBs);
    ID3D11UnorderedAccessView* csUAVs[] = { g_pIndirectArgsUAVView, g_pVisibleIdsUAV };
    g_pDeviceContext->CSSetUnorderedAccessViews(0, 2, csUAVs, nullptr);
    g_pDeviceContext->CSSetShader(g_pCullCS, nullptr, 0);

    UINT groupCount = (g_InstanceCount + 63) / 64;
    g_pDeviceContext->Dispatch(groupCount, 1, 1);

    // Сброс состояний compute
    ID3D11UnorderedAccessView* nullUAV = nullptr;
    g_pDeviceContext->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    g_pDeviceContext->CSSetUnorderedAccessViews(1, 1, &nullUAV, nullptr);
    ID3D11Buffer* nullCB = nullptr;
    g_pDeviceContext->CSSetConstantBuffers(0, 1, &nullCB);
    g_pDeviceContext->CSSetConstantBuffers(1, 1, &nullCB);
    g_pDeviceContext->CSSetShader(nullptr, nullptr, 0);

    // Копирование аргументов для косвенной отрисовки
    g_pDeviceContext->CopyResource(g_pIndirectArgsDraw, g_pIndirectArgsUAV);

    // Установка structured buffer visibleIds для вершинного и пиксельного шейдеров
    ID3D11ShaderResourceView* srvVisible = g_pVisibleIdsSRV;
    g_pDeviceContext->VSSetShaderResources(2, 1, &srvVisible);
    g_pDeviceContext->PSSetShaderResources(2, 1, &srvVisible);

    // Instanced отрисовка
    UINT stride = sizeof(TextureNormalTangentVertex);
    UINT offset = 0;
    ID3D11Buffer* vbCube[] = { g_pVertexBuffer };
    g_pDeviceContext->IASetVertexBuffers(0, 1, vbCube, &stride, &offset);
    g_pDeviceContext->IASetIndexBuffer(g_pIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
    g_pDeviceContext->IASetInputLayout(g_pInstancedInputLayout);
    g_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_pDeviceContext->VSSetShader(g_pInstancedVS, nullptr, 0);
    g_pDeviceContext->PSSetShader(g_pInstancedPS, nullptr, 0);

    ID3D11Buffer* cbInstVS[] = { nullptr, g_pGeomBufferInst, g_pViewProjBuffer };
    g_pDeviceContext->VSSetConstantBuffers(0, 3, cbInstVS);
    ID3D11Buffer* cbInstPS[] = { nullptr, g_pGeomBufferInst, g_pSceneBuffer };
    g_pDeviceContext->PSSetConstantBuffers(1, 1, &g_pGeomBufferInst);
    g_pDeviceContext->PSSetConstantBuffers(3, 1, &g_pSceneBuffer);

    ID3D11ShaderResourceView* texArraySRV[] = { g_pTextureArrayView, g_pNormalMapView };
    g_pDeviceContext->PSSetShaderResources(0, 2, texArraySRV);
    g_pDeviceContext->PSSetSamplers(0, 1, &g_pSampler);

    // Косвенная отрисовка с запросом статистики
    g_pDeviceContext->Begin(g_pQueries[g_curFrame % 10]);
    g_pDeviceContext->DrawIndexedInstancedIndirect(g_pIndirectArgsDraw, 0);
    g_pDeviceContext->End(g_pQueries[g_curFrame % 10]);
    g_curFrame++;

    ReadQueries();

    // Обновление заголовка окна
    static double lastTitleUpdate = 0;
    double now = (double)GetTickCount64() / 1000.0;
    if (now - lastTitleUpdate > 1.0) {
        wchar_t title[256];
        swprintf(title, 256, L"8 lab. GPU Frustum Culling - Visible instances: %d", g_gpuVisibleInstances);
        SetWindowTextW(g_hWnd, title);
        lastTitleUpdate = now;
    }

    // Постпроцессинг: если фильтр включен, применяем его к текстуре и выводим на экран
    if (g_UseFilter && g_pFilterVS && g_pFilterPS)
    {
        g_pDeviceContext->OMSetRenderTargets(1, &g_pBackBufferRTV, nullptr);
        g_pDeviceContext->ClearRenderTargetView(g_pBackBufferRTV, clearColor);
        g_pDeviceContext->OMSetDepthStencilState(nullptr, 0);
        g_pDeviceContext->RSSetState(nullptr);
        g_pDeviceContext->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
        g_pDeviceContext->IASetInputLayout(nullptr);
        g_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_pDeviceContext->VSSetShader(g_pFilterVS, nullptr, 0);
        g_pDeviceContext->PSSetShader(g_pFilterPS, nullptr, 0);
        ID3D11ShaderResourceView* srv[] = { g_pColorBufferSRV };
        g_pDeviceContext->PSSetShaderResources(0, 1, srv);
        g_pDeviceContext->PSSetSamplers(0, 1, &g_pSampler);
        g_pDeviceContext->Draw(3, 0);
    }

    g_pSwapChain->Present(1, 0);
}

// ------------------------------------------------------------------
// Обработка изменения размера окна
// ------------------------------------------------------------------
void OnResize(UINT newWidth, UINT newHeight)
{
    if (!g_pSwapChain || !g_pDevice || !g_pDeviceContext) return;
    g_pDeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
    SAFE_RELEASE(g_pBackBufferRTV);
    SAFE_RELEASE(g_pDepthStencilView);

    HRESULT hr = g_pSwapChain->ResizeBuffers(2, newWidth, newHeight, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) { OutputDebugStringA("ResizeBuffers failed\n"); return; }

    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    if (SUCCEEDED(hr)) { g_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pBackBufferRTV); pBackBuffer->Release(); }

    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = newWidth; depthDesc.Height = newHeight; depthDesc.MipLevels = 1; depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT; depthDesc.SampleDesc.Count = 1; depthDesc.SampleDesc.Quality = 0;
    depthDesc.Usage = D3D11_USAGE_DEFAULT; depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ID3D11Texture2D* pDepthStencil = nullptr;
    hr = g_pDevice->CreateTexture2D(&depthDesc, nullptr, &pDepthStencil);
    if (SUCCEEDED(hr)) { g_pDevice->CreateDepthStencilView(pDepthStencil, nullptr, &g_pDepthStencilView); pDepthStencil->Release(); }

    g_ClientWidth = newWidth; g_ClientHeight = newHeight;
    SetupColorBuffer(g_ClientWidth, g_ClientHeight);
}

// ------------------------------------------------------------------
// Очистка ресурсов
// ------------------------------------------------------------------
void CleanupDirectX()
{
    if (g_pDeviceContext) g_pDeviceContext->ClearState();

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
    SAFE_RELEASE(g_pDepthStencilView);
    SAFE_RELEASE(g_pSwapChain);
    SAFE_RELEASE(g_pTextureView);
    SAFE_RELEASE(g_pCubemapView);
    SAFE_RELEASE(g_pSampler);
    SAFE_RELEASE(g_pRSCullBack);
    SAFE_RELEASE(g_pRSCullNone);
    SAFE_RELEASE(g_pNormalMapView);
    SAFE_RELEASE(g_pTexture);
    SAFE_RELEASE(g_pNormalMapTexture);
    SAFE_RELEASE(g_pCubemapTexture);

    SAFE_RELEASE(g_pInstancedVS);
    SAFE_RELEASE(g_pInstancedPS);
    SAFE_RELEASE(g_pInstancedInputLayout);
    SAFE_RELEASE(g_pGeomBufferInst);
    SAFE_RELEASE(g_pVisibleIdsBuffer);
    SAFE_RELEASE(g_pTextureArrayView);

    SAFE_RELEASE(g_pColorBuffer);
    SAFE_RELEASE(g_pColorBufferRTV);
    SAFE_RELEASE(g_pColorBufferSRV);
    SAFE_RELEASE(g_pFilterVS);
    SAFE_RELEASE(g_pFilterPS);

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

    SAFE_RELEASE(g_pCullCS);
    SAFE_RELEASE(g_pIndirectArgsUAV);
    SAFE_RELEASE(g_pIndirectArgsDraw);
    SAFE_RELEASE(g_pIndirectArgsUAVView);
    SAFE_RELEASE(g_pVisibleIdsStructured);
    SAFE_RELEASE(g_pVisibleIdsUAV);
    SAFE_RELEASE(g_pVisibleIdsSRV);
    SAFE_RELEASE(g_pCullParamsCB);
    SAFE_RELEASE(g_pFrustumPlanesCB);
    for (int i = 0; i < 10; ++i) SAFE_RELEASE(g_pQueries[i]);
}