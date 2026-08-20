#include "d3d11_context_vtable.h"

#include <windows.h>
#include <d3d11.h>

namespace camera {

bool GetContextVTable(void**& vtable) {
    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "_AIHT_CtxProbe";
    RegisterClassExA(&wc);
    HWND hwnd = CreateWindowA(wc.lpszClassName, "_p", WS_POPUP, 0, 0, 16, 16, nullptr, nullptr,
                              wc.hInstance, nullptr);
    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferDesc.Width = 16;
    scd.BufferDesc.Height = 16;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.RefreshRate = {60, 1};
    scd.SampleDesc = {1, 0};
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 1;
    scd.OutputWindow = hwnd;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL lvl = D3D_FEATURE_LEVEL_11_0;
    IDXGISwapChain* swap = nullptr;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &lvl,
                                               1, D3D11_SDK_VERSION, &scd, &swap, &dev, nullptr,
                                               &ctx);
    if (FAILED(hr)) {
        DestroyWindow(hwnd);
        return false;
    }
    vtable = *reinterpret_cast<void***>(ctx);
    swap->Release();
    ctx->Release();
    dev->Release();
    DestroyWindow(hwnd);
    return true;
}

}  // namespace camera
