/**
 * @file HealthView.h
 * @brief Compact health-state formatter for MCP45HVX1 examples.
 */

#pragma once

#include <Arduino.h>

#include "MCP45HVX1/MCP45HVX1.h"
#include "examples/common/Log.h"

namespace health_view {

inline const char* boolColor(bool value) {
  return value ? LOG_COLOR_GREEN : LOG_COLOR_RED;
}

inline const char* failureColor(uint32_t failures) {
  if (failures == 0U) {
    return LOG_COLOR_GREEN;
  }
  if (failures < 3U) {
    return LOG_COLOR_YELLOW;
  }
  return LOG_COLOR_RED;
}

inline const char* successColor(uint32_t successes) {
  return (successes > 0U) ? LOG_COLOR_GREEN : LOG_COLOR_GRAY;
}

inline const char* successRateColor(float pct) {
  if (pct >= 99.9f) {
    return LOG_COLOR_GREEN;
  }
  if (pct >= 80.0f) {
    return LOG_COLOR_YELLOW;
  }
  return LOG_COLOR_RED;
}

inline const char* stateName(MCP45HVX1::DriverState state) {
  switch (state) {
    case MCP45HVX1::DriverState::UNINIT:
      return "UNINIT";
    case MCP45HVX1::DriverState::READY:
      return "READY";
    case MCP45HVX1::DriverState::DEGRADED:
      return "DEGRADED";
    case MCP45HVX1::DriverState::OFFLINE:
      return "OFFLINE";
    default:
      return "?";
  }
}

inline const char* variantName(MCP45HVX1::Resolution resolution) {
  return resolution == MCP45HVX1::Resolution::Bits7 ? "HV31" : "HV51";
}

inline const char* errName(MCP45HVX1::Err code) {
  switch (code) {
    case MCP45HVX1::Err::OK:
      return "OK";
    case MCP45HVX1::Err::IN_PROGRESS:
      return "IN_PROGRESS";
    case MCP45HVX1::Err::INVALID_CONFIG:
      return "INVALID_CONFIG";
    case MCP45HVX1::Err::NOT_INITIALIZED:
      return "NOT_INITIALIZED";
    case MCP45HVX1::Err::INVALID_PARAM:
      return "INVALID_PARAM";
    case MCP45HVX1::Err::I2C_ERROR:
      return "I2C_ERROR";
    case MCP45HVX1::Err::TIMEOUT:
      return "TIMEOUT";
    case MCP45HVX1::Err::I2C_TIMEOUT:
      return "I2C_TIMEOUT";
    case MCP45HVX1::Err::I2C_NACK_ADDR:
      return "I2C_NACK_ADDR";
    case MCP45HVX1::Err::I2C_NACK_DATA:
      return "I2C_NACK_DATA";
    case MCP45HVX1::Err::I2C_BUS:
      return "I2C_BUS";
    case MCP45HVX1::Err::DEVICE_NOT_FOUND:
      return "DEVICE_NOT_FOUND";
    case MCP45HVX1::Err::REGISTER_MISMATCH:
      return "REGISTER_MISMATCH";
    case MCP45HVX1::Err::BUSY:
      return "BUSY";
    case MCP45HVX1::Err::UNSUPPORTED:
      return "UNSUPPORTED";
    default:
      return "UNKNOWN";
  }
}

inline void printSummary(const MCP45HVX1::MCP45HVX1& dev, Print& out = Serial) {
  MCP45HVX1::SettingsSnapshot snap;
  (void)dev.getSettings(snap);
  const MCP45HVX1::DeviceInfo info = dev.getDeviceInfo();
  const uint32_t totalSuccess = dev.totalSuccess();
  const uint32_t totalFailures = dev.totalFailures();
  const uint32_t total = totalSuccess + totalFailures;
  const float pct = (total > 0U)
                        ? (100.0f * static_cast<float>(totalSuccess) /
                           static_cast<float>(total))
                        : 0.0f;
  const bool dirty = !snap.cachedWiperKnown || !snap.cachedTconKnown || !dev.isOnline();
  out.printf("MCP45HVX1 state: %s%s%s online=%s%s%s addr=0x%02X variant=%s ",
             LOG_COLOR_STATE(dev.isOnline(), dev.consecutiveFailures()),
             stateName(dev.state()),
             LOG_COLOR_RESET,
             boolColor(dev.isOnline()),
             dev.isOnline() ? "yes" : "no",
             LOG_COLOR_RESET,
             info.i2cAddress,
             variantName(info.resolution));
  if (snap.cachedWiperKnown) {
    out.printf("wiper=0x%02X ", snap.cachedWiper);
  } else {
    out.printf("wiper=unknown ");
  }
  if (snap.cachedTconKnown) {
    out.printf("tcon=0x%02X ", snap.cachedTcon);
  } else {
    out.printf("tcon=unknown ");
  }
  out.printf("failures=%s%u%s dirty=%s%s%s ok=%s%lu%s fail=%s%lu%s rate=%s%.1f%%%s lastErr=%s%s%s(%u)\n",
             failureColor(dev.consecutiveFailures()),
             static_cast<unsigned>(dev.consecutiveFailures()),
             LOG_COLOR_RESET,
             dirty ? LOG_COLOR_YELLOW : LOG_COLOR_GREEN,
             dirty ? "yes" : "no",
             LOG_COLOR_RESET,
             successColor(totalSuccess),
             static_cast<unsigned long>(totalSuccess),
             LOG_COLOR_RESET,
             failureColor(totalFailures),
             static_cast<unsigned long>(totalFailures),
             LOG_COLOR_RESET,
             successRateColor(pct),
             static_cast<double>(pct),
             LOG_COLOR_RESET,
             dev.lastError().ok() ? LOG_COLOR_GREEN : LOG_COLOR_RED,
             errName(dev.lastError().code),
             LOG_COLOR_RESET,
             static_cast<unsigned>(dev.lastError().code));
}

}  // namespace health_view
