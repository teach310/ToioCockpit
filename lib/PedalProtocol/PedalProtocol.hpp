#ifndef PEDAL_PROTOCOL_HPP
#define PEDAL_PROTOCOL_HPP

#include <stdint.h>

// cockpit と pedal の BLE 通信の決まりごと
// 変えるときは送る側 (pedal) と受ける側 (cockpit) を同じ PR で直す
namespace PedalProtocol
{
    // pedal がアドバタイズする名前。cockpit はサービスUUIDで見つける
    constexpr const char* DEVICE_NAME = "ToioPedal";

    constexpr const char* SERVICE_UUID = "826c2fc1-c825-4213-9c2e-61ae43b87bd2";

    // ペダルを踏んだ強さ。1バイト (0 = 踏んでいない, 100 = 一番強く踏んだ)
    // pedal が通知 (notify) で送る。読み出しでも今の値を返す
    constexpr const char* PRESSURE_CHAR_UUID = "5ebd084d-6808-4282-aef7-707e2020a9eb";

    constexpr uint8_t PRESSURE_MAX = 100;

    // pedal は値が変わったときに送り、変わらなくても KEEPALIVE_MS ごとに送る
    // cockpit は STALE_MS のあいだ何も届かなければ、踏んでいない (0) とみなす
    // 電波が途切れて最後の値のまま走り続けないようにするため
    constexpr uint32_t KEEPALIVE_MS = 200;
    constexpr uint32_t STALE_MS = 1000;
}

#endif
