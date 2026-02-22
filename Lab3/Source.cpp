// Lab03_Cube.cpp
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <cassert>
#include <cstdio>
#include <string>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

using namespace DirectX;

// ──────────────────────────────────────────────
// Глобальные переменные
// ──────────────────────────────────────────────

HWND g_hWnd = nullptr;

ID3D11Device* g_pDevice = nullptr;
ID3D11DeviceContext* g_pDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_pBackBufferRTV = nullptr;

// Геометрия куба
struct Vertex
{
    float x, y, z;
    UINT color;
};

ID3D11Buffer* g_pVertexBuffer = nullptr;
ID3D11Buffer* g_pIndexBuffer = nullptr;
ID3D11VertexShader* g_pVertexShader = nullptr;
ID3D11PixelShader* g_pPixelShader = nullptr;
ID3D11InputLayout* g_pInputLayout = nullptr;

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

UINT g_ClientWidth = 1280;
UINT g_ClientHeight = 720;

// Управление камерой
float g_CameraYaw = 0.0f;      // вращение Y
float g_CameraPitch = 0.3f;     // угол горизонт
float g_CameraDist = 3.0f;
bool g_KeyLeft = false, g_KeyRight = false, g_KeyUp = false, g_KeyDown = false;

// Время для вращения
double g_LastTime = 0.0;

// ──────────────────────────────────────────────
// Макросы
// ──────────────────────────────────────────────

#define SAFE_RELEASE(p) if (p) { (p)->Release(); (p) = nullptr; }

inline HRESULT SetResourceName(ID3D11DeviceChild* pResource, const std::string& name)
{
    return pResource->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)name.length(), name.c_str());
}

// ──────────────────────────────────────────────
// Прототипы
// ──────────────────────────────────────────────

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
bool InitDirectX();
void CreateCubeResources();
void CompileShaders();
void CleanupDirectX();
void RenderFrame();
void OnResize(UINT newWidth, UINT newHeight);
void UpdateCamera(double deltaTime);

// ──────────────────────────────────────────────
// WinMain
// ──────────────────────────────────────────────

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int nCmdShow)
{
    // Rласса окна
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"DX11Lab04Class";

    if (!RegisterClassExW(&wc))
    {
        MessageBoxW(nullptr, L"RegisterClassEx failed", L"Error", MB_OK | MB_ICONERROR);
        return 0;
    }

    RECT rc = { 0, 0, (LONG)g_ClientWidth, (LONG)g_ClientHeight };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    int winWidth = rc.right - rc.left;
    int winHeight = rc.bottom - rc.top;

    // Создание окна
    g_hWnd = CreateWindowW(wc.lpszClassName, L"Lab 04 - Cube",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        winWidth, winHeight, nullptr, nullptr, hInstance, nullptr);
    if (!g_hWnd)
    {
        MessageBoxW(nullptr, L"CreateWindow failed", L"Error", MB_OK | MB_ICONERROR);
        return 0;
    }

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    // DirectX
    if (!InitDirectX())
    {
        CleanupDirectX();
        DestroyWindow(g_hWnd);
        return -1;
    }

    // Создание ресурсов куба
    CreateCubeResources();
    CompileShaders();

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(ModelBuffer);
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = 0;
    HRESULT hr = g_pDevice->CreateBuffer(&desc, nullptr, &g_pModelBuffer);
    assert(SUCCEEDED(hr));
    SetResourceName(g_pModelBuffer, "ModelBuffer");

    desc.ByteWidth = sizeof(ViewProjBuffer);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = g_pDevice->CreateBuffer(&desc, nullptr, &g_pViewProjBuffer);
    assert(SUCCEEDED(hr));
    SetResourceName(g_pViewProjBuffer, "ViewProjBuffer");

    // Инициализация времени
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
        {
            RenderFrame();
        }
    }

    CleanupDirectX();
    return (int)msg.wParam;
}

// ──────────────────────────────────────────────
// Оконная процедура
// ──────────────────────────────────────────────

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

// ──────────────────────────────────────────────
// Инициализация DirectX
// ──────────────────────────────────────────────

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

    hr = g_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pBackBufferRTV);
    pBackBuffer->Release();
    if (FAILED(hr)) return false;

    return true;
}

// ──────────────────────────────────────────────
// Создание вершинного и индексного буферов для куба
// ──────────────────────────────────────────────

