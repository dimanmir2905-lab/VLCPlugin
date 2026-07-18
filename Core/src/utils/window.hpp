#pragma once
#include <windows.h> // Добавлено для типа HWND

namespace Utils {
    // Устанавливает заголовок и иконку окна GTA:SA:MP
    void SetCustomWindow(const char* title, const char* iconRelativePath);

    // Возвращает HWND найденного окна игры
    HWND GetGameHwnd();
}