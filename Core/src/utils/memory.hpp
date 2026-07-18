#pragma once
#include "sdk.hpp"
#include <psapi.h>
#include <vector>

// Подключаем библиотеку psapi для линкера
#pragma comment(lib, "psapi.lib")

namespace Memory {

    // Безопасная запись значения по адресу
    template<typename T>
    inline void Write(uintptr_t address, T value) {
        DWORD oldProtect;
        size_t len = sizeof(T);
        if (VirtualProtect(reinterpret_cast<void*>(address), len, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            *reinterpret_cast<T*>(address) = value;
            VirtualProtect(reinterpret_cast<void*>(address), len, oldProtect, &oldProtect);
        }
    }

    // Заполнение памяти значением (например, NOP)
    inline void Fill(uintptr_t address, BYTE value, size_t size) {
        DWORD oldProtect;
        if (VirtualProtect(reinterpret_cast<void*>(address), size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            memset(reinterpret_cast<void*>(address), value, size);
            VirtualProtect(reinterpret_cast<void*>(address), size, oldProtect, &oldProtect);
        }
    }

    // Копирование блока данных в память
    inline void Copy(uintptr_t address, const void* data, size_t size) {
        DWORD oldProtect;
        if (VirtualProtect(reinterpret_cast<void*>(address), size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            memcpy(reinterpret_cast<void*>(address), data, size);
            VirtualProtect(reinterpret_cast<void*>(address), size, oldProtect, &oldProtect);
        }
    }

    // Поиск адреса по байтовой сигнатуре (поддерживает '?' как wildcard)
    inline uintptr_t FindPattern(HMODULE hModule, const char* pattern, const char* mask) {
        MODULEINFO moduleInfo;
        GetModuleInformation(GetCurrentProcess(), hModule, &moduleInfo, sizeof(moduleInfo));

        uintptr_t base = reinterpret_cast<uintptr_t>(hModule);
        size_t moduleSize = moduleInfo.SizeOfImage;
        size_t maskLen = strlen(mask);

        for (uintptr_t i = 0; i < moduleSize - maskLen; i++) {
            bool found = true;
            for (size_t j = 0; j < maskLen; j++) {
                if (mask[j] != '?' && pattern[j] != *reinterpret_cast<char*>(base + i + j)) {
                    found = false;
                    break;
                }
            }
            if (found) {
                return base + i;
            }
        }
        return 0;
    }

    // Простой поиск строки (как в твоём оригинальном коде)
    inline uintptr_t FindString(HMODULE hModule, const char* str) {
        MODULEINFO moduleInfo;
        GetModuleInformation(GetCurrentProcess(), hModule, &moduleInfo, sizeof(moduleInfo));

        uintptr_t base = reinterpret_cast<uintptr_t>(hModule);
        size_t strSize = strlen(str);

        for (uintptr_t i = 0; i < moduleInfo.SizeOfImage - strSize; i++) {
            if (memcmp(reinterpret_cast<void*>(base + i), str, strSize) == 0) {
                return base + i;
            }
        }
        return 0;
    }
}