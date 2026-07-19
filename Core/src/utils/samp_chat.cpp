#pragma execution_character_set("utf-8")
#include "samp_chat.hpp"
#include "sdk.hpp" 
#include <windows.h>
#include <algorithm>
#include <sstream>
#include <cctype>
#include <chrono>
#include <iomanip>
#include <vector>
#include <string>
#include <imgui.h>

namespace Utils {
    namespace SampChat {

        static std::vector<ChatMessage> messages;
        static bool isVisible = true;
        static bool isInputActive = false;
        static char inputBuffer[256] = { 0 };

        // Трамплин для оригинального метода добавления строки в чат SA-MP
        typedef void(__thiscall* CChat__AddEntry_t)(void*, int, const char*, const char*, uint32_t, uint32_t);
        static CChat__AddEntry_t orig_CChat__AddEntry = nullptr;

        // Наш хук: перехватывает ВСЕ сообщения (серверные, клиентские, от игроков)
        void __fastcall Hooked__CChat__AddEntry(void* ecx, void* edx, int nType, const char* szText, const char* szPrefix, uint32_t textColor, uint32_t prefixColor) {
            std::string fullText = "";
            if (szPrefix && strlen(szPrefix) > 0) {
                fullText += std::string(szPrefix) + ": ";
            }
            if (szText) {
                fullText += szText;
            }

            // Добавляем сообщение в наш ImGui чат
            AddMessage(textColor, fullText);

            // Вызываем оригинал, чтобы SA-MP продолжал писать строки в чатлог (chatlog.txt)
            if (orig_CChat__AddEntry) {
                orig_CChat__AddEntry(ecx, nType, szText, szPrefix, textColor, prefixColor);
            }
        }

        static std::string Cp1251ToUtf8(const std::string& str) {
            if (str.empty()) return "";
            int wlen = MultiByteToWideChar(1251, 0, reinterpret_cast<LPCCH>(str.c_str()), -1, nullptr, 0);
            if (wlen <= 0) return "";
            std::wstring wstr(wlen, 0);
            MultiByteToWideChar(1251, 0, reinterpret_cast<LPCCH>(str.c_str()), -1, &wstr[0], wlen);
            if (!wstr.empty() && wstr.back() == L'\0') wstr.pop_back();

            int u8len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (u8len <= 0) return "";
            std::string u8str(u8len, 0);
            WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &u8str[0], u8len, nullptr, nullptr);
            if (!u8str.empty() && u8str.back() == '\0') u8str.pop_back();
            return u8str;
        }

