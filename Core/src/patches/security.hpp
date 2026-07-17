#pragma once
#include "utils/memory.hpp"

namespace Patches::Security {
    // Применяет все патчи безопасности (память + хуки)
    void ApplyAllSecurityFixes();
    void ShutdownSecurityFixes();
}