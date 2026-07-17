#include "RakHook/rakhook.hpp"
#include "RakHook/detail.hpp"
#include "RakHook/offsets.hpp"
#include "RakHook/hooked_rakclient_interface.hpp"

#include "RakNet/PacketEnumerations.h"

// Используем MinHook вместо polyhook2
#include <MinHook.h>

#ifndef MAX_ALLOCA_STACK_ALLOCATION
#define MAX_ALLOCA_STACK_ALLOCATION 1048576
#endif

hooked_rakclient_interface* hooked_interface = nullptr;
void* rakpeer = nullptr;
PlayerID                    gplayerid;

using destroy_ri_t = void(__cdecl*)(void*);
using handle_rpc_packet_t = bool(__thiscall*)(void*, const char*, int, PlayerID);

// Трамплины для MinHook
void* orig_destroy_ri = nullptr;
void* orig_handle_rpc = nullptr;

// ==========================================
// Коллбэки для MinHook
// ==========================================

void __cdecl my_destroy_ri(void* rakclient_interface) {
    if (rakclient_interface == hooked_interface) {
        rakclient_interface = rakhook::orig;
        delete hooked_interface;
        hooked_interface = nullptr;
    }
    auto orig_func = reinterpret_cast<destroy_ri_t>(orig_destroy_ri);
    return orig_func(rakclient_interface);
}

// ВАЖНО: __fastcall нужен для корректного перехвата __thiscall в x86
bool __fastcall my_handle_rpc(void* rp, void* /*edx*/, const char* data, int length, PlayerID playerid) {
    rakpeer = rp;
    gplayerid = playerid;

    RakNet::BitStream                  incoming{ reinterpret_cast<unsigned char*>(const_cast<char*>(data)), static_cast<unsigned int>(length), true };
    unsigned char                      id = 0;
    unsigned char* input = nullptr;
    unsigned int                       bits_data = 0;
    std::shared_ptr<RakNet::BitStream> callback_bs{ std::make_shared<RakNet::BitStream>() };

    incoming.IgnoreBits(8);
    if (data[0] == ID_TIMESTAMP)
        incoming.IgnoreBits(8 * (sizeof(RakNetTime) + sizeof(unsigned char)));

    int offset = incoming.GetReadOffset();
    incoming.Read(id);

    if (!incoming.ReadCompressed(bits_data))
        return false;

    if (bits_data) {
        bool used_alloca = false;
        if (BITS_TO_BYTES(incoming.GetNumberOfUnreadBits()) < MAX_ALLOCA_STACK_ALLOCATION) {
            input = reinterpret_cast<unsigned char*>(alloca(BITS_TO_BYTES(incoming.GetNumberOfUnreadBits())));
            used_alloca = true;
        }
        else
            input = new unsigned char[BITS_TO_BYTES(incoming.GetNumberOfUnreadBits())];

        if (!incoming.ReadBits(input, bits_data, false)) {
            if (!used_alloca)
                delete[] input;
            return false;
        }

        callback_bs = std::make_shared<RakNet::BitStream>(input, BITS_TO_BYTES(bits_data), true);
        if (!used_alloca)
            delete[] input;
    }

    for (auto it = rakhook::on_receive_rpc.begin(); it != rakhook::on_receive_rpc.end();) {
        if (auto f = *it) {
            // Вызываем коллбэк. id передается по ссылке, bs - по указателю
            if (!f(id, callback_bs.get()))
                return false;
            it++;
        }
        else {
            it = rakhook::on_receive_rpc.erase(it);
        }
    }

    incoming.SetWriteOffset(offset);
    incoming.Write(id);
    bits_data = BYTES_TO_BITS(callback_bs->GetNumberOfBytesUsed());
    incoming.WriteCompressed(bits_data);
    if (bits_data)
        incoming.WriteBits(callback_bs->GetData(), bits_data, false);

    auto orig_func = reinterpret_cast<handle_rpc_packet_t>(orig_handle_rpc);
    return orig_func(rp, reinterpret_cast<char*>(incoming.GetData()), incoming.GetNumberOfBytesUsed(), playerid);
}

// ==========================================
// Основные функции rakhook
// ==========================================

