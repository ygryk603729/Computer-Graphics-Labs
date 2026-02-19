// Lab02_Init_DX11.cpp
// Минимальное DirectX 11 приложение — заливка цветом + треугольник

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

// ──────────────────────────────────────────────
// Глобальные переменные / члены "класса" приложения
// ──────────────────────────────────────────────

HWND g_hWnd = nullptr;

ID3D11Device* g_pDevice = nullptr;
ID3D11DeviceContext* m_pDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_pBackBufferRTV = nullptr;

// ресурсы для треугольника
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

UINT g_ClientWidth = 1280;
UINT g_ClientHeight = 720;

// ──────────────────────────────────────────────
// Вспомогательные макросы
// ──────────────────────────────────────────────

#define SAFE_RELEASE(p)     if (p) { (p)->Release(); (p) = nullptr; }

inline HRESULT SetResourceName(ID3D11DeviceChild* pResource, const std::string& name)
{
    return pResource->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)name.length(), name.c_str());
}

// ──────────────────────────────────────────────
// Forward declarations
// ──────────────────────────────────────────────

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
bool InitDirectX();
void CreateTriangleResources();
void CompileShaders();
void CleanupDirectX();
void RenderFrame();
void OnResize(UINT newWidth, UINT newHeight);

// ──────────────────────────────────────────────
// WinMain
// ──────────────────────────────────────────────

int WINAPI wWinMain(
    _In_     HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_     LPWSTR    lpCmdLine,
    _In_     int       nCmdShow
)
{
    // Регистрация класса окна
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"DX11Lab02WindowClass";

    if (!RegisterClassExW(&wc))
    {
        MessageBoxW(nullptr, L"RegisterClassEx failed", L"Error", MB_OK | MB_ICONERROR);
        return 0;
    }

    // Вычисление размеров окна по области
    RECT rc = { 0, 0, (LONG)g_ClientWidth, (LONG)g_ClientHeight };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    int winWidth = rc.right - rc.left;
    int winHeight = rc.bottom - rc.top;

    // 3. Создание окна
    g_hWnd = CreateWindowW(
        wc.lpszClassName,
        L"Lab 02 — DirectX 11 Initialization",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        winWidth, winHeight,
        nullptr, nullptr, hInstance, nullptr);

    if (!g_hWnd)
    {
        MessageBoxW(nullptr, L"CreateWindow failed", L"Error", MB_OK | MB_ICONERROR);
        return 0;
    }

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    // Инициализация DirectX
    if (!InitDirectX())
    {
        CleanupDirectX();
        DestroyWindow(g_hWnd);
        return -1;
    }

    // создание ресурсов треугольника
    CreateTriangleResources();
    CompileShaders();

    // Основной цикл
    MSG msg = {};
    bool done = false;

    while (!done)
    {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                done = true;

            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        else
        {
            RenderFrame();
        }
    }

    CleanupDirectX();
    return (int)msg.wParam;
}

// ──────────────────────────────────────────────
// Обработчик сообщений окна
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
            {
                OnResize(newW, newH);
            }
        }
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

    hr = D3D11CreateDevice(
        pSelectedAdapter,
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,
        flags,
        levels, 1,
        D3D11_SDK_VERSION,
        &g_pDevice,
        &obtainedLevel,
        &m_pDeviceContext);

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
// создание вершинного и индексного буферов
// ──────────────────────────────────────────────

void CreateTriangleResources()
{
    const Vertex vertices[] = {
        {-0.5f, -0.5f, 0.0f, RGB(255, 0, 0)},
        { 0.5f, -0.5f, 0.0f, RGB(0, 255, 0)},
        { 0.0f,  0.5f, 0.0f, RGB(0, 0, 255)}
    };

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(vertices);
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    desc.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA data = {};
    data.pSysMem = vertices;

    HRESULT hr = g_pDevice->CreateBuffer(&desc, &data, &g_pVertexBuffer);
    if (FAILED(hr))
    {
        assert(SUCCEEDED(hr));
        return;
    }
    SetResourceName(g_pVertexBuffer, "VertexBuffer");

    const USHORT indices[] = { 0, 2, 1 };
    desc.ByteWidth = sizeof(indices);
    desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    data.pSysMem = indices;

    hr = g_pDevice->CreateBuffer(&desc, &data, &g_pIndexBuffer);
    if (FAILED(hr))
    {
        assert(SUCCEEDED(hr));
        return;
    }
    SetResourceName(g_pIndexBuffer, "IndexBuffer");
}

// ──────────────────────────────────────────────
// компиляция шейдеров
// ──────────────────────────────────────────────

