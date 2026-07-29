// =============================================================================
// PortalScreenCast.cpp — trình tự D-Bus với xdg-desktop-portal, cài bằng GDBus.
//
// BỐ CỤC
//   Phần ẩn danh ở đầu: tiện ích chung — sinh handle_token, dựng object path của
//   Request, và PortalRequest() (gọi phương thức + CHỜ tín hiệu Response).
//   Open(): bốn bước CreateSession → SelectSources → Start → OpenPipeWireRemote.
//   Close(): đóng session + fd.
//
// ⚠ BA CÁI BẪY CỦA API PORTAL, GHI LẠI ĐỂ KHÔNG DẪM LẠI
//
//   1. PHẢI ĐĂNG KÝ NHẬN Response TRƯỚC KHI GỌI PHƯƠNG THỨC. Portal có thể phát
//      tín hiệu trước khi lời gọi D-Bus kịp trả về object path, và tín hiệu không
//      được phát lại. Nên ta TỰ dựng object path của Request từ handle_token của
//      chính mình (tài liệu portal khuyến nghị đúng cách này) và subscribe trước.
//      Làm ngược lại thì lỗi chỉ hiện ra dưới dạng "thỉnh thoảng Share bị treo" —
//      một cuộc đua rất khó tái hiện.
//
//   2. OBJECT PATH CỦA REQUEST DÙNG TÊN BUS ĐÃ BIẾN ĐỔI. ":1.234" phải thành
//      "1_234": bỏ dấu ':' đầu và đổi mọi '.' thành '_'. Sai một ký tự thì
//      subscribe vào một path không ai phát, và lại treo im lặng.
//
//   3. GDBus GIAO CALLBACK TÍN HIỆU VÀO THREAD-DEFAULT MAIN CONTEXT TẠI THỜI ĐIỂM
//      SUBSCRIBE. Ta chạy ngoài main thread nên phải tự dựng một GMainContext và
//      push nó làm thread-default TRƯỚC khi subscribe, rồi chạy GMainLoop trên
//      chính context đó. Không làm thế thì callback rơi vào main context của GTK —
//      mà thread này đang chặn chờ nó, nên deadlock.
//
// VÌ SAO CÓ HẠN 180 GIÂY
//   Hộp thoại chọn màn hình chờ NGƯỜI THẬT bấm, nên không thể đặt hạn kiểu mạng
//   (vài giây). Nhưng cũng không thể chờ vô hạn: người dùng bỏ đi, hoặc portal
//   chết giữa chừng, thì thread Share phải thoát được để UI trở lại bình thường.
//
// LIÊN QUAN: capture/PortalScreenCast.h (lý do kiến trúc + trình tự),
//            capture/ScreenCapture.cpp (bên tiêu thụ nodeId + fd)
// =============================================================================
#include "capture/PortalScreenCast.h"

#include <gio/gio.h>
#include <gio/gunixfdlist.h>

#include <unistd.h>

#include <atomic>
#include <cstdio>

#include "Log.h"