void CreateCubeResources()
{
    // 8 вершин куба
    const Vertex vertices[] = {
        // задняя грань
        {-0.5f, -0.5f, -0.5f, RGB(255,0,0)},   // красный
        { 0.5f, -0.5f, -0.5f, RGB(0,255,0)},   // зелёный
        { 0.5f,  0.5f, -0.5f, RGB(0,0,255)},   // син
        {-0.5f,  0.5f, -0.5f, RGB(255,255,0)}, // жёлт
        // передняя грань
        {-0.5f, -0.5f,  0.5f, RGB(255,0,255)}, // пурпурный
        { 0.5f, -0.5f,  0.5f, RGB(0,255,255)}, // голуб
        { 0.5f,  0.5f,  0.5f, RGB(128,128,128)}, // серый
        {-0.5f,  0.5f,  0.5f, RGB(255,128,0)}  // оранж
    };

    // 12 треугольников
    const USHORT indices[] = {

        0,1,2, 0,2,3,

        4,6,5, 4,7,6,

        0,3,7, 0,7,4,

        1,5,6, 1,6,2,

        3,2,6, 3,6,7,

        0,4,5, 0,5,1
    };

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(vertices);
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA data = { vertices };
    HRESULT hr = g_pDevice->CreateBuffer(&desc, &data, &g_pVertexBuffer);
    if (FAILED(hr)) { OutputDebugStringA("CreateBuffer failed\n"); return; }
    SetResourceName(g_pVertexBuffer, "VertexBuffer");

    desc.ByteWidth = sizeof(indices);
    desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    data.pSysMem = indices;
    hr = g_pDevice->CreateBuffer(&desc, &data, &g_pIndexBuffer);
    if (FAILED(hr)) { OutputDebugStringA("CreateBuffer failed\n"); return; }
    SetResourceName(g_pIndexBuffer, "IndexBuffer");
}

// ──────────────────────────────────────────────
// Компиляция шейдеров
// ──────────────────────────────────────────────

void CompileShaders()
{
    const char* vsCode = R"(
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
            float4 color : COLOR;
        };
        struct VSOutput
        {
            float4 pos : SV_Position;
            float4 color : COLOR;
        };
        VSOutput vs(VSInput vertex)
        {
            VSOutput result;
            float4 worldPos = mul(float4(vertex.pos, 1.0), model);
            result.pos = mul(worldPos, vp);
            result.color = vertex.color;
            return result;
        }
    )";

    const char* psCode = R"(
        struct VSOutput
        {
            float4 pos : SV_Position;
            float4 color : COLOR;
        };
        float4 ps(VSOutput pixel) : SV_Target0
        {
            return pixel.color;
        }
    )";

    UINT flags1 = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags1 |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ID3DBlob* pVsBlob = nullptr, * pPsBlob = nullptr, * pErrorBlob = nullptr;

    HRESULT hr = D3DCompile(vsCode, strlen(vsCode), nullptr, nullptr, nullptr,
        "vs", "vs_5_0", flags1, 0, &pVsBlob, &pErrorBlob);
    if (FAILED(hr))
    {
        if (pErrorBlob) OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer());        
        return;
    }
    hr = g_pDevice->CreateVertexShader(pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), nullptr, &g_pVertexShader);
    if (FAILED(hr)) { OutputDebugStringA("VertexShader creation failed\n"); return; }
    SetResourceName(g_pVertexShader, "VertexShader");

    hr = D3DCompile(psCode, strlen(psCode), nullptr, nullptr, nullptr,
        "ps", "ps_5_0", flags1, 0, &pPsBlob, &pErrorBlob);
    if (FAILED(hr))
    {
        if (pErrorBlob) OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer());        
        SAFE_RELEASE(pVsBlob);
        return;
    }
    hr = g_pDevice->CreatePixelShader(pPsBlob->GetBufferPointer(), pPsBlob->GetBufferSize(), nullptr, &g_pPixelShader);
    if (FAILED(hr)) { OutputDebugStringA("CreateBuffer failed\n"); return; }
    SetResourceName(g_pPixelShader, "PixelShader");

    D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    hr = g_pDevice->CreateInputLayout(layoutDesc, 2, pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), &g_pInputLayout);
    if (FAILED(hr)) { OutputDebugStringA("CreateBuffer failed\n"); return; }
    SetResourceName(g_pInputLayout, "InputLayout");

    SAFE_RELEASE(pVsBlob);
    SAFE_RELEASE(pPsBlob);
    SAFE_RELEASE(pErrorBlob);
}

// ──────────────────────────────────────────────
// Обновление камеры на основе нажатых клавиш
// ──────────────────────────────────────────────

void UpdateCamera(double deltaTime)
{
	float speed = 1.0f; // скорость вращения в радианах в секунду
    if (g_KeyLeft)  g_CameraYaw -= speed * (float)deltaTime;
    if (g_KeyRight) g_CameraYaw += speed * (float)deltaTime;
    if (g_KeyUp)    g_CameraPitch += speed * (float)deltaTime;
    if (g_KeyDown)  g_CameraPitch -= speed * (float)deltaTime;

    // Адекватное ограничение pitch, чтобы не переворачиваться
    if (g_CameraPitch > 1.5f) g_CameraPitch = 1.5f;
    if (g_CameraPitch < -1.5f) g_CameraPitch = -1.5f;
}

// ──────────────────────────────────────────────
// Отрисовка кадра
// ──────────────────────────────────────────────

