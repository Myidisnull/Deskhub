#include "deskhubp/system/Environment.h"

#include <cstdlib>

namespace deskhubp {

std::string EnvValue(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string();
}

}