namespace {

constexpr const char* kBusName = "org.freedesktop.portal.Desktop";
constexpr const char* kObjPath = "/org/freedesktop/portal/desktop";
constexpr const char* kScreenCast = "org.freedesktop.portal.ScreenCast";
constexpr const char* kRequestIface = "org.freedesktop.portal.Request";
constexpr const char* kSessionIface = "org.freedesktop.portal.Session";

// Hộp thoại chờ người thật bấm — xem lý do ở đầu file.
constexpr int kQuestionTimeoutSec = 180;

// Bitmask của SelectSources.types (đặc tả portal).
constexpr uint32_t kSourceTypeMonitor = 1;

// SelectSources.cursor_mode (đặc tả portal).
constexpr uint32_t kCursorModeHidden = 1;
constexpr uint32_t kCursorModeEmbedded = 2;

// SelectSources.persist_mode = 2 (persistent) — portal nhớ lựa chọn và trả về
// restore_token. Chỉ có từ ScreenCast version 4.
constexpr uint32_t kPersistModePersistent = 2;
constexpr uint32_t kScreenCastVersionPersist = 4;

// Bộ đếm để mọi handle_token trong một lần chạy app đều khác nhau. Portal dựng
// object path từ token này; trùng token giữa hai lời gọi đang chạy song song sẽ
// khiến hai Response lẫn vào nhau.
std::atomic<uint64_t> g_tokenCounter{0};

std::string NextToken(const char* prefix) {
    char b[64];
    std::snprintf(b, sizeof(b), "deskhub_%s_%llu", prefix,
        (unsigned long long)g_tokenCounter.fetch_add(1) + 1);
    return b;
}

// ":1.234" -> "1_234". Xem bẫy số 2 ở đầu file.
std::string SenderPathPart(GDBusConnection* conn) {
    const char* unique = g_dbus_connection_get_unique_name(conn);
    std::string s = unique ? unique : "";
    if (!s.empty() && s[0] == ':') s.erase(0, 1);
    for (char& c : s)
        if (c == '.') c = '_';
    return s;
}

std::string RequestPath(GDBusConnection* conn, const std::string& token) {
    return std::string("/org/freedesktop/portal/desktop/request/") + SenderPathPart(conn) + "/" +
           token;
}

// Trạng thái của một lần chờ Response.
struct ResponseWait {
    GMainLoop* loop = nullptr;
    guint32 code = 2;            // mặc định = "ended" (thất bại), phòng khi hết hạn
    GVariant* results = nullptr; // a{sv}, sở hữu
};

void OnResponse(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*,
    GVariant* params, gpointer user) {
    auto* w = static_cast<ResponseWait*>(user);
    guint32 code = 2;
    GVariant* results = nullptr;
    g_variant_get(params, "(u@a{sv})", &code, &results);
    w->code = code;
    w->results = results; // g_variant_get với @ đã cho ta một ref
    g_main_loop_quit(w->loop);
}

gboolean OnWaitTimeout(gpointer user) {
    auto* w = static_cast<ResponseWait*>(user);
    LOGE("[Portal] No Response signal within %d s — giving up.", kQuestionTimeoutSec);
    g_main_loop_quit(w->loop);
    return G_SOURCE_REMOVE;
}

// Gọi một phương thức portal kiểu Request rồi CHỜ tín hiệu Response.
//
// `params` là GVariant floating chứa handle_token khớp với `token` — người gọi
// dựng nó, vì mỗi phương thức có chữ ký khác nhau.
// Trả về a{sv} results (người gọi unref) hoặc nullptr nếu huỷ/lỗi/hết hạn.
GVariant* PortalRequest(GDBusConnection* conn, GMainContext* ctx, const char* method,
    GVariant* params, const std::string& token, std::string* err) {
    const std::string reqPath = RequestPath(conn, token);

    ResponseWait wait;
    wait.loop = g_main_loop_new(ctx, FALSE);

    // Subscribe TRƯỚC khi gọi — bẫy số 1 ở đầu file.
    const guint sub = g_dbus_connection_signal_subscribe(conn, kBusName, kRequestIface, "Response",
        reqPath.c_str(), nullptr, G_DBUS_SIGNAL_FLAGS_NONE, OnResponse, &wait, nullptr);

    GError* gerr = nullptr;
    GVariant* ret = g_dbus_connection_call_sync(conn, kBusName, kObjPath, kScreenCast, method,
        params, G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &gerr);
    if (!ret) {
        if (err) *err = std::string(method) + ": " + (gerr ? gerr->message : "D-Bus call failed");
        if (gerr) g_error_free(gerr);
        g_dbus_connection_signal_unsubscribe(conn, sub);
        g_main_loop_unref(wait.loop);
        return nullptr;
    }
    g_variant_unref(ret);

    GSource* to = g_timeout_source_new_seconds(kQuestionTimeoutSec);
    g_source_set_callback(to, OnWaitTimeout, &wait, nullptr);
    g_source_attach(to, ctx);

    g_main_loop_run(wait.loop);

    g_source_destroy(to);
    g_source_unref(to);
    g_dbus_connection_signal_unsubscribe(conn, sub);
    g_main_loop_unref(wait.loop);

    // code: 0 = thành công, 1 = người dùng huỷ, 2 = kết thúc vì lý do khác.
    if (wait.code != 0) {
        if (wait.results) g_variant_unref(wait.results);
        if (err)
            *err = wait.code == 1 ? "cancelled by the user"
                                  : std::string(method) + ": portal ended the request";
        return nullptr;
    }
    return wait.results;
}

// Đọc một property u của giao diện ScreenCast. 0 nếu không đọc được — người gọi
// coi đó là "portal đời cũ" và tự lùi về hành vi tối thiểu.
uint32_t ReadUintProperty(GDBusConnection* conn, const char* name) {
    GError* gerr = nullptr;
    GVariant* ret = g_dbus_connection_call_sync(conn, kBusName, kObjPath,
        "org.freedesktop.DBus.Properties", "Get",
        g_variant_new("(ss)", kScreenCast, name), G_VARIANT_TYPE("(v)"),
        G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, &gerr);
    if (!ret) {
        if (gerr) g_error_free(gerr);
        return 0;
    }
    GVariant* inner = nullptr;
    g_variant_get(ret, "(v)", &inner);
    uint32_t v = 0;
    if (inner && g_variant_is_of_type(inner, G_VARIANT_TYPE_UINT32)) v = g_variant_get_uint32(inner);
    if (inner) g_variant_unref(inner);
    g_variant_unref(ret);
    return v;
}

} // namespace

