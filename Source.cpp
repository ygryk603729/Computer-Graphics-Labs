// Lab02_Init_DX11.cpp
// Минимальное DirectX 11 приложение — заливка цветом + поддержка изменения размера окна

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cassert>
#include <cstdio>   // для _T

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
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

UINT g_ClientWidth = 1280;
UINT g_ClientHeight = 720;

// ──────────────────────────────────────────────
// Вспомогательные макросы
// ──────────────────────────────────────────────

#define SAFE_RELEASE(p)     if (p) { (p)->Release(); (p) = nullptr; }

// ──────────────────────────────────────────────
// Forward declarations
// ──────────────────────────────────────────────

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
bool InitDirectX();
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

    // фабрика DXGI
    IDXGIFactory* pFactory = nullptr;
    hr = CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&pFactory);
    if (FAILED(hr)) return false;

    // адаптер
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

    // устройство D3D11
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
    // pFactory понадобится для swap chain

    if (FAILED(hr) || obtainedLevel != D3D_FEATURE_LEVEL_11_0)
    {
        pFactory->Release();
        return false;
    }

    // Swap Chain
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

    // освобождение фабрики
    pFactory->Release();

    if (FAILED(hr)) return false;

    // back buffer и RTV
    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
    if (FAILED(hr)) return false;

    hr = g_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pBackBufferRTV);
    pBackBuffer->Release();

    if (FAILED(hr)) return false;

    return true;
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

    // swap chain
    HRESULT hr = g_pSwapChain->ResizeBuffers(
        2,                      
        newWidth, newHeight,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        0);

    if (FAILED(hr))
    {
        //PostQuitMessage(-1);
        assert(SUCCEEDED(hr));
        return;
    }

    // новый back buffer и RTV
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
    static const FLOAT clearColor[4] = { 0.1f, 0.2f, 0.3f, 1.0f };   // тёмно-синий
    m_pDeviceContext->ClearRenderTargetView(g_pBackBufferRTV, clearColor);

    // показ кадра
    g_pSwapChain->Present(1, 0);   // 1 - VSync включён, 0 - нет
}

// ──────────────────────────────────────────────
// Очистка всех ресурсов
// ──────────────────────────────────────────────

void CleanupDirectX()
{    
    if (m_pDeviceContext)
        m_pDeviceContext->ClearState();

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