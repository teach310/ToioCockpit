#include <M5Unified.h>
#include <Toio.h>
#include "GpioButton.hpp"
#include "Joystick.hpp"
#include "Dashboard.hpp"
#include "ShovelLink.hpp"
#include <Preferences.h>

Dashboard dashboard(M5.Display);
ShovelLink shovel;

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
constexpr uint32_t DIM_AFTER_MS = 90 * 1000;     // 触らないまま、この時間たったら画面を暗くする
constexpr uint32_t SLEEP_AFTER_MS = 3 * 60 * 1000; // 触らないまま、この時間たったらtoioとの接続を切って画面を消す
constexpr uint8_t DIM_BRIGHTNESS = 20;
constexpr uint8_t MAX_AUTO_RETRIES = 3;       // 放置時の電池消費を抑えるため、自動再試行は回数を限る
constexpr uint8_t SCAN_DURATION_SEC = 3;
constexpr uint32_t JOYSTICK_WARNING_MS = 2000; // JoyStickが見つからないときの警告の表示時間

// 走行のパラメータ。スロットルの変化量は CONTROL_INTERVAL_MS ごとの値
// 速さのギア。メーター左上のボタンをタッチして SLOW → LOW → HIGH → SLOW の順に切り替え、本体に保存する
// 数字は本体に保存する値。前からある LO / HI の値は変えない（LOW / HIGH は Arduino のマクロとぶつかるので使わない）
enum class SpeedGear : uint8_t { LO = 0, HI = 1, SLOW = 2 };
SpeedGear speedGear = SpeedGear::LO;
// SLOW は内側の車輪（STEERING_GAIN の分だけ遅くなる）が、toio のモーターが回る一番遅い速さ 8 を下回らないようにする
constexpr int8_t FORWARD_THROTTLE_SLOW = 40;
constexpr int8_t BACKWARD_THROTTLE_SLOW = -30;
constexpr int8_t FORWARD_THROTTLE_LOW = 70;
constexpr int8_t BACKWARD_THROTTLE_LOW = -50;
constexpr int8_t FORWARD_THROTTLE_HIGH = 100; // toio の drive() で出せる一番速い速さ
constexpr int8_t BACKWARD_THROTTLE_HIGH = -70; // 後ろ向きは見えにくいので前進より控えめにする
constexpr int8_t ACCEL_STEP = 30;    // ボタンを押している間の加速
constexpr int8_t DECEL_STEP = 10;    // ボタンを離したときの減速
// 走っているときのハンドルの切り方。メーター右下の STEER 表示をタッチして切り替え、本体に保存する
//   TILT : 本体の傾きで曲がる
//   STICK: JoyStick の左右で曲がる
enum class SteeringMode : uint8_t { TILT, STICK };
SteeringMode steeringMode = SteeringMode::TILT;
Preferences preferences;
// ステアリングの最大値。TILT は本体を真横(accel.x = 1G)まで傾けたとき、STICK は一番倒したときにこの値になる
constexpr float STEERING_GAIN = 40;
constexpr uint8_t SPIN_SPEED = 100;  // JoyStickを押し込んだときの、その場で回転する速さ
// JoyStickを左右に倒したときは、その場でゆっくり回る
constexpr uint8_t SLOW_SPIN_SPEED = 8; // toioのモーターが回る一番遅い速さ。8未満だと回らない（toio通信仕様）
constexpr int SLOW_SPIN_DIRECTION = 1;      // 右に倒して左に回るときは -1 にする

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
bool lastSpinClockwise = true;
uint8_t lastSpinSpeed = 0;
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

// ショベルのアーム。JoyStickを上下に倒している間、倒した量に応じて動く（離すとその位置で止まる）
constexpr float ARM_STICK_SPEED = 10.0f; // 一番倒したときに CONTROL_INTERVAL_MS ごとに動く量。0から100まで約0.5秒
constexpr int ARM_STICK_DIRECTION = 1;  // 上に倒して下がるときは -1 にする
float armPosition = 0;                  // 0 = 一番下, 100 = 一番上
bool shovelWasConnected = false;
bool shovelAreaWasTouched = false; // 左下のショベル表示を、前回の制御周期で押していたか
bool steeringAreaWasTouched = false; // 右下の STEER 表示を、前回の制御周期で押していたか
bool gearAreaWasTouched = false;     // 左のギアの列を、前回の制御周期で押していたか

// 押し込んでいる間のJoyStickのライトの色。最大だと眩しいので、水色のまま1/10くらいの明るさにする
constexpr uint32_t JOYSTICK_LED_COLOR = 0x001419;

// 最後に鳴らした音。同じ音を送り直さないために使う
enum class SoundMode { NONE, HORN, BACKUP };
SoundMode lastSoundMode = SoundMode::NONE;

bool joystickLedOn = false;
uint32_t lastJoystickLedMs = 0;
constexpr uint32_t JOYSTICK_LED_REFRESH_MS = 1000; // ユニットが再起動して色が戻っても直るよう、この間隔で送り直す
uint32_t loggedJoystickFailures = 0;
uint32_t lastJoystickLogMs = 0;

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

// clockwise が true なら右回り（左のモーターを前、右を後ろに回す）
void sendSpin(bool clockwise, uint8_t speed) {
  if (lastMotorMode == MotorMode::SPIN && lastSpinClockwise == clockwise && lastSpinSpeed == speed) {
    return;
  }
  toiocore->controlMotor(clockwise, speed, !clockwise, speed);
  lastMotorMode = MotorMode::SPIN;
  lastSpinClockwise = clockwise;
  lastSpinSpeed = speed;
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

Dashboard::SpeedGear toDashboardGear(SpeedGear gear) {
  switch (gear) {
    case SpeedGear::SLOW:
      return Dashboard::SpeedGear::SLOW;
    case SpeedGear::HI:
      return Dashboard::SpeedGear::HI;
    case SpeedGear::LO:
    default:
      return Dashboard::SpeedGear::LO;
  }
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
  joyCfg.yCalibration = {200, 32390, 65380}; // 実測値。中心は起動時に測り直す
  joystickAvailable = joystick.begin(joyCfg);
  if (joystickAvailable) {
    joystick.setRgbColor(0);
    // 中心は個体差やずれがあるので、触れていない起動直後の値を中心にする（ずれていると勝手に回ったりアームが動いたりする）
    joystick.calibrateCenter();
  }

  normalBrightness = M5.Display.getBrightness();
  preferences.begin("cockpit");
  steeringMode = static_cast<SteeringMode>(preferences.getUChar("steer", static_cast<uint8_t>(SteeringMode::TILT)));
  dashboard.begin();
  dashboard.setStickSteering(steeringMode == SteeringMode::STICK);
  speedGear = static_cast<SpeedGear>(preferences.getUChar("gear", static_cast<uint8_t>(SpeedGear::LO)));
  if (speedGear != SpeedGear::LO && speedGear != SpeedGear::HI && speedGear != SpeedGear::SLOW) {
    speedGear = SpeedGear::LO;
  }
  dashboard.setSpeedGear(toDashboardGear(speedGear));
  updateCoreBattery();

  // JoyStickがなくてもボタンで走れるので、知らせるだけで先に進む
  if (!joystickAvailable) {
    showStatus("WARNING", "Joystick not found", Dashboard::Tone::WARN);
    delay(JOYSTICK_WARNING_MS);
  }

  state = AppState::SCANNING;
  showStatus("SCANNING", "Searching toio...", Dashboard::Tone::INFO);
}

// targetThrottle に向けて加速しながら、steeringMode に応じて本体の傾きか JoyStick の左右で曲がる
void driveToward(int8_t targetThrottle, int stickX) {
  updateCurrentThrottle(targetThrottle, ACCEL_STEP);
  int8_t steering;
  if (steeringMode == SteeringMode::STICK) {
    steering = (int8_t)(stickX / 100.0f * STEERING_GAIN); // stickX は -100～100
  } else {
    M5.Imu.update();
    auto data = M5.Imu.getImuData();
    steering = (int8_t)(-data.accel.x * STEERING_GAIN);
  }
  sendDrive(currentThrottle, steering);
}

// 速さのギアを1段上げる。一番上の次は一番下に戻る
void cycleSpeedGear() {
  switch (speedGear) {
    case SpeedGear::SLOW:
      speedGear = SpeedGear::LO;
      break;
    case SpeedGear::LO:
      speedGear = SpeedGear::HI;
      break;
    case SpeedGear::HI:
      speedGear = SpeedGear::SLOW;
      break;
  }
  preferences.putUChar("gear", static_cast<uint8_t>(speedGear));
  dashboard.setSpeedGear(toDashboardGear(speedGear));
}

int8_t forwardThrottle() {
  switch (speedGear) {
    case SpeedGear::SLOW:
      return FORWARD_THROTTLE_SLOW;
    case SpeedGear::HI:
      return FORWARD_THROTTLE_HIGH;
    case SpeedGear::LO:
    default:
      return FORWARD_THROTTLE_LOW;
  }
}

int8_t backwardThrottle() {
  switch (speedGear) {
    case SpeedGear::SLOW:
      return BACKWARD_THROTTLE_SLOW;
    case SpeedGear::HI:
      return BACKWARD_THROTTLE_HIGH;
    case SpeedGear::LO:
    default:
      return BACKWARD_THROTTLE_LOW;
  }
}

void toggleSteeringMode() {
  steeringMode = steeringMode == SteeringMode::TILT ? SteeringMode::STICK : SteeringMode::TILT;
  preferences.putUChar("steer", static_cast<uint8_t>(steeringMode));
  dashboard.setStickSteering(steeringMode == SteeringMode::STICK);
}

// 左右のモーターを逆回転させてその場で回転する
void spinInPlace(bool clockwise, uint8_t speed) {
  currentThrottle = 0;
  sendSpin(clockwise, speed);
}

bool isHornButtonTouched() {
  auto touch = M5.Touch.getDetail();
  return touch.isPressed() && dashboard.isHornHit(touch.x, touch.y);
}

// JoyStickを押し込んでいる間はライトを付ける。押し込みが読めているかを目で確かめられるようにする
// ユニットが途中で再起動すると初期の色（緑）に戻るので、変わらなくても一定の間隔で送り直す
void updateJoystickLed(bool on) {
  uint32_t now = millis();
  if (!joystickAvailable || (on == joystickLedOn && now - lastJoystickLedMs < JOYSTICK_LED_REFRESH_MS)) {
    return;
  }
  if (joystick.setRgbColor(on ? JOYSTICK_LED_COLOR : 0)) {
    joystickLedOn = on;
    lastJoystickLedMs = now;
  }
}

// JoyStickの読み取りの失敗を、1秒に1回まで Serial に出す（原因を調べる用）
void logJoystickFailures() {
  uint32_t now = millis();
  if (joystick.failureCount() == loggedJoystickFailures || now - lastJoystickLogMs < 1000) {
    return;
  }
  Serial.printf("[joystick] i2c read failed: total=%lu recover=%lu healthy=%d\n",
                (unsigned long)joystick.failureCount(), (unsigned long)joystick.recoveryCount(), joystick.healthy());
  loggedJoystickFailures = joystick.failureCount();
  lastJoystickLogMs = now;
}

// ERRORにして、ボタンを押すか一定時間たったら nextState からやり直す
// 自動再試行を使い切ったあとは、ボタンを押したときだけやり直す
void enterError(const char* detail, AppState nextState) {
  state = AppState::ERROR;
  retryState = nextState;
  errorStartMs = millis();
  updateJoystickLed(false);
  // toioのスキャンとぶつからないよう、ショベルを探すのもやめる
  shovel.stop();
  dashboard.setShovel(Dashboard::ShovelState::OFF, -1);
  dashboard.setRunning(false);
  dashboard.setGear(Dashboard::Gear::P);
  dashboard.setSpeed(0);
  dashboard.setHornPressed(false);
  dashboard.setTouch(false, 0, 0);
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
  shovel.stop();
  dashboard.setShovel(Dashboard::ShovelState::OFF, -1);
  updateJoystickLed(false);
  dashboard.setRunning(false);
  dashboard.setGear(Dashboard::Gear::P);
  dashboard.setSpeed(0);
  dashboard.setHornPressed(false);
  dashboard.setTouch(false, 0, 0);
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
  shovel.startSearch();
  state = AppState::RUNNING;
}

// JoyStickがないときはI2Cを読まない（押されていない扱いのまま）
void updateInputs() {
  buttonBlue.update();
  buttonRed.update();
  if (joystickAvailable) {
    joystick.update();
    logJoystickFailures();
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

      // 斜めに倒したときに回転とアームが両方動かないよう、大きく倒している方の軸だけ使う
      int rawX = joystickAvailable ? joystick.x() : 0;
      int rawY = joystickAvailable ? joystick.y() : 0;
      // 読み取りに失敗している間は、画面の上の状態表示で知らせる
      if (joystickAvailable && !joystick.healthy()) {
        dashboard.setStatus("JOYSTICK ERR", "", Dashboard::Tone::WARN);
      } else {
        dashboard.setStatus("RUNNING", "", Dashboard::Tone::OK);
      }
      bool xDominant = abs(rawX) > abs(rawY);
      int stickX = xDominant ? rawX : 0;
      int stickY = xDominant ? 0 : rawY;

      // 優先順: 押し込み（速く回る）→ 青・赤ボタン（走る）→ 左右に倒す（ゆっくり回る）→ 止まる
      // ショベルにつながっている間は、速く回るとアームが振られて危ないので押し込みでは回らない
      bool spinning = joystick.isPressed() && !shovel.isConnected();
      bool driving = !spinning && (buttonBlue.isPressed() || buttonRed.isPressed());
      bool slowSpinning = !spinning && !driving && stickX != 0;
      updateJoystickLed(spinning);
      if (spinning) {
        spinInPlace(true, SPIN_SPEED);
      } else if (buttonBlue.isPressed()) {
        driveToward(forwardThrottle(), stickX);
      } else if (buttonRed.isPressed()) {
        driveToward(backwardThrottle(), stickX);
      } else if (slowSpinning) {
        spinInPlace(stickX * SLOW_SPIN_DIRECTION > 0, SLOW_SPIN_SPEED);
      } else {
        updateCurrentThrottle(0, DECEL_STEP);
        sendDrive(currentThrottle, 0);
      }

      // ショベル。使わない日も多いので、toioとつないだときと左下をタッチしたときに数回だけ探す
      // 接続は数秒止まることがあるので、toioが止まっているときだけ
      // wasPressed() は M5.update() ごとに消え、50msごとの制御周期では見逃すので、押しているかの変化で見る
      auto touch = M5.Touch.getDetail();
      dashboard.setTouch(touch.isPressed(), touch.x, touch.y); // 触れているボタンを押し込んだ見た目にする
      bool shovelAreaTouched = touch.isPressed() && dashboard.isShovelHit(touch.x, touch.y);
      if (shovelAreaTouched && !shovelAreaWasTouched) {
        shovel.startSearch();
      }
      shovelAreaWasTouched = shovelAreaTouched;
      bool steeringAreaTouched = touch.isPressed() && dashboard.isSteeringHit(touch.x, touch.y);
      if (steeringAreaTouched && !steeringAreaWasTouched) {
        toggleSteeringMode();
      }
      steeringAreaWasTouched = steeringAreaTouched;
      bool gearAreaTouched = touch.isPressed() && dashboard.isGearHit(touch.x, touch.y);
      if (gearAreaTouched && !gearAreaWasTouched) {
        cycleSpeedGear();
      }
      gearAreaWasTouched = gearAreaTouched;
      bool toioStopped = !spinning && !slowSpinning && currentThrottle == 0;
      shovel.update(toioStopped);
      bool shovelConnected = shovel.isConnected();
      if (shovelConnected && !shovelWasConnected && shovel.arm() >= 0) {
        armPosition = shovel.arm(); // つないだ瞬間にアームが動かないよう、今の位置から始める
      }
      shovelWasConnected = shovelConnected;
      if (shovelConnected) {
        armPosition += stickY / 100.0f * ARM_STICK_SPEED * ARM_STICK_DIRECTION;
        armPosition = constrain(armPosition, 0.0f, 100.0f);
        shovel.setArm(static_cast<uint8_t>(armPosition + 0.5f));
      }
      Dashboard::ShovelState shovelState = shovelConnected     ? Dashboard::ShovelState::CONNECTED
                                           : shovel.isSearching() ? Dashboard::ShovelState::SEARCHING
                                                                  : Dashboard::ShovelState::OFF;
      dashboard.setShovel(shovelState, static_cast<int>(armPosition + 0.5f));

      bool hornTouched = isHornButtonTouched();
      dashboard.setHornPressed(hornTouched);
      dashboard.setSpeed(currentThrottle);
      if (spinning || slowSpinning) {
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
                    M5.Touch.getDetail().isPressed() || currentThrottle != 0 || stickX != 0 || stickY != 0;
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
