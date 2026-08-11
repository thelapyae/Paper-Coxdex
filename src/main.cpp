#include <Arduino.h>
#include <ArduinoJson.h>
#include <M5Unified.h>

#include <BLE2902.h>
#include <BLECharacteristic.h>
#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLEServer.h>
#include <BLESecurity.h>

#include <array>
#include <cmath>
#include <string>

#include "codex_protocol.h"

namespace {

BLEHIDDevice* hid = nullptr;
BLECharacteristic* rpcInput = nullptr;
BLECharacteristic* rpcOutput = nullptr;
BLECharacteristic* keyboardInput = nullptr;

bool bleConnected = false;
bool screenDirty = true;
bool fullRefreshPending = true;
bool rpcReady = false;
String rpcBuffer;
String pendingRpc;
portMUX_TYPE rpcMux = portMUX_INITIALIZER_UNLOCKED;

std::array<codex::AgentSlot, codex::kAgentCount> agents;
codex::AgentState pendingBuzz = codex::AgentState::off;

int pressedButton = -1;
uint32_t lastStatusDraw = 0;
constexpr int kThinkingSliderControl = 100;

bool approvalPopup = false;
int approvalAgent = -1;
bool dismissPopupOnRelease = false;

constexpr int kThinkingLevelCount = 6;
constexpr const char* kThinkingLabels[kThinkingLevelCount] = {
    "LOW", "MED", "HIGH", "XHIGH", "MAX", "ULTRA",
};
int thinkingLevel = 2;
bool thinkingSynced = false;
bool modelPickerOpen = false;
int modelPickerSliderLevel = 2;

bool imuAvailable = false;
uint8_t currentRotation = 1;
uint8_t candidateRotation = 1;
uint32_t candidateRotationSince = 0;
uint32_t lastOrientationSample = 0;

struct Rect {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;

