#include "deskhubp/system/Environment.h"

#include <cstdlib>

namespace deskhubp {

std::string EnvValue(const char* name) {
    char* value = nullptr;
    size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) return std::string();
    std::string out(value);
    std::free(value);
    return out;
}

}
