#pragma execution_character_set("utf-8")

#include "imgui_menu.hpp"
#include <imgui.h>
#include <imgui_impl_dx9.h>
#include <imgui_impl_win32.h>

namespace ImGuiMenu {
    static bool g_initialized = false;
    static bool g_visible = false;  // Меню скрыто по умолчанию
    static HWND g_hwnd = nullptr;

    void Initialize(HWND hwnd, IDirect3DDevice9* device) {
        if (g_initialized) return;
        g_hwnd = hwnd;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        // Загрузка шрифта с кириллицей
        ImFontConfig font_cfg;
        font_cfg.OversampleH = 3;
        font_cfg.OversampleV = 3;
        font_cfg.PixelSnapH = true;

        // Явный диапазон кириллицы
        static const ImWchar cyrillic_ranges[] = {
            0x0020, 0x00FF, // Базовая латиница + символы
            0x0400, 0x044F, // Кириллица
            0
        };

        ImFont* font = io.Fonts->AddFontFromFileTTF(
            "C:\\Windows\\Fonts\\arial.ttf",
            16.0f,
            &font_cfg,
            cyrillic_ranges
        );

        if (!font) {
            OutputDebugStringA("[VLC] ОШИБКА: Не удалось загрузить шрифт!\n");
            return;
        }

        ImGui::StyleColorsDark();
        ImGui_ImplWin32_Init(g_hwnd);
        ImGui_ImplDX9_Init(device);

        g_initialized = true;
        OutputDebugStringA("[VLC] Шрифт с кириллицей загружен\n");
    }

    void Shutdown() {
        if (!g_initialized) return;
        ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_initialized = false;
    }

    void Render(IDirect3DDevice9* device) {
        if (!g_initialized) return;

        // Обработка F4
        static bool lastKeyState = false;
        bool currentKeyState = (GetAsyncKeyState(VK_F4) & 0x8000) != 0;
        if (currentKeyState && !lastKeyState) {
            g_visible = !g_visible;
        }
        lastKeyState = currentKeyState;

        if (!g_visible) return;

        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Vialence Plugin Menu", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

        float fps = ImGui::GetIO().Framerate;
        ImGui::Text("FPS: %.1f", fps);
        ImGui::Separator();

        // ТЕСТОВЫЕ СТРОКИ
        ImGui::Text("Test ABC");              // Латиница
        ImGui::Text("Тест Кириллица");        // Кириллица
        ImGui::Text("Версия 1.0");

        ImGui::End();

        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    }

    void InvalidateDeviceObjects() {
        if (g_initialized) ImGui_ImplDX9_InvalidateDeviceObjects();
    }

    void CreateDeviceObjects() {
        if (g_initialized) ImGui_ImplDX9_CreateDeviceObjects();
    }

    bool IsVisible() {
        return g_visible;
    }

    void SetVisible(bool visible) {
        g_visible = visible;
    }

    void Toggle() {
        g_visible = !g_visible;
    }
}