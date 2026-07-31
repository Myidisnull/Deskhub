#include "gtk/ViewerWindow.h"

#include <gdk/gdkkeysyms.h>

#include <algorithm>
#include <cstdio>
#include <utility>

#include "deskhubp/diag/Log.h"
#include "gtk/GtkUtil.h"
#include "input/LinuxKeyMap.h"

#include "deskhub/media/ViewFit.h"

namespace {

constexpr guint kKeyLockPointer = GDK_KEY_F9;
constexpr guint kKeyPauseInput = GDK_KEY_F10;

constexpr int32_t kWheelDelta = 120;

constexpr int kInitialW = 1024;
constexpr int kInitialH = 600;

GdkRectangle WorkArea(GtkWidget* w) {
    GdkRectangle wa{0, 0, kInitialW, kInitialH};
    GdkDisplay* d = gtk_widget_get_display(w);
    if (!d) return wa;
    GdkMonitor* m = nullptr;
    if (GdkWindow* gw = gtk_widget_get_window(w)) m = gdk_display_get_monitor_at_window(d, gw);
    if (!m) m = gdk_display_get_primary_monitor(d);
    if (!m) m = gdk_display_get_monitor(d, 0);
    if (m) gdk_monitor_get_workarea(m, &wa);
    return wa;
}

void LargestScreenPixels(GtkWidget* w, uint32_t& outW, uint32_t& outH) {
    outW = outH = 0;
    GdkDisplay* d = gtk_widget_get_display(w);
    if (!d) return;
    const int n = gdk_display_get_n_monitors(d);
    long bestArea = 0;
    for (int i = 0; i < n; ++i) {
        GdkMonitor* m = gdk_display_get_monitor(d, i);
        if (!m) continue;
        GdkRectangle g{};
        gdk_monitor_get_geometry(m, &g);
        const int s = gdk_monitor_get_scale_factor(m);
        const long pw = long(g.width) * (s > 0 ? s : 1), ph = long(g.height) * (s > 0 ? s : 1);
        if (pw <= 0 || ph <= 0 || pw * ph <= bestArea) continue;
        bestArea = pw * ph;
        outW = uint32_t(pw);
        outH = uint32_t(ph);
    }
}

}

ViewerWindow* ViewerWindow::Open(const NetAddr& server, uint8_t sourceId,
    const std::string& sourceName, std::function<void()> onClosed) {
    auto* v = new ViewerWindow();
    v->onClosed_ = std::move(onClosed);
    if (!v->Build(server, sourceId, sourceName)) {
        delete v;
        return nullptr;
    }
    return v;
}

ViewerWindow::~ViewerWindow() {
    if (statusTimer_) g_source_remove(statusTimer_);
    loop_.Stop();
}