        static std::string Utf8ToCp1251(const std::string& str) {
            if (str.empty()) return "";
            int wlen = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
            std::wstring wstr(wlen, 0);
            MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], wlen);
            if (!wstr.empty() && wstr.back() == L'\0') wstr.pop_back();

            int cpLen = WideCharToMultiByte(1251, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
            std::string cpStr(cpLen, 0);
            WideCharToMultiByte(1251, 0, wstr.c_str(), -1, &cpStr[0], cpLen, nullptr, nullptr);
            if (!cpStr.empty() && cpStr.back() == '\0') cpStr.pop_back();
            return cpStr;
        }

        static std::string GetCurrentTimestamp() {
            auto now = std::chrono::system_clock::now();
            auto in_time_t = std::chrono::system_clock::to_time_t(now);
            std::tm tm_buf;
            localtime_s(&tm_buf, &in_time_t);
            std::ostringstream oss;
            oss << std::put_time(&tm_buf, "[%H:%M:%S] ");
            return oss.str();
        }

        static std::vector<TextSegment> ParseColoredText(const std::string& text, uint32_t defaultColor) {
            std::vector<TextSegment> segments;
            std::string currentText;

            // Конвертируем ARGB/RGBA порядок байт корректно под формат ImColor
            uint8_t a = (defaultColor >> 24) & 0xFF;
            uint8_t r = (defaultColor >> 16) & 0xFF;
            uint8_t g = (defaultColor >> 8) & 0xFF;
            uint8_t b = defaultColor & 0xFF;
            if (a == 0) a = 255;
            ImColor currentColor(r, g, b, a);

            for (size_t i = 0; i < text.size(); ++i) {
                if (text[i] == '{' && i + 7 < text.size() && text[i + 7] == '}') {
                    std::string hexCode = text.substr(i + 1, 6);
                    bool isColor = true;
                    for (char c : hexCode) {
                        if (!std::isxdigit(static_cast<unsigned char>(c))) {
                            isColor = false;
                            break;
                        }
                    }
                    if (isColor) {
                        if (!currentText.empty()) {
                            segments.push_back({ currentText, currentColor });
                            currentText.clear();
                        }
                        unsigned int r_hex = 0, g_hex = 0, b_hex = 0;
                        sscanf_s(hexCode.c_str(), "%02x%02x%02x", &r_hex, &g_hex, &b_hex);
                        currentColor = ImColor(static_cast<int>(r_hex), static_cast<int>(g_hex), static_cast<int>(b_hex), static_cast<int>(a));
                        i += 7;
                        continue;
                    }
                }
                currentText += text[i];
            }
            if (!currentText.empty()) {
                segments.push_back({ currentText, currentColor });
            }
            return segments;
        }

        void Initialize() {
            messages.clear();
            isVisible = true;
            isInputActive = false;
            memset(inputBuffer, 0, sizeof(inputBuffer));

            uintptr_t sampBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("samp.dll"));
            if (!sampBase) return;

            // 1. Отключаем отрисовку черной подложки оригинального чата (SAMPGUI)
            DWORD oldProtect;
            VirtualProtect(reinterpret_cast<void*>(sampBase + 0x643FD), 5, PAGE_EXECUTE_READWRITE, &oldProtect);
            memset(reinterpret_cast<void*>(sampBase + 0x643FD), 0x90, 5);
            VirtualProtect(reinterpret_cast<void*>(sampBase + 0x643FD), 5, oldProtect, &oldProtect);

            // 2. Глушим нативный рендер строки ввода DXUTEditBox
            VirtualProtect(reinterpret_cast<void*>(sampBase + 0x71480), 1, PAGE_EXECUTE_READWRITE, &oldProtect);
            *reinterpret_cast<uint8_t*>(sampBase + 0x71480) = 0xEB;
            VirtualProtect(reinterpret_cast<void*>(sampBase + 0x71480), 1, oldProtect, &oldProtect);

            // 3. Устанавливаем хук на правильный адрес CChat::AddEntry для 0.3.7-R3 (0x67460)
            void* targetAddress = reinterpret_cast<void*>(sampBase + 0x67460);

            if (MH_CreateHook(targetAddress, &Hooked__CChat__AddEntry, reinterpret_cast<void**>(&orig_CChat__AddEntry)) == MH_OK) {
                MH_EnableHook(targetAddress);
            }
        }

        void EnableInput(bool enable) {
            auto pInput = sampapi::v037r3::RefInputBox();
            if (!pInput) return;

            isInputActive = enable;

            // Вместо pInput->Open()/Close() просто ставим флаг блокировки управления игроком
            // Это заставит игру игнорировать WASD, но не вызовет отрисовку GUI
            pInput->m_bEnabled = enable;

            if (enable) {
                memset(inputBuffer, 0, sizeof(inputBuffer));
                ImGui::GetIO().WantTextInput = true;
            }
            else {
                ImGui::GetIO().WantTextInput = false;
                memset(pInput->m_szInput, 0, sizeof(pInput->m_szInput));
                // Принудительно возвращаем фокус игре, если нужно
                SetFocus(GetActiveWindow());
            }
        }

        void Update(float deltaTime) {
            if (deltaTime > 0.1f) deltaTime = 0.1f;
            if (deltaTime < 0.0f) deltaTime = 0.0f;

            for (auto it = messages.begin(); it != messages.end(); ) {
                if (!isInputActive) {
                    it->lifetime -= deltaTime;
                }
                else {
                    it->lifetime = Utils::SampChat::MESSAGE_LIFETIME;
                    it->targetAlpha = 1.0f;
                }

                if (it->lifetime <= 0.0f) {
                    it->targetAlpha = 0.0f;
                }

                float t = deltaTime * Utils::SampChat::FADE_SPEED;
                if (t > 1.0f) t = 1.0f;
                it->alpha = it->alpha + (it->targetAlpha - it->alpha) * t;

                if (it->alpha < 0.01f && it->targetAlpha == 0.0f) {
                    it = messages.erase(it);
                }
                else {
                    ++it;
                }
            }
        }

        void Render() {
            if (!ImGui::GetCurrentContext() || !isVisible) return;

            // Принудительно гасим оригинальный чат SA-MP, чтобы он не прорисовывался сзади
            if (sampapi::v037r3::RefChat()) {
                sampapi::v037r3::RefChat()->m_nMode = 0;
            }

            ImGuiIO& io = ImGui::GetIO();

            ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoBackground;

            if (!isInputActive) {
                flags |= ImGuiWindowFlags_NoInputs;
            }

            // НАСТРОЙКА ПОЗИЦИИ: Ставим чат в левый верхний угол, как в оригинале
            float sampChatX = 20.0f;           // Отступ слева (можно поставить 15.0f-25.0f под разрешение)
            float sampChatY = 25.0f;           // Отступ сверху (оставляем место под худ/килллист если нужно)
            float sampChatWidth = io.DisplaySize.x * 0.55f; // Оптимальная ширина для чата (55% экрана)
            float totalWindowHeight = 280.0f;  // Высота зоны чата

            ImGui::SetNextWindowPos(ImVec2(sampChatX, sampChatY), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(sampChatWidth, totalWindowHeight), ImGuiCond_Always);

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 3.0f));

            if (ImGui::Begin("##SampChat", nullptr, flags)) {
                // Если инпут активен, строка ввода будет снизу сообщений
                float historyHeight = isInputActive ? (totalWindowHeight - 35.0f) : totalWindowHeight;

                ImGui::BeginChild("##ChatMessagesRegion", ImVec2(sampChatWidth, historyHeight), false, ImGuiWindowFlags_NoScrollbar);

                for (const auto& msg : messages) {
                    float currentAlpha = isInputActive ? 1.0f : msg.alpha;
                    if (currentAlpha < 0.01f) continue;

                    ImGui::BeginGroup();
                    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

                    // Рендерим Таймстамп
                    ImColor timeColor(180, 180, 180, static_cast<unsigned char>(currentAlpha * 255));
                    std::string timeStr = msg.timestamp;
                    ImVec2 timeStrSize = ImGui::CalcTextSize(timeStr.c_str());

                    ImVec2 timePos = ImGui::GetCursorScreenPos();
                    ImGui::GetWindowDrawList()->AddText(ImVec2(timePos.x + 1.0f, timePos.y + 1.0f), ImColor(0, 0, 0, static_cast<unsigned char>(currentAlpha * 255)), timeStr.c_str());
                    ImGui::TextColored(timeColor, "%s", timeStr.c_str());

                    float textStartPosX = timeStrSize.x + 5.0f;
                    ImGui::SameLine(textStartPosX, 0.0f);

                    float currentX = textStartPosX;
                    float wrapPosX = sampChatWidth - 20.0f;

                    for (size_t i = 0; i < msg.segments.size(); ++i) {
                        if (msg.segments[i].text.empty()) continue;

                        if (i > 0) {
                            ImGui::SameLine(0.0f, 0.0f);
                        }

                        ImVec2 textSize = ImGui::CalcTextSize(msg.segments[i].text.c_str());

                        // Если сегмент целиком не влезает в текущую строку, переносим на следующую
                        if (currentX + textSize.x > wrapPosX && i > 0) {
                            ImGui::NewLine();
                            ImGui::SetCursorPosX(textStartPosX);
                            currentX = textStartPosX;
                        }

                        ImVec2 pos = ImGui::GetCursorScreenPos();
                        ImColor textColor = msg.segments[i].color;
                        textColor.Value.w = currentAlpha;

                        // Тень
                        ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + 1.0f, pos.y + 1.0f), ImColor(0, 0, 0, static_cast<unsigned char>(currentAlpha * 255)), msg.segments[i].text.c_str());

                        // Основной текст
                        ImGui::PushStyleColor(ImGuiCol_Text, textColor.Value);
                        ImGui::TextUnformatted(msg.segments[i].text.c_str());
                        ImGui::PopStyleColor();

                        currentX += textSize.x;
                    }

                    ImGui::PopStyleVar();
                    ImGui::EndGroup();
                }

                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10.0f) {
                    ImGui::SetScrollHereY(1.0f);
                }
                ImGui::EndChild();

                if (isInputActive) {
                    ImGui::SetCursorPosY(totalWindowHeight - 30.0f);

                    if (ImGui::IsWindowAppearing() || !ImGui::IsAnyItemActive()) {
                        ImGui::SetKeyboardFocusHere();
                    }

                    ImGui::PushItemWidth(sampChatWidth - 10.0f);
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.05f, 0.05f, 0.05f, 0.6f));
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f));

                    if (ImGui::InputText("##ChatInput", inputBuffer, sizeof(inputBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
                        std::string command(inputBuffer);

                        if (!command.empty()) {
                            auto pInput = sampapi::v037r3::RefInputBox();
                            if (pInput) {
                                // На всякий случай дублируем текст в структуру SA-MP
                                strncpy(pInput->m_szInput, command.c_str(), sizeof(pInput->m_szInput) - 1);
                                Utils::SampChat::SendCommand(command);
                            }
                        }

                        EnableInput(false); // Закрываем чат и разблокируем игрока
                    }

                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor();
                    ImGui::PopItemWidth();
                }
            }
            ImGui::End();
            ImGui::PopStyleVar(2);
        }

        void AddMessage(uint32_t color, const std::string& text) {
            if (text.empty()) return;

            std::string utf8Text = Cp1251ToUtf8(text);

            ChatMessage msg;
            msg.segments = ParseColoredText(utf8Text, color);
            msg.alpha = 0.0f;
            msg.targetAlpha = 1.0f;
            msg.lifetime = Utils::SampChat::MESSAGE_LIFETIME;
            msg.timestamp = GetCurrentTimestamp();

            messages.push_back(std::move(msg));

            if (messages.size() > Utils::SampChat::MAX_MESSAGES) {
                messages.erase(messages.begin());
            }
        }

        void SendCommand(const std::string& command) {
            if (command.empty()) return;

            std::string gameAnsiCommand = Utf8ToCp1251(command);
            auto pInput = sampapi::v037r3::RefInputBox();

            if (pInput) {
                // Просто отправляем данные напрямую в сеть
                pInput->Send(gameAnsiCommand.c_str());
            }
        }

        bool IsInputActive() { return isInputActive; }
        void Toggle() { isVisible = !isVisible; }
        void Clear() { messages.clear(); }

    } // namespace SampChat
} // namespace Utils