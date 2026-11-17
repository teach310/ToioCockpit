#include "AttachmentLink.hpp"

namespace
{
    constexpr uint32_t SCAN_DURATION_SEC = 2;
    constexpr uint8_t SEARCH_ATTEMPTS = 3;      // startSearch() 1回につき探す回数。見つからなければやめる
    constexpr uint32_t SCAN_INTERVAL_MS = 5000; // 見つからなかったとき、次に探すまでの間隔
    constexpr uint8_t CONNECT_TIMEOUT_SEC = 3;   // 接続を待つ時間。この間は操作が止まるので短くする

    // スキャンの完了は NimBLE のタスクから知らされるので、どのアタッチメントのものかを覚えておく
    AttachmentLink* scanningLink = nullptr;
}

void AttachmentLink::ScanCallbacks::onResult(NimBLEAdvertisedDevice* device)
{
    if (!device->isAdvertisingService(NimBLEUUID(_link._serviceUuid)))
    {
        return;
    }
    _link._address = device->getAddress();
    _link._state = State::FOUND;
    NimBLEDevice::getScan()->stop();
}

void AttachmentLink::onScanComplete(NimBLEScanResults results)
{
    if (scanningLink && scanningLink->_state == State::SCANNING)
    {
        scanningLink->_state = State::IDLE;
    }
}

void AttachmentLink::startSearch()
{
    if (isConnected())
    {
        return;
    }
    _remainingScans = SEARCH_ATTEMPTS;
    _lastScanMs = 0; // 次に update() を呼んだらすぐ探す
}

bool AttachmentLink::isSearching() const
{
    return _state == State::SCANNING || _state == State::FOUND || (_state == State::IDLE && _remainingScans > 0);
}

void AttachmentLink::update(bool canConnect)
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
        NimBLEScan* scan = NimBLEDevice::getScan();
        if (scan->isScanning())
        {
            // ほかのアタッチメントが探している。回数は減らさずに、終わってから探す
            return;
        }
        _lastScanMs = now;
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
            onDisconnected();
            _state = State::IDLE;
            startSearch();
        }
        break;
    }
}

void AttachmentLink::connect()
{
    // toio やほかのアタッチメントのスキャンに影響しないよう、使い終わったコールバックは外しておく
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
    if (!onConnected(_client))
    {
        onDisconnected();
        _client->disconnect();
        return;
    }
    _state = State::CONNECTED;
}

void AttachmentLink::stop()
{
    NimBLEScan* scan = NimBLEDevice::getScan();
    if (_state == State::SCANNING && scan->isScanning())
    {
        scan->stop();
    }
    if (scanningLink == this)
    {
        scan->setAdvertisedDeviceCallbacks(nullptr);
    }
    if (_client && _client->isConnected())
    {
        _client->disconnect();
    }
    onDisconnected();
    _state = State::IDLE;
    _remainingScans = 0;
}

bool AttachmentLink::isConnected()
{
    return _state == State::CONNECTED && _client && _client->isConnected();
}
