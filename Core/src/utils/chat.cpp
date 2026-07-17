#include "utils/chat.hpp"
#include <cstdarg>

// Используем официальное пространство имен из sampapi.h для 0.3.7-R3-1
using namespace sampapi::v037r3;

namespace Utils {

    void PrintChat(DWORD color, const char* message) {
        // RefChat() - это официальный способ получить указатель на CChat из sampapi
        CChat* pChat = RefChat();
        if (pChat) {
            pChat->AddMessage(color, message);
        }
    }

    void PrintChatF(DWORD color, const char* format, ...) {
        CChat* pChat = RefChat();
        if (!pChat) return;

        char buffer[512];
        va_list args;
        va_start(args, format);
        vsnprintf_s(buffer, sizeof(buffer), format, args);
        va_end(args);

        pChat->AddMessage(color, buffer);
    }

}