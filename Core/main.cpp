#define GTA_SA
#define _WINSOCKAPI_ 

// Подавляем предупреждения IntelliSense от сторонних библиотек
#pragma warning(push)
#pragma warning(disable: 4005) // Макрос уже определен
#pragma warning(disable: 4996) // Устаревшая функция
#include "sdk.hpp" 
#pragma warning(pop)

#include <thread>

// Подключаем наши модули
#include "patches/manager.hpp"
#include "patches/security.hpp"
#include "patches/splash_d3d.hpp" // <-- 1. ДОБАВЛЕНО: Подключаем наш сплэш-скрин
#include "hooks/samp_events.hpp"
#include "utils/window.hpp"
#include "utils/chat.hpp"

using namespace sampapi::v037r3;

namespace {
    void InitThread() {
        // 1. Применяем базовые патчи памяти (лимиты, версия клиента и т.д.)
        Patches::ApplyAll();

        // 2. Применяем патчи безопасности (RCE фиксы, хуки RPC, TXD)
        Patches::Security::ApplyAllSecurityFixes();

        // 3. Инициализируем D3D хук (делает NOP оригинального бара GTA и перехватывает Present)
        Patches::SplashD3D::Initialize(); // <-- 2. ДОБАВЛЕНО

        // 4. Инициализируем перехваты событий чата (они будут управлять статусами сплэш-скрина)
        Hooks::InitializeSampEvents();

        // 5. Ждём полной инициализации объектов SAMP
        while (!RefChat() || !RefNetGame()) {
            Sleep(100);
        }

        // 6. Настраиваем окно игры
        Utils::SetCustomWindow("Vialence Online", "data\\Icons\\icon.ico");

        // 7. Выводим приветственное сообщение
        Utils::PrintChat(0xB9C9BFFF, "Добро пожаловать на {ffa500}Vialence Role Play {B9C9BF}(0.3.7-R3)");
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        // Запускаем поток инициализации в фоне
        std::thread(InitThread).detach();
    }
    else if (ul_reason_for_call == DLL_PROCESS_DETACH) {
        // Чистая деинициализация при выгрузке плагина
        Patches::SplashD3D::Shutdown(); // <-- 3. ДОБАВЛЕНО: Освобождаем ресурсы D3D и хуки
        Patches::Security::ShutdownSecurityFixes();
    }
    return TRUE;
}