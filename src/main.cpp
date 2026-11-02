#include <M5Unified.h>
#include <Toio.h>
#include "GpioButton.hpp"
#include "Joystick.hpp"

static auto &dsp = M5.Display;
LGFX_Sprite canvas(&dsp);

GpioButton buttonBlue(8);
GpioButton buttonRed(9);
Joystick joystick;

Toio toio;
ToioCore* toiocore = nullptr;

// State machine
enum class AppState {
  INIT,
  SCANNING,
  CONNECTING,
  CONNECTED,
  RUNNING,
  ERROR
};
AppState state = AppState::INIT;

int8_t currentThrottle = 0;
std::vector<ToioCore*> toiocore_list;

// 引数で渡した一定単位ごとに近づける
void updateCurrentThrottle(int8_t targetThrottle, int8_t step = 1) {
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

void showStatus(const char* status, const char* detail = nullptr) {
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
  joystick.begin(joyCfg);

  dsp.setTextSize(2);
  canvas.createSprite(dsp.width(), 40);
  canvas.setTextSize(2);
  state = AppState::SCANNING;
  showStatus("SCANNING", "Searching toio...");
}

void runForward() {
  auto data = M5.Imu.getImuData();
  updateCurrentThrottle(70, 30);
  int8_t steering = (int8_t)(-data.accel.x * 40);
  toiocore->drive(currentThrottle, steering);
  showStatus("RUNNING", "");
}

void runBackward() {
  auto data = M5.Imu.getImuData();
  updateCurrentThrottle(-50, 30);
  int8_t steering = (int8_t)(-data.accel.x * 40);
  toiocore->drive(currentThrottle, steering);
  showStatus("RUNNING", "Backward");
}

// 左右のモーターを逆回転させてその場で回転する
void spinInPlace() {
  currentThrottle = 0;
  toiocore->controlMotor(true, 100, false, 100);
  showStatus("RUNNING", "Spin");
}

void loop(void)
{
  M5.update();
  buttonBlue.update();
  buttonRed.update();
  joystick.update();
  toio.loop();

  switch (state) {
    case AppState::SCANNING:
      toiocore_list = toio.scan(3); // 3 seconds scan
      if (!toiocore_list.empty()) {
        toiocore = toiocore_list.at(0);
        state = AppState::CONNECTING;
        showStatus("CONNECTING", "Connecting to toio...");
      } else {
        state = AppState::ERROR;
        showStatus("ERROR", "No toio found");
      }
      break;
    case AppState::CONNECTING:
      if (toiocore && toiocore->connect()) {
        state = AppState::CONNECTED;
        showStatus("CONNECTED", "toio connected");
      } else {
        state = AppState::ERROR;
        showStatus("ERROR", "Connect failed");
      }
      break;
    case AppState::CONNECTED:
      state = AppState::RUNNING;
      break;
    case AppState::RUNNING: {
      if (!toiocore || !toiocore->isConnected()) {
        state = AppState::ERROR;
        showStatus("ERROR", "Disconnected");
        break;
      }
      if (joystick.isPressed()) {
        spinInPlace();
        break;
      }
      if (!buttonBlue.isPressed() && !buttonRed.isPressed()) {
        updateCurrentThrottle(0, 10);
        toiocore->drive(currentThrottle, 0);
        showStatus("RUNNING", "Push Button to drive");
        break;
      }

      M5.Imu.update();
      if (buttonBlue.isPressed()) {
        runForward();
      } else if (buttonRed.isPressed()) {
        runBackward();
      }
      
      break;
    }
    case AppState::ERROR:
      // Do nothing, just show error
      break;
    case AppState::INIT:
    default:
      showStatus("INIT", "");
      break;
  }
}
