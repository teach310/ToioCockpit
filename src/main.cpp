#include <M5Unified.h>
#include <Toio.h>
#include "GpioButton.hpp"
#include "Joystick.hpp"
#include "Dashboard.hpp"

Dashboard dashboard(M5.Display);

constexpr uint8_t BUTTON_BLUE_PIN = 8; // Port B
constexpr uint8_t BUTTON_RED_PIN = 9;  // Port B

GpioButton buttonBlue(BUTTON_BLUE_PIN);
GpioButton buttonRed(BUTTON_RED_PIN);
Joystick joystick;
bool joystickAvailable = false; // 起動時にJoyStickが応答したか

Toio toio;
ToioCore* toiocore = nullptr;

// State machine
enum class AppState {
  SCANNING,
  CONNECTING,
  RUNNING,
  ERROR,
  SLEEP
};
AppState state = AppState::SCANNING;

constexpr uint32_t CONTROL_INTERVAL_MS = 50;  // 入力読み取りとモーター制御の周期
constexpr uint32_t CORE_BATTERY_INTERVAL_MS = 2000; // CoreS3の電池残量を読む周期
constexpr uint32_t RETRY_INTERVAL_MS = 5000;  // ERRORから自動で再試行するまでの時間
constexpr uint32_t DIM_AFTER_MS = 30 * 1000;     // 触らないまま、この時間たったら画面を暗くする
constexpr uint32_t SLEEP_AFTER_MS = 3 * 60 * 1000; // 触らないまま、この時間たったらtoioとの接続を切って画面を消す
constexpr uint8_t DIM_BRIGHTNESS = 20;
constexpr uint8_t MAX_AUTO_RETRIES = 3;       // 放置時の電池消費を抑えるため、自動再試行は回数を限る
constexpr uint8_t SCAN_DURATION_SEC = 3;
constexpr uint32_t JOYSTICK_WARNING_MS = 2000; // JoyStickが見つからないときの警告の表示時間

// 走行のパラメータ。スロットルの変化量は CONTROL_INTERVAL_MS ごとの値
constexpr int8_t FORWARD_THROTTLE = 70;
constexpr int8_t BACKWARD_THROTTLE = -50;
constexpr int8_t ACCEL_STEP = 30;    // ボタンを押している間の加速
constexpr int8_t DECEL_STEP = 10;    // ボタンを離したときの減速
constexpr float STEERING_GAIN = 40;  // 本体の傾き(accel.x)をステアリングに変換する倍率
constexpr uint8_t SPIN_SPEED = 100;  // その場で回転するときのモーターの速さ

int8_t currentThrottle = 0;
uint32_t lastControlMs = 0;

// ERRORになった時刻と、再試行で戻る先の状態
uint32_t errorStartMs = 0;
AppState retryState = AppState::SCANNING;
uint8_t autoRetryCount = 0;

// 自動スリープ用。最後に操作した時刻と、画面を暗くしているか
uint32_t lastActivityMs = 0;
bool dimmed = false;
uint8_t normalBrightness = 0;
AppState wakeState = AppState::SCANNING; // スリープから起きたときに戻る先

// 最後に送ったモーター命令。同じ命令を送り直さないために使う
enum class MotorMode { NONE, DRIVE, SPIN };
MotorMode lastMotorMode = MotorMode::NONE;
int8_t lastThrottle = 0;
int8_t lastSteering = 0;

// toioのMIDI再生データ: {0x03, 繰り返し回数(0は無限), 音の数, (長さ[10ms], ノート番号, 音量)...}
// toioの効果音には合うものがないので作る。ノート番号128は無音
// クラクション: 低めの1音(F4)をそのまま伸ばす。太い「ブーッ」
uint8_t HORN_SOUND[] = {
  0x03, 0, 1,
  100, 65, 255,
};
// バック音: 「ピーッ、ピーッ」(E5を0.4秒鳴らして0.4秒休む)
uint8_t BACKUP_SOUND[] = {
  0x03, 0, 2,
  40, 76, 160,
  40, 128, 0,
};

// 押し込んでいる間のJoyStickのライトの色。最大だと眩しいので、水色のまま1/10くらいの明るさにする
constexpr uint32_t JOYSTICK_LED_COLOR = 0x001419;

// 最後に鳴らした音。同じ音を送り直さないために使う
enum class SoundMode { NONE, HORN, BACKUP };
SoundMode lastSoundMode = SoundMode::NONE;

