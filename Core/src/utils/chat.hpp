#pragma once
#include "sdk.hpp" // Здесь уже подключен samp/samp.hpp со всеми классами

namespace Utils {
    void PrintChat(DWORD color, const char* message);
    void PrintChatF(DWORD color, const char* format, ...);
}