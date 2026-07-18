#define GTA_SA
#define _WINSOCKAPI_ // Защищает от конфликта winsock.h и winsock2.h

#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>

#pragma warning(push)
#pragma warning(disable: 4005) 
#pragma warning(disable: 4996) 
#include "sdk.hpp" 
#pragma warning(pop)

#include <thread>
#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <string>

// Подключаем наши модули
#include "patches/manager.hpp"
#include "patches/security.hpp"
#include "patches/splash_d3d.hpp" 
#include "hooks/samp_events.hpp"
#include "utils/window.hpp"
#include "utils/chat.hpp"
#include "utils/crash_handler.hpp"
#include "utils/samp_chat.hpp" // <-- МОДУЛЬ КАСТОМНОГО ЧАТА

// === ПОДКЛЮЧАЕМ RAKHOOK ===
#include <RakHook/rakhook.hpp>
#include <RakNet/BitStream.h>

// === ПОДКЛЮЧАЕМ IMGUI ===
#include <imgui.h>
#include <imgui_impl_dx9.h>
#include <imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

using namespace sampapi::v037r3;

namespace {
    // ==========================================
    // Простая система логирования в корень GTA
    // ==========================================
    void LogToFile(const std::string& message) {
        char exePath[MAX_PATH];
        if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) == 0) return;

        std::string path = exePath;
        path = path.substr(0, path.find_last_of("\\/")) + "\\VialencePlugin.log";

        std::ofstream logFile(path, std::ios::app);
        if (logFile.is_open()) {
            auto now = std::chrono::system_clock::now();
            auto in_time_t = std::chrono::system_clock::to_time_t(now);
            std::tm tm_buf;
            localtime_s(&tm_buf, &in_time_t);

            logFile << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S") << " | " << message << "\n";
        }
    }

    // Глобальная переменная для хранения оригинального обработчика окон
    WNDPROC orig_WndProc = nullptr;

    LRESULT CALLBACK WndProcHook(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        // 1. Отдаем сообщение ImGui. Если он его обработал, возвращаем 0
        if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam)) {
            return 0;
        }

        // 2. Иначе передаем оригинальной функции
        if (orig_WndProc != nullptr) {
            return CallWindowProcA(orig_WndProc, hWnd, uMsg, wParam, lParam);
        }

        return DefWindowProcA(hWnd, uMsg, wParam, lParam);
    }

    void InstallWndProcHook() {
        HWND hwnd = FindWindowA("GTA:SA:MP", nullptr);
        if (!hwnd) hwnd = FindWindowA("GTA:SA", nullptr);
        if (!hwnd) hwnd = GetActiveWindow();

        if (hwnd) {
            orig_WndProc = (WNDPROC)SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)WndProcHook);
            LogToFile("WndProcHook успешно установлен на HWND: " + std::to_string(reinterpret_cast<uintptr_t>(hwnd)));
        }
        else {
            LogToFile("ОШИБКА: Не удалось найти окно игры для WndProcHook!");
        }
    }

    void SetupNetworkHooks() {
        if (!rakhook::initialize()) {
            LogToFile("RakHook init FAILED!");
            return;
        }
        LogToFile("RakHook initialized successfully!");

        // Перехват ВХОДЯЩИХ RPC
        rakhook::on_receive_rpc += [](unsigned char& id, RakNet::BitStream* bs) -> bool {
            if (!bs) return true;

            // RPC 93: SendClientMessage (Системные сообщения, команды и т.д.)
            if (id == 93) {
                bs->ResetReadPointer();
                uint32_t color = 0xFFFFFFFF;
                uint32_t msgLength = 0;

                if (bs->Read(color) && bs->Read(msgLength) && msgLength > 0 && msgLength < 1024) {
                    std::string message(msgLength, '\0');
                    bs->Read(message.data(), msgLength);

                    // Передаем в наш ImGui чат
                    Utils::SampChat::AddMessage(color, message);

                    // Возвращаем false, чтобы SA-MP НЕ отрисовывал это сообщение в своем стандартном чате
                    return false;
                }
            }

            // RPC 101: ChatMessage (Сообщения от других игроков в чат)
            if (id == 101) {
                bs->ResetReadPointer();
                uint8_t msgLength = 0;

                if (bs->Read(msgLength) && msgLength > 0 && msgLength < 256) {
                    std::string message(msgLength, '\0');
                    bs->Read(message.data(), msgLength);

                    // Белый цвет по умолчанию, так как цвет обычно зашит в текст ({FFFFFF}Nick: msg)
                    Utils::SampChat::AddMessage(0xFFFFFFFF, message);

                    return false; // Блокируем стандартный чат
                }
            }

            return true; // Пропускаем все остальные RPC
            };

        // Перехват ИСХОДЯЩИХ пакетов
        rakhook::on_send_packet += [](RakNet::BitStream* bs, PacketPriority& priority, PacketReliability& reliability, char& ord_channel) -> bool {
            return true;
            };
    }

    void InitThread() {
        LogToFile("Запуск потока инициализации...");

        Patches::ApplyAll();
        Patches::Security::ApplyAllSecurityFixes();
        Patches::SplashD3D::Initialize();
        Hooks::InitializeSampEvents();

        while (!RefChat() || !RefNetGame()) {
            Sleep(100);
        }

        // Инициализируем чат ДО установки хуков
        Utils::SampChat::Initialize();
        LogToFile("Кастомный ImGui чат инициализирован.");

        // Теперь устанавливаем хуки
        SetupNetworkHooks();

        Utils::SetCustomWindow("Vialence Online", "data\\Icons\\icon.ico");

        HWND gameHwnd = Utils::GetGameHwnd();
        if (gameHwnd) {
            orig_WndProc = (WNDPROC)SetWindowLongPtrA(gameHwnd, GWLP_WNDPROC, (LONG_PTR)WndProcHook);
            if (orig_WndProc == nullptr) {
                LogToFile("ПРЕДУПРЕЖДЕНИЕ: SetWindowLongPtrA вернула NULL!");
            }
            else {
                LogToFile("WndProcHook успешно установлен.");
            }
        }
        else {
            LogToFile("ОШИБКА: Не удалось получить HWND окна игры.");
        }

        // Приветствие в чате
        Utils::PrintChat(0xB9C9BFFF, "Добро пожаловать на {ffa500}Vialence Role Play {B9C9BF}(0.3.7-R3)");

        // Убрали лишнее сообщение об активации, оставили только запись в лог
        LogToFile("Плагин успешно запущен и готов к работе.");
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        // 1. Инициализируем обработчик крашей ПЕРВЫМ ДЕЛОМ
        Utils::InitializeCrashHandler();
        LogToFile("DllMain: PROCESS_ATTACH. Crash Handler initialized.");

        // 2. Запускаем основной поток инициализации
        std::thread(InitThread).detach();
    }
    else if (ul_reason_for_call == DLL_PROCESS_DETACH) {
        LogToFile("DllMain: PROCESS_DETACH. Очистка ресурсов...");

        HWND hwnd = FindWindowA("GTA:SA:MP", nullptr);
        if (!hwnd) hwnd = FindWindowA("GTA:SA", nullptr);

        if (hwnd && orig_WndProc) {
            SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)orig_WndProc);
        }

        rakhook::destroy();
        Patches::SplashD3D::Shutdown();
        Patches::Security::ShutdownSecurityFixes();
    }
    return TRUE;
}