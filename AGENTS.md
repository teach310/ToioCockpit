# ToioCockpit

M5Stack CoreS3 を運転席（cockpit）にして toio を操作するプロジェクト。
コントローラーがメインで、ショベルやアクセルは後から付け足すアタッチメントという位置づけ。

## 構成

役割ごとに `src/<役割>/` と PlatformIO の env を分ける。機種名ではなく役割の名前を使う。

| env | フォルダ | 機種 | 役割 |
|---|---|---|---|
| `cockpit` | `src/cockpit/` | CoreS3 | 運転席。toio の操作とメーターパネル表示（メイン） |
| `shovel` | `src/shovel/` | AtomS3 | toio に載せるショベル。Servo Kit 180° でアームを動かす (#13) |
| `pedal` | `src/pedal/` | CoreS3 | 圧力センサーのアクセル (#12)。今はセンサーの値を Serial と画面に出すテストだけ |

- 各 env は `build_src_filter = +<役割/>` で自分のフォルダだけをビルドする。`setup()` / `loop()` を持つファイルが入口
- 複数の env で使うコード（BLE の UUID やデータの形など）は `lib/` に置く。通信の決まりごとを変えるときは、送る側と受ける側を同じ PR で直す
  - `lib/ShovelProtocol/`: cockpit と shovel の BLE の決まりごと
- pedal（CoreS3）は内蔵の電池で動かす。感圧センサーと 10kΩ を Port B につなぎ、黄（G9）を HIGH にしてセンサーの電源を出し、白（G8）で読む。今の配線は配線D（`docs/wiring/pedal-d.json`）。Port B の赤（5V）は使わず絶縁する
  - 感圧センサーの端子には FFC用コネクタ（スイッチサイエンス 3327）を付けてピンヘッダ2本にしてあり、ジャンパワイヤ（オス‐メス）でブレッドボードまでのばす
  - `pedal-a/b/c` は ATOM Lite で試したときの配線。`pedal-wiring.html` では下の折りたたみに入れている
  - Port A は I2C 用で、基板の中で 3.3V に引っぱられている（Core2 では 2.2kΩ。10kΩ でつなぐと押していなくても約2.7Vになった）。アナログのセンサーは Port B につなぐ
  - cockpit を Core2 に替えて空いた CoreS3 を使う（cockpit の置き換えは別の PR）。電源の問題が片付けば ATOM のほうが pedal に向いている
- 配線図は `docs/wiring/` に置く。ブレッドボードの配線図は、ユーザー全体の skill `breadboard-diagram` で配置の JSON（`pedal-a.json` など）から SVG を作る。配線を変えたら JSON を直して作り直す。`pedal-wiring.html` はペダルの配線をまとめたページ
- 同じ役割の機体が複数台あるときは、`extends` で env を継承し、`build_flags`（例: `-DSHOVEL_ID=1`）と `upload_port` で台ごとに分ける

## ビルドと書き込み

```sh
pio run -e cockpit                # ビルド
pio run -e cockpit -t upload      # 書き込み
pio run -e shovel -t upload       # shovel に書き込み
pio run -e pedal -t upload        # pedal に書き込み
pio device monitor -e pedal       # pedal の Serial を見る
```

- 書き込みはユーザーの指示があってから行う。機体を USB から外していることがある
- 機体を複数つなぐようになったら、env ごとに `upload_port` を書いて書き込み先を取り違えないようにする

## cockpit（CoreS3）の入出力

- Port A (I2C): M5Unit-Joystick2。押し込みでその場回転（速い）、押し込み中はライトが付く。ショベルにつながっている間は押し込みでは回らない。左右に倒すとその場でゆっくり回る。上下に倒すとショベルのアームが動く（離すとその位置で止まる）。斜めのときは大きく倒した方の軸だけ使う
- shovel とは `ShovelLink` で BLE 接続する。shovel は使わない日も多いので探し続けない。toio とつないだとき、メーター左下の SHOVEL 表示をタッチしたとき、途中で切れたときに3回だけ探す。接続は toio が止まっているときだけ行う（接続中は最大数秒止まるため）
- Port B: GPIO 8 = 青ボタン（前進）、GPIO 9 = 赤ボタン（後退）
- 速さのギア: SLOW（前進40・後退30）、LOW（前進70・後退50）、HIGH（前進100・後退70）の3段。メーター左上のボタンをタッチして SLOW → LOW → HIGH → SLOW の順に切り替え、Preferences に保存する
- タッチで操作できるところ（速さのギア・HORN・SHOVEL・STEER）はボタンの見た目にする（影で浮かせ、押している間は沈める）。押せない表示（P R N D S など）はボタンにしない
- ハンドル: 本体の傾き（IMU の accel.x、TILT）か JoyStick の左右（STICK）。メーター右下の STEER 表示をタッチして切り替え、Preferences に保存する
- タッチパネル: メーター中央の HORN ボタン
- toio のスピーカーでクラクションとバック音（MIDI 再生で作った音）

## shovel（AtomS3）の入出力

- Grove 端子の G2: Servo Kit 180°。`ESP32Servo` でパルス幅 1000〜2000μs を使う
- BLE ペリフェラルとして `ToioShovel` の名前でアドバタイズし、アームの位置（0〜100）を受け取る
- 画面のボタン（BtnA）: cockpit なしで試すための上げ下げ。スリープ中は起こすだけ
- 自動スリープ: cockpit とつながっていない間に触らなければ、90秒で画面を暗くし、3分で画面・アドバタイズ・サーボ信号を止める
- 電源: ATOM Mate for toio に載せる
  - 電池の電圧は半分にされて G8 に出ている（ATOM Mate の回路図では ATOM Lite の GPIO33。AtomS3 の同じ位置が G8）
  - 充電中を知らせる信号は ATOM のピンにつながっていないので、電圧の上がり方で推定している

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
