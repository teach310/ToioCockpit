#include "PedalLink.hpp"
#include <PedalProtocol.hpp>

PedalLink::PedalLink() : AttachmentLink(PedalProtocol::SERVICE_UUID) {}

bool PedalLink::onConnected(NimBLEClient* client)
{
    NimBLERemoteService* service = client->getService(PedalProtocol::SERVICE_UUID);
    NimBLERemoteCharacteristic* pressureChar =
        service ? service->getCharacteristic(PedalProtocol::PRESSURE_CHAR_UUID) : nullptr;
    if (!pressureChar || !pressureChar->canNotify())
    {
        return false;
    }
    bool subscribed = pressureChar->subscribe(
        true, [this](NimBLERemoteCharacteristic*, uint8_t* data, size_t length, bool) { setPressure(data, length); });
    if (!subscribed)
    {
        return false;
    }
    _pressureChar = pressureChar;
    // 最初の通知が届くまでの値も、今の値を読んで埋めておく
    NimBLEAttValue value = pressureChar->readValue();
    setPressure(value.data(), value.length());
    return true;
}

void PedalLink::setPressure(const uint8_t* data, size_t length)
{
    if (length < 1)
    {
        return;
    }
    _pressure = min(data[0], PedalProtocol::PRESSURE_MAX);
    _lastReceivedMs = millis();
}

uint8_t PedalLink::pressure()
{
    if (!isConnected() || millis() - _lastReceivedMs > PedalProtocol::STALE_MS)
    {
        return 0;
    }
    return _pressure;
}
