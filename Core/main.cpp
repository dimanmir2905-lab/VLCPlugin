#define GTA_SA
#define _WINSOCKAPI_ // КРИТИЧНО: Должно быть до любых #include, чтобы winsock2.h не конфликтовал с windows.h

// Подавляем предупреждения IntelliSense от сторонних библиотек
#pragma warning(push)
#pragma warning(disable: 4005) 
#pragma warning(disable: 4996) 
#include "sdk.hpp" 
#pragma warning(pop)

#include <thread>
#include <iostream>

// Подключаем наши модули
#include "patches/manager.hpp"
#include "patches/security.hpp"
#include "patches/splash_d3d.hpp" 
#include "hooks/samp_events.hpp"
#include "utils/window.hpp"
#include "utils/chat.hpp"

// === ПОДКЛЮЧАЕМ RAKHOOK ===
#include <RakHook/rakhook.hpp>

using namespace sampapi::v037r3;

namespace {
    void SetupNetworkHooks() {
        if (!rakhook::initialize()) {
            OutputDebugStringA("[VLCPlugin] RakHook init FAILED!\n");
            return;
        }

        OutputDebugStringA("[VLCPlugin] RakHook initialized successfully!\n");

        // Входящие RPC от сервера
        rakhook::on_receive_rpc += [](unsigned char& id, RakNet::BitStream* bs) -> bool {
            std::string msg = "[VLC] Incoming RPC ID: " + std::to_string(id) + "\n";
            OutputDebugStringA(msg.c_str());

            // Возвращаем true, чтобы samp.dll обработал пакет
            return true;
            };

        // Исходящие пакеты
        rakhook::on_send_packet += [](RakNet::BitStream* bs, PacketPriority& priority, PacketReliability& reliability, char& ord_channel) -> bool {
            return true;
            };
    }

    void InitThread() {
        // 1. Применяем базовые патчи памяти
        Patches::ApplyAll();

        // 2. Патчи безопасности
        Patches::Security::ApplyAllSecurityFixes();

        // 3. Инициализируем D3D хук
        Patches::SplashD3D::Initialize();

        // 4. Перехваты событий чата
        Hooks::InitializeSampEvents();

        // 5. Ждём полной инициализации объектов SAMP (samp.dll уже в памяти!)
        while (!RefChat() || !RefNetGame()) {
            Sleep(100);
        }

        // === ЗДЕСЬ БЕЗОПАСНО ИНИЦИАЛИЗИРОВАТЬ RAKHOOK ===
        SetupNetworkHooks();

        // 6. Настраиваем окно игры
        Utils::SetCustomWindow("Vialence Online", "data\\Icons\\icon.ico");

        // 7. Приветствие
        Utils::PrintChat(0xB9C9BFFF, "Добро пожаловать на {ffa500}Vialence Role Play {B9C9BF}(0.3.7-R3)");

        if (rakhook::samp_version() != rakhook::samp_ver::unknown) {
            Utils::PrintChat(0x00FF00FF, "[VLC] {ffffff}Сетевой движок {00ff00}активирован!");
        }
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        std::thread(InitThread).detach();
    }
    else if (ul_reason_for_call == DLL_PROCESS_DETACH) {
        rakhook::destroy(); // Оставляем только это

        Patches::SplashD3D::Shutdown();
        Patches::Security::ShutdownSecurityFixes();
    }
    return TRUE;
}