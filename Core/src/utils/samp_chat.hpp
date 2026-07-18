#pragma once
#include <string>
#include <vector>
#include <RakNet/BitStream.h>
#include <imgui.h>

namespace Utils {
    namespace SampChat {

        // Максимальное количество сообщений в истории
        constexpr int MAX_MESSAGES = 100;

        // Время жизни сообщения (секунды)
        constexpr float MESSAGE_LIFETIME = 15.0f;

        // Скорость анимации появления
        constexpr float FADE_SPEED = 5.0f;

        // Структура сегмента текста с цветом
        struct TextSegment {
            std::string text;
            ImColor color;
        };

        // Структура сообщения чата
        struct ChatMessage {
            std::vector<TextSegment> segments; // Разбитое на цветные сегменты
            float alpha;                       // Текущая прозрачность (для анимации)
            float targetAlpha;                 // Целевая прозрачность
            float lifetime;                    // Оставшееся время жизни
            bool isSystem;                     // Системное сообщение (без ника)
        };

        // Инициализация чата
        void Initialize();

        // Обновление каждый кадр (анимации, время жизни)
        void Update(float deltaTime);

        // Отрисовка чата через ImGui
        void Render();

        // Добавление сообщения (вызывается из хука RPC)
        void AddMessage(uint32_t color, const std::string& text);

        // Отправка команды на сервер
        void SendCommand(const std::string& command);

        // Проверка, открыт ли ввод текста
        bool IsInputActive();

        // Скрыть/показать чат
        void Toggle();

        // Очистка истории
        void Clear();

    } // namespace SampChat
} // namespace Utils