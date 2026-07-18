#pragma once
#include <cstdint>
#include <cstring>

namespace Utils {
    // Простой и безопасный читатель байтов из пакета SA-MP
    class SampReader {
    private:
        const uint8_t* data;
        size_t length;
        size_t offset;

    public:
        SampReader(const uint8_t* pData, size_t pLength)
            : data(pData), length(pLength), offset(0) {
        }

        bool CanRead(size_t bytes) const {
            return (offset + bytes) <= length;
        }

        bool Read(uint8_t& val) {
            if (!CanRead(1)) return false;
            val = data[offset++];
            return true;
        }

        bool Read(uint16_t& val) {
            if (!CanRead(2)) return false;
            val = *reinterpret_cast<const uint16_t*>(data + offset);
            offset += 2;
            return true;
        }

        bool Read(float& val) {
            if (!CanRead(4)) return false;
            val = *reinterpret_cast<const float*>(data + offset);
            offset += 4;
            return true;
        }

        // Пропустить N байт
        void Ignore(size_t bytes) {
            if (CanRead(bytes)) {
                offset += bytes;
            }
            else {
                offset = length; // Защита от выхода за границы
            }
        }

        // Получить оставшуюся длину пакета
        size_t Remaining() const {
            return length - offset;
        }
    };
}