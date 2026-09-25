/*
 * Host Win32: D3D11/DXGI present acceleration (see RoboWindowPresentWin32Gpu.h).
 */

#if defined(RoboWindowHasWin32) && RoboWindowHasWin32

#include "RoboWindowPresentWin32Gpu.h"

#include "RoboBase/RoboLog.h"
#include "RoboGraphics/RoboGraphicsPixelFormat.h"

#define COBJMACROS
#include <initguid.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdlib.h>
#include <string.h>

struct Win32GpuWindow {
    HWND hwnd;
    IDXGISwapChain *swapChain;
    ID3D11Texture2D *staging;
    int32_t width;
    int32_t height;
};

static ID3D11Device *gDevice;
static ID3D11DeviceContext *gContext;
static IDXGIFactory *gFactory;
static int gProbed;
static int gAvailable;

static int ForceCpuPresent(void)
{
    return getenv("ROBO_WINDOW_FORCE_CPU_PRESENT") != NULL;
}

static void ReleaseCom(void **pp)
{
    IUnknown *unk;

    if (!pp || !*pp) {
        return;
    }
    unk = (IUnknown *)*pp;
    unk->lpVtbl->Release(unk);
    *pp = NULL;
}

static Result EnsureDevice(void)
{
    D3D_FEATURE_LEVEL want[3];
    D3D_FEATURE_LEVEL got;
    HRESULT hr;
    IDXGIDevice *dxgiDevice = NULL;
    IDXGIAdapter *adapter = NULL;
    UINT flags;

    if (gProbed) {
        return gAvailable ? RoboOk : RoboNotSupported;
    }
    gProbed = 1;
    if (ForceCpuPresent()) {
        LogInfo("robo_window.win32.gpu: forced CPU present (ROBO_WINDOW_FORCE_CPU_PRESENT)");
        return RoboNotSupported;
    }

    want[0] = D3D_FEATURE_LEVEL_11_0;
    want[1] = D3D_FEATURE_LEVEL_10_1;
    want[2] = D3D_FEATURE_LEVEL_10_0;
    flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags, want, 3u,
                           D3D11_SDK_VERSION, &gDevice, &got, &gContext);
    if (FAILED(hr) || !gDevice || !gContext) {
        /* Retry without the debug layer — CI machines often lack it. */
        flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags, want, 3u,
                               D3D11_SDK_VERSION, &gDevice, &got, &gContext);
    }
    if (FAILED(hr) || !gDevice || !gContext) {
        LogInfo("robo_window.win32.gpu: no hardware D3D11 device (hr=0x%08lx) — CPU present",
                (unsigned long)hr);
        ReleaseCom((void **)&gContext);
        ReleaseCom((void **)&gDevice);
        return RoboNotSupported;
    }

    hr = gDevice->lpVtbl->QueryInterface(gDevice, &IID_IDXGIDevice, (void **)&dxgiDevice);
    if (FAILED(hr) || !dxgiDevice) {
        Win32GpuPresentShutdownGlobal();
        return RoboNotSupported;
    }
    hr = dxgiDevice->lpVtbl->GetAdapter(dxgiDevice, &adapter);
    ReleaseCom((void **)&dxgiDevice);
    if (FAILED(hr) || !adapter) {
        Win32GpuPresentShutdownGlobal();
        return RoboNotSupported;
    }
    hr = adapter->lpVtbl->GetParent(adapter, &IID_IDXGIFactory, (void **)&gFactory);
    ReleaseCom((void **)&adapter);
    if (FAILED(hr) || !gFactory) {
        Win32GpuPresentShutdownGlobal();
        return RoboNotSupported;
    }

    gAvailable = 1;
    LogInfo("robo_window.win32.gpu: hardware D3D11 present enabled (feature level 0x%x)",
            (unsigned)got);
    return RoboOk;
}

int Win32GpuPresentAvailable(void)
{
    return EnsureDevice() == RoboOk ? 1 : 0;
}

void Win32GpuPresentShutdownGlobal(void)
{
    ReleaseCom((void **)&gFactory);
    ReleaseCom((void **)&gContext);
    ReleaseCom((void **)&gDevice);
    gAvailable = 0;
    gProbed = 0;
}

void Win32GpuWindowDestroy(Win32GpuWindow **window)
{
    Win32GpuWindow *w;

    if (!window || !*window) {
        return;
    }
    w = *window;
    ReleaseCom((void **)&w->staging);
    ReleaseCom((void **)&w->swapChain);
    free(w);
    *window = NULL;
}

