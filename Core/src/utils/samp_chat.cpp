#define IMGUI_DEFINE_MATH_OPERATORS // <-- ВАЖНО: Должно быть в самом верху файла!

#include "utils/samp_chat.hpp"
#include <windows.h>
#include <imgui.h>
#include <algorithm>
#include <sstream>
#include <cctype>

namespace Utils {
    namespace SampChat {

        static std::vector<ChatMessage> messages;
        static bool isVisible = true;
        static bool isInputActive = false;
        static char inputBuffer[256] = { 0 };
        static ImVec2 windowPos(20.0f, 20.0f);
        static ImVec2 windowSize(400.0f, 300.0f);

        static std::vector<TextSegment> ParseColoredText(const std::string& text, uint32_t defaultColor) {
            std::vector<TextSegment> segments;
            std::string currentText;
            ImColor currentColor(defaultColor);

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
                        unsigned int r = 0, g = 0, b = 0;
                        sscanf_s(hexCode.c_str(), "%02x%02x%02x", &r, &g, &b);
                        currentColor = ImColor(r, g, b, 255);
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
        }

        void Update(float deltaTime) {
            if (deltaTime > 0.1f) deltaTime = 0.1f;
            if (deltaTime < 0.0f) deltaTime = 0.0f;

            for (auto it = messages.begin(); it != messages.end(); ) {
                it->lifetime -= deltaTime;
                if (it->lifetime <= 0.0f) {
                    it->targetAlpha = 0.0f;
                }

                float t = deltaTime * 5.0f; // FADE_SPEED = 5.0f
                if (t > 1.0f) t = 1.0f;

                // Математический lerp (работает всегда, даже без макросов ImGui)
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

            ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

            ImGui::SetNextWindowPos(windowPos, ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(windowSize, ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowBgAlpha(0.0f);

            if (ImGui::Begin("##SampChat", nullptr, flags)) {
                if (isInputActive) {
                    ImGui::SetScrollY(ImGui::GetScrollMaxY());
                }

                for (const auto& msg : messages) {
                    if (msg.alpha < 0.01f) continue;
                    for (const auto& segment : msg.segments) {
                        ImColor color = segment.color;
                        color.Value.w = msg.alpha;
                        ImGui::TextColored(color, "%s", segment.text.c_str());
                        ImGui::SameLine(0.0f, 0.0f);
                    }
                    ImGui::NewLine();
                }

                if (isInputActive) {
                    ImGui::SetKeyboardFocusHere();
                    if (ImGui::InputText("##ChatInput", inputBuffer, sizeof(inputBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
                        std::string command(inputBuffer);
                        if (!command.empty()) {
                            OutputDebugStringA(("[SampChat] Отправка: " + command + "\n").c_str());
                            // TODO: Здесь будет реальная отправка через RakNet
                        }
                        memset(inputBuffer, 0, sizeof(inputBuffer));
                        isInputActive = false;
                    }
                }
            }
            ImGui::End();
        }

        void AddMessage(uint32_t color, const std::string& text) {
            if (text.empty()) return;
            ChatMessage msg;
            msg.segments = ParseColoredText(text, color);
            msg.alpha = 0.0f;
            msg.targetAlpha = 1.0f;
            msg.lifetime = 15.0f; // MESSAGE_LIFETIME
            messages.push_back(std::move(msg));
            if (messages.size() > 100) { // MAX_MESSAGES
                messages.erase(messages.begin());
            }
        }

        bool IsInputActive() { return isInputActive; }
        void Toggle() { isVisible = !isVisible; }
        void Clear() { messages.clear(); }

    } // namespace SampChat
} // namespace Utils