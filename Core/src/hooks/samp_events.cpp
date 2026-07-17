#include "hooks/samp_events.hpp"
#include "utils/chat.hpp"
#include "patches/splash_d3d.hpp" // <-- ВАЖНО: Подключаем наш D3D модуль

using namespace sampapi::v037r3;

namespace {
    // Обработчики событий: обновляют И чат, И D3D-оверлей
    static void __cdecl OnSampConnect() {
        Utils::PrintChat(0xB9C9BFFF, "Подключение к серверу...");
        Patches::SplashD3D::SetConnectionStatus("Подключение к серверу...");
    }

    static void __cdecl OnSampWelcome() {
        Utils::PrintChat(0xB9C9BFFF, "Успешный вход. Приятной игры на сервере :)");
        Patches::SplashD3D::SetConnectionSuccess("Успешный вход! Приятной игры :)"); // <-- Запускает fade-out (исчезновение)
    }

    static void __cdecl OnSampBan() {
        Utils::PrintChat(0xB9C9BFFF, "Вы забанены на данном сервере");
        Patches::SplashD3D::SetConnectionError("Вы забанены на данном сервере");
    }

    static void __cdecl OnSampFull() {
        Utils::PrintChat(0xB9C9BFFF, "Сервер заполнен. Повторяем подключение...");
        Patches::SplashD3D::SetConnectionError("Сервер заполнен. Повторяем подключение...");
    }

    static void __cdecl OnSampWrongPass() {
        Utils::PrintChat(0xB9C9BFFF, "Неверный пароль.");
        Patches::SplashD3D::SetConnectionError("Неверный пароль.");
    }

    static void __cdecl OnSampCloseCon() {
        Utils::PrintChat(0xB9C9BFFF, "Сервер закрыл соединение.");
        Patches::SplashD3D::SetConnectionError("Сервер закрыл соединение.");
    }

    static void __cdecl OnSampNoOtven() {
        Utils::PrintChat(0xB9C9BFFF, "Сервер не отвечает. Повторяем подключение...");
        Patches::SplashD3D::SetConnectionError("Сервер не отвечает. Повторяем подключение...");
    }

    static void __cdecl OnSampRestart() {
        Utils::PrintChat(0xB9C9BFFF, "Сервер перезагрузился.");
        Patches::SplashD3D::SetConnectionError("Сервер перезагрузился.");
    }

    static void __cdecl OnSampPotera() {
        Utils::PrintChat(0xB9C9BFFF, "Потеря соединения с сервером");
        Patches::SplashD3D::SetConnectionError("Потеря соединения с сервером");
    }

    static void __cdecl OnSampJoin() {
        Utils::PrintChat(0xB9C9BFFF, "Подключились, входим в игру...");
        Patches::SplashD3D::SetConnectionStatus("Подключились, входим в игру...");
    }

    static void __cdecl OnSampStart() {
        /* Заглушка */
    }
}

namespace Hooks {

    void InitializeSampEvents() {
        while (!GetModuleHandleA("samp.dll")) {
            Sleep(50);
        }

        // Перехватываем вызовы SAMP и перенаправляем на наши функции
        plugin::patch::RedirectCall(sampapi::GetAddress(0xB7CA), OnSampStart);

        plugin::patch::RedirectCall(sampapi::GetAddress(0x8603), OnSampConnect);
        plugin::patch::RedirectCall(sampapi::GetAddress(0x10750), OnSampWelcome);

        plugin::patch::RedirectCall(sampapi::GetAddress(0x8A0B), OnSampBan);
        plugin::patch::RedirectCall(sampapi::GetAddress(0xAFE8), OnSampBan);

        plugin::patch::RedirectCall(sampapi::GetAddress(0x8A4E), OnSampFull);
        plugin::patch::RedirectCall(sampapi::GetAddress(0xB050), OnSampFull);

        plugin::patch::RedirectCall(sampapi::GetAddress(0x8A7E), OnSampCloseCon);

        plugin::patch::RedirectCall(sampapi::GetAddress(0x8ABE), OnSampWrongPass);
        plugin::patch::RedirectCall(sampapi::GetAddress(0xB02E), OnSampWrongPass);

        plugin::patch::RedirectCall(sampapi::GetAddress(0x8AFE), OnSampNoOtven);
        plugin::patch::RedirectCall(sampapi::GetAddress(0xB0450), OnSampNoOtven);

        plugin::patch::RedirectCall(sampapi::GetAddress(0xA3CA), OnSampRestart);
        plugin::patch::RedirectCall(sampapi::GetAddress(0xA9AF), OnSampPotera);
        plugin::patch::RedirectCall(sampapi::GetAddress(0xAB16), OnSampJoin);
    }

}