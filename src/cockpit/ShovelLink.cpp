#include "ShovelLink.hpp"
#include <ShovelProtocol.hpp>

ShovelLink::ShovelLink() : AttachmentLink(ShovelProtocol::SERVICE_UUID) {}

bool ShovelLink::onConnected(NimBLEClient* client)
{
    NimBLERemoteService* service = client->getService(ShovelProtocol::SERVICE_UUID);
    NimBLERemoteCharacteristic* armChar =
        service ? service->getCharacteristic(ShovelProtocol::ARM_CHAR_UUID) : nullptr;
    if (!armChar)
    {
        return false;
    }
    _armChar = armChar;
    // 今のアームの位置を読んでおき、つないだ瞬間にアームが動かないようにする
    NimBLEAttValue value = armChar->readValue();
    _lastSentArm = value.length() < 1 ? -1 : value.data()[0];
    return true;
}

void ShovelLink::setArm(uint8_t arm)
{
    if (!isConnected() || arm == _lastSentArm)
    {
        return;
    }
    if (_armChar->writeValue(&arm, 1, false))
    {
        _lastSentArm = arm;
    }
}