  bool contains(int16_t px, int16_t py) const {
    return px >= x && px < x + w && py >= y && py < y + h;
  }
};

std::array<Rect, codex::kAgentCount> agentRects;
std::array<Rect, codex::kAgentCount> actionRects;
Rect thinkingRect;
Rect popupAcceptRect;
Rect popupRejectRect;

void requestScreenDraw() {
  screenDirty = true;
}

void playNotification(codex::AgentState state) {
  switch (state) {
    case codex::AgentState::unread:
      M5.Speaker.tone(1800, 90);
      delay(120);
      M5.Speaker.tone(2400, 130);
      break;
    case codex::AgentState::needsInput:
      M5.Speaker.tone(1200, 100);
      delay(135);
      M5.Speaker.tone(1200, 100);
      delay(135);
      M5.Speaker.tone(1600, 150);
      break;
    case codex::AgentState::error:
      M5.Speaker.tone(420, 220);
      delay(260);
      M5.Speaker.tone(320, 300);
      break;
    default:
      M5.Speaker.tone(2200, 35);
      break;
  }
}

void drawCentered(const char* text, const Rect& rect, int yOffset = 0) {
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString(text, rect.x + rect.w / 2,
                        rect.y + rect.h / 2 + yOffset);
}

void layoutButtons() {
  const int width = M5.Display.width();
  const int height = M5.Display.height();
  const int margin = 16;
  const int gap = 10;
  const int headerH = 64;

  if (width >= height) {
    const int agentY = headerH + 14;
    const int agentH = 170;
    const int agentW = (width - margin * 2 - gap * 5) / 6;
    for (size_t i = 0; i < codex::kAgentCount; ++i) {
      agentRects[i] = {
          static_cast<int16_t>(margin + i * (agentW + gap)),
          static_cast<int16_t>(agentY), static_cast<int16_t>(agentW),
          static_cast<int16_t>(agentH)};
    }

    const int controlY = agentY + agentH + 14;
    const int controlH = 80;
    const int modelW = 160;
    actionRects[0] = {static_cast<int16_t>(margin),
                      static_cast<int16_t>(controlY),
                      static_cast<int16_t>(modelW),
                      static_cast<int16_t>(controlH)};
    thinkingRect = {static_cast<int16_t>(margin + modelW + gap),
                    static_cast<int16_t>(controlY),
                    static_cast<int16_t>(width - margin * 2 - modelW - gap),
                    static_cast<int16_t>(controlH)};

    const int actionY = controlY + controlH + 14;
    const int actionH = height - actionY - 14;
    const int unitW = (width - margin * 2 - gap * 4) / 6;
    int actionX = margin;
    for (size_t i = 1; i < codex::kAgentCount; ++i) {
      const int actionW = (i == 4) ? unitW * 2 : unitW;
      actionRects[i] = {static_cast<int16_t>(actionX),
                        static_cast<int16_t>(actionY),
                        static_cast<int16_t>(actionW),
                        static_cast<int16_t>(actionH)};
      actionX += actionW + gap;
    }

    const int popupY = height / 2 + 24;
    const int popupW = (width - margin * 3) / 2;
    const int popupH = height - popupY - 18;
    popupAcceptRect = {static_cast<int16_t>(margin),
                       static_cast<int16_t>(popupY),
                       static_cast<int16_t>(popupW),
                       static_cast<int16_t>(popupH)};
    popupRejectRect = {static_cast<int16_t>(margin * 2 + popupW),
                       static_cast<int16_t>(popupY),
                       static_cast<int16_t>(popupW),
                       static_cast<int16_t>(popupH)};
    return;
  }

  const int agentY = headerH + 14;
  const int agentGap = 10;
  const int agentW = (width - margin * 2 - agentGap) / 2;
  const int agentH = 144;
  for (size_t i = 0; i < codex::kAgentCount; ++i) {
    const int row = i / 2;
    const int col = i % 2;
    agentRects[i] = {
        static_cast<int16_t>(margin + col * (agentW + agentGap)),
        static_cast<int16_t>(agentY + row * (agentH + agentGap)),
        static_cast<int16_t>(agentW), static_cast<int16_t>(agentH)};
  }

  const int controlY = agentY + 3 * agentH + 2 * agentGap + 14;
  const int controlH = 90;
  const int modelW = 150;
  actionRects[0] = {static_cast<int16_t>(margin),
                    static_cast<int16_t>(controlY),
                    static_cast<int16_t>(modelW),
                    static_cast<int16_t>(controlH)};
  thinkingRect = {static_cast<int16_t>(margin + modelW + gap),
                  static_cast<int16_t>(controlY),
                  static_cast<int16_t>(width - margin * 2 - modelW - gap),
                  static_cast<int16_t>(controlH)};

  const int firstActionY = controlY + controlH + 14;
  const int firstActionH = 136;
  const int firstActionW = (width - margin * 2 - gap * 2) / 3;
  for (size_t i = 1; i <= 3; ++i) {
    actionRects[i] = {
        static_cast<int16_t>(margin + (i - 1) * (firstActionW + gap)),
        static_cast<int16_t>(firstActionY),
        static_cast<int16_t>(firstActionW),
        static_cast<int16_t>(firstActionH)};
  }

  const int secondActionY = firstActionY + firstActionH + 14;
  const int secondActionH = height - secondActionY - 14;
  const int secondUnitW = (width - margin * 2 - gap) / 3;
  actionRects[4] = {static_cast<int16_t>(margin),
                    static_cast<int16_t>(secondActionY),
                    static_cast<int16_t>(secondUnitW * 2),
                    static_cast<int16_t>(secondActionH)};
  actionRects[5] = {static_cast<int16_t>(margin + secondUnitW * 2 + gap),
                    static_cast<int16_t>(secondActionY),
                    static_cast<int16_t>(secondUnitW),
                    static_cast<int16_t>(secondActionH)};

  const int popupW = width - margin * 2;
  const int popupH = 150;
  const int popupY = height / 2 + 12;
  popupAcceptRect = {static_cast<int16_t>(margin),
                     static_cast<int16_t>(popupY),
                     static_cast<int16_t>(popupW),
                     static_cast<int16_t>(popupH)};
  popupRejectRect = {static_cast<int16_t>(margin),
                     static_cast<int16_t>(popupY + popupH + 18),
                     static_cast<int16_t>(popupW),
                     static_cast<int16_t>(popupH)};
}

void drawStatusBar() {
  M5.Display.fillRect(0, 0, M5.Display.width(), 64, TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextDatum(middle_left);
  M5.Display.setTextSize(3);
  M5.Display.drawString("PAPER MICRO", 18, 32);

  M5.Display.setTextDatum(middle_right);
  M5.Display.setTextSize(2);
  String status = bleConnected ? "LIVE" : "PAIRING";
  int battery = M5.Power.getBatteryLevel();
  if (battery >= 0) status += "  " + String(battery) + "%";
  M5.Display.drawString(status, M5.Display.width() - 18, 32);
}

void drawKeycap(const Rect& r, bool inverted, bool pressed, int border = 3) {
  const int shift = pressed ? 4 : 0;
  const int shadow = pressed ? 1 : 5;
  const int x = r.x + shift;
  const int y = r.y + shift;
  const int w = r.w - shift;
  const int h = r.h - shift;
  const uint16_t fill = inverted ? TFT_BLACK : TFT_WHITE;
  const uint16_t ink = inverted ? TFT_WHITE : TFT_BLACK;

  M5.Display.fillRoundRect(r.x + shadow, r.y + shadow, r.w, r.h, 12,
                           TFT_BLACK);
  M5.Display.fillRoundRect(x, y, w, h, 12, fill);
  for (int i = 0; i < border; ++i) {
    M5.Display.drawRoundRect(x + i, y + i, w - i * 2, h - i * 2, 12,
                             ink);
  }
}

void drawAgent(size_t index) {
  const Rect& r = agentRects[index];
  const auto state = agents[index].state;
  const bool attention = state == codex::AgentState::working ||
                         state == codex::AgentState::needsInput ||
                         state == codex::AgentState::error;
  const bool pressed = pressedButton == static_cast<int>(index);
  const int border = state == codex::AgentState::unread ? 7 : 3;
  drawKeycap(r, attention, pressed, border);

  const int shift = pressed ? 4 : 0;
  const uint16_t fill = attention ? TFT_BLACK : TFT_WHITE;
  const uint16_t text = attention ? TFT_WHITE : TFT_BLACK;
  M5.Display.setTextColor(text, fill);

  char number[4];
  snprintf(number, sizeof(number), "%02u", static_cast<unsigned>(index + 1));
  M5.Display.setTextDatum(top_left);
  M5.Display.setTextSize(r.h >= 160 ? 4 : 3);
  M5.Display.drawString(number, r.x + 14 + shift, r.y + 12 + shift);

  const char* glyph = "--";
  if (state == codex::AgentState::idle) glyph = "+";
  if (state == codex::AgentState::working) glyph = "...";
  if (state == codex::AgentState::unread) glyph = "OK";
  if (state == codex::AgentState::needsInput) glyph = "!";
  if (state == codex::AgentState::error) glyph = "X";
  if (state == codex::AgentState::active) glyph = "*";
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(r.h >= 160 ? 4 : 3);
  M5.Display.drawString(glyph, r.x + r.w / 2 + shift,
                        r.y + r.h / 2 + 10 + shift);

  M5.Display.setTextSize(2);
  M5.Display.drawString(codex::stateLabel(state), r.x + r.w / 2 + shift,
                        r.y + r.h - 22 + shift);
}

void drawAction(size_t index) {
  const Rect& r = actionRects[index];
  const bool pressed =
      pressedButton == static_cast<int>(codex::kAgentCount + index);
  drawKeycap(r, pressed, pressed, 3);

  const int shift = pressed ? 4 : 0;
  const uint16_t fill = pressed ? TFT_BLACK : TFT_WHITE;
  const uint16_t text = pressed ? TFT_WHITE : TFT_BLACK;
  M5.Display.setTextColor(text, fill);
  M5.Display.setTextDatum(middle_center);
  const char* glyph =
      index == 0 && modelPickerOpen ? "OK" : codex::kActionGlyphs[index];
  const char* label =
      index == 0 && modelPickerOpen ? "SELECT" : codex::kActionLabels[index];
  M5.Display.setTextSize(index == 0 ? 3 : (index == 4 ? 4 : 5));
  M5.Display.drawString(glyph,
                        r.x + r.w / 2 + shift,
                        r.y + (index == 0 ? r.h / 2 - 12 : r.h * 35 / 100) +
                            shift);
  M5.Display.setTextSize(2);
  M5.Display.drawString(label,
                        r.x + r.w / 2 + shift,
                        r.y + (index == 0 ? r.h / 2 + 22 : r.h * 70 / 100) +
                            shift);
  if (index != 0 && r.h >= 120) {
    M5.Display.setTextSize(1);
    M5.Display.drawString(codex::kActionHints[index],
                          r.x + r.w / 2 + shift,
                          r.y + r.h - 18 + shift);
  }
}

void drawThinkingSlider() {
  const Rect& r = thinkingRect;
  const bool active = pressedButton == kThinkingSliderControl;
  M5.Display.fillRoundRect(r.x, r.y, r.w, r.h, 10,
                           active ? TFT_BLACK : TFT_WHITE);
  for (int i = 0; i < 3; ++i) {
    M5.Display.drawRoundRect(r.x + i, r.y + i, r.w - i * 2, r.h - i * 2,
                             10, active ? TFT_WHITE : TFT_BLACK);
  }

  const uint16_t fill = active ? TFT_BLACK : TFT_WHITE;
  const uint16_t text = active ? TFT_WHITE : TFT_BLACK;
  M5.Display.setTextColor(text, fill);
  M5.Display.setTextDatum(top_left);
  M5.Display.setTextSize(2);
  M5.Display.drawString(modelPickerOpen ? "CHOOSE MODEL" : "THINKING",
                        r.x + 14, r.y + 10);
  M5.Display.setTextDatum(top_right);
  M5.Display.drawString(modelPickerOpen
                            ? "PRESS MODEL"
                            : (thinkingSynced ? kThinkingLabels[thinkingLevel]
                                              : "TOUCH TO SET"),
                        r.x + r.w - 14, r.y + 10);

  const int trackLeft = r.x + 18;
  const int trackRight = r.x + r.w - 18;
  const int trackY = r.y + r.h - 23;
  M5.Display.fillRect(trackLeft, trackY - 2, trackRight - trackLeft, 5, text);
  for (int i = 0; i < kThinkingLevelCount; ++i) {
    const int x = trackLeft +
                  i * (trackRight - trackLeft) / (kThinkingLevelCount - 1);
    const bool selected = modelPickerOpen
                              ? i <= modelPickerSliderLevel
                              : thinkingSynced && i <= thinkingLevel;
    M5.Display.fillCircle(x, trackY, selected ? 8 : 5, text);
    if (!selected) M5.Display.fillCircle(x, trackY, 2, fill);
  }
}

void drawApprovalPopup() {
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.fillRect(0, 0, M5.Display.width(), 72, TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(M5.Display.width() >= M5.Display.height() ? 3 : 2);
  M5.Display.drawString("ACTION REQUIRED", M5.Display.width() / 2, 36);

  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.setTextSize(M5.Display.width() >= M5.Display.height() ? 6 : 5);
  char agentTitle[18];
  snprintf(agentTitle, sizeof(agentTitle), "AGENT %02d", approvalAgent + 1);
  const int titleY = M5.Display.width() >= M5.Display.height() ? 145 : 190;
  M5.Display.drawString(agentTitle, M5.Display.width() / 2, titleY);
  M5.Display.setTextSize(2);
  M5.Display.drawString("CODEX NEEDS YOUR DECISION", M5.Display.width() / 2,
                        titleY + 62);

  const bool acceptPressed =
      pressedButton == static_cast<int>(codex::kAgentCount + 1);
  const bool rejectPressed =
      pressedButton == static_cast<int>(codex::kAgentCount + 2);
  drawKeycap(popupAcceptRect, acceptPressed, acceptPressed, 5);
  drawKeycap(popupRejectRect, rejectPressed, rejectPressed, 5);

  auto drawPopupButton = [](const Rect& r, const char* glyph,
                            const char* label, bool pressed) {
    const int shift = pressed ? 4 : 0;
    M5.Display.setTextColor(pressed ? TFT_WHITE : TFT_BLACK,
                            pressed ? TFT_BLACK : TFT_WHITE);
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextSize(5);
    M5.Display.drawString(glyph, r.x + r.w / 2 + shift,
                          r.y + r.h * 38 / 100 + shift);
    M5.Display.setTextSize(3);
    M5.Display.drawString(label, r.x + r.w / 2 + shift,
                          r.y + r.h * 72 / 100 + shift);
  };
  drawPopupButton(popupAcceptRect, "OK", "ACCEPT", acceptPressed);
  drawPopupButton(popupRejectRect, "X", "REJECT", rejectPressed);
}

void drawScreen() {
  const bool fullRefresh = fullRefreshPending;
  if (fullRefresh) M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.startWrite();
  if (approvalPopup) {
    drawApprovalPopup();
  } else {
    M5.Display.fillScreen(TFT_WHITE);
    drawStatusBar();
    for (size_t i = 0; i < codex::kAgentCount; ++i) drawAgent(i);
    for (size_t i = 0; i < codex::kAgentCount; ++i) drawAction(i);
    drawThinkingSlider();
  }
  M5.Display.endWrite();
  if (fullRefresh) {
    M5.Display.setEpdMode(epd_mode_t::epd_fast);
    fullRefreshPending = false;
  }
  screenDirty = false;
  lastStatusDraw = millis();
}

void sendRpcText(const String& text) {
  if (!bleConnected || rpcInput == nullptr) return;

  String framed = text;
  if (!framed.endsWith("\n")) framed += '\n';

  size_t offset = 0;
  while (offset < framed.length()) {
    const size_t chunk =
        min(codex::kRpcChunkSize, framed.length() - offset);
    uint8_t packet[codex::kReportPayloadSize] = {};
    packet[0] = codex::kRpcChannel;
    packet[1] = static_cast<uint8_t>(chunk);
    memcpy(packet + 2, framed.c_str() + offset, chunk);
    rpcInput->setValue(packet, sizeof(packet));
    rpcInput->notify();
    offset += chunk;
    delay(8);
  }
}

void sendKeyEvent(const char* key, int action, int agent = -1) {
  JsonDocument doc;
  doc["m"] = "v.oai.hid";
  JsonObject params = doc["p"].to<JsonObject>();
  params["k"] = key;
  params["act"] = action;
  if (agent >= 0) params["ag"] = agent;
  String json;
  serializeJson(doc, json);
  sendRpcText(json);
}

void sendKeyboardReport(uint8_t modifier, uint8_t key) {
  if (!bleConnected || keyboardInput == nullptr) return;
  uint8_t report[8] = {};
  report[0] = modifier;
  report[2] = key;
  keyboardInput->setValue(report, sizeof(report));
  keyboardInput->notify();
}

void tapKeyboardKey(uint8_t modifier, uint8_t key, int pauseMs = 70) {
  sendKeyboardReport(modifier, key);
  delay(pauseMs);
  sendKeyboardReport(0, 0);
  delay(pauseMs);
}

void sendModelPickerShortcut(int action) {
  if (action != 1) return;
  if (!modelPickerOpen) {
    Serial.println("MODEL: open picker");
    tapKeyboardKey(0x03, 0x10, 90);  // Left Control + Shift + M.
    modelPickerOpen = true;
    modelPickerSliderLevel = kThinkingLevelCount / 2;
  } else {
    Serial.println("MODEL: confirm highlighted model");
    tapKeyboardKey(0x00, 0x28, 90);  // Enter.
    modelPickerOpen = false;
  }
  requestScreenDraw();
}

void sendThinkingStep(bool increase) {
  // Codex Micro maps counter-clockwise to higher reasoning and clockwise to
  // lower reasoning in its default reasoning encoder mode.
  sendKeyEvent(increase ? "ENC_CC" : "ENC_CW", 2);
  delay(22);
}

void setThinkingLevelFromTouch(int16_t x) {
  const int trackLeft = thinkingRect.x + 18;
  const int trackRight = thinkingRect.x + thinkingRect.w - 18;
  int target = (x - trackLeft) * kThinkingLevelCount /
               max(1, trackRight - trackLeft + 1);
  target = constrain(target, 0, kThinkingLevelCount - 1);

  if (modelPickerOpen) {
    const int delta = target - modelPickerSliderLevel;
    for (int i = 0; i < abs(delta); ++i) sendThinkingStep(delta > 0);
    modelPickerSliderLevel = target;
    requestScreenDraw();
    return;
  }

  if (thinkingSynced && target == thinkingLevel) return;

  if (!thinkingSynced) {
    // Codex does not report the current reasoning level to the device. First
    // drive it to the minimum, then move to the selected absolute notch.
    for (int i = 0; i < kThinkingLevelCount + 2; ++i) {
      sendThinkingStep(false);
    }
    for (int i = 0; i < target; ++i) sendThinkingStep(true);
    thinkingSynced = true;
  } else {
    const int delta = target - thinkingLevel;
    for (int i = 0; i < abs(delta); ++i) sendThinkingStep(delta > 0);
  }

  thinkingLevel = target;
  playNotification(codex::AgentState::active);
  requestScreenDraw();
}

void sendRpcResult(JsonVariantConst id, JsonVariantConst result) {
  JsonDocument response;
  response["id"] = id;
  response["result"].set(result);
  String json;
  serializeJson(response, json);
  sendRpcText(json);
}

void sendOk(JsonVariantConst id) {
  JsonDocument response;
  response["id"] = id;
  response["result"] = true;
  String json;
  serializeJson(response, json);
  sendRpcText(json);
}

void updateThreadLighting(JsonArrayConst updates) {
  bool changed = false;
  for (JsonObjectConst update : updates) {
    int id = update["id"] | -1;
    if (id < 0 || id >= static_cast<int>(codex::kAgentCount)) continue;

    auto& slot = agents[id];
    const auto before = slot.state;
    if (!update["c"].isNull()) slot.color = update["c"].as<uint32_t>();
    if (!update["b"].isNull()) slot.brightness = update["b"].as<float>();
    if (!update["e"].isNull()) slot.effect = update["e"].as<int>();
    slot.state =
        codex::stateFromLighting(slot.color, slot.brightness, slot.effect);

    if (slot.state != before) {
      changed = true;
      if (codex::shouldBuzz(before, slot.state)) pendingBuzz = slot.state;
      if (slot.state == codex::AgentState::needsInput) {
        approvalPopup = true;
        approvalAgent = id;
        pressedButton = -1;
      } else if (approvalPopup && approvalAgent == id &&
                 before == codex::AgentState::needsInput) {
        approvalPopup = false;
        approvalAgent = -1;
      }
    }
  }
  if (changed) requestScreenDraw();
}

void processRpc(const String& json) {
  JsonDocument request;
  DeserializationError error = deserializeJson(request, json);
  if (error) {
    Serial.printf("RPC parse error: %s\n", error.c_str());
    return;
  }

  const char* method = request["method"] | request["m"] | "";
  JsonVariantConst id = request["id"];
  JsonVariantConst params = request["params"];
  if (params.isNull()) params = request["p"];

  Serial.printf("RPC: %s\n", method);

  if (strcmp(method, "v.oai.thstatus") == 0) {
    updateThreadLighting(params.as<JsonArrayConst>());
    sendOk(id);
    return;
  }

  if (strcmp(method, "v.oai.rgbcfg") == 0 ||
      strcmp(method, "lights.preview") == 0) {
    sendOk(id);
    return;
  }

  if (strcmp(method, "sys.version") == 0) {
    JsonDocument result;
    result["version"] = "99.0.0";
    sendRpcResult(id, result.as<JsonVariantConst>());
    return;
  }

  if (strcmp(method, "device.status") == 0) {
    JsonDocument result;
    result["version"] = "99.0.0";
    result["profile_index"] = 0;
    result["layer_index"] = 0;
    int battery = M5.Power.getBatteryLevel();
    result["battery"] = battery < 0 ? 100 : battery;
    result["is_charging"] = digitalRead(5) == HIGH;
    sendRpcResult(id, result.as<JsonVariantConst>());
    return;
  }

  JsonDocument response;
  response["id"] = id;
  response["error"]["message"] = String("Unsupported method: ") + method;
  String responseJson;
  serializeJson(response, responseJson);
  sendRpcText(responseJson);
}

void emitButton(int button, int action) {
  if (button < 0) return;
  if (button < static_cast<int>(codex::kAgentCount)) {
    sendKeyEvent(codex::kAgentKeys[button], action, button);
    return;
  }
  int actionIndex = button - codex::kAgentCount;
  if (actionIndex >= 0 &&
      actionIndex < static_cast<int>(codex::kAgentCount)) {
    if (actionIndex == 0) {
      sendModelPickerShortcut(action);
      return;
    }
    sendKeyEvent(codex::kActionKeys[actionIndex], action);
  }
}

int hitTest(int16_t x, int16_t y) {
  if (approvalPopup) {
    if (popupAcceptRect.contains(x, y)) {
      return static_cast<int>(codex::kAgentCount + 1);
    }
    if (popupRejectRect.contains(x, y)) {
      return static_cast<int>(codex::kAgentCount + 2);
    }
    return -1;
  }
  for (size_t i = 0; i < codex::kAgentCount; ++i) {
    if (agentRects[i].contains(x, y)) return static_cast<int>(i);
  }
  for (size_t i = 0; i < codex::kAgentCount; ++i) {
    if (actionRects[i].contains(x, y)) {
      return static_cast<int>(codex::kAgentCount + i);
    }
  }
  return -1;
}

void handleTouch() {
  auto touch = M5.Touch.getDetail();
  if (touch.wasPressed()) {
    if (!approvalPopup && thinkingRect.contains(touch.x, touch.y)) {
      pressedButton = kThinkingSliderControl;
      setThinkingLevelFromTouch(touch.x);
      requestScreenDraw();
      return;
    }
    pressedButton = hitTest(touch.x, touch.y);
    if (pressedButton >= 0) {
      emitButton(pressedButton, 1);
      dismissPopupOnRelease = approvalPopup;
      playNotification(codex::AgentState::active);
      requestScreenDraw();
    }
  }
  if (pressedButton == kThinkingSliderControl && touch.isPressed()) {
    setThinkingLevelFromTouch(touch.x);
  }
  if (touch.wasReleased() && pressedButton >= 0) {
    if (pressedButton != kThinkingSliderControl) emitButton(pressedButton, 0);
    pressedButton = -1;
    if (dismissPopupOnRelease) {
      approvalPopup = false;
      approvalAgent = -1;
      dismissPopupOnRelease = false;
    }
    requestScreenDraw();
  }
}

uint8_t rotationFromGravity(float ax, float ay) {
  const float absX = fabsf(ax);
  const float absY = fabsf(ay);
  if (absX < 0.35f && absY < 0.35f) return currentRotation;
  // PaperS3's IMU axes are rotated 90 degrees from M5GFX's display rotation
  // indices. This mapping keeps text upright with USB at all four edges.
  if (absX > absY + 0.05f) return ax > 0 ? 0 : 2;
  if (absY > absX + 0.05f) return ay > 0 ? 1 : 3;
  return currentRotation;
}

void updateAutoRotation() {
  if (!imuAvailable || millis() - lastOrientationSample < 120) return;
  lastOrientationSample = millis();
  if (!M5.Imu.update()) return;

  const auto data = M5.Imu.getImuData();
  const uint8_t desired =
      rotationFromGravity(data.accel.x, data.accel.y);
  if (desired == currentRotation) {
    candidateRotation = currentRotation;
    candidateRotationSince = millis();
    return;
  }
  if (desired != candidateRotation) {
    candidateRotation = desired;
    candidateRotationSince = millis();
    return;
  }
  if (millis() - candidateRotationSince < 320) return;

  currentRotation = candidateRotation;
  M5.Display.setRotation(currentRotation);
  layoutButtons();
  pressedButton = -1;
  fullRefreshPending = true;
  requestScreenDraw();
  Serial.printf("Auto rotation: %u (ax=%.2f ay=%.2f)\n", currentRotation,
                data.accel.x, data.accel.y);
}

class ServerCallbacks final : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {
    bleConnected = true;
    requestScreenDraw();
    pendingBuzz = codex::AgentState::active;
    Serial.println("BLE connected");
  }

  void onDisconnect(BLEServer* server) override {
    bleConnected = false;
    requestScreenDraw();
    Serial.println("BLE disconnected; advertising again");
    delay(200);
    server->getAdvertising()->start();
  }
};

class RpcOutputCallbacks final : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* characteristic) override {
    std::string value = characteristic->getValue();
    if (value.size() < 2) return;

