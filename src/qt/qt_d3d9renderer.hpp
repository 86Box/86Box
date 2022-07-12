#ifndef D3D9RENDERER_HPP
#define D3D9RENDERER_HPP

#include <QWidget>
#include "qt_renderercommon.hpp"

#include <windows.h>
#include <d3d9.h>
#include <atomic>

class D3D9Renderer : public QWidget, public RendererCommon
{
    Q_OBJECT
public:
    explicit D3D9Renderer(QWidget *parent = nullptr, int monitor_index = 0);
    ~D3D9Renderer();
    bool hasBlitFunc() override { return true; }
    void blit(int x, int y, int w, int h) override;
    void finalize() override;

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    bool event(QEvent* event) override;
    QPaintEngine* paintEngine() const override { return nullptr; }

signals:
    void initialized();
    void error(QString);

private:
    HWND windowHandle = 0;
    D3DPRESENT_PARAMETERS params{};
    IDirect3D9Ex* d3d9 = nullptr;
    IDirect3DDevice9Ex* d3d9dev = nullptr;
    IDirect3DSurface9* d3d9surface = nullptr;

    std::atomic<bool> surfaceInUse{false}, finalized{false};
    bool alreadyInitialized = false;
    int m_monitor_index = 0;
};

#endif // D3D9RENDERER_HPP
