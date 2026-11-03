# ToioCockpit

M5Stack CoreS3 を運転席（cockpit）にして toio を操作するプロジェクト。
コントローラーがメインで、ショベルやアクセルは後から付け足すアタッチメントという位置づけ。

## 構成

役割ごとに `src/<役割>/` と PlatformIO の env を分ける。機種名ではなく役割の名前を使う。

| env | フォルダ | 機種 | 役割 |
|---|---|---|---|
| `cockpit` | `src/cockpit/` | CoreS3 | 運転席。toio の操作とメーターパネル表示（メイン） |
| `shovel` | `src/shovel/` | AtomS3 | toio に載せるショベル。Servo Kit 180° でアームを動かす (#13) |
| `pedal`（予定） | `src/pedal/` | Core2 | 圧力センサーのアクセル (#12) |

- 各 env は `build_src_filter = +<役割/>` で自分のフォルダだけをビルドする。`setup()` / `loop()` を持つファイルが入口
- 複数の env で使うコード（BLE の UUID やデータの形など）は `lib/` に置く。通信の決まりごとを変えるときは、送る側と受ける側を同じ PR で直す
  - `lib/ShovelProtocol/`: cockpit と shovel の BLE の決まりごと
- 同じ役割の機体が複数台あるときは、`extends` で env を継承し、`build_flags`（例: `-DSHOVEL_ID=1`）と `upload_port` で台ごとに分ける

## ビルドと書き込み

```sh
pio run -e cockpit                # ビルド
pio run -e cockpit -t upload      # 書き込み
pio run -e shovel -t upload       # shovel に書き込み
```

- 書き込みはユーザーの指示があってから行う。機体を USB から外していることがある
- 機体を複数つなぐようになったら、env ごとに `upload_port` を書いて書き込み先を取り違えないようにする

## cockpit（CoreS3）の入出力

- Port A (I2C): M5Unit-Joystick2。押し込みでその場回転、押し込み中はライトが付く
- Port B: GPIO 8 = 青ボタン（前進）、GPIO 9 = 赤ボタン（後退）
- 本体の傾き（IMU の accel.x）でハンドル
- タッチパネル: メーター中央の HORN ボタン
- toio のスピーカーでクラクションとバック音（MIDI 再生で作った音）

## shovel（AtomS3）の入出力

- Grove 端子の G2: Servo Kit 180°。`ESP32Servo` でパルス幅 1000〜2000μs を使う
- BLE ペリフェラルとして `ToioShovel` の名前でアドバタイズし、アームの位置（0〜100）を受け取る
- 画面のボタン（BtnA）: cockpit なしで試すための上げ下げ
- 電源: ATOM Mate for toio に載せる

## toio ライブラリ（M5StackToio）の注意

- `toio.loop()` は最初のスキャンで見つけたオブジェクトしか処理しない。再スキャン後も通知が届くよう `toiocore->_loop()` を直接呼んでいる
- `toio.scan()` は見つけた toio のオブジェクトを毎回 `new` して解放しない
- toio の MIDI 再生は新しい音の命令で一度止まってから鳴り直す。音をつなげて余韻を付けることはできない
- NimBLE-Arduino の同時接続数の初期上限は 3（`CONFIG_BT_NIMBLE_MAX_CONNECTIONS`）

## 作業の進め方

- 変更はブランチを切って行い、PR を作る。マージはユーザーに言われてから
- PR の「確認」には、ビルドだけか、実機で確かめたかを正直に書く
- 走りや音の値は名前付きの定数にして、ファイルの先頭にまとめる
- やり取りとコメント、コミットメッセージ、PR は日本語で書く
