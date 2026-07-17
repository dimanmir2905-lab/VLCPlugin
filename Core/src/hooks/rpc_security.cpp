#include "hooks/rpc_security.hpp"
#include "utils/samp_reader.hpp"
#include "utils/chat.hpp"
#include <cstdint>

// ==========================================
// Минимальные определения структур RakNet
// ==========================================
namespace RakNet {
    struct SystemAddress {
        unsigned short port;
        unsigned int binaryAddress;
    };

    struct Packet {
        uint32_t length;
        uint32_t bitSize;
        uint8_t* data;
        bool deleteData;
        SystemAddress systemAddress;
        SystemAddress systemIndex;
    };
}

// Оригинальная функция Receive — это __thiscall метод класса
typedef RakNet::Packet* (__thiscall* Receive_t)(void* pRakClient, uint32_t timeout);
static Receive_t oReceive = nullptr;

namespace {
    constexpr size_t MAX_MENU_ITEMS = 12;
    constexpr size_t MAX_MENU_LINE = 32;
    constexpr size_t MAX_MENUS = 128;

    bool IsListingStyle(uint8_t style) {
        return style == 2 || style == 4 || style == 5;
    }
}

// ==========================================
// Хук на Receive (__fastcall для MinHook)
// ==========================================
RakNet::Packet* __fastcall hReceive(void* pRakClient, void* edx, uint32_t timeout) {
    // Вызываем оригинальную функцию (__thiscall: this в ecx, timeout в стеке)
    RakNet::Packet* packet = oReceive(pRakClient, timeout);

    if (packet && packet->length > 1 && packet->data && packet->data[0] == 199) {
        Utils::SampReader reader(packet->data + 1, packet->length - 1);
        uint8_t rpcId;

        if (reader.Read(rpcId)) {
            bool dropPacket = false;

            // 1. ApplyAnimation (RPC ID: 86)
            if (rpcId == 86) {
                reader.Ignore(2);
                uint8_t libLen, nameLen;
                if (!reader.Read(libLen) || libLen > 15) dropPacket = true;
                else reader.Ignore(libLen);

                if (!dropPacket && (!reader.Read(nameLen) || nameLen > 24)) dropPacket = true;
            }
            // 2. ApplyActorAnimation (RPC ID: 173)
            else if (rpcId == 173) {
                reader.Ignore(2);
                uint8_t libLen, nameLen;
                if (!reader.Read(libLen) || libLen > 15) dropPacket = true;
                else reader.Ignore(libLen);

                if (!dropPacket && (!reader.Read(nameLen) || nameLen > 24)) dropPacket = true;
            }
            // 3. ShowDialog (RPC ID: 61)
            else if (rpcId == 61) {
                uint16_t dialogId;
                uint8_t style;
                if (!reader.Read(dialogId) || !reader.Read(style)) {
                    dropPacket = true;
                }
                else if (IsListingStyle(style)) {
                    uint8_t titleLen, btn1Len, btn2Len;
                    if (!reader.Read(titleLen)) dropPacket = true;
                    else reader.Ignore(titleLen);

                    if (!dropPacket && !reader.Read(btn1Len)) dropPacket = true;
                    else reader.Ignore(btn1Len);

                    if (!dropPacket && !reader.Read(btn2Len)) dropPacket = true;
                    else reader.Ignore(btn2Len);

                    if (!dropPacket && reader.Remaining() > 2000) {
                        dropPacket = true;
                    }
                }
            }
            // 4. InitMenu (RPC ID: 76)
            else if (rpcId == 76) {
                uint8_t menuId;
                if (!reader.Read(menuId) || menuId >= MAX_MENUS) {
                    dropPacket = true;
                }
                else {
                    uint8_t twoColumns;
                    if (!reader.Read(twoColumns)) {
                        dropPacket = true;
                    }
                    else {
                        reader.Ignore(32);
                        reader.Ignore(twoColumns ? 16 : 12);

                        uint8_t rowCount;
                        if (!reader.Read(rowCount) || rowCount > MAX_MENU_ITEMS) {
                            dropPacket = true;
                        }
                        else {
                            for (int i = 0; i < rowCount; i++) {
                                reader.Ignore(MAX_MENU_LINE);
                            }

                            if (!dropPacket && twoColumns) {
                                reader.Ignore(MAX_MENU_LINE);
                                uint8_t rowCount2;
                                if (!reader.Read(rowCount2) || rowCount2 > MAX_MENU_ITEMS) {
                                    dropPacket = true;
                                }
                                else {
                                    for (int i = 0; i < rowCount2; i++) {
                                        reader.Ignore(MAX_MENU_LINE);
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if (dropPacket) {
                packet->data[1] = 255;
            }
        }
    }

    return packet;
}

// ==========================================
// Инициализация и деинициализация
// ==========================================
namespace Hooks::RPCSecurity {

    void Initialize() {
        uintptr_t sampBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("samp.dll"));
        if (!sampBase) return;

        void** pRakClientPtr = reinterpret_cast<void**>(sampBase + 0x26E8CC);
        if (!pRakClientPtr || !*pRakClientPtr) return;

        void** vtable = *reinterpret_cast<void***>(*pRakClientPtr);

        // Приводим vtable[8] к void* для MinHook
        MH_CreateHook(
            reinterpret_cast<void*>(vtable[8]),
            reinterpret_cast<void*>(hReceive),
            reinterpret_cast<void**>(&oReceive)
        );
        MH_EnableHook(reinterpret_cast<void*>(vtable[8]));
    }

    void Shutdown() {
        MH_DisableHook(MH_ALL_HOOKS);
    }
}