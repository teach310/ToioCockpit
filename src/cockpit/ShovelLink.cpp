#include "ShovelLink.hpp"
#include <ShovelProtocol.hpp>

namespace
{
    constexpr uint32_t SCAN_DURATION_SEC = 2;
    constexpr uint8_t SEARCH_ATTEMPTS = 3;      // startSearch() 1回につき探す回数。見つからなければやめる
    constexpr uint32_t SCAN_INTERVAL_MS = 5000; // 見つからなかったとき、次に探すまでの間隔
    constexpr uint8_t CONNECT_TIMEOUT_SEC = 3;   // 接続を待つ時間。この間は操作が止まるので短くする

    // スキャンの完了は NimBLE のタスクから知らされるので、どの ShovelLink のものかを覚えておく
    ShovelLink* scanningLink = nullptr;
}

void ShovelLink::ScanCallbacks::onResult(NimBLEAdvertisedDevice* device)
{
    if (!device->isAdvertisingService(NimBLEUUID(ShovelProtocol::SERVICE_UUID)))
    {
        return;
    }
    _link._address = device->getAddress();
    _link._state = State::FOUND;
    NimBLEDevice::getScan()->stop();
}

void ShovelLink::onScanComplete(NimBLEScanResults results)
{
    if (scanningLink && scanningLink->_state == State::SCANNING)
    {
        scanningLink->_state = State::IDLE;
    }
}

void ShovelLink::startSearch()
{
    if (isConnected())
    {
        return;
    }
    _remainingScans = SEARCH_ATTEMPTS;
    _lastScanMs = 0; // 次に update() を呼んだらすぐ探す
}

bool ShovelLink::isSearching() const
{
    return _state == State::SCANNING || _state == State::FOUND || (_state == State::IDLE && _remainingScans > 0);
}

void ShovelLink::update(bool canConnect)
{
    switch (_state)
    {
    case State::IDLE:
    {
        if (_remainingScans == 0)
        {
            return;
        }
        uint32_t now = millis();
        if (_lastScanMs != 0 && now - _lastScanMs < SCAN_INTERVAL_MS)
        {
            return;
        }
        _lastScanMs = now;
        NimBLEScan* scan = NimBLEDevice::getScan();
        if (scan->isScanning())
        {
            return;
        }
        scan->setAdvertisedDeviceCallbacks(&_scanCallbacks);
        scan->setActiveScan(true);
        scanningLink = this;
        _state = State::SCANNING;
        _remainingScans--;
        // 終わるのを待たずに戻る。見つかったら onResult で FOUND になる
        if (!scan->start(SCAN_DURATION_SEC, onScanComplete, false))
        {
            _state = State::IDLE;
        }
        break;
    }
    case State::SCANNING:
        break;
    case State::FOUND:
        if (canConnect)
        {
            connect();
        }
        break;
    case State::CONNECTED:
        if (!_client->isConnected())
        {
            // 途中で切れたら、決まった回数だけつなぎ直しを試す
            _armChar = nullptr;
            _state = State::IDLE;
            startSearch();
        }
        break;
    }
}

void ShovelLink::connect()
{
    // toio のスキャンに影響しないよう、使い終わったコールバックは外しておく
    NimBLEDevice::getScan()->setAdvertisedDeviceCallbacks(nullptr);

    if (!_client)
    {
        _client = NimBLEDevice::createClient();
        _client->setConnectTimeout(CONNECT_TIMEOUT_SEC);
    }
    _state = State::IDLE;
    if (!_client->connect(_address))
    {
        return;
    }
    NimBLERemoteService* service = _client->getService(ShovelProtocol::SERVICE_UUID);
    NimBLERemoteCharacteristic* armChar =
        service ? service->getCharacteristic(ShovelProtocol::ARM_CHAR_UUID) : nullptr;
    if (!armChar)
    {
        _client->disconnect();
        return;
    }
    _armChar = armChar;
    // 今のアームの位置を読んでおき、つないだ瞬間にアームが動かないようにする
    NimBLEAttValue value = armChar->readValue();
    _lastSentArm = value.length() < 1 ? -1 : value.data()[0];
    _state = State::CONNECTED;
}

void ShovelLink::stop()
{
    NimBLEScan* scan = NimBLEDevice::getScan();
    if (_state == State::SCANNING && scan->isScanning())
    {
        scan->stop();
    }
    scan->setAdvertisedDeviceCallbacks(nullptr);
    if (_client && _client->isConnected())
    {
        _client->disconnect();
    }
    _armChar = nullptr;
    _state = State::IDLE;
    _remainingScans = 0;
}

bool ShovelLink::isConnected()
{
    return _state == State::CONNECTED && _client && _client->isConnected();
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
