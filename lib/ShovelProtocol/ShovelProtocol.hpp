#ifndef SHOVEL_PROTOCOL_HPP
#define SHOVEL_PROTOCOL_HPP

#include <stdint.h>

// cockpit (CoreS3) と shovel (AtomS3) の BLE 通信の決まりごと
// 変えるときは送る側 (cockpit) と受ける側 (shovel) を同じ PR で直す
namespace ShovelProtocol
{
    // shovel がアドバタイズする名前。cockpit はこの名前かサービスUUIDで見つける
    constexpr const char* DEVICE_NAME = "ToioShovel";

    constexpr const char* SERVICE_UUID = "f26b2ba2-a7eb-4af0-9c1a-8d885357122a";

    // アームの位置。1バイト (0 = 一番下, 100 = 一番上)
    // cockpit が書き込み(応答なし)で送り、shovel は今の目標値を読み出しでも返す
    constexpr const char* ARM_CHAR_UUID = "b243eeba-79f5-4ddb-b3e1-88e8dee70fae";

    constexpr uint8_t ARM_MIN = 0;
    constexpr uint8_t ARM_MAX = 100;
}

#endif