static Result EnsureWindow(HWND hwnd,
                                   Win32GpuWindow **window,
                                   int32_t width,
                                   int32_t height)
{
    Win32GpuWindow *w;
    DXGI_SWAP_CHAIN_DESC desc;
    HRESULT hr;
    D3D11_TEXTURE2D_DESC td;

    if (!hwnd || !window || width <= 0 || height <= 0) {
        return RoboInvalidArgument;
    }
    if (EnsureDevice() != RoboOk) {
        return RoboNotSupported;
    }

    w = *window;
    if (w && w->hwnd == hwnd && w->swapChain && w->staging && w->width == width &&
        w->height == height) {
        return RoboOk;
    }

    if (!w) {
        w = (Win32GpuWindow *)calloc(1, sizeof(*w));
        if (!w) {
            return RoboOutOfMemory;
        }
        w->hwnd = hwnd;
        *window = w;
    }

    ReleaseCom((void **)&w->staging);
    if (w->swapChain && (w->width != width || w->height != height || w->hwnd != hwnd)) {
        ReleaseCom((void **)&w->swapChain);
    }
    w->hwnd = hwnd;
    w->width = width;
    w->height = height;

    if (!w->swapChain) {
        memset(&desc, 0, sizeof(desc));
        desc.BufferCount = 2;
        desc.BufferDesc.Width = (UINT)width;
        desc.BufferDesc.Height = (UINT)height;
        desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.BufferDesc.RefreshRate.Numerator = 0;
        desc.BufferDesc.RefreshRate.Denominator = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.OutputWindow = hwnd;
        desc.SampleDesc.Count = 1;
        desc.Windowed = TRUE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        desc.Flags = 0;
        hr = gFactory->lpVtbl->CreateSwapChain(gFactory, (IUnknown *)gDevice, &desc,
                                               &w->swapChain);
        if (FAILED(hr) || !w->swapChain) {
            LogWarn("robo_window.win32.gpu: CreateSwapChain failed hr=0x%08lx",
                    (unsigned long)hr);
            return RoboNotSupported;
        }
        /* Stop DXGI from handling Alt+Enter. */
        (void)gFactory->lpVtbl->MakeWindowAssociation(gFactory, hwnd, DXGI_MWA_NO_ALT_ENTER);
    } else {
        hr = w->swapChain->lpVtbl->ResizeBuffers(w->swapChain, 0, (UINT)width, (UINT)height,
                                                 DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(hr)) {
            LogWarn("robo_window.win32.gpu: ResizeBuffers failed hr=0x%08lx",
                    (unsigned long)hr);
            ReleaseCom((void **)&w->swapChain);
            return RoboNotSupported;
        }
    }

    memset(&td, 0, sizeof(td));
    td.Width = (UINT)width;
    td.Height = (UINT)height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DYNAMIC;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = gDevice->lpVtbl->CreateTexture2D(gDevice, &td, NULL, &w->staging);
    if (FAILED(hr) || !w->staging) {
        LogWarn("robo_window.win32.gpu: CreateTexture2D staging failed hr=0x%08lx",
                (unsigned long)hr);
        return RoboNotSupported;
    }
    return RoboOk;
}

static uint32_t PackBgra(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return ((uint32_t)b) | ((uint32_t)g << 8) | ((uint32_t)r << 16) | ((uint32_t)a << 24);
}

