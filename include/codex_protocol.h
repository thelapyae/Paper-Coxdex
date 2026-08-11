#pragma once

#include <Arduino.h>

namespace codex {

constexpr uint16_t kVendorId = 0x303A;
constexpr uint16_t kProductId = 0x8360;
// ESP32 BLEDevice::pnp() serializes uint16_t values in big-endian order while
// macOS interprets the Bluetooth PnP characteristic as little-endian.
constexpr uint16_t kBlePnpVendorId = 0x3A30;
constexpr uint16_t kBlePnpProductId = 0x6083;
constexpr uint8_t kKeyboardReportId = 1;
constexpr uint8_t kReportId = 6;
constexpr uint8_t kRpcChannel = 2;
constexpr size_t kReportPayloadSize = 63;
constexpr size_t kRpcChunkSize = 61;
constexpr size_t kAgentCount = 6;

// Codex desktop currently identifies agent keys with AG00 through AG05.
constexpr const char* kAgentKeys[kAgentCount] = {
    "AG00", "AG01", "AG02", "AG03", "AG04", "AG05",
};

// These are the six default command positions on Codex Micro. ACT10 and ACT11
// form one wide voice key, so the PaperS3 surface emits ACT10 for that button.
constexpr const char* kActionKeys[kAgentCount] = {
    "ACT06", "ACT07", "ACT08", "ACT09", "ACT10", "ACT12",
};

constexpr const char* kActionLabels[kAgentCount] = {
    "MODEL", "ACCEPT", "REJECT", "NEW", "VOICE", "CODEX",
};

constexpr const char* kActionGlyphs[kAgentCount] = {
    "AI", "OK", "X", "+", "MIC", "C",
};

constexpr const char* kActionHints[kAgentCount] = {
    "CHOOSE", "APPROVE", "DECLINE", "NEW CHAT", "HOLD TO TALK", "SEND",
};

// Vendor-defined HID collection, report ID 6, 63 bytes in each direction.
// macOS exposes this as usage page 0xFF00, which Codex desktop watches.
constexpr uint8_t kHidReportMap[] = {
    // Standard keyboard collection. Used only for Codex's native model-picker
    // shortcut (Ctrl+Shift+M); normal controls remain on the vendor channel.
    0x05, 0x01,                    // Usage Page (Generic Desktop)
    0x09, 0x06,                    // Usage (Keyboard)
    0xA1, 0x01,                    // Collection (Application)
    0x85, kKeyboardReportId,       //   Report ID (1)
    0x05, 0x07,                    //   Usage Page (Keyboard)
    0x19, 0xE0,                    //   Usage Minimum (Left Control)
    0x29, 0xE7,                    //   Usage Maximum (Right GUI)
    0x15, 0x00,                    //   Logical Minimum (0)
    0x25, 0x01,                    //   Logical Maximum (1)
    0x75, 0x01,                    //   Report Size (1)
    0x95, 0x08,                    //   Report Count (8)
    0x81, 0x02,                    //   Input (Data, Variable, Absolute)
    0x95, 0x01,                    //   Report Count (1)
    0x75, 0x08,                    //   Report Size (8)
    0x81, 0x01,                    //   Input (Constant)
    0x95, 0x06,                    //   Report Count (6)
    0x75, 0x08,                    //   Report Size (8)
    0x15, 0x00,                    //   Logical Minimum (0)
    0x25, 0x65,                    //   Logical Maximum (101)
    0x05, 0x07,                    //   Usage Page (Keyboard)
    0x19, 0x00,                    //   Usage Minimum (Reserved)
    0x29, 0x65,                    //   Usage Maximum (Keyboard Application)
    0x81, 0x00,                    //   Input (Data, Array)
    0xC0,                          // End Collection

    0x06, 0x00, 0xFF,        // Usage Page (Vendor Defined 0xFF00)
    0x09, 0x01,              // Usage (1)
    0xA1, 0x01,              // Collection (Application)
    0x85, kReportId,         //   Report ID (6)
    0x09, 0x01,              //   Usage (1)
    0x15, 0x00,              //   Logical Minimum (0)
    0x26, 0xFF, 0x00,        //   Logical Maximum (255)
    0x75, 0x08,              //   Report Size (8)
    0x95, 0x3F,              //   Report Count (63)
    0x81, 0x02,              //   Input (Data, Variable, Absolute)
    0x09, 0x01,              //   Usage (1)
    0x91, 0x02,              //   Output (Data, Variable, Absolute)
    0xC0,                    // End Collection
};

enum class AgentState : uint8_t {
  off,
  idle,
  working,
  unread,
  needsInput,
  error,
  active,
};

struct AgentSlot {
  uint32_t color = 0;
  float brightness = 0;
  int effect = 0;
  AgentState state = AgentState::off;
};

inline AgentState stateFromLighting(uint32_t color, float brightness, int effect) {
  if (brightness <= 0.001f || color == 0) return AgentState::off;

  switch (color) {
    case 0xFFFFFF: return AgentState::idle;
    case 0x304FFE: return AgentState::working;
    case 0x00FF4C: return AgentState::unread;
    case 0xFF6D00: return AgentState::needsInput;
    case 0xFF0033: return AgentState::error;
    default: break;
  }

  return effect == 2 ? AgentState::working : AgentState::active;
}

inline const char* stateLabel(AgentState state) {
  switch (state) {
    case AgentState::off: return "OFF";
    case AgentState::idle: return "IDLE";
    case AgentState::working: return "THINKING";
    case AgentState::unread: return "COMPLETE";
    case AgentState::needsInput: return "NEEDS INPUT";
    case AgentState::error: return "ERROR";
    case AgentState::active: return "ACTIVE";
  }
  return "UNKNOWN";
}

inline bool shouldBuzz(AgentState before, AgentState after) {
  if (before == after) return false;
  return after == AgentState::unread || after == AgentState::needsInput ||
         after == AgentState::error;
}

}  // namespace codex
