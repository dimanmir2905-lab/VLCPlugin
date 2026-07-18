#pragma once
#include <windows.h>
#include <d3d9.h>

namespace ImGuiMenu {
    void Initialize(HWND hwnd, IDirect3DDevice9* device);
    void Shutdown();
    void Render(IDirect3DDevice9* device);
    void InvalidateDeviceObjects();
    void CreateDeviceObjects();
    bool IsVisible();
    void SetVisible(bool visible);
    void Toggle();
}