#include "deskhubp/system/KeepAwake.h"

#include <gio/gio.h>
#include <gio/gunixfdlist.h>

#include <unistd.h>

#include "deskhubp/diag/Log.h"

namespace deskhubp {

namespace {

constexpr const char* kAppName = "Deskhub";
constexpr const char* kReason = "Screen sharing session is active";
constexpr int kCallTimeoutMs = 2000;

int g_sleepInhibitFd = -1;
uint32_t g_screenSaverCookie = 0;
bool g_screenSaverInhibited = false;

void AcquireSleepInhibit() {
    if (g_sleepInhibitFd >= 0) return;
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, nullptr);
    if (!bus) return;
    GError* error = nullptr;
    GUnixFDList* fdList = nullptr;
    GVariant* reply = g_dbus_connection_call_with_unix_fd_list_sync(bus,
        "org.freedesktop.login1", "/org/freedesktop/login1",
        "org.freedesktop.login1.Manager", "Inhibit",
        g_variant_new("(ssss)", "sleep:idle", kAppName, kReason, "block"),
        G_VARIANT_TYPE("(h)"), G_DBUS_CALL_FLAGS_NONE, kCallTimeoutMs, nullptr, &fdList,
        nullptr, &error);
    if (!reply) {
        LOGW("[KeepAwake] login1 Inhibit failed: %s", error ? error->message : "unknown");
        g_clear_error(&error);
        g_object_unref(bus);
        return;
    }
    gint32 handleIndex = -1;
    g_variant_get(reply, "(h)", &handleIndex);
    if (fdList) g_sleepInhibitFd = g_unix_fd_list_get(fdList, handleIndex, nullptr);
    g_variant_unref(reply);
    if (fdList) g_object_unref(fdList);
    g_object_unref(bus);
}

void ReleaseSleepInhibit() {
    if (g_sleepInhibitFd < 0) return;
    close(g_sleepInhibitFd);
    g_sleepInhibitFd = -1;
}

void AcquireScreenSaverInhibit() {
    if (g_screenSaverInhibited) return;
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    if (!bus) return;
    GVariant* reply = g_dbus_connection_call_sync(bus, "org.freedesktop.ScreenSaver",
        "/org/freedesktop/ScreenSaver", "org.freedesktop.ScreenSaver", "Inhibit",
        g_variant_new("(ss)", kAppName, kReason), G_VARIANT_TYPE("(u)"),
        G_DBUS_CALL_FLAGS_NONE, kCallTimeoutMs, nullptr, nullptr);
    if (reply) {
        g_variant_get(reply, "(u)", &g_screenSaverCookie);
        g_variant_unref(reply);
        g_screenSaverInhibited = true;
    }
    g_object_unref(bus);
}

void ReleaseScreenSaverInhibit() {
    if (!g_screenSaverInhibited) return;
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    if (bus) {
        GVariant* reply = g_dbus_connection_call_sync(bus, "org.freedesktop.ScreenSaver",
            "/org/freedesktop/ScreenSaver", "org.freedesktop.ScreenSaver", "UnInhibit",
            g_variant_new("(u)", g_screenSaverCookie), nullptr, G_DBUS_CALL_FLAGS_NONE,
            kCallTimeoutMs, nullptr, nullptr);
        if (reply) g_variant_unref(reply);
        g_object_unref(bus);
    }
    g_screenSaverCookie = 0;
    g_screenSaverInhibited = false;
}

}

void SetKeepAwakeActive(bool on) {
    if (on) {
        AcquireSleepInhibit();
        AcquireScreenSaverInhibit();
        return;
    }
    ReleaseScreenSaverInhibit();
    ReleaseSleepInhibit();
}

}
