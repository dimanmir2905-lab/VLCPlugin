#include "utils/crash_handler.hpp"
#include <windows.h>
#include <dbghelp.h>
#include <fstream>
#include <string>
#include <ctime>
#include <iomanip>

#pragma comment(lib, "dbghelp.lib")

namespace Utils {
    namespace {
        LPTOP_LEVEL_EXCEPTION_FILTER g_previousFilter = nullptr;

        std::string GetCurrentDateTime() {
            std::time_t now = std::time(nullptr);
            std::tm tm_info;
            localtime_s(&tm_info, &now);
            char buffer[32];
            std::strftime(buffer, sizeof(buffer), "%Y-%m-%d_%H-%M-%S", &tm_info);
            return std::string(buffer);
        }

        // Получаем гарантированный путь к папке с gta_sa.exe
        std::string GetGameDirectory() {
            char path[MAX_PATH];
            GetModuleFileNameA(nullptr, path, MAX_PATH);
            std::string sPath(path);
            return sPath.substr(0, sPath.find_last_of("\\/")) + "\\";
        }

        LONG WINAPI CustomCrashHandler(EXCEPTION_POINTERS* ExceptionInfo) {
            std::string timestamp = GetCurrentDateTime();
            std::string gameDir = GetGameDirectory();

            std::string dumpFileName = gameDir + "VialencePlugin_Crash_" + timestamp + ".dmp";
            std::string logFileName = gameDir + "VialencePlugin_Crash_" + timestamp + ".log";

            // 1. Создаем текстовый лог
            std::ofstream logFile(logFileName);
            if (logFile.is_open()) {
                logFile << "=== VLC Plugin Crash Report ===" << std::endl;
                logFile << "Time: " << timestamp << std::endl;
                logFile << "Exception Code: 0x" << std::hex << ExceptionInfo->ExceptionRecord->ExceptionCode << std::endl;
                logFile << "Exception Address: 0x" << std::hex << ExceptionInfo->ExceptionRecord->ExceptionAddress << std::endl;

                HMODULE hModule = nullptr;
                if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    (LPCSTR)ExceptionInfo->ExceptionRecord->ExceptionAddress, &hModule)) {
                    char moduleName[MAX_PATH];
                    GetModuleFileNameA(hModule, moduleName, MAX_PATH);
                    logFile << "Faulting Module: " << moduleName << std::endl;
                }
                else {
                    logFile << "Faulting Module: Unknown (Null pointer or severe corruption)" << std::endl;
                }
                logFile.close();
            }

            // 2. Создаем MiniDump
            HANDLE hFile = CreateFileA(dumpFileName.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile != INVALID_HANDLE_VALUE) {
                MINIDUMP_EXCEPTION_INFORMATION mdei;
                mdei.ThreadId = GetCurrentThreadId();
                mdei.ExceptionPointers = ExceptionInfo;
                mdei.ClientPointers = FALSE;

                MiniDumpWriteDump(
                    GetCurrentProcess(),
                    GetCurrentProcessId(),
                    hFile,
                    MiniDumpWithDataSegs,
                    &mdei,
                    nullptr,
                    nullptr
                );
                CloseHandle(hFile);
            }

            // 3. Передаем управление дальше (например, в fastman92 или SAMPFUNCS)
            if (g_previousFilter) {
                return g_previousFilter(ExceptionInfo);
            }

            return EXCEPTION_EXECUTE_HANDLER;
        }
    }

    void InitializeCrashHandler() {
        g_previousFilter = SetUnhandledExceptionFilter(CustomCrashHandler);
    }
}