bool joystickLedOn = false;

void updateSound(SoundMode mode) {
  if (mode == lastSoundMode) {
    return;
  }
  switch (mode) {
    case SoundMode::HORN:
      toiocore->playSoundRaw(HORN_SOUND, sizeof(HORN_SOUND));
      break;
    case SoundMode::BACKUP:
      toiocore->playSoundRaw(BACKUP_SOUND, sizeof(BACKUP_SOUND));
      break;
    case SoundMode::NONE:
      toiocore->stopSound();
      break;
  }
  lastSoundMode = mode;
}

void sendDrive(int8_t throttle, int8_t steering) {
  if (lastMotorMode == MotorMode::DRIVE && lastThrottle == throttle && lastSteering == steering) {
    return;
  }
  toiocore->drive(throttle, steering);
  lastMotorMode = MotorMode::DRIVE;
  lastThrottle = throttle;
  lastSteering = steering;
}

void sendSpin() {
  if (lastMotorMode == MotorMode::SPIN) {
    return;
  }
  toiocore->controlMotor(true, SPIN_SPEED, false, SPIN_SPEED);
  lastMotorMode = MotorMode::SPIN;
}

// 引数で渡した一定単位ごとに近づける
void updateCurrentThrottle(int8_t targetThrottle, int8_t step) {
  if (currentThrottle == targetThrottle) {
    return;
  }
  if (currentThrottle < targetThrottle) {
    currentThrottle += step;
    if (currentThrottle > targetThrottle) {
      currentThrottle = targetThrottle;
    }
  } else {
    currentThrottle -= step;
    if (currentThrottle < targetThrottle) {
      currentThrottle = targetThrottle;
    }
  }
}

// 状態が変わったときの表示。SCANNINGのように次の処理で止まる状態があるので、すぐ描く
void showStatus(const char* status, const char* detail, Dashboard::Tone tone) {
  dashboard.setStatus(status, detail, tone);
  dashboard.draw(true);
}

void updateCoreBattery() {
  static uint32_t lastMs = 0;
  uint32_t now = millis();
  if (lastMs != 0 && now - lastMs < CORE_BATTERY_INTERVAL_MS) {
    return;
  }
  lastMs = now;
  bool charging = M5.Power.isCharging() == m5::Power_Class::is_charging;
  dashboard.setCoreBattery(M5.Power.getBatteryLevel(), charging);
}

void setup(void)
{
  auto cfg = M5.config();
  M5.begin(cfg);
  buttonBlue.begin();
  buttonRed.begin();

  Joystick::Config joyCfg;
  joyCfg.sda = M5.getPin(m5::pin_name_t::port_a_sda);
  joyCfg.scl = M5.getPin(m5::pin_name_t::port_a_scl);
  joyCfg.yCalibration = {200, 32390, 65380}; // 実測値のキャリブレーション値
  joystickAvailable = joystick.begin(joyCfg);
  if (joystickAvailable) {
    joystick.setRgbColor(0);
  }

  normalBrightness = M5.Display.getBrightness();
  dashboard.begin();
  updateCoreBattery();

  // JoyStickがなくてもボタンで走れるので、知らせるだけで先に進む
  if (!joystickAvailable) {
    showStatus("WARNING", "Joystick not found", Dashboard::Tone::WARN);
    delay(JOYSTICK_WARNING_MS);
  }

  state = AppState::SCANNING;
  showStatus("SCANNING", "Searching toio...", Dashboard::Tone::INFO);
}

// targetThrottle に向けて加速しながら、本体の傾きで曲がる
void driveToward(int8_t targetThrottle) {
  M5.Imu.update();
  auto data = M5.Imu.getImuData();
  updateCurrentThrottle(targetThrottle, ACCEL_STEP);
  int8_t steering = (int8_t)(-data.accel.x * STEERING_GAIN);
  sendDrive(currentThrottle, steering);
}

// 左右のモーターを逆回転させてその場で回転する
void spinInPlace() {
  currentThrottle = 0;
  sendSpin();
}

bool isHornButtonTouched() {
  auto touch = M5.Touch.getDetail();
  return touch.isPressed() && dashboard.isHornHit(touch.x, touch.y);
}

