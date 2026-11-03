#include <M5Unified.h>
#include <Toio.h>
#include "GpioButton.hpp"
#include "Joystick.hpp"

static auto &dsp = M5.Display;
LGFX_Sprite canvas(&dsp);

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
  ERROR
};
AppState state = AppState::SCANNING;

constexpr uint32_t CONTROL_INTERVAL_MS = 50;  // 入力読み取りとモーター制御の周期
constexpr uint32_t DISPLAY_INTERVAL_MS = 500; // 表示内容が同じときの再描画周期
constexpr uint32_t RETRY_INTERVAL_MS = 5000;  // ERRORから自動で再試行するまでの時間
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
// バック音: 「ピーッ、ピーッ」(B5を0.4秒鳴らして0.4秒休む)
uint8_t BACKUP_SOUND[] = {
  0x03, 0, 2,
  40, 83, 160,
  40, 128, 0,
};

// 最後に鳴らした音。同じ音を送り直さないために使う
enum class SoundMode { NONE, HORN, BACKUP };
SoundMode lastSoundMode = SoundMode::NONE;

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

// 表示内容が変わったときか、一定時間たったとき（バッテリー表示の更新）だけ再描画する
void showStatus(const char* status, const char* detail = nullptr) {
  static const char* lastStatus = nullptr;
  static const char* lastDetail = nullptr;
  static uint32_t lastDrawMs = 0;
  uint32_t now = millis();
  auto same = [](const char* a, const char* b) {
    return a == b || (a && b && strcmp(a, b) == 0);
  };
  if (same(status, lastStatus) && same(detail, lastDetail) && now - lastDrawMs < DISPLAY_INTERVAL_MS) {
    return;
  }
  lastStatus = status;
  lastDetail = detail;
  lastDrawMs = now;

  canvas.fillScreen(TFT_BLACK);
  canvas.setTextColor(TFT_WHITE, TFT_BLACK);
  canvas.setCursor(4, 2);
  canvas.printf("State: %s", status);

  // バッテリー表示
  int percent = M5.Power.getBatteryLevel();
  canvas.setCursor(160, 2);
  canvas.printf("Bat: %d%%", percent);

  if (detail) {
    canvas.setCursor(4, 22);
    canvas.printf("%s", detail);
  }
  canvas.pushSprite(0, 0);
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

  dsp.setTextSize(2);
  canvas.createSprite(dsp.width(), 40);
  canvas.setTextSize(2);

  // JoyStickがなくてもボタンで走れるので、知らせるだけで先に進む
  if (!joystickAvailable) {
    showStatus("WARNING", "Joystick not found");
    delay(JOYSTICK_WARNING_MS);
  }

  state = AppState::SCANNING;
  showStatus("SCANNING", "Searching toio...");
}

// targetThrottle に向けて加速しながら、本体の傾きで曲がる
void driveToward(int8_t targetThrottle, const char* detail) {
  M5.Imu.update();
  auto data = M5.Imu.getImuData();
  updateCurrentThrottle(targetThrottle, ACCEL_STEP);
  int8_t steering = (int8_t)(-data.accel.x * STEERING_GAIN);
  sendDrive(currentThrottle, steering);
  showStatus("RUNNING", detail);
}

// 左右のモーターを逆回転させてその場で回転する
void spinInPlace() {
  currentThrottle = 0;
  sendSpin();
  showStatus("RUNNING", "Spin");
}

// ERRORにして、ボタンを押すか一定時間たったら nextState からやり直す
// 自動再試行を使い切ったあとは、ボタンを押したときだけやり直す
void enterError(const char* detail, AppState nextState) {
  state = AppState::ERROR;
  retryState = nextState;
  errorStartMs = millis();
  showStatus("ERROR", autoRetryCount < MAX_AUTO_RETRIES ? detail : "Push Button to retry");
}

// 接続できたらRUNNINGに入る。前回の接続で残ったモーター制御の状態は初期化する
void startRunning() {
  autoRetryCount = 0;
  currentThrottle = 0;
  lastMotorMode = MotorMode::NONE;
  lastSoundMode = SoundMode::NONE;
  lastControlMs = 0;
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
  toio.loop();

  switch (state) {
    case AppState::SCANNING: {
      std::vector<ToioCore*> found = toio.scan(SCAN_DURATION_SEC);
      if (!found.empty()) {
        toiocore = found.at(0);
        state = AppState::CONNECTING;
        showStatus("CONNECTING", "Connecting to toio...");
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

      if (joystick.isPressed()) {
        spinInPlace();
      } else if (buttonBlue.isPressed()) {
        driveToward(FORWARD_THROTTLE, "Forward");
      } else if (buttonRed.isPressed()) {
        driveToward(BACKWARD_THROTTLE, "Backward");
      } else {
        updateCurrentThrottle(0, DECEL_STEP);
        sendDrive(currentThrottle, 0);
        showStatus("RUNNING", "Push Button to drive");
      }

      // 画面に触れている間はクラクション。それ以外で後ろに動いている間はバック音
      if (M5.Touch.getDetail().isPressed()) {
        updateSound(SoundMode::HORN);
      } else if (currentThrottle < 0) {
        updateSound(SoundMode::BACKUP);
      } else {
        updateSound(SoundMode::NONE);
      }
      break;
    }
    case AppState::ERROR: {
      updateInputs();
      bool pressed = buttonBlue.wasPressed() || buttonRed.wasPressed() || joystick.wasPressed();
      bool canAutoRetry = autoRetryCount < MAX_AUTO_RETRIES;
      if (!pressed && !(canAutoRetry && millis() - errorStartMs >= RETRY_INTERVAL_MS)) {
        delay(CONTROL_INTERVAL_MS);
        break;
      }
      // 手動でやり直したときは、自動再試行の回数も元に戻す
      autoRetryCount = pressed ? 0 : autoRetryCount + 1;
      state = retryState;
      if (state == AppState::CONNECTING) {
        showStatus("CONNECTING", "Reconnecting to toio...");
      } else {
        showStatus("SCANNING", "Searching toio...");
      }
      break;
    }
  }
}
