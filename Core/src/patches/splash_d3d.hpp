#pragma once
#include <d3d9.h>

namespace Patches::SplashD3D {
    void Initialize();
    void Shutdown();

    void SetConnectionStatus(const char* text);
    void SetConnectionSuccess(const char* text);
    void SetConnectionError(const char* text);
}