#ifndef SHOVEL_LINK_HPP
#define SHOVEL_LINK_HPP

#include <Arduino.h>
#include <NimBLEDevice.h>

// shovel (AtomS3) との BLE 接続を受け持つ
// 見つからなくても toio の操作は続けられるよう、探すのも接続するのも update() の中で少しずつ進める
// shovel を使わない日も多いので、探し続けずに startSearch() のたびに決まった回数だけ探す
class ShovelLink
{
public:
    /// 決まった回数だけ探し始める。toio とつないだときや、画面でタッチされたときに呼ぶ
    void startSearch();
    /// 呼ぶたびに、探している途中なら探し、見つかっていればつなぐ
    /// @param canConnect 接続は最大数秒止まるので、toio が止まっているときだけ true にする
    void update(bool canConnect);
    /// 探すのをやめて切断する。toio のスキャン前やスリープ前に呼ぶ
    void stop();

    bool isConnected();
    /// 探している途中か、見つけてつなぐのを待っているか
    bool isSearching() const;

    /// アームの位置 (0～100) を送る。前回と同じ値なら送らない
    void setArm(uint8_t arm);
    /// 最後に送った、または接続したときに shovel から読んだアームの位置。不明なら -1
    int arm() const { return _lastSentArm; }

private:
    enum class State { IDLE, SCANNING, FOUND, CONNECTED };

    class ScanCallbacks : public NimBLEAdvertisedDeviceCallbacks
    {
    public:
        explicit ScanCallbacks(ShovelLink& link) : _link(link) {}
        void onResult(NimBLEAdvertisedDevice* device) override;

    private:
        ShovelLink& _link;
    };

    static void onScanComplete(NimBLEScanResults results);
    void connect();

    State _state = State::IDLE;
    ScanCallbacks _scanCallbacks{*this};
    NimBLEAddress _address;
    NimBLEClient* _client = nullptr;
    NimBLERemoteCharacteristic* _armChar = nullptr;
    uint32_t _lastScanMs = 0;
    uint8_t _remainingScans = 0;
    int _lastSentArm = -1;
};

#endif
