#pragma once

#include "deskhub/ui/Locale.h"

#include <cstddef>

namespace deskhub::ui {

struct CatalogEntry {
    const char* en;
    const char* tr;
};

const CatalogEntry* CatalogFor(UiLanguage language, size_t& count);

}
