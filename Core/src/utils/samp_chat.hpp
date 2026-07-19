#pragma once
#include <string>
#include <vector>
#include <RakNet/BitStream.h>
#include <imgui.h>

namespace Utils {
    namespace SampChat {

        // Структура для сегмента текста с цветом
        struct TextSegment {
            std::string text;
            ImColor color;
        };

        // Структура сообщения чата
        struct ChatMessage {
            std::vector<TextSegment> segments;
            float alpha;
            float targetAlpha;
            float lifetime;
            std::string timestamp; // <-- Добавлено для исправления ошибки "не содержит члена timestamp"
        };

        // Константы определены ТОЛЬКО здесь, чтобы избежать ошибок переопределения (redefinition)
        constexpr int MAX_MESSAGES = 100;
        constexpr float MESSAGE_LIFETIME = 15.0f;
        constexpr float FADE_SPEED = 5.0f;

        // Позиция и размер чата (настройте под себя)
        constexpr float CHAT_X = 20.0f;
        constexpr float CHAT_Y = 300.0f;
        constexpr float CHAT_WIDTH = 400.0f;

        void Initialize();
        void Update(float deltaTime);
        void Render();
        void AddMessage(uint32_t color, const std::string& text);
        void SendCommand(const std::string& command);
        bool IsInputActive();
        void EnableInput(bool enable);
        void Toggle();
        void Clear();

    } // namespace SampChat
} // namespace Utils