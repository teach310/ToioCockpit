#ifndef SHOVEL_LINK_HPP
#define SHOVEL_LINK_HPP

#include "AttachmentLink.hpp"

// shovel との BLE 接続。アームの位置を送る
class ShovelLink : public AttachmentLink
{
public:
    ShovelLink();

    /// アームの位置 (0～100) を送る。前回と同じ値なら送らない
    void setArm(uint8_t arm);
    /// 最後に送った、または接続したときに shovel から読んだアームの位置。不明なら -1
    int arm() const { return _lastSentArm; }

protected:
    bool onConnected(NimBLEClient* client) override;
    void onDisconnected() override { _armChar = nullptr; }

private:
    NimBLERemoteCharacteristic* _armChar = nullptr;
    int _lastSentArm = -1;
};

#endif
