#pragma once
#include "sdk.hpp"

namespace Hooks {
    // Инициализирует все перехваты событий SAMP
    void InitializeSampEvents();
    void InitializeChatCommands(); // <-- Добавляем эту строку
}