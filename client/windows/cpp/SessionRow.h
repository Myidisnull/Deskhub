#pragma once
// =============================================================================
// SessionRow.h — một dòng trong danh sách nguồn đang chia sẻ (phía host).
//
// Là kiểu dữ liệu AgentControl::SetRows dùng để đẩy danh sách nguồn ra frontend
// (HeadlessAgentControl → C# hiện ở màn Sharing status). Trước đây dùng chung với
// lớp UI Win32 (SessionWindow/ViewerWindow, đã gỡ ở M4b); nay chỉ còn AgentControl.
// =============================================================================
#include <cstdint>
#include <string>

struct SessionSourceRow {
    uint8_t sourceId = 0;
    std::wstring label;   // "tên (WxH, ...)" / "tên (starting...)"
    bool pending = false; // true = đang chờ (frame đầu / đàm phán), chưa chạy hẳn

    bool operator==(const SessionSourceRow& o) const {
        return sourceId == o.sourceId && pending == o.pending && label == o.label;
    }
};
