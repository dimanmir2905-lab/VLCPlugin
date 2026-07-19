#define GTA_SA
#define _WINSOCKAPI_ 

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

#include "patches/manager.hpp"
#include "patches/security.hpp"
#include "patches/splash_d3d.hpp" 
#include "hooks/samp_events.hpp"
#include "utils/window.hpp"
#include "utils/chat.hpp"
#include "utils/crash_handler.hpp"
#include "utils/samp_chat.hpp"

#include <RakHook/rakhook.hpp>
#include <RakNet/BitStream.h>

#include <imgui.h>
#include <imgui_impl_dx9.h>
#include <imgui_impl_win32.h>

// Глобальные переменные для хука окна
WNDPROC oWndProc = nullptr;
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

using namespace sampapi::v037r3;

namespace {
    void LogToFile(const std::string& message) {
        char exePath[MAX_PATH];
        if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) == 0) return;
        std::string path = std::string(exePath).substr(0, std::string(exePath).find_last_of("\\/")) + "\\VialencePlugin.log";
        std::ofstream logFile(path, std::ios::app);
        if (logFile.is_open()) {
            auto now = std::chrono::system_clock::now();
            auto in_time_t = std::chrono::system_clock::to_time_t(now);
            std::tm tm_buf;
            localtime_s(&tm_buf, &in_time_t);
            logFile << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S") << " | " << message << "\n";
        }
    }

    // Исправленный хук окна
    LRESULT CALLBACK hkWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        if (Utils::SampChat::IsInputActive()) {
            ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);
            // Блокируем клавиши, чтобы персонаж не двигался при наборе текста
            if (uMsg == WM_KEYDOWN || uMsg == WM_KEYUP || uMsg == WM_CHAR || uMsg == WM_MOUSEMOVE) {
                return TRUE;
            }
        }
        return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
    }

    void SetupNetworkHooks() {
        if (!rakhook::initialize()) {
            LogToFile("RakHook init FAILED!");
            return;
        }
        rakhook::on_receive_rpc += [](unsigned char& id, RakNet::BitStream* bs) -> bool { return true; };
        rakhook::on_send_packet += [](RakNet::BitStream* bs, PacketPriority& priority, PacketReliability& reliability, char& ord_channel) -> bool { return true; };
    }

    void InitThread() {
        Patches::ApplyAll();
        Patches::Security::ApplyAllSecurityFixes();
        Patches::SplashD3D::Initialize();
        Hooks::InitializeSampEvents();

        while (!RefChat() || !RefNetGame() || !RefInputBox()) { Sleep(100); }

        RefChat()->m_nMode = 0; // Скрываем оригинальный чат
        Utils::SampChat::Initialize();
        SetupNetworkHooks();
        Utils::SetCustomWindow("Vialence Online", "data\\Icons\\icon.ico");

        HWND gameHwnd = Utils::GetGameHwnd();
        if (gameHwnd) {
            oWndProc = (WNDPROC)SetWindowLongPtrA(gameHwnd, GWLP_WNDPROC, (LONG_PTR)hkWndProc);
            LogToFile("WndProcHook успешно установлен.");
        }
        Utils::PrintChat(0xB9C9BFFF, "Vialence Plugin loaded (0.3.7-R3)");
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        Utils::InitializeCrashHandler();
        std::thread(InitThread).detach();
    }
    else if (ul_reason_for_call == DLL_PROCESS_DETACH) {
        HWND hwnd = Utils::GetGameHwnd();
        if (hwnd && oWndProc) {
            SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)oWndProc);
        }
        rakhook::destroy();
        Patches::SplashD3D::Shutdown();
        Patches::Security::ShutdownSecurityFixes();
    }
    return TRUE;
}