#include "utils/window.hpp"
#include <string>

namespace Utils {
    // Статическая переменная для хранения найденного HWND
    static HWND g_gameHwnd = nullptr;

    HWND GetGameHwnd() {
        return g_gameHwnd;
    }

    void SetCustomWindow(const char* title, const char* iconRelativePath) {
        HWND hwnd = nullptr;

        // 1. Пробуем найти окно по наиболее частым названиям заголовка
        const char* possibleTitles[] = {
            "GTA:SA:MP",
            "GTA: San Andreas",
            "GTA:SA"
        };

        for (const char* possibleTitle : possibleTitles) {
            hwnd = FindWindowA(nullptr, possibleTitle);
            if (hwnd) break;
        }

        // 2. Если не нашли по заголовку, пробуем найти по классу окна
        if (!hwnd) {
            hwnd = FindWindowA("GTA:SA:MP", nullptr);
        }
        if (!hwnd) {
            hwnd = FindWindowA("GTA:SA", nullptr);
        }

        // Если окно найдено, СОХРАНЯЕМ его и применяем изменения
        if (hwnd) {
            g_gameHwnd = hwnd; // <--- КЛЮЧЕВОЕ ИЗМЕНЕНИЕ: сохраняем HWND

            // 3. Устанавливаем новый заголовок
            SetWindowTextA(hwnd, title);

            // 4. Формируем абсолютный путь к иконке
            char exePath[MAX_PATH];
            GetModuleFileNameA(GetModuleHandleA(nullptr), exePath, MAX_PATH);

            std::string dirPath = exePath;
            size_t lastSlash = dirPath.find_last_of("\\/");
            if (lastSlash != std::string::npos) {
                dirPath = dirPath.substr(0, lastSlash + 1);
            }

            std::string fullPath = dirPath + iconRelativePath;

            // 5. Загружаем и устанавливаем иконку
            HICON hIconBig = reinterpret_cast<HICON>(LoadImageA(nullptr, fullPath.c_str(), IMAGE_ICON, 32, 32, LR_LOADFROMFILE));
            HICON hIconSmall = reinterpret_cast<HICON>(LoadImageA(nullptr, fullPath.c_str(), IMAGE_ICON, 16, 16, LR_LOADFROMFILE));

            if (hIconBig) {
                SendMessageA(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIconBig));
            }
            if (hIconSmall) {
                SendMessageA(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIconSmall));
            }
        }
    }
}