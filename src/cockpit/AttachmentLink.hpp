#ifndef ATTACHMENT_LINK_HPP
#define ATTACHMENT_LINK_HPP

#include <Arduino.h>
#include <NimBLEDevice.h>

// アタッチメント (shovel や pedal) との BLE 接続を受け持つ共通の部分
// 見つからなくても toio の操作は続けられるよう、探すのも接続するのも update() の中で少しずつ進める
// 使わない日も多いので、探し続けずに startSearch() のたびに決まった回数だけ探す
// スキャンは同時に1つしか動かせないので、ほかのアタッチメントが探している間は終わるのを待つ
class AttachmentLink
{
public:
    explicit AttachmentLink(const char* serviceUuid) : _serviceUuid(serviceUuid) {}
    virtual ~AttachmentLink() = default;

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

protected:
    /// つないだ直後に呼ぶ。characteristic を探す。false を返すと切断する
    virtual bool onConnected(NimBLEClient* client) = 0;
    /// 切れたときや stop() のときに呼ぶ。characteristic を忘れる
    virtual void onDisconnected() = 0;

private:
    enum class State { IDLE, SCANNING, FOUND, CONNECTED };

    class ScanCallbacks : public NimBLEAdvertisedDeviceCallbacks
    {
    public:
        explicit ScanCallbacks(AttachmentLink& link) : _link(link) {}
        void onResult(NimBLEAdvertisedDevice* device) override;

    private:
        AttachmentLink& _link;
    };

    static void onScanComplete(NimBLEScanResults results);
    void connect();

    const char* _serviceUuid;
    State _state = State::IDLE;
    ScanCallbacks _scanCallbacks{*this};
    NimBLEAddress _address;
    NimBLEClient* _client = nullptr;
    uint32_t _lastScanMs = 0;
    uint8_t _remainingScans = 0;
};

#endif