void RenderFrame()
{
    if (!g_pDeviceContext || !g_pBackBufferRTV || !g_pSwapChain)
        return;

    double currentTime = (double)GetTickCount64() / 1000.0;
    double deltaTime = currentTime - g_LastTime;
    g_LastTime = currentTime;

    UpdateCamera(deltaTime);

    // Очистка
    g_pDeviceContext->ClearState();
    ID3D11RenderTargetView* views[] = { g_pBackBufferRTV };
    g_pDeviceContext->OMSetRenderTargets(1, views, nullptr);
    const FLOAT clearColor[4] = { 0.25f, 0.25f, 0.25f, 1.0f };
    g_pDeviceContext->ClearRenderTargetView(g_pBackBufferRTV, clearColor);

    // Viewport
    D3D11_VIEWPORT viewport = {};
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width = (FLOAT)g_ClientWidth;
    viewport.Height = (FLOAT)g_ClientHeight;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    g_pDeviceContext->RSSetViewports(1, &viewport);

    float angle = (float)currentTime * 0.5f;
    XMMATRIX model = XMMatrixRotationY(angle);

    // Матрица
    float camX = g_CameraDist * sin(g_CameraYaw) * cos(g_CameraPitch);
    float camY = g_CameraDist * sin(g_CameraPitch);
    float camZ = g_CameraDist * cos(g_CameraYaw) * cos(g_CameraPitch);
    XMVECTOR eye = XMVectorSet(camX, camY, camZ, 0);
    XMVECTOR target = XMVectorZero();
    XMVECTOR up = XMVectorSet(0, 1, 0, 0);
    XMMATRIX view = XMMatrixLookAtLH(eye, target, up);

    // Матрица проекции
    float aspect = (float)g_ClientWidth / (float)g_ClientHeight;
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PI / 3, aspect, 0.1f, 100.0f);

    // VP = view * proj
    XMMATRIX vp = XMMatrixMultiply(view, proj);

    ModelBuffer modelData;
    XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&modelData.model), XMMatrixTranspose(model));

    g_pDeviceContext->UpdateSubresource(g_pModelBuffer, 0, nullptr, &modelData, 0, 0);

    // Обновление буфера VP
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = g_pDeviceContext->Map(g_pViewProjBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) { OutputDebugStringA("CreateBuffer failed\n"); return; }
    ViewProjBuffer* pVP = (ViewProjBuffer*)mapped.pData;
    XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&pVP->vp), XMMatrixTranspose(vp));
    g_pDeviceContext->Unmap(g_pViewProjBuffer, 0);

    // Установка вершинных/индексных буферов
    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    ID3D11Buffer* vertexBuffers[] = { g_pVertexBuffer };
    UINT strides[] = { stride };
    UINT offsets[] = { offset };
    g_pDeviceContext->IASetVertexBuffers(0, 1, vertexBuffers, strides, offsets);
    g_pDeviceContext->IASetIndexBuffer(g_pIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
    g_pDeviceContext->IASetInputLayout(g_pInputLayout);
    g_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Установка константных буферов
    ID3D11Buffer* constantBuffers[2] = { g_pModelBuffer, g_pViewProjBuffer };
    g_pDeviceContext->VSSetConstantBuffers(0, 2, constantBuffers);

    // Установка шейдеров
    g_pDeviceContext->VSSetShader(g_pVertexShader, nullptr, 0);
    g_pDeviceContext->PSSetShader(g_pPixelShader, nullptr, 0);

    // Отрисовка куба
    g_pDeviceContext->DrawIndexed(36, 0, 0);

    g_pSwapChain->Present(1, 0);
}

// ──────────────────────────────────────────────
// Обработка изменения размера окна
// ──────────────────────────────────────────────

void OnResize(UINT newWidth, UINT newHeight)
{
    if (!g_pSwapChain || !g_pDevice || !g_pDeviceContext)
        return;

    g_pDeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
    SAFE_RELEASE(g_pBackBufferRTV);

    HRESULT hr = g_pSwapChain->ResizeBuffers(2, newWidth, newHeight,
        DXGI_FORMAT_UNKNOWN, 0);

    if (FAILED(hr))
    {        
        OutputDebugStringA("ResizeBuffers failed\n");
        return;
    }

    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
    if (FAILED(hr))
    {
        OutputDebugStringA("GetBuffer failed\n");
        return;
    }

    hr = g_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pBackBufferRTV);
    pBackBuffer->Release();
    if (FAILED(hr))
    {
        OutputDebugStringA("CreateRenderTargetView failed\n");
        return;
    }

    g_ClientWidth = newWidth;
    g_ClientHeight = newHeight;
}

// ──────────────────────────────────────────────
// Очистка ресурсов
// ──────────────────────────────────────────────

void CleanupDirectX()
{
    if (g_pDeviceContext)
        g_pDeviceContext->ClearState();

    SAFE_RELEASE(g_pModelBuffer);
    SAFE_RELEASE(g_pViewProjBuffer);
    SAFE_RELEASE(g_pInputLayout);
    SAFE_RELEASE(g_pVertexShader);
    SAFE_RELEASE(g_pPixelShader);
    SAFE_RELEASE(g_pIndexBuffer);
    SAFE_RELEASE(g_pVertexBuffer);
    SAFE_RELEASE(g_pBackBufferRTV);
    SAFE_RELEASE(g_pSwapChain);

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