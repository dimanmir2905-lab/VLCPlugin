#include "patches/security.hpp"
#include "hooks/rpc_security.hpp"
#include "utils/chat.hpp"
#include <fstream>

namespace Patches::Security {

    // Вспомогательная функция для определения версии SAMP (R1 или R3)
    static bool IsSampR1() {
        uintptr_t sampBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("samp.dll"));
        if (!sampBase) return false;
        return (*reinterpret_cast<unsigned char*>(sampBase + 0x129) == 0xF4);
    }

    // ==========================================
    // 1. Fix SetSpawnInfo Buffer Overflow (ИСПРАВЛЕНО: поддержка R1 и R3)
    // ==========================================
    void FixSetSpawnInfoBufferOverflow() {
        uintptr_t sampBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("samp.dll"));
        if (!sampBase) return;

        // Адрес для R1: 0x460773, Адрес для R3: 0x4607D3
        DWORD patchOffset = IsSampR1() ? 0x460773 : 0x4607D3;

        // Заменяем 7 байт на NOP (0x90)
        Memory::Fill(sampBase + patchOffset, 0x90, 7);
    }

    // ==========================================
    // 2. Fix GameText (AddBigMessage) RCE/Crash
    // ==========================================
    typedef void(__thiscall* AddBigMessage_t)(void* pMessages, char* text, uint32_t duration, uint16_t style);
    static AddBigMessage_t oAddBigMessage = nullptr;

    void __fastcall hAddBigMessage(void* pMessages, void* edx, char* text, uint32_t duration, uint16_t style) {
        // Если стиль больше 6 (максимум в GTA SA), блокируем вызов
        if (style > 6) {
            return;
        }
        oAddBigMessage(pMessages, text, duration, style);
    }

    void InstallGameTextHook() {
        uintptr_t gtaBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("gta_sa.exe"));
        if (!gtaBase) return;

        uintptr_t addr = gtaBase + 0x69F2B0;
        MH_CreateHook(reinterpret_cast<void*>(addr), hAddBigMessage, reinterpret_cast<void**>(&oAddBigMessage));
        MH_EnableHook(reinterpret_cast<void*>(addr));
    }

    // ==========================================
    // 3. Fix Malicious TXD Loading (ИСПРАВЛЕНО: Calling Convention)
    // ==========================================
    // Оригинальная функция является __thiscall, поэтому хук должен быть __fastcall
    typedef int(__thiscall* LoadTxdFile_t)(void* this_ptr, uint32_t txdIndex, char* fileName);
    static LoadTxdFile_t oLoadTxdFile = nullptr;

    struct ChunkHeader {
        uint32_t type;
        uint32_t length;
        uint32_t version;
    };

    int __fastcall hLoadTxdFile(void* this_ptr, void* edx, uint32_t txdIndex, char* fileName) {
        if (fileName && strlen(fileName) > 0) {
            // Блокируем очевидные попытки выхода за пределы директории
            if (strstr(fileName, "..") != nullptr || strstr(fileName, ":\\") != nullptr) {
                return 0;
            }

            // Проверяем заголовок файла (ВНИМАНИЕ: это может вызывать микро-лаги при загрузке, 
            // но безопасно предотвращает загрузку битых TXD)
            std::ifstream file(fileName, std::ios::binary);
            if (file.is_open()) {
                ChunkHeader outer{}, first{};
                if (file.read(reinterpret_cast<char*>(&outer), sizeof(outer)) &&
                    outer.type == 0x16 && // kRwTextureDictionary
                    outer.length >= sizeof(ChunkHeader) &&
                    file.read(reinterpret_cast<char*>(&first), sizeof(first)) &&
                    first.type == 0x01) { // kRwStruct

                    file.close();
                    // ВЫЗЫВАЕМ ОРИГИНАЛ С ПРАВИЛЬНЫМ this_ptr!
                    return oLoadTxdFile(this_ptr, txdIndex, fileName);
                }
                file.close();
                return 0; // Блокируем загрузку невалидного TXD
            }
        }
        // Если файл не найден на диске (например, он в .img архиве), передаем управление оригиналу
        return oLoadTxdFile(this_ptr, txdIndex, fileName);
    }

    void InstallTxdHook() {
        uintptr_t gtaBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("gta_sa.exe"));
        if (!gtaBase) return;

        uintptr_t addr = gtaBase + 0x7320B0;
        MH_CreateHook(reinterpret_cast<void*>(addr), hLoadTxdFile, reinterpret_cast<void**>(&oLoadTxdFile));
        MH_EnableHook(reinterpret_cast<void*>(addr));
    }

    // ==========================================
    // Главный вызов
    // ==========================================
    void ApplyAllSecurityFixes() {
        MH_Initialize();

        FixSetSpawnInfoBufferOverflow();
        InstallGameTextHook();
        InstallTxdHook();

        // Инициализируем защиту RPC
        Hooks::RPCSecurity::Initialize();
    }

    void ShutdownSecurityFixes() {
        Hooks::RPCSecurity::Shutdown();
        MH_Uninitialize();
    }
}