static Result UploadSurface(Win32GpuWindow *w, const GfxSurface *surface)
{
    const uint8_t *src;
    uint32_t sw;
    uint32_t sh;
    size_t srcStride;
    GfxPixelFormat sfmt;
    uint8_t sbpp = 0u;
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr;
    uint32_t y;
    uint32_t copyW;
    uint32_t copyH;
    ID3D11Texture2D *backBuffer = NULL;

    if (!w || !w->staging || !surface || !gContext) {
        return RoboInvalidArgument;
    }
    sw = GfxSurfaceWidth(surface);
    sh = GfxSurfaceHeight(surface);
    sfmt = GfxSurfaceFormat(surface);
    srcStride = GfxSurfaceStride(surface);
    src = GfxSurfacePixelsConst(surface);
    if (!src || sw == 0u || sh == 0u) {
        return RoboInvalidArgument;
    }
    if (sfmt != RoboGfxFormatBgra8 && sfmt != RoboGfxFormatRgba8 &&
        sfmt != RoboGfxFormatRgb8) {
        return RoboNotSupported;
    }
    if (GfxPixelFormatBytesPerPixel(sfmt, &sbpp) != RoboOk || sbpp == 0u) {
        return RoboNotSupported;
    }

    hr = gContext->lpVtbl->Map(gContext, (ID3D11Resource *)w->staging, 0,
                               D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr) || !mapped.pData) {
        return RoboIoError;
    }

    copyW = sw;
    copyH = sh;
    if (copyW > (uint32_t)w->width) {
        copyW = (uint32_t)w->width;
    }
    if (copyH > (uint32_t)w->height) {
        copyH = (uint32_t)w->height;
    }

    for (y = 0u; y < (uint32_t)w->height; y++) {
        uint32_t *dst = (uint32_t *)((uint8_t *)mapped.pData + (size_t)y * mapped.RowPitch);
        uint32_t x;
        if (y >= copyH) {
            memset(dst, 0, (size_t)w->width * 4u);
            continue;
        }
        for (x = 0u; x < copyW; x++) {
            const uint8_t *p = src + (size_t)y * srcStride + (size_t)x * sbpp;
            uint8_t r;
            uint8_t g;
            uint8_t b;
            uint8_t a = 255u;
            if (sfmt == RoboGfxFormatBgra8) {
                b = p[0];
                g = p[1];
                r = p[2];
                a = p[3];
            } else if (sfmt == RoboGfxFormatRgba8) {
                r = p[0];
                g = p[1];
                b = p[2];
                a = p[3];
            } else {
                r = p[0];
                g = p[1];
                b = p[2];
            }
            dst[x] = PackBgra(r, g, b, a);
        }
        if (copyW < (uint32_t)w->width) {
            memset(dst + copyW, 0, (size_t)((uint32_t)w->width - copyW) * 4u);
        }
    }
    gContext->lpVtbl->Unmap(gContext, (ID3D11Resource *)w->staging, 0);

    hr = w->swapChain->lpVtbl->GetBuffer(w->swapChain, 0, &IID_ID3D11Texture2D,
                                         (void **)&backBuffer);
    if (FAILED(hr) || !backBuffer) {
        return RoboIoError;
    }
    gContext->lpVtbl->CopyResource(gContext, (ID3D11Resource *)backBuffer,
                                   (ID3D11Resource *)w->staging);
    ReleaseCom((void **)&backBuffer);
    return RoboOk;
}

Result Win32GpuPresentSurface(HWND hwnd,
                                      Win32GpuWindow **window,
                                      const GfxSurface *surface)
{
    RECT cr;
    int32_t cw;
    int32_t ch;
    Result status;
    HRESULT hr;

    if (!hwnd || !window || !surface) {
        return RoboInvalidArgument;
    }
    if (!GetClientRect(hwnd, &cr)) {
        return RoboIoError;
    }
    cw = (int32_t)(cr.right - cr.left);
    ch = (int32_t)(cr.bottom - cr.top);
    if (cw <= 0 || ch <= 0) {
        cw = (int32_t)GfxSurfaceWidth(surface);
        ch = (int32_t)GfxSurfaceHeight(surface);
    }
    if (cw <= 0 || ch <= 0) {
        return RoboInvalidArgument;
    }

    status = EnsureWindow(hwnd, window, cw, ch);
    if (status != RoboOk) {
        return status;
    }
    status = UploadSurface(*window, surface);
    if (status != RoboOk) {
        return status;
    }

    /* Sync interval 0: GPU flip without blocking the caller on VBlank (tests and
     * the shell stay responsive). Set ROBO_WINDOW_GPU_VSYNC=1 for paced Present. */
    {
        UINT sync = 0u;
        if (getenv("ROBO_WINDOW_GPU_VSYNC") != NULL) {
            sync = 1u;
        }
        hr = (*window)->swapChain->lpVtbl->Present((*window)->swapChain, sync, 0u);
    }
    if (FAILED(hr)) {
        LogWarn("robo_window.win32.gpu: Present failed hr=0x%08lx — falling back",
                (unsigned long)hr);
        return RoboNotSupported;
    }
    LogDebug("robo_window.win32.gpu: present %dx%d", (int)cw, (int)ch);
    return RoboOk;
}

#endif /* RoboWindowHasWin32 */