// JoyStickを押し込んでいる間はライトを付ける。押し込みが読めているかを目で確かめられるようにする
void updateJoystickLed(bool on) {
  if (!joystickAvailable || on == joystickLedOn) {
    return;
  }
  joystick.setRgbColor(on ? JOYSTICK_LED_COLOR : 0);
  joystickLedOn = on;
}

// ERRORにして、ボタンを押すか一定時間たったら nextState からやり直す
// 自動再試行を使い切ったあとは、ボタンを押したときだけやり直す
void enterError(const char* detail, AppState nextState) {
  state = AppState::ERROR;
  retryState = nextState;
  errorStartMs = millis();
  updateJoystickLed(false);
  dashboard.setRunning(false);
  dashboard.setGear(Dashboard::Gear::P);
  dashboard.setSpeed(0);
  dashboard.setHornPressed(false);
  dashboard.setToioBattery(-1);
  showStatus("ERROR", autoRetryCount < MAX_AUTO_RETRIES ? detail : "Push Button to retry",
             Dashboard::Tone::ERROR);
}

// 操作があったら呼ぶ。暗くしていた画面を元に戻す
void markActivity() {
  lastActivityMs = millis();
  if (dimmed) {
    M5.Display.setBrightness(normalBrightness);
    dimmed = false;
  }
}

// 触らない時間に応じて画面を暗くする。スリープに入るべきなら true を返す
bool checkIdle() {
  uint32_t idle = millis() - lastActivityMs;
  if (!dimmed && idle >= DIM_AFTER_MS) {
    M5.Display.setBrightness(DIM_BRIGHTNESS);
    dimmed = true;
  }
  return idle >= SLEEP_AFTER_MS;
}

// 放置時の電池消費を抑えるため、toioとの接続を切って画面を消す。nextState は起きたときに戻る先
void enterSleep(AppState nextState) {
  if (toiocore && toiocore->isConnected()) {
    toiocore->stopSound();
    toiocore->drive(0, 0);
    toiocore->disconnect();
  }
  updateJoystickLed(false);
  dashboard.setRunning(false);
  dashboard.setGear(Dashboard::Gear::P);
  dashboard.setSpeed(0);
  dashboard.setHornPressed(false);
  dashboard.setToioBattery(-1);
  showStatus("SLEEP", "Push Button to wake", Dashboard::Tone::INFO);
  M5.Display.setBrightness(0);
  M5.Display.sleep();
  wakeState = nextState;
  state = AppState::SLEEP;
}

void wakeUp() {
  M5.Display.wakeup();
  dimmed = false;
  M5.Display.setBrightness(normalBrightness);
  markActivity();
  autoRetryCount = 0;
  state = wakeState;
  if (state == AppState::CONNECTING) {
    showStatus("CONNECTING", "Reconnecting to toio...", Dashboard::Tone::INFO);
  } else {
    showStatus("SCANNING", "Searching toio...", Dashboard::Tone::INFO);
  }
}

// 接続できたらRUNNINGに入る。前回の接続で残ったモーター制御の状態は初期化する
void startRunning() {
  autoRetryCount = 0;
  currentThrottle = 0;
  lastMotorMode = MotorMode::NONE;
  lastSoundMode = SoundMode::NONE;
  lastControlMs = 0;
  // toioの電池残量は最初に1回読み、あとは変化の通知で受け取る。通知は loop() の toiocore->_loop() の中で届く
  dashboard.setToioBattery(toiocore->getBatteryLevel());
  toiocore->onBattery([](uint8_t level) {
    dashboard.setToioBattery(level);
  });
  dashboard.setRunning(true);
  dashboard.setGear(Dashboard::Gear::N);
  showStatus("RUNNING", "", Dashboard::Tone::OK);
  markActivity();
  state = AppState::RUNNING;
}

// JoyStickがないときはI2Cを読まない（押されていない扱いのまま）
void updateInputs() {
  buttonBlue.update();
  buttonRed.update();
  if (joystickAvailable) {
    joystick.update();
  }
}

