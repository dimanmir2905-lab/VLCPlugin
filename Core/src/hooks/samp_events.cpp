#include "hooks/samp_events.hpp"
#include "sdk.hpp" 
#include "utils/chat.hpp"
#include "utils/samp_chat.hpp"      
#include "patches/splash_d3d.hpp"
#include <windows.h>
#include <string>

using namespace sampapi::v037r3;

namespace {
    // Хук для AddChatMessage
    typedef void(__thiscall* CChat_AddChatMessage_t)(void* pChat, int nType, const char* szText, const char* szPrefix, uint32_t textColor, uint32_t prefixColor);
    CChat_AddChatMessage_t g_originalAddChatMessage = nullptr; // Добавили тип переменной

    // --- НОВОЕ: Хук для глушения Render ---
    typedef void(__thiscall* CChat_Render_t)(void* pChat);
    CChat_Render_t g_originalChatRender = nullptr;

    struct CChatHook {
        void __thiscall HookedAddChatMessage(int nType, const char* szText, const char* szPrefix, uint32_t textColor, uint32_t prefixColor) {
            void* pChat = this;

            if (szText && strlen(szText) > 0) {
                std::string fullText;
                if (szPrefix && strlen(szPrefix) > 0) {
                    fullText = std::string(szPrefix) + ": " + std::string(szText);
                }
                else {
                    fullText = std::string(szText);
                }
                Utils::SampChat::AddMessage(textColor, fullText);
            }

            if (g_originalAddChatMessage) {
                g_originalAddChatMessage(pChat, nType, szText, szPrefix, textColor, prefixColor);
            }
        }

        // --- НОВОЕ: Пустой рендер ---
        void __thiscall HookedRender() {
            // Просто возвращаем управление, ничего не рисуя.
            // Стандартный чат SA-MP теперь полностью невидим глазу.
            return;
        }
    };

    // Свежее подключение, рестарты и т.д.
    static void __cdecl OnSampStart() {}
    static void __cdecl OnSampConnect() {
        Utils::PrintChat(0xB9C9BFFF, "Подключение к серверу...");
        Patches::SplashD3D::SetConnectionStatus("Подключение к серверу...");
    }
    static void __cdecl OnSampWelcome() {
        Utils::PrintChat(0xB9C9BFFF, "Успешный вход. Приятной игры на сервере :)");
        Patches::SplashD3D::SetConnectionSuccess("Успешный вход! Приятной игры :)");
    }
    static void __cdecl OnSampBan() {
        Utils::PrintChat(0xB9C9BFFF, "Вы забанены на данном сервере");
        Patches::SplashD3D::SetConnectionError("Вы забанены на данном сервере");
    }
    static void __cdecl OnSampFull() {
        Utils::PrintChat(0xB9C9BFFF, "Сервер заполнен. Повторяем подключение...");
        Patches::SplashD3D::SetConnectionError("Сервер заполнен. Повторяем подключение...");
    }
    static void __cdecl OnSampWrongPass() {
        Utils::PrintChat(0xB9C9BFFF, "Неверный пароль.");
        Patches::SplashD3D::SetConnectionError("Неверный пароль.");
    }
    static void __cdecl OnSampCloseCon() {
        Utils::PrintChat(0xB9C9BFFF, "Сервер закрыл соединение.");
        Patches::SplashD3D::SetConnectionError("Сервер закрыл соединение.");
    }
    static void __cdecl OnSampNoOtven() {
        Utils::PrintChat(0xB9C9BFFF, "Сервер не отвечает. Повторяем подключение...");
        Patches::SplashD3D::SetConnectionError("Сервер не отвечает. Повторяем подключение...");
    }
    static void __cdecl OnSampRestart() {
        Utils::PrintChat(0xB9C9BFFF, "Сервер перезагрузился.");
        Patches::SplashD3D::SetConnectionError("Сервер перезагрузился.");
    }
    static void __cdecl OnSampPotera() {
        Utils::PrintChat(0xB9C9BFFF, "Потеря соединения с сервером");
        Patches::SplashD3D::SetConnectionError("Потеря соединения с сервером");
    }
    static void __cdecl OnSampJoin() {
        Utils::PrintChat(0xB9C9BFFF, "Подключились, входим в игру...");
        Patches::SplashD3D::SetConnectionStatus("Подключились, входим в игру...");

        if (sampapi::v037r3::RefChat()) {
            sampapi::v037r3::RefChat()->m_nMode = 1;
        }
    }
}

namespace Hooks {

    void InitializeSampEvents() {
        while (!GetModuleHandleA("samp.dll")) {
            Sleep(50);
        }

        // Оставляем перенаправления вызовов для статусов сети (они используют адреса из sampapi)
        plugin::patch::RedirectCall(sampapi::GetAddress(0xB7CA), OnSampStart);
        plugin::patch::RedirectCall(sampapi::GetAddress(0x8603), OnSampConnect);
        plugin::patch::RedirectCall(sampapi::GetAddress(0x10750), OnSampWelcome);
        plugin::patch::RedirectCall(sampapi::GetAddress(0x8A0B), OnSampBan);
        plugin::patch::RedirectCall(sampapi::GetAddress(0xAFE8), OnSampBan);
        plugin::patch::RedirectCall(sampapi::GetAddress(0x8A4E), OnSampFull);
        plugin::patch::RedirectCall(sampapi::GetAddress(0xB050), OnSampFull);
        plugin::patch::RedirectCall(sampapi::GetAddress(0x8A7E), OnSampCloseCon);
        plugin::patch::RedirectCall(sampapi::GetAddress(0x8ABE), OnSampWrongPass);
        plugin::patch::RedirectCall(sampapi::GetAddress(0xB02E), OnSampWrongPass);
        plugin::patch::RedirectCall(sampapi::GetAddress(0x8AFE), OnSampNoOtven);
        plugin::patch::RedirectCall(sampapi::GetAddress(0xB0450), OnSampNoOtven);
        plugin::patch::RedirectCall(sampapi::GetAddress(0xA3CA), OnSampRestart);
        plugin::patch::RedirectCall(sampapi::GetAddress(0xA9AF), OnSampPotera);
        plugin::patch::RedirectCall(sampapi::GetAddress(0xAB16), OnSampJoin);

        uintptr_t sampBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("samp.dll"));
        if (sampBase) {
            MH_Initialize();

            // 1. Хукаем CChat::AddChatMessage (0x678A0 для 0.3.7-R3)
            uintptr_t addChatAddr = sampBase + 0x678A0;
            auto hookAddChatFn = &CChatHook::HookedAddChatMessage;
            void* hookAddChatAddress = *reinterpret_cast<void**>(&hookAddChatFn);

            if (MH_CreateHook(reinterpret_cast<void*>(addChatAddr), hookAddChatAddress, reinterpret_cast<void**>(&g_originalAddChatMessage)) == MH_OK) {
                MH_EnableHook(reinterpret_cast<void*>(addChatAddr));
            }

            // 2. Хукаем CChat::Render вместо затирания байт (0x6440 для 0.3.7-R3)
            uintptr_t renderChatAddr = sampBase + 0x6440;
            auto hookRenderFn = &CChatHook::HookedRender;
            void* hookRenderAddress = *reinterpret_cast<void**>(&hookRenderFn);

            if (MH_CreateHook(reinterpret_cast<void*>(renderChatAddr), hookRenderAddress, reinterpret_cast<void**>(&g_originalChatRender)) == MH_OK) {
                MH_EnableHook(reinterpret_cast<void*>(renderChatAddr));
            }
        }
    }

    void InitializeChatCommands() {}
}