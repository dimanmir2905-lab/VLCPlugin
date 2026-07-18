#define GTA_SA
#define _WINSOCKAPI_ // Защищает от конфликта winsock.h и winsock2.h

#include <windows.h>
#include <d3d9.h>    // DirectX подключаем явно до sdk.hpp
#include <d3dx9.h>

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
#include "utils/crash_handler.hpp" // <-- Добавьте этот инклуд

// === ПОДКЛЮЧАЕМ RAKHOOK ===
#include <RakHook/rakhook.hpp>

// === ПОДКЛЮЧАЕМ IMGUI ===
#include <imgui.h>
#include <imgui_impl_dx9.h>
#include <imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

using namespace sampapi::v037r3;

namespace {
    // Глобальная переменная для хранения оригинального обработчика окон
    WNDPROC orig_WndProc = nullptr;

    LRESULT CALLBACK WndProcHook(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        // 1. Отдаем сообщение ImGui. Если он его обработал, возвращаем 0 (стандарт Windows для обработанных сообщений)
        if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam)) {
            return 0;
        }

        // 2. Иначе передаем оригинальной функции. 
        // Добавлена защита: если orig_WndProc по какой-то причине NULL, используем стандартный обработчик
        if (orig_WndProc != nullptr) {
            return CallWindowProcA(orig_WndProc, hWnd, uMsg, wParam, lParam);
        }

        return DefWindowProcA(hWnd, uMsg, wParam, lParam);
    }

    void InstallWndProcHook() {
        HWND hwnd = FindWindowA("GTA:SA:MP", nullptr);
        if (!hwnd) {
            hwnd = FindWindowA("GTA:SA", nullptr);
        }
        if (!hwnd) {
            hwnd = GetActiveWindow(); // Запасной вариант
        }

        if (hwnd) {
            orig_WndProc = (WNDPROC)SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)WndProcHook);

            // Выводим адрес окна, на которое повесили хук
            char msg[128];
            sprintf_s(msg, "[VLC] WndProcHook установлен на HWND: %p\n", hwnd);
            OutputDebugStringA(msg);
        }
        else {
            OutputDebugStringA("[VLC] ОШИБКА: Не удалось найти окно игры для WndProcHook!\n");
        }
    }

    void SetupNetworkHooks() {
        if (!rakhook::initialize()) {
            OutputDebugStringA("[VLCPlugin] RakHook init FAILED!\n");
            return;
        }
        OutputDebugStringA("[VLCPlugin] RakHook initialized successfully!\n");

        rakhook::on_receive_rpc += [](unsigned char& id, RakNet::BitStream* bs) -> bool {
            // std::string msg = "[VLC] Incoming RPC ID: " + std::to_string(id) + "\n";
            // OutputDebugStringA(msg.c_str());
            return true;
            };

        rakhook::on_send_packet += [](RakNet::BitStream* bs, PacketPriority& priority, PacketReliability& reliability, char& ord_channel) -> bool {
            return true;
            };
    }

    void InitThread() {
        // 1. Применяем базовые патчи памяти
        Patches::ApplyAll();

        // 2. Патчи безопасности
        Patches::Security::ApplyAllSecurityFixes();

        // 3. Инициализируем D3D хук (Splash Screen)
        Patches::SplashD3D::Initialize();

        // 4. Перехваты событий чата
        Hooks::InitializeSampEvents();

        // 5. Ждём полной инициализации объектов SAMP
        while (!RefChat() || !RefNetGame()) {
            Sleep(100);
        }

        // 6. Инициализируем сетевой перехватчик (RakHook)
        SetupNetworkHooks();

        // 7. === ИНИЦИАЛИЗИРУЕМ ПЕРЕХВАТ КОМАНД ЧАТА ===
        Hooks::InitializeChatCommands();

        // 8. Настраиваем окно игры (эта функция теперь также сохраняет правильный HWND внутри Utils)
        Utils::SetCustomWindow("Vialence Online", "data\\Icons\\icon.ico");

        // 9. Устанавливаем WndProc хук ИМЕННО на этот сохраненный HWND
        HWND gameHwnd = Utils::GetGameHwnd();
        if (gameHwnd) {
            orig_WndProc = (WNDPROC)SetWindowLongPtrA(gameHwnd, GWLP_WNDPROC, (LONG_PTR)WndProcHook);

            // Проверка, что хук действительно установился
            if (orig_WndProc == nullptr) {
                OutputDebugStringA("[VLC] ПРЕДУПРЕЖДЕНИЕ: SetWindowLongPtrA вернула NULL!\n");
            }
            else {
                OutputDebugStringA("[VLC] WndProcHook успешно установлен\n");
            }
        }
        else {
            OutputDebugStringA("[VLC] ОШИБКА: Не удалось получить HWND окна игры для WndProcHook\n");
        }

        // 10. Приветствие
        Utils::PrintChat(0xB9C9BFFF, "Добро пожаловать на {ffa500}Vialence Role Play {B9C9BF}(0.3.7-R3)");

        if (rakhook::samp_version() != rakhook::samp_ver::unknown) {
            Utils::PrintChat(0x00FF00FF, "[VLC] {ffffff}Сетевой движок {00ff00}активирован!");
            Utils::PrintChat(0x00FF00FF, "[VLC] {ffffff}Введите {00ff00}F4{ffffff} для открытия меню");
        }
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        // 1. ИНИЦИАЛИЗИРУЕМ ОБРАБОТЧИК КРАШЕЙ ПЕРВЫМ ДЕЛОМ
        // Это гарантирует, что если InitThread или что-то еще упадет, мы это перехватим
        Utils::InitializeCrashHandler();

        // 2. Запускаем основной поток инициализации
        std::thread(InitThread).detach();
    }
    else if (ul_reason_for_call == DLL_PROCESS_DETACH) {
        // === ВАЖНО: Восстанавливаем оригинальный WndProc при выгрузке ===
        HWND hwnd = FindWindowA("GTA:SA:MP", nullptr);
        if (!hwnd) hwnd = FindWindowA("GTA:SA", nullptr);

        if (hwnd && orig_WndProc) {
            SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)orig_WndProc);
        }

        // Очистка ресурсов
        rakhook::destroy();
        Patches::SplashD3D::Shutdown();
        Patches::Security::ShutdownSecurityFixes();

        // Примечание: специально восстанавливать фильтр исключений при DETACH не нужно,
        // так как процесс завершается, и Windows очистит всё сама.
    }
    return TRUE;
}