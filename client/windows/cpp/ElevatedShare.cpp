#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "ElevatedShare.h"

bool IsProcessElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation{};
    DWORD len = 0;
    const bool ok = GetTokenInformation(token, TokenElevation, &elevation,
                        sizeof(elevation), &len) != FALSE;
    CloseHandle(token);
    return ok && elevation.TokenIsElevated != 0;
}