void CompileShaders()
{
    const char* vsCode = R"(
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
            result.pos = float4(vertex.pos, 1.0);
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

    ID3DBlob* pVsBlob = nullptr;
    ID3DBlob* pPsBlob = nullptr;
    ID3DBlob* pErrorBlob = nullptr;
    HRESULT hr = S_OK;

    hr = D3DCompile(vsCode, strlen(vsCode), nullptr, nullptr, nullptr,
        "vs", "vs_5_0", flags1, 0, &pVsBlob, &pErrorBlob);
    if (FAILED(hr))
    {
        if (pErrorBlob)
        {
            OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer());
            pErrorBlob->Release();
        }
        assert(SUCCEEDED(hr));
        return;
    }

    hr = g_pDevice->CreateVertexShader(pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(),
        nullptr, &g_pVertexShader);
    if (FAILED(hr))
    {
        assert(SUCCEEDED(hr));
        SAFE_RELEASE(pVsBlob);
        return;
    }
    SetResourceName(g_pVertexShader, "VertexShader");

    hr = D3DCompile(psCode, strlen(psCode), nullptr, nullptr, nullptr,
        "ps", "ps_5_0", flags1, 0, &pPsBlob, &pErrorBlob);
    if (FAILED(hr))
    {
        if (pErrorBlob)
        {
            OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer());
            pErrorBlob->Release();
        }
        assert(SUCCEEDED(hr));
        SAFE_RELEASE(pVsBlob);
        return;
    }

    hr = g_pDevice->CreatePixelShader(pPsBlob->GetBufferPointer(), pPsBlob->GetBufferSize(),
        nullptr, &g_pPixelShader);
    if (FAILED(hr))
    {
        assert(SUCCEEDED(hr));
        SAFE_RELEASE(pVsBlob);
        SAFE_RELEASE(pPsBlob);
        return;
    }
    SetResourceName(g_pPixelShader, "PixelShader");

    D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };

    hr = g_pDevice->CreateInputLayout(layoutDesc, 2,
        pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), &g_pInputLayout);
    if (FAILED(hr))
    {
        assert(SUCCEEDED(hr));
        SAFE_RELEASE(pVsBlob);
        SAFE_RELEASE(pPsBlob);
        return;
    }
    SetResourceName(g_pInputLayout, "InputLayout");

    SAFE_RELEASE(pVsBlob);
    SAFE_RELEASE(pPsBlob);
    SAFE_RELEASE(pErrorBlob);
}

// ──────────────────────────────────────────────
// Обработка изменения размера окна
// ──────────────────────────────────────────────

void OnResize(UINT newWidth, UINT newHeight)
{
    if (!g_pSwapChain || !g_pDevice || !m_pDeviceContext)
        return;

    m_pDeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
    SAFE_RELEASE(g_pBackBufferRTV);

    HRESULT hr = g_pSwapChain->ResizeBuffers(
        2, newWidth, newHeight,
        DXGI_FORMAT_R8G8B8A8_UNORM, 0);

    if (FAILED(hr))
    {
        assert(SUCCEEDED(hr));
        return;
    }

    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
    if (FAILED(hr)) return;

    hr = g_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pBackBufferRTV);
    pBackBuffer->Release();

    g_ClientWidth = newWidth;
    g_ClientHeight = newHeight;
}

// ──────────────────────────────────────────────
// Отрисовка одного кадра 
// ──────────────────────────────────────────────

void RenderFrame()
{
    if (!m_pDeviceContext || !g_pBackBufferRTV || !g_pSwapChain)
        return;

    m_pDeviceContext->ClearState();

    ID3D11RenderTargetView* views[] = { g_pBackBufferRTV };
    m_pDeviceContext->OMSetRenderTargets(1, views, nullptr);

    // Цвет заливки
    static const FLOAT clearColor[4] = { 0.1f, 0.2f, 0.3f, 1.0f };
    m_pDeviceContext->ClearRenderTargetView(g_pBackBufferRTV, clearColor);

    // настройка viewport
    D3D11_VIEWPORT viewport = {};
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width = (FLOAT)g_ClientWidth;
    viewport.Height = (FLOAT)g_ClientHeight;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    m_pDeviceContext->RSSetViewports(1, &viewport);

    // настройка и отрисовка треугольника
    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    ID3D11Buffer* vertexBuffers[] = { g_pVertexBuffer };
    UINT strides[] = { stride };
    UINT offsets[] = { offset };

    m_pDeviceContext->IASetVertexBuffers(0, 1, vertexBuffers, strides, offsets);
    m_pDeviceContext->IASetIndexBuffer(g_pIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
    m_pDeviceContext->IASetInputLayout(g_pInputLayout);
    m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    m_pDeviceContext->VSSetShader(g_pVertexShader, nullptr, 0);
    m_pDeviceContext->PSSetShader(g_pPixelShader, nullptr, 0);

    m_pDeviceContext->DrawIndexed(3, 0, 0);

    // показ кадра
    g_pSwapChain->Present(1, 0);
}


void CleanupDirectX()
{
    if (m_pDeviceContext)
        m_pDeviceContext->ClearState();

    
    SAFE_RELEASE(g_pInputLayout);
    SAFE_RELEASE(g_pVertexShader);
    SAFE_RELEASE(g_pPixelShader);
    SAFE_RELEASE(g_pIndexBuffer);
    SAFE_RELEASE(g_pVertexBuffer);

    SAFE_RELEASE(g_pBackBufferRTV);
    SAFE_RELEASE(g_pSwapChain);

    if (g_pDevice)
    {
#ifdef _DEBUG
        ID3D11Debug* pDebug = nullptr;
        if (SUCCEEDED(g_pDevice->QueryInterface(__uuidof(ID3D11Debug), (void**)&pDebug)))
        {
            pDebug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL | D3D11_RLDO_IGNORE_INTERNAL);
            pDebug->Release();
        }
#endif
        g_pDevice->Release();
        g_pDevice = nullptr;
    }

    SAFE_RELEASE(m_pDeviceContext);
}