bool ViewerWindow::Build(const NetAddr& server, uint8_t sourceId, const std::string& sourceName) {
    baseTitle_ = "Deskhub - viewing";
    if (!sourceName.empty()) baseTitle_ += ": " + sourceName;

    window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window_), baseTitle_.c_str());
    gtk_window_set_default_size(GTK_WINDOW(window_), kInitialW, kInitialH);

    glArea_ = gtk_gl_area_new();
    gtk_gl_area_set_has_depth_buffer(GTK_GL_AREA(glArea_), FALSE);
    gtk_gl_area_set_has_stencil_buffer(GTK_GL_AREA(glArea_), FALSE);
    gtk_gl_area_set_auto_render(GTK_GL_AREA(glArea_), FALSE);
    gtk_container_add(GTK_CONTAINER(window_), glArea_);

    g_signal_connect(glArea_, "render", G_CALLBACK(OnRender), this);
    g_signal_connect(glArea_, "realize", G_CALLBACK(OnRealize), this);
    g_signal_connect(glArea_, "unrealize", G_CALLBACK(OnUnrealize), this);

    gtk_widget_add_events(window_, GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK |
                                       GDK_BUTTON_RELEASE_MASK | GDK_SCROLL_MASK |
                                       GDK_SMOOTH_SCROLL_MASK | GDK_KEY_PRESS_MASK |
                                       GDK_KEY_RELEASE_MASK | GDK_FOCUS_CHANGE_MASK);
    g_signal_connect(window_, "key-press-event", G_CALLBACK(OnKey), this);
    g_signal_connect(window_, "key-release-event", G_CALLBACK(OnKey), this);
    g_signal_connect(window_, "motion-notify-event", G_CALLBACK(OnMotion), this);
    g_signal_connect(window_, "button-press-event", G_CALLBACK(OnButton), this);
    g_signal_connect(window_, "button-release-event", G_CALLBACK(OnButton), this);
    g_signal_connect(window_, "scroll-event", G_CALLBACK(OnScroll), this);
    g_signal_connect(window_, "focus-out-event", G_CALLBACK(OnFocusOut), this);
    g_signal_connect(window_, "destroy", G_CALLBACK(OnDestroy), this);

    uint32_t sw = 0, sh = 0;
    LargestScreenPixels(window_, sw, sh);
    if (!loop_.Start(server, sourceId, &renderer_, sw, sh)) {
        gtk_widget_destroy(window_);
        return false;
    }

    tickId_ = gtk_widget_add_tick_callback(glArea_, OnTick, this, nullptr);
    statusTimer_ = g_timeout_add(500, OnStatusTimer, this);

    UpdateTitle();
    gtk_widget_show_all(window_);
    return true;
}

void ViewerWindow::VideoRect(int& x, int& y, int& w, int& h) const {
    GtkAllocation alloc{};
    gtk_widget_get_allocation(glArea_, &alloc);
    const int vw = int(loop_.videoWidth()), vh = int(loop_.videoHeight());
    x = alloc.x;
    y = alloc.y;
    w = alloc.width;
    h = alloc.height;
    if (vw <= 0 || vh <= 0 || w <= 0 || h <= 0) return;

    int dw = w, dh = int(int64_t(w) * vh / vw);
    if (dh > h) {
        dh = h;
        dw = int(int64_t(h) * vw / vh);
    }
    x += (w - dw) / 2;
    y += (h - dh) / 2;
    w = dw;
    h = dh;
}

bool ViewerWindow::InContent(double px, double py) const {
    GtkAllocation a{};
    gtk_widget_get_allocation(glArea_, &a);
    return px >= a.x && py >= a.y && px < a.x + a.width && py < a.y + a.height;
}

bool ViewerWindow::ToNormalized(double px, double py, int32_t& nx, int32_t& ny) const {
    int rx = 0, ry = 0, rw = 0, rh = 0;
    VideoRect(rx, ry, rw, rh);
    const deskhub::ViewRect rect{double(rx), double(ry), double(rw), double(rh)};
    return deskhub::NormalizePointer(px, py, rect, nx, ny);
}

void ViewerWindow::OnRealize(GtkGLArea* area, gpointer user) {
    auto* self = static_cast<ViewerWindow*>(user);
    gtk_gl_area_make_current(area);
    if (gtk_gl_area_get_error(area)) {
        LOGE("[Viewer] GtkGLArea failed to create an OpenGL context.");
        return;
    }
    self->renderer_.Realize();
}

void ViewerWindow::OnUnrealize(GtkGLArea* area, gpointer user) {
    auto* self = static_cast<ViewerWindow*>(user);
    gtk_gl_area_make_current(area);
    self->renderer_.Unrealize();
}

gboolean ViewerWindow::OnRender(GtkGLArea* area, GdkGLContext*, gpointer user) {
    auto* self = static_cast<ViewerWindow*>(user);
    GtkAllocation alloc{};
    gtk_widget_get_allocation(GTK_WIDGET(area), &alloc);
    const int scale = gtk_widget_get_scale_factor(GTK_WIDGET(area));
    if (!self->renderer_.Render(alloc.width * scale, alloc.height * scale))
        self->renderer_.ClearBlack();
    return TRUE;
}

gboolean ViewerWindow::OnTick(GtkWidget* w, GdkFrameClock*, gpointer) {
    gtk_gl_area_queue_render(GTK_GL_AREA(w));
    return G_SOURCE_CONTINUE;
}

gboolean ViewerWindow::OnStatusTimer(gpointer user) {
    auto* self = static_cast<ViewerWindow*>(user);
    if (self->loop_.phase() == ClientLoop::Phase::Ended) {
        self->EndSession();
        return G_SOURCE_REMOVE;
    }
    self->SizeToVideo();
    self->UpdateTitle();
    return G_SOURCE_CONTINUE;
}

void ViewerWindow::UpdateTitle() {
    std::string stats = loop_.StatusLine();
    if (stats.empty()) stats = "connecting...";

    std::string title = baseTitle_ + " — " + stats + " · " +
                        (pointerLocked_ ? "Mouse locked - press F9 or Esc to release"
                                        : "Press F9 to lock mouse");
    if (inputPaused_) title += " · input paused (F10)";

    if (title == shownTitle_) return;
    shownTitle_ = std::move(title);
    gtk_window_set_title(GTK_WINDOW(window_), shownTitle_.c_str());
}

void ViewerWindow::SizeToVideo() {
    if (sizedToVideo_) return;
    const int vw = int(loop_.videoWidth()), vh = int(loop_.videoHeight());
    if (vw <= 0 || vh <= 0) return;
    sizedToVideo_ = true;

    const GdkRectangle wa = WorkArea(window_);
    const int maxW = std::max(320, wa.width - 48), maxH = std::max(240, wa.height - 48);
    const double scale = std::min({1.0, double(maxW) / vw, double(maxH) / vh});
    gtk_window_resize(GTK_WINDOW(window_), std::max(1, int(vw * scale)),
        std::max(1, int(vh * scale)));
}

void ViewerWindow::EndSession() {
    if (ended_) return;
    ended_ = true;
    statusTimer_ = 0;

    const std::string why = loop_.EndReason();
    gtk_widget_destroy(window_);
    RunOnMain([why] {
        ShowInfo(nullptr, "Connection ended", why.empty() ? std::string("disconnected") : why);
    });
}

void ViewerWindow::SetPointerLocked(bool locked) {
    if (locked == pointerLocked_) return;
    pointerLocked_ = locked;
    GdkWindow* gw = gtk_widget_get_window(window_);
    if (!gw) return;

    GdkDisplay* display = gtk_widget_get_display(window_);
    GdkSeat* seat = gdk_display_get_default_seat(display);
    if (locked) {
        GdkCursor* blank = gdk_cursor_new_for_display(display, GDK_BLANK_CURSOR);
        gdk_seat_grab(seat, gw, GDK_SEAT_CAPABILITY_ALL_POINTING, FALSE, blank, nullptr, nullptr,
            nullptr);
        if (blank) g_object_unref(blank);
    } else {
        gdk_seat_ungrab(seat);
    }
    haveLastPos_ = false;
    UpdateTitle();
}

gboolean ViewerWindow::OnKey(GtkWidget*, GdkEventKey* e, gpointer user) {
    auto* self = static_cast<ViewerWindow*>(user);
    const bool down = e->type == GDK_KEY_PRESS;

    if (down && e->keyval == kKeyLockPointer) {
        self->SetPointerLocked(!self->pointerLocked_);
        return TRUE;
    }
    if (down && e->keyval == kKeyPauseInput) {
        self->inputPaused_ = !self->inputPaused_;
        if (self->inputPaused_) self->loop_.ReleaseAllInput();
        self->UpdateTitle();
        return TRUE;
    }
    if (down && e->keyval == GDK_KEY_Escape && self->pointerLocked_) {
        self->SetPointerLocked(false);
        return TRUE;
    }

    if (self->inputPaused_) return TRUE;

    int32_t vk = 0, scan = 0;
    const uint16_t evdev = linuxkeys::GdkKeycodeToEvdev(e->hardware_keycode);
    if (!linuxkeys::EvdevToWin(evdev, vk, scan)) return TRUE;
    self->loop_.QueueKey(vk, scan, down);
    return TRUE;
}

