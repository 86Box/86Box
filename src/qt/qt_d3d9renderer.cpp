#include "qt_mainwindow.hpp"
#include "qt_d3d9renderer.hpp"
#include <QResizeEvent>
#include <QTimer>

extern "C"
{
#include <86box/86box.h>
#include <86box/video.h>
}

D3D9Renderer::D3D9Renderer(QWidget *parent, int monitor_index)
    : QWidget{parent}, RendererCommon()
{
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setAutoFillBackground(true);
    setPalette(pal);

    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_OpaquePaintEvent);

    windowHandle = (HWND)winId();
    surfaceInUse = true;

    RendererCommon::parentWidget = parent;

    this->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    this->m_monitor_index = monitor_index;
}

D3D9Renderer::~D3D9Renderer()
{
    finalize();
}

void D3D9Renderer::finalize()
{
    if (!finalized) {
        while (surfaceInUse) {}
        finalized = true;
    }
    surfaceInUse = true;
    if (d3d9surface) { d3d9surface->Release(); d3d9surface = nullptr;}
    if (d3d9dev) { d3d9dev->Release(); d3d9dev = nullptr; }
    if (d3d9) { d3d9->Release(); d3d9 = nullptr; };
}

void D3D9Renderer::hideEvent(QHideEvent *event)
{
    finalize();
}

void D3D9Renderer::showEvent(QShowEvent *event)
{
    params = {};

    if (FAILED(Direct3DCreate9Ex(D3D_SDK_VERSION, &d3d9))) {
        return error("Failed to create Direct3D 9 context");
    }

    params.Windowed                   = true;
    params.SwapEffect                 = D3DSWAPEFFECT_FLIPEX;
    params.BackBufferWidth            = width() * devicePixelRatioF();
    params.BackBufferHeight           = height() * devicePixelRatioF();
    params.BackBufferCount            = 1;
    params.FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;
    params.PresentationInterval       = D3DPRESENT_INTERVAL_IMMEDIATE;
    params.hDeviceWindow              = windowHandle;

    HRESULT result = d3d9->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, windowHandle, D3DCREATE_MULTITHREADED | D3DCREATE_HARDWARE_VERTEXPROCESSING, &params, nullptr, &d3d9dev);
    if (FAILED(result)) result = d3d9->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_REF, windowHandle, D3DCREATE_MULTITHREADED | D3DCREATE_SOFTWARE_VERTEXPROCESSING, &params, nullptr, &d3d9dev);
    if (FAILED(result)) {
        return error("Failed to create Direct3D 9 device");
    }

    result = d3d9dev->CreateOffscreenPlainSurface(2048, 2048, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &d3d9surface, nullptr);
    if (FAILED(result)) result = d3d9dev->CreateOffscreenPlainSurface(1024, 1024, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &d3d9surface, nullptr);
    if (FAILED(result)) {
        return error("Failed to create Direct3D 9 surface");
    }
    if (!alreadyInitialized) {
        emit initialized();
        alreadyInitialized = true;
    }
    surfaceInUse = false;
    finalized = false;
}

void D3D9Renderer::paintEvent(QPaintEvent *event)
{
    IDirect3DSurface9* backbuffer = nullptr;
    RECT srcRect, dstRect;
    HRESULT result = d3d9dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer);

    if (FAILED(result)) {
        return;
    }

    srcRect.top = source.top();
    srcRect.bottom = source.bottom();
    srcRect.left = source.left();
    srcRect.right = source.right();
    dstRect.top = destination.top();
    dstRect.bottom = destination.bottom();
    dstRect.left = destination.left();
    dstRect.right = destination.right();
    d3d9dev->BeginScene();
    d3d9dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000, 0, 0);
    while (surfaceInUse) {}
    surfaceInUse = true;
    d3d9dev->StretchRect(d3d9surface, &srcRect, backbuffer, &dstRect, video_filter_method == 0 ? D3DTEXF_POINT : D3DTEXF_LINEAR);
    result = d3d9dev->EndScene();
    surfaceInUse = false;
    if (SUCCEEDED(result)) {
        if (FAILED(d3d9dev->PresentEx(nullptr, nullptr, 0, nullptr, 0))) {
            finalize();
            showEvent(nullptr);
        }
    }
}

bool D3D9Renderer::event(QEvent *event)
{
    bool res = false;
    if (!eventDelegate(event, res)) return QWidget::event(event);
    return res;
}

void D3D9Renderer::resizeEvent(QResizeEvent *event)
{
    onResize(event->size().width() * devicePixelRatioF(), event->size().height() * devicePixelRatioF());

    params.BackBufferWidth = event->size().width() * devicePixelRatioF();
    params.BackBufferHeight = event->size().height() * devicePixelRatioF();
    if (d3d9dev) d3d9dev->Reset(&params);
    QWidget::resizeEvent(event);
}

void D3D9Renderer::blit(int x, int y, int w, int h)
{
    if (blitDummied || (x < 0) || (y < 0) || (w <= 0) || (h <= 0) || (w > 2048) || (h > 2048) || (monitors[m_monitor_index].target_buffer == NULL) || surfaceInUse) {
        video_blit_complete_monitor(m_monitor_index);
        return;
    }
    surfaceInUse = true;
    auto origSource = source;
    source.setRect(x, y, w, h);
    RECT srcRect;
    D3DLOCKED_RECT lockRect;
    srcRect.top = source.top();
    srcRect.bottom = source.bottom();
    srcRect.left = source.left();
    srcRect.right = source.right();

    if (monitors[m_monitor_index].mon_screenshots) {
        video_screenshot_monitor((uint32_t *) &(monitors[m_monitor_index].target_buffer->line[y][x]), 0, 0, 2048, m_monitor_index);
    }
    if (SUCCEEDED(d3d9surface->LockRect(&lockRect, &srcRect, 0))) {
        for (int y1 = 0; y1 < h; y1++) {
            video_copy(((uint8_t*)lockRect.pBits) + (y1 * lockRect.Pitch), &(monitors[m_monitor_index].target_buffer->line[y + y1][x]), w * 4);
        }
        video_blit_complete_monitor(m_monitor_index);
        d3d9surface->UnlockRect();
    }
    else video_blit_complete_monitor(m_monitor_index);
    if (origSource != source) onResize(this->width() * devicePixelRatioF(), this->height() * devicePixelRatioF());
    surfaceInUse = false;
    QTimer::singleShot(0, this, [this] { this->update(); });
}
