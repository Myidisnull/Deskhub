#pragma once
#include <gtk/gtk.h>

#include <functional>
#include <memory>
#include <string>

#include "ClientLoop.h"
#include "deskhubp/UdpSocket.h"
#include "render/VideoRenderer.h"

class ViewerWindow {
public:
    static ViewerWindow* Open(const NetAddr& server, uint8_t sourceId,
        const std::string& sourceName, std::function<void()> onClosed);

private:
    ViewerWindow() = default;
    ~ViewerWindow();
    ViewerWindow(const ViewerWindow&) = delete;
    ViewerWindow& operator=(const ViewerWindow&) = delete;

    bool Build(const NetAddr& server, uint8_t sourceId, const std::string& sourceName);

    void VideoRect(int& x, int& y, int& w, int& h) const;
    bool ToNormalized(double px, double py, int32_t& nx, int32_t& ny) const;

    bool InContent(double px, double py) const;

    void SetPointerLocked(bool locked);
    void UpdateTitle();
    void SizeToVideo();
    void EndSession();

    static gboolean OnRender(GtkGLArea* area, GdkGLContext* ctx, gpointer user);
    static void OnRealize(GtkGLArea* area, gpointer user);
    static void OnUnrealize(GtkGLArea* area, gpointer user);
    static gboolean OnKey(GtkWidget* w, GdkEventKey* e, gpointer user);
    static gboolean OnMotion(GtkWidget* w, GdkEventMotion* e, gpointer user);
    static gboolean OnButton(GtkWidget* w, GdkEventButton* e, gpointer user);
    static gboolean OnScroll(GtkWidget* w, GdkEventScroll* e, gpointer user);
    static gboolean OnFocusOut(GtkWidget* w, GdkEventFocus* e, gpointer user);
    static gboolean OnTick(GtkWidget* w, GdkFrameClock* clock, gpointer user);
    static gboolean OnStatusTimer(gpointer user);
    static void OnDestroy(GtkWidget* w, gpointer user);

    GtkWidget* window_ = nullptr;
    GtkWidget* glArea_ = nullptr;
    guint statusTimer_ = 0;
    guint tickId_ = 0;

    std::string baseTitle_;
    std::string shownTitle_;
    bool sizedToVideo_ = false;
    bool ended_ = false;
    std::function<void()> onClosed_;

    VideoRenderer renderer_;
    ClientLoop loop_;

    bool pointerLocked_ = false;
    bool inputPaused_ = false;
    double lastPx_ = 0, lastPy_ = 0;
    bool haveLastPos_ = false;
};
