#include "support/TestSupport.h"

#include <cstdio>

int g_failures = 0;

void Check(bool ok, const char* what) {
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what);
    }
}