void loop(void)
{
  M5.update();
  // toio.loop() は最初のスキャンで見つけたオブジェクトしか回さない。スキャンし直すと
  // toiocore は別のオブジェクトになり、電池残量の通知が届かなくなるので直接回す
  if (toiocore) {
    toiocore->_loop();
  }
  // スリープ中は画面を消しているので、電池残量の読み取りも描き直しもしない
  if (state != AppState::SLEEP) {
    updateCoreBattery();
  }

  switch (state) {
    case AppState::SCANNING: {
      std::vector<ToioCore*> found = toio.scan(SCAN_DURATION_SEC);
      if (!found.empty()) {
        toiocore = found.at(0);
        state = AppState::CONNECTING;
        showStatus("CONNECTING", "Connecting to toio...", Dashboard::Tone::INFO);
      } else {
        enterError("No toio found. Retry...", AppState::SCANNING);
      }
      break;
    }
    case AppState::CONNECTING:
      if (toiocore && toiocore->connect()) {
        startRunning();
      } else {
        enterError("Connect failed. Retry...", AppState::SCANNING);
      }
      break;
    case AppState::RUNNING: {
      if (!toiocore || !toiocore->isConnected()) {
        // 同じtoioにつなぎ直す。失敗したらスキャンからやり直す
        enterError("Disconnected. Retry...", AppState::CONNECTING);
        break;
      }

      // 加速・減速の速さがループの回る速さに左右されないよう、一定周期で制御する
      uint32_t now = millis();
      if (now - lastControlMs < CONTROL_INTERVAL_MS) {
        delay(1);
        break;
      }
      lastControlMs = now;

      updateInputs();

      bool spinning = joystick.isPressed();
      updateJoystickLed(spinning);
      if (spinning) {
        spinInPlace();
      } else if (buttonBlue.isPressed()) {
        driveToward(FORWARD_THROTTLE);
      } else if (buttonRed.isPressed()) {
        driveToward(BACKWARD_THROTTLE);
      } else {
        updateCurrentThrottle(0, DECEL_STEP);
        sendDrive(currentThrottle, 0);
      }

      bool hornTouched = isHornButtonTouched();
      dashboard.setHornPressed(hornTouched);
      dashboard.setSpeed(currentThrottle);
      if (spinning) {
        dashboard.setGear(Dashboard::Gear::S);
      } else if (currentThrottle > 0) {
        dashboard.setGear(Dashboard::Gear::D);
      } else if (currentThrottle < 0) {
        dashboard.setGear(Dashboard::Gear::R);
      } else {
        dashboard.setGear(Dashboard::Gear::N);
      }

      // クラクションボタンを押している間はクラクション。それ以外で後ろに動いている間はバック音
      if (hornTouched) {
        updateSound(SoundMode::HORN);
      } else if (currentThrottle < 0) {
        updateSound(SoundMode::BACKUP);
      } else {
        updateSound(SoundMode::NONE);
      }

      // 動いている間や、ボタン・画面に触れている間は操作中とみなす
      bool active = spinning || buttonBlue.isPressed() || buttonRed.isPressed() ||
                    M5.Touch.getDetail().isPressed() || currentThrottle != 0;
      if (active) {
        markActivity();
      } else if (checkIdle()) {
        enterSleep(AppState::CONNECTING);
      }
      break;
    }
    case AppState::ERROR: {
      updateInputs();
      bool pressed = buttonBlue.wasPressed() || buttonRed.wasPressed() || joystick.wasPressed();
      bool canAutoRetry = autoRetryCount < MAX_AUTO_RETRIES;
      if (pressed) {
        markActivity();
      }
      if (!pressed && !(canAutoRetry && millis() - errorStartMs >= RETRY_INTERVAL_MS)) {
        // 自動再試行を使い切ってボタン待ちのまま放置されたら、スリープに入る
        if (!canAutoRetry && checkIdle()) {
          enterSleep(retryState);
          break;
        }
        delay(CONTROL_INTERVAL_MS);
        break;
      }
      // 手動でやり直したときは、自動再試行の回数も元に戻す
      autoRetryCount = pressed ? 0 : autoRetryCount + 1;
      state = retryState;
      if (state == AppState::CONNECTING) {
        showStatus("CONNECTING", "Reconnecting to toio...", Dashboard::Tone::INFO);
      } else {
        showStatus("SCANNING", "Searching toio...", Dashboard::Tone::INFO);
      }
      break;
    }
    case AppState::SLEEP: {
      updateInputs();
      bool pressed = buttonBlue.wasPressed() || buttonRed.wasPressed() || joystick.wasPressed() ||
                     M5.Touch.getDetail().wasPressed();
      if (pressed) {
        wakeUp();
      } else {
        delay(CONTROL_INTERVAL_MS);
      }
      break;
    }
  }
  if (state != AppState::SLEEP) {
    dashboard.draw();
  }
}