PortalScreenCast& PortalScreenCast::Instance() {
    static PortalScreenCast inst;
    return inst;
}

PortalScreenCast::~PortalScreenCast() {
    Close();
}

// ---------------------------------------------------------------------------
// Open — bốn bước, đánh dấu bằng các mốc "--- ... ---" bên dưới
// ---------------------------------------------------------------------------
bool PortalScreenCast::Open() {
    if (isOpen()) return true;
    lastError_.clear();
    streams_.clear();

    // Context riêng làm thread-default — bẫy số 3 ở đầu file.
    GMainContext* ctx = g_main_context_new();
    g_main_context_push_thread_default(ctx);

    // Dọn dẹp một chỗ duy nhất cho mọi đường thoát.
    auto finish = [&](bool ok) {
        g_main_context_pop_thread_default(ctx);
        g_main_context_unref(ctx);
        return ok;
    };

    GError* gerr = nullptr;
    GDBusConnection* conn = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &gerr);
    if (!conn) {
        lastError_ = std::string("no session D-Bus: ") + (gerr ? gerr->message : "?");
        if (gerr) g_error_free(gerr);
        LOGE("[Portal] %s", lastError_.c_str());
        return finish(false);
    }

    const uint32_t version = ReadUintProperty(conn, "version");
    const uint32_t cursorModes = ReadUintProperty(conn, "AvailableCursorModes");
    if (version == 0) {
        // Property đọc không được nghĩa là giao diện ScreenCast không có mặt —
        // gần như chắc chắn máy chưa cài xdg-desktop-portal-* cho compositor của
        // mình. Nói thẳng, vì đây là lỗi cấu hình phổ biến nhất của bản Ubuntu.
        lastError_ =
            "xdg-desktop-portal ScreenCast not available — install "
            "xdg-desktop-portal-gnome (GNOME), -kde (KDE) or -wlr (wlroots)";
        LOGE("[Portal] %s", lastError_.c_str());
        g_object_unref(conn);
        return finish(false);
    }
    LOGI("[Portal] ScreenCast version %u, cursor modes 0x%x.", version, cursorModes);

    // --- 1. CreateSession ---
    const std::string tok1 = NextToken("req");
    const std::string sessTok = NextToken("sess");
    {
        GVariantBuilder ob;
        g_variant_builder_init(&ob, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&ob, "{sv}", "handle_token", g_variant_new_string(tok1.c_str()));
        g_variant_builder_add(&ob, "{sv}", "session_handle_token",
            g_variant_new_string(sessTok.c_str()));

        GVariant* res = PortalRequest(conn, ctx, "CreateSession", g_variant_new("(a{sv})", &ob),
            tok1, &lastError_);
        if (!res) {
            LOGE("[Portal] CreateSession failed: %s", lastError_.c_str());
            g_object_unref(conn);
            return finish(false);
        }
        const char* sh = nullptr;
        g_variant_lookup(res, "session_handle", "&s", &sh);
        if (sh) sessionHandle_ = sh;
        g_variant_unref(res);
        if (sessionHandle_.empty()) {
            lastError_ = "portal returned no session_handle";
            LOGE("[Portal] %s", lastError_.c_str());
            g_object_unref(conn);
            return finish(false);
        }
    }

    // --- 2. SelectSources ---
    // types = MONITOR: Deskhub chỉ chia sẻ MÀN HÌNH (share theo cửa sổ đã bỏ
    // 2026-07-27 trên mọi nền tảng), nên không xin quyền quay cửa sổ.
    // multiple = true: một phiên portal cho tất cả màn hình, xem PortalScreenCast.h.
    // cursor_mode = EMBEDDED: con trỏ vẽ THẲNG vào frame. Bắt buộc, vì người xem từ
    //   xa cần thấy con trỏ của máy host; chế độ METADATA gửi con trỏ ra kênh riêng
    //   và ta không có kênh đó trên dây.
    {
        const std::string tok2 = NextToken("req");
        GVariantBuilder ob;
        g_variant_builder_init(&ob, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&ob, "{sv}", "handle_token", g_variant_new_string(tok2.c_str()));
        g_variant_builder_add(&ob, "{sv}", "types", g_variant_new_uint32(kSourceTypeMonitor));
        g_variant_builder_add(&ob, "{sv}", "multiple", g_variant_new_boolean(TRUE));
        g_variant_builder_add(&ob, "{sv}", "cursor_mode",
            g_variant_new_uint32((cursorModes & kCursorModeEmbedded) ? kCursorModeEmbedded
                                                                     : kCursorModeHidden));
        // persist_mode/restore_token chỉ có từ version 4. Gửi option lạ cho portal
        // đời cũ thì nó từ chối cả lời gọi, nên phải rào theo version.
        if (version >= kScreenCastVersionPersist) {
            g_variant_builder_add(&ob, "{sv}", "persist_mode",
                g_variant_new_uint32(kPersistModePersistent));
            if (!restoreToken_.empty())
                g_variant_builder_add(&ob, "{sv}", "restore_token",
                    g_variant_new_string(restoreToken_.c_str()));
        }

        GVariant* res = PortalRequest(conn, ctx, "SelectSources",
            g_variant_new("(oa{sv})", sessionHandle_.c_str(), &ob), tok2, &lastError_);
        if (!res) {
            LOGE("[Portal] SelectSources failed: %s", lastError_.c_str());
            g_object_unref(conn);
            return finish(false);
        }
        g_variant_unref(res);
    }

    // --- 3. Start — HỘP THOẠI hệ thống hiện ở bước này ---
    // parent_window = "" : ta không truyền định danh cửa sổ cha. Trên Wayland nó
    // phải là chuỗi kiểu "wayland:<handle>" lấy từ xdg-foreign, mà GTK3 không phơi
    // ra API đó. Hệ quả: hộp thoại portal không neo vào cửa sổ Deskhub mà đứng
    // riêng — xấu hơn một chút, không ảnh hưởng chức năng.
    {
        const std::string tok3 = NextToken("req");
        GVariantBuilder ob;
        g_variant_builder_init(&ob, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&ob, "{sv}", "handle_token", g_variant_new_string(tok3.c_str()));

        LOGI("[Portal] Asking the compositor for screen capture — a system dialog will appear.");
        GVariant* res = PortalRequest(conn, ctx, "Start",
            g_variant_new("(osa{sv})", sessionHandle_.c_str(), "", &ob), tok3, &lastError_);
        if (!res) {
            LOGE("[Portal] Start failed: %s", lastError_.c_str());
            Close();
            g_object_unref(conn);
            return finish(false);
        }

        const char* rt = nullptr;
        if (g_variant_lookup(res, "restore_token", "&s", &rt) && rt) restoreToken_ = rt;

        GVariant* list = g_variant_lookup_value(res, "streams", G_VARIANT_TYPE("a(ua{sv})"));
        if (list) {
            GVariantIter it;
            g_variant_iter_init(&it, list);
            guint32 nodeId = 0;
            GVariant* props = nullptr;
            while (g_variant_iter_next(&it, "(u@a{sv})", &nodeId, &props)) {
                PortalStream s;
                s.nodeId = nodeId;

                gint32 x = 0, y = 0, w = 0, h = 0;
                if (GVariant* pos = g_variant_lookup_value(props, "position",
                        G_VARIANT_TYPE("(ii)"))) {
                    g_variant_get(pos, "(ii)", &x, &y);
                    g_variant_unref(pos);
                }
                if (GVariant* sz = g_variant_lookup_value(props, "size", G_VARIANT_TYPE("(ii)"))) {
                    g_variant_get(sz, "(ii)", &w, &h);
                    g_variant_unref(sz);
                }
                s.x = x;
                s.y = y;
                // Kẹp âm về 0: "size" luôn dương trong thực tế, nhưng nó là int32
                // trên dây và một giá trị âm lọt vào uint32 sẽ thành số khổng lồ,
                // rồi cấp phát theo nó là sập.
                s.width = w > 0 ? uint32_t(w) : 0;
                s.height = h > 0 ? uint32_t(h) : 0;

                // "id" (tên connector, ví dụ "DP-1") chỉ có ở portal đời mới; không
                // có thì đặt tên theo thứ tự cho người dùng còn phân biệt được.
                const char* id = nullptr;
                g_variant_lookup(props, "id", "&s", &id);
                char label[128];
                if (id && *id)
                    std::snprintf(label, sizeof(label), "%s (%ux%u)", id, s.width, s.height);
                else
                    std::snprintf(label, sizeof(label), "Screen %zu (%ux%u)", streams_.size() + 1,
                        s.width, s.height);
                s.name = label;

                LOGI("[Portal] Stream node %u: %s at %d,%d", s.nodeId, s.name.c_str(), s.x, s.y);
                streams_.push_back(std::move(s));
                g_variant_unref(props);
            }
            g_variant_unref(list);
        }
        g_variant_unref(res);

        if (streams_.empty()) {
            lastError_ = "portal returned no stream (no screen selected)";
            LOGE("[Portal] %s", lastError_.c_str());
            Close();
            g_object_unref(conn);
            return finish(false);
        }
    }

    // --- 4. OpenPipeWireRemote — KHÔNG phải Request, trả fd ngay ---
    {
        GVariantBuilder ob;
        g_variant_builder_init(&ob, G_VARIANT_TYPE_VARDICT);

        GUnixFDList* fdList = nullptr;
        GVariant* ret = g_dbus_connection_call_with_unix_fd_list_sync(conn, kBusName, kObjPath,
            kScreenCast, "OpenPipeWireRemote",
            g_variant_new("(oa{sv})", sessionHandle_.c_str(), &ob), G_VARIANT_TYPE("(h)"),
            G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &fdList, nullptr, &gerr);
        if (!ret) {
            lastError_ = std::string("OpenPipeWireRemote: ") + (gerr ? gerr->message : "?");
            if (gerr) g_error_free(gerr);
            LOGE("[Portal] %s", lastError_.c_str());
            if (fdList) g_object_unref(fdList);
            Close();
            g_object_unref(conn);
            return finish(false);
        }

        gint32 idx = -1;
        g_variant_get(ret, "(h)", &idx);
        g_variant_unref(ret);

        // g_unix_fd_list_get trả về một bản DUP mà ta sở hữu — đúng thứ ta cần, vì
        // fdList bị unref ngay dưới đây.
        pipewireFd_ = fdList ? g_unix_fd_list_get(fdList, idx, &gerr) : -1;
        if (fdList) g_object_unref(fdList);
        if (pipewireFd_ < 0) {
            lastError_ = std::string("no PipeWire fd: ") + (gerr ? gerr->message : "?");
            if (gerr) g_error_free(gerr);
            LOGE("[Portal] %s", lastError_.c_str());
            Close();
            g_object_unref(conn);
            return finish(false);
        }
    }

    LOGI("[Portal] Ready: %zu screen(s), PipeWire fd %d.", streams_.size(), pipewireFd_);
    g_object_unref(conn);
    return finish(true);
}

void PortalScreenCast::Close() {
    if (pipewireFd_ >= 0) {
        close(pipewireFd_);
        pipewireFd_ = -1;
    }
    if (!sessionHandle_.empty()) {
        // Đóng session là phép lịch sự với compositor: nó tắt chỉ báo "đang quay
        // màn hình" trên thanh trạng thái ngay, thay vì đợi app thoát hẳn. Gọi
        // fire-and-forget (không chờ hồi đáp) vì ta đang trên đường dọn dẹp và
        // không có gì để làm với kết quả.
        GError* gerr = nullptr;
        if (GDBusConnection* conn = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &gerr)) {
            g_dbus_connection_call(conn, kBusName, sessionHandle_.c_str(), kSessionIface, "Close",
                nullptr, nullptr, G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr, nullptr);
            g_dbus_connection_flush_sync(conn, nullptr, nullptr);
            g_object_unref(conn);
        } else if (gerr) {
            g_error_free(gerr);
        }
        sessionHandle_.clear();
    }
    streams_.clear();
}