    size_t header = 0;
    // Some stacks include the report ID in the characteristic value; others
    // use it only to route the report. Accept both forms.
    if (static_cast<uint8_t>(value[0]) == codex::kReportId &&
        value.size() >= 3) {
      header = 1;
    }

    const uint8_t channel = static_cast<uint8_t>(value[header]);
    const uint8_t length = static_cast<uint8_t>(value[header + 1]);
    if (channel != codex::kRpcChannel ||
        value.size() < header + 2 + length) {
      return;
    }

    portENTER_CRITICAL(&rpcMux);
    for (size_t i = 0; i < length; ++i) {
      rpcBuffer += value[header + 2 + i];
    }

    JsonDocument probe;
    if (!deserializeJson(probe, rpcBuffer)) {
      pendingRpc = rpcBuffer;
      rpcBuffer = "";
      rpcReady = true;
    }
    portEXIT_CRITICAL(&rpcMux);
  }
};

ServerCallbacks serverCallbacks;
RpcOutputCallbacks rpcCallbacks;

void startBluetooth() {
  BLEDevice::init("Paper Micro");
  BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);

  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(&serverCallbacks);

  hid = new BLEHIDDevice(server);
  keyboardInput = hid->inputReport(codex::kKeyboardReportId);
  rpcInput = hid->inputReport(codex::kReportId);
  rpcOutput = hid->outputReport(codex::kReportId);
  rpcOutput->setCallbacks(&rpcCallbacks);

  hid->manufacturer()->setValue("Paper Micro");
  hid->pnp(0x02, codex::kBlePnpVendorId, codex::kBlePnpProductId, 0x0001);
  hid->hidInfo(0x00, 0x01);
  hid->reportMap(const_cast<uint8_t*>(codex::kHidReportMap),
                 sizeof(codex::kHidReportMap));
  hid->startServices();
  hid->setBatteryLevel(100);

  auto* security = new BLESecurity();
  security->setAuthenticationMode(ESP_LE_AUTH_BOND);
  security->setCapability(ESP_IO_CAP_NONE);

  BLEAdvertising* advertising = server->getAdvertising();
  advertising->setAppearance(0x03C0);  // Generic HID
  advertising->addServiceUUID(hid->hidService()->getUUID());
  advertising->setScanResponse(true);
  advertising->start();
  Serial.println("BLE advertising as Paper Micro");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(250);

  auto config = M5.config();
  config.internal_spk = true;
  config.internal_imu = true;
  M5.begin(config);
  M5.Display.setRotation(M5.Display.width() < M5.Display.height() ? 1 : 0);
  currentRotation = M5.Display.getRotation();
  candidateRotation = currentRotation;
  candidateRotationSince = millis();
  imuAvailable = M5.Imu.getType() != m5::imu_none;
  M5.Display.setEpdMode(epd_mode_t::epd_fast);
  M5.Display.setTextFont(1);
  M5.Speaker.setVolume(80);
  pinMode(5, INPUT);

  layoutButtons();
  drawScreen();
  playNotification(codex::AgentState::active);
  startBluetooth();
}

void loop() {
  M5.update();
  updateAutoRotation();
  handleTouch();

  if (rpcReady) {
    String json;
    portENTER_CRITICAL(&rpcMux);
    json = pendingRpc;
    pendingRpc = "";
    rpcReady = false;
    portEXIT_CRITICAL(&rpcMux);
    processRpc(json);
  }

  if (pendingBuzz != codex::AgentState::off) {
    auto state = pendingBuzz;
    pendingBuzz = codex::AgentState::off;
    playNotification(state);
  }

  if (screenDirty) drawScreen();

  // Refresh battery text occasionally without repeatedly refreshing the EPD.
  if (millis() - lastStatusDraw > 300000) requestScreenDraw();
  delay(8);
}