bool rakhook::initialize() {
    if (initialized)
        return true;

    if (!samp_addr())
        return false;
    const uintptr_t samp_info = *reinterpret_cast<uintptr_t*>(offsets::samp_info(true));
    if (!samp_info)
        return false;

    auto** rakclient_interface = reinterpret_cast<RakClientInterface**>(samp_info + offsets::rakclient_interface());
    if (!*rakclient_interface)
        return false;

    orig = *rakclient_interface;
    hooked_interface = new hooked_rakclient_interface(orig);
    *rakclient_interface = reinterpret_cast<RakClientInterface*>(hooked_interface);

    // Инициализация MinHook
    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        return false;
    }

    auto func_destroy = reinterpret_cast<destroy_ri_t>(offsets::destroy_interface(true));
    if (MH_CreateHook(reinterpret_cast<LPVOID>(func_destroy), reinterpret_cast<LPVOID>(&my_destroy_ri), &orig_destroy_ri) != MH_OK) return false;
    if (MH_EnableHook(reinterpret_cast<LPVOID>(func_destroy)) != MH_OK) return false;

    auto func_rpc = reinterpret_cast<handle_rpc_packet_t>(offsets::handle_rpc_packet(true));
    if (MH_CreateHook(reinterpret_cast<LPVOID>(func_rpc), reinterpret_cast<LPVOID>(&my_handle_rpc), &orig_handle_rpc) != MH_OK) return false;
    if (MH_EnableHook(reinterpret_cast<LPVOID>(func_rpc)) != MH_OK) return false;

    initialized = true;
    return true;
}

void rakhook::destroy() {
    if (!initialized)
        return;

    const uintptr_t samp_info = *reinterpret_cast<uintptr_t*>(samp_addr(offsets::samp_info()));
    if (!samp_info)
        return;

    auto** rakclient_interface = reinterpret_cast<RakClientInterface**>(samp_info + offsets::rakclient_interface());
    *rakclient_interface = orig;

    auto func_destroy = reinterpret_cast<destroy_ri_t>(offsets::destroy_interface(true));
    MH_DisableHook(reinterpret_cast<LPVOID>(func_destroy));
    MH_RemoveHook(reinterpret_cast<LPVOID>(func_destroy));

    auto func_rpc = reinterpret_cast<handle_rpc_packet_t>(offsets::handle_rpc_packet(true));
    MH_DisableHook(reinterpret_cast<LPVOID>(func_rpc));
    MH_RemoveHook(reinterpret_cast<LPVOID>(func_rpc));

    delete hooked_interface;
    hooked_interface = nullptr;
    initialized = false;
}

bool rakhook::send(RakNet::BitStream* bs, PacketPriority priority, PacketReliability reliability, char ord_channel) {
    if (!initialized) return false;
    return orig->Send(bs, priority, reliability, ord_channel);
}

bool rakhook::send_rpc(int id, RakNet::BitStream* bs, PacketPriority priority, PacketReliability reliability, char ord_channel, bool sh_timestamp) {
    if (!initialized) return false;
    return orig->RPC(&id, bs, priority, reliability, ord_channel, sh_timestamp);
}

bool rakhook::emul_rpc(unsigned char id, RakNet::BitStream& rpc_bs) {
    if (!initialized || !rakpeer) return false;

    RakNet::BitStream bs;
    bs.Write<unsigned char>(ID_RPC);
    bs.Write(id);
    bs.WriteCompressed<unsigned int>(BYTES_TO_BITS(rpc_bs.GetNumberOfBytesUsed()));
    bs.WriteBits(rpc_bs.GetData(), BYTES_TO_BITS(rpc_bs.GetNumberOfBytesUsed()), false);

    handle_rpc_packet_t handle_rpc = orig_handle_rpc ? reinterpret_cast<handle_rpc_packet_t>(orig_handle_rpc)
        : reinterpret_cast<handle_rpc_packet_t>(offsets::handle_rpc_packet(true));
    return handle_rpc(rakpeer, reinterpret_cast<char*>(bs.GetData()), bs.GetNumberOfBytesUsed(), gplayerid);
}

bool rakhook::emul_packet(RakNet::BitStream& pbs) {
    if (!initialized || !rakpeer) return false;
    Packet* send_packet = reinterpret_cast<Packet * (*)(size_t)>(samp_addr(offsets::alloc_packet()))(pbs.GetNumberOfBytesUsed());
    memcpy(send_packet->data, pbs.GetData(), send_packet->length);

    char* packets = static_cast<char*>(rakpeer) + offsets::offset_packets();
    auto  write_lock = reinterpret_cast<Packet * *(__thiscall*)(void*)>(samp_addr(offsets::write_lock()));
    auto  write_unlock = reinterpret_cast<void(__thiscall*)(void*)>(samp_addr(offsets::write_unlock()));

    *write_lock(packets) = send_packet;
    write_unlock(packets);

    return true;
}