#ifndef PEDAL_LINK_HPP
#define PEDAL_LINK_HPP

#include "AttachmentLink.hpp"
#include <atomic>

// pedal との BLE 接続。踏んだ強さを通知で受け取る
class PedalLink : public AttachmentLink
{
public:
    PedalLink();

    /// 踏んだ強さ (0～100)。つながっていないときや、しばらく通知が届いていないときは 0
    uint8_t pressure();

protected:
    bool onConnected(NimBLEClient* client) override;
    void onDisconnected() override { _pressureChar = nullptr; }

private:
    void setPressure(const uint8_t* data, size_t length);

    NimBLERemoteCharacteristic* _pressureChar = nullptr;
    // 通知は NimBLE のタスクから届くので、制御のループと分けて読み書きできるようにする
    std::atomic<uint8_t> _pressure{0};
    std::atomic<uint32_t> _lastReceivedMs{0};
};

#endif
