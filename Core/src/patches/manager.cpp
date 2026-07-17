#include "patches/manager.hpp"

namespace Patches {

    // Вспомогательная функция для определения версии SAMP (R1 или R3)
    static bool IsSampR1() {
        uintptr_t sampBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("samp.dll"));
        if (!sampBase) return false;
        return (*reinterpret_cast<unsigned char*>(sampBase + 0x129) == 0xF4);
    }

    void ApplyClientVersion(const char* version) {
        uintptr_t sampBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("samp.dll"));
        if (!sampBase) return;

        // Выделяем память для новой строки версии
        LPVOID allocatedMem = VirtualAlloc(nullptr, strlen(version) + 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!allocatedMem) return;

        strcpy_s(reinterpret_cast<char*>(allocatedMem), strlen(version) + 1, version);

        uintptr_t addrPush = sampBase + 0xAC6F;
        Memory::Write<BYTE>(addrPush, 0x68); // push
        Memory::Write<uintptr_t>(addrPush + 1, reinterpret_cast<uintptr_t>(allocatedMem));
    }

    void DisableOpenSAA() {
        uintptr_t sampBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("samp.dll"));
        if (!sampBase) return;

        DWORD patchOffset = IsSampR1() ? 0x5F06C : 0x6240C;
        Memory::Write<BYTE>(sampBase + patchOffset, 0xEB); // jmp
    }

    void RemoveVehicleLimit() {
        HMODULE hSamp = GetModuleHandleA("samp.dll");
        if (!hSamp) return;

        // Сигнатура: 3D 90 01 00 00 0F 8C 32 01 00 00 3D 63 02 00 00 0F 8F 27 01 00 00
        const char* pattern = "\x3D\x90\x01\x00\x00\x0F\x8C\x32\x01\x00\x00\x3D\x63\x02\x00\x00\x0F\x8F\x27\x01\x00\x00";
        const char* mask = "xxxxxxxxxxxxxxxxxxxxxx";

        uintptr_t foundAddress = Memory::FindPattern(hSamp, pattern, mask);
        if (!foundAddress) return;

        // Патчим два условных перехода на NOP
        Memory::Fill(foundAddress + 5, 0x90, 6);
        Memory::Fill(foundAddress + 5 + 11, 0x90, 6);
    }

    void FixCNetStatFreeze() {
        uintptr_t sampBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("samp.dll"));
        if (!sampBase) return;

        DWORD patchOffset = IsSampR1() ? 0x5D419 : 0x607B1;
        Memory::Write<BYTE>(sampBase + patchOffset, 0xEB); // jmp
    }

    void ApplyCustomData() {
        // 1. mem.copy (72 байта)
        const char patchData[] =
            "\x36\x46\x45\x50\x5F\x52\x45\x53\x00\x0B\x00\x00\x40\x01\xAA\x00\x03\x00\x05"
            "\x46\x45\x48\x5F\x4D\x41\x50\x00\x0B\x05\x00\x40\x01\xC8\x00\x03\x00\x05"
            "\x46\x45\x50\x5F\x4F\x50\x54\x00\x0B\x21\x00\x40\x01\xE6\x00\x03\x00\x05"
            "\x46\x45\x50\x5F\x51\x55\x49\x00\x0B\x23\x00\x40\x01\x04\x01\x03\x00";
        Memory::Copy(0x8D0444, patchData, 72);

        // 2. mem.fill (144 байта нулем)
        Memory::Fill(0x8D048C, 0x00, 144);

        // 3. mem.write (запись 1 байта)
        Memory::Write<BYTE>(0x8CE47B, 1);
        Memory::Write<BYTE>(0x8CFD33, 2);
        Memory::Write<BYTE>(0x8CFEF7, 3);

        // 4. Отключение подсказок (hints)
        Memory::Fill(0x57E3AE, 0x90, 5);

        // 5. Отключение надписи в верхнем меню
        Memory::Fill(0x579698, 0x90, 5);

        // 6. Дополнительный NOP-патч
        Memory::Fill(0x588188, 0x90, 5);
    }

    void DisableSampDialogs() {
        uintptr_t sampBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("samp.dll"));
        if (!sampBase) return;

        // Отключение HelpDialog (F1)
        Memory::Fill(sampBase + 0x752E2, 0x90, 5);

        // Отключение "Returning to class selection after next" (F4)
        Memory::Fill(sampBase + 0x79B0, 0x90, 10);
        Memory::Fill(sampBase + 0x79C6, 0x90, 5);
    }

    void ApplyAll() {
        // Ждем загрузки samp.dll перед применением патчей
        while (!GetModuleHandleA("samp.dll")) {
            Sleep(50);
        }

        ApplyClientVersion("Vialence");
        DisableOpenSAA();
        RemoveVehicleLimit();
        FixCNetStatFreeze();
        ApplyCustomData();
        DisableSampDialogs();
    }

}