#include <M5Unified.h>
#include <Toio.h>

static auto &dsp = M5.Display;
LGFX_Sprite canvas(&dsp);
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

std::vector<ToioCore*> toiocore_list;

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
  dsp.setTextSize(2);
  canvas.createSprite(dsp.width(), 40);
  canvas.setTextSize(2);
  state = AppState::SCANNING;
  showStatus("SCANNING", "Searching toio...");
}

void loop(void)
{
  M5.update();
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
      if (M5.Touch.getCount() == 0) {
        toiocore->drive(0, 0);
        showStatus("RUNNING", "Touch to drive");
        break;
      }
      auto imu_update = M5.Imu.update();
      auto data = M5.Imu.getImuData();
      int8_t throttle = 50;
      int8_t steering = (int8_t)(-data.accel.x * 25);
      toiocore->drive(throttle, steering);
      showStatus("RUNNING", "");
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
