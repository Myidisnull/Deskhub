#pragma once

inline constexpr const char* kTestPasscode = "0417";

extern int g_failures;
void Check(bool ok, const char* what);