gboolean ViewerWindow::OnMotion(GtkWidget*, GdkEventMotion* e, gpointer user) {
    auto* self = static_cast<ViewerWindow*>(user);
    if (!self->pointerLocked_ && !self->InContent(e->x, e->y)) return FALSE;
    if (self->inputPaused_) return TRUE;

    if (!self->pointerLocked_) {
        int32_t nx = 0, ny = 0;
        if (self->ToNormalized(e->x, e->y, nx, ny)) self->loop_.QueueMouseMoveAbs(nx, ny);
        self->haveLastPos_ = false;
        return TRUE;
    }

    GtkAllocation alloc{};
    gtk_widget_get_allocation(self->window_, &alloc);
    const double cx = alloc.width / 2.0, cy = alloc.height / 2.0;

    if (self->haveLastPos_) {
        const int32_t dx = int32_t(e->x - self->lastPx_);
        const int32_t dy = int32_t(e->y - self->lastPy_);
        if (dx || dy) self->loop_.QueueMouseMoveRel(dx, dy);
    }
    self->lastPx_ = cx;
    self->lastPy_ = cy;
    self->haveLastPos_ = true;

    GdkWindow* gw = gtk_widget_get_window(self->window_);
    if (gw) {
        int ox = 0, oy = 0;
        gdk_window_get_origin(gw, &ox, &oy);
        gdk_device_warp(gdk_event_get_device(reinterpret_cast<GdkEvent*>(e)),
            gtk_widget_get_screen(self->window_), ox + int(cx), oy + int(cy));
    }
    return TRUE;
}

gboolean ViewerWindow::OnButton(GtkWidget*, GdkEventButton* e, gpointer user) {
    auto* self = static_cast<ViewerWindow*>(user);
    if (!self->pointerLocked_ && !self->InContent(e->x, e->y)) return FALSE;
    if (self->inputPaused_) return TRUE;
    if (e->type != GDK_BUTTON_PRESS && e->type != GDK_BUTTON_RELEASE) return TRUE;

    int32_t btn = 0;
    switch (e->button) {
        case 1: btn = int32_t(deskhub::MouseButton::Left); break;
        case 2: btn = int32_t(deskhub::MouseButton::Middle); break;
        case 3: btn = int32_t(deskhub::MouseButton::Right); break;
        case 8: btn = int32_t(deskhub::MouseButton::X1); break;
        case 9: btn = int32_t(deskhub::MouseButton::X2); break;
        default: return TRUE;
    }
    if (!self->pointerLocked_) {
        int32_t nx = 0, ny = 0;
        if (self->ToNormalized(e->x, e->y, nx, ny)) self->loop_.QueueMouseMoveAbs(nx, ny);
    }
    self->loop_.QueueMouseButton(btn, e->type == GDK_BUTTON_PRESS);
    return TRUE;
}

gboolean ViewerWindow::OnScroll(GtkWidget*, GdkEventScroll* e, gpointer user) {
    auto* self = static_cast<ViewerWindow*>(user);
    if (!self->pointerLocked_ && !self->InContent(e->x, e->y)) return FALSE;
    if (self->inputPaused_) return TRUE;

    int32_t delta = 0;
    switch (e->direction) {
        case GDK_SCROLL_UP: delta = kWheelDelta; break;
        case GDK_SCROLL_DOWN: delta = -kWheelDelta; break;
        case GDK_SCROLL_SMOOTH:
            delta = int32_t(-e->delta_y * kWheelDelta);
            break;
        default: return TRUE;
    }
    self->loop_.QueueMouseWheel(delta);
    return TRUE;
}

gboolean ViewerWindow::OnFocusOut(GtkWidget*, GdkEventFocus*, gpointer user) {
    auto* self = static_cast<ViewerWindow*>(user);
    self->loop_.ReleaseAllInput();
    self->SetPointerLocked(false);
    return FALSE;
}

void ViewerWindow::OnDestroy(GtkWidget*, gpointer user) {
    auto* self = static_cast<ViewerWindow*>(user);
    self->tickId_ = 0;
    self->window_ = nullptr;
    auto done = std::move(self->onClosed_);
    delete self;
    if (done) done();
}
