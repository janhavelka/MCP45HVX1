/**
 * @file HealthDiag.h
 * @brief Detailed health/settings printer for MCP45HVX1 examples.
 */

#pragma once

#include <Arduino.h>

#include "MCP45HVX1/MCP45HVX1.h"
#include "examples/common/HealthView.h"
#include "examples/common/Log.h"

namespace health_diag {

inline const char* errName(MCP45HVX1::Err code) {
  switch (code) {
    case MCP45HVX1::Err::OK:
      return "OK";
    case MCP45HVX1::Err::NOT_INITIALIZED:
      return "NOT_INITIALIZED";
    case MCP45HVX1::Err::INVALID_CONFIG:
      return "INVALID_CONFIG";
    case MCP45HVX1::Err::I2C_ERROR:
      return "I2C_ERROR";
    case MCP45HVX1::Err::TIMEOUT:
      return "TIMEOUT";
    case MCP45HVX1::Err::INVALID_PARAM:
      return "INVALID_PARAM";
    case MCP45HVX1::Err::DEVICE_NOT_FOUND:
      return "DEVICE_NOT_FOUND";
    case MCP45HVX1::Err::REGISTER_MISMATCH:
      return "REGISTER_MISMATCH";
    case MCP45HVX1::Err::BUSY:
      return "BUSY";
    case MCP45HVX1::Err::IN_PROGRESS:
      return "IN_PROGRESS";
    case MCP45HVX1::Err::UNSUPPORTED:
      return "UNSUPPORTED";
    case MCP45HVX1::Err::I2C_NACK_ADDR:
      return "I2C_NACK_ADDR";
    case MCP45HVX1::Err::I2C_NACK_DATA:
      return "I2C_NACK_DATA";
    case MCP45HVX1::Err::I2C_TIMEOUT:
      return "I2C_TIMEOUT";
    case MCP45HVX1::Err::I2C_BUS:
      return "I2C_BUS";
    default:
      return "UNKNOWN";
  }
}

inline const char* resistanceName(MCP45HVX1::ResistanceOption option) {
  switch (option) {
    case MCP45HVX1::ResistanceOption::R5K:
      return "5k";
    case MCP45HVX1::ResistanceOption::R10K:
      return "10k";
    case MCP45HVX1::ResistanceOption::R50K:
      return "50k";
    case MCP45HVX1::ResistanceOption::R100K:
      return "100k";
    default:
      return "?";
  }
}

inline void printSettings(const MCP45HVX1::MCP45HVX1& dev, Print& out = Serial) {
  MCP45HVX1::SettingsSnapshot s;
  const MCP45HVX1::Status st = dev.getSettings(s);
  if (!st.ok()) {
    out.println("=== Driver Health ===");
    out.printf("  Status: %s%s%s (code=%u, detail=%ld)\n",
               LOG_COLOR_RED,
               errName(st.code),
               LOG_COLOR_RESET,
               static_cast<unsigned>(st.code),
               static_cast<long>(st.detail));
    if (st.msg != nullptr && st.msg[0] != '\0') {
      out.printf("  Message: %s\n", st.msg);
    }
    return;
  }
  const MCP45HVX1::DeviceInfo info = dev.getDeviceInfo();
  const uint32_t total = s.totalSuccess + s.totalFailures;
  const float successRate =
      (total > 0U)
          ? (100.0f * static_cast<float>(s.totalSuccess) / static_cast<float>(total))
          : 0.0f;

  out.println("=== Driver Health ===");
  out.printf("  State: %s%s%s\n",
             LOG_COLOR_STATE(dev.isOnline(), s.consecutiveFailures),
             health_view::stateName(s.state),
             LOG_COLOR_RESET);
  out.printf("  Initialized: %s%s%s\n",
             health_view::boolColor(s.initialized),
             s.initialized ? "true" : "false",
             LOG_COLOR_RESET);
  out.printf("  Online: %s%s%s\n",
             health_view::boolColor(dev.isOnline()),
             dev.isOnline() ? "true" : "false",
             LOG_COLOR_RESET);
  out.printf("  Consecutive failures: %s%u%s\n",
             health_view::failureColor(s.consecutiveFailures),
             static_cast<unsigned>(s.consecutiveFailures),
             LOG_COLOR_RESET);
  out.printf("  Total success: %s%lu%s\n",
             health_view::successColor(s.totalSuccess),
             static_cast<unsigned long>(s.totalSuccess),
             LOG_COLOR_RESET);
  out.printf("  Total failures: %s%lu%s\n",
             health_view::failureColor(s.totalFailures),
             static_cast<unsigned long>(s.totalFailures),
             LOG_COLOR_RESET);
  out.printf("  Success rate: %s%.1f%%%s\n",
             health_view::successRateColor(successRate),
             static_cast<double>(successRate),
             LOG_COLOR_RESET);
  out.printf("  Last error: %s%s%s (detail=%ld)\n",
             s.lastError.ok() ? LOG_COLOR_GREEN : LOG_COLOR_RED,
             errName(s.lastError.code),
             LOG_COLOR_RESET,
             static_cast<long>(s.lastError.detail));
  if (!s.lastError.ok() && s.lastError.msg != nullptr && s.lastError.msg[0] != '\0') {
    out.printf("  Message: %s\n", s.lastError.msg);
  }

  out.println("=== I2C Config ===");
  out.printf("  Address: 0x%02X\n", s.config.i2cAddress);
  out.printf("  Timeout: %lu ms\n", static_cast<unsigned long>(s.config.i2cTimeoutMs));
  out.printf("  Resolution: %u-bit\n", static_cast<unsigned>(s.config.resolution));
  out.printf("  Alternate range: %s%s%s\n",
             s.config.allowAlternateAddressRange ? LOG_COLOR_YELLOW : LOG_COLOR_RESET,
             s.config.allowAlternateAddressRange ? "true" : "false",
             LOG_COLOR_RESET);
  out.printf("  Bus reset callback: %s%s%s\n",
             health_view::boolColor(s.config.busReset != nullptr),
             s.config.busReset != nullptr ? "true" : "false",
             LOG_COLOR_RESET);

  out.println("=== Device ===");
  out.printf("  Resistance: %s (%u ohm nominal)\n",
             resistanceName(info.resistance),
             static_cast<unsigned>(info.nominalResistanceOhms));
  out.printf("  Wiper step: %.3f ohm\n", static_cast<double>(info.nominalStepOhms));
  out.printf("  Max terminal current: %.1f mA\n",
             static_cast<double>(info.maxTerminalCurrentMilliAmps));
  out.printf("  Max code: 0x%02X\n", info.maxWiperCode);
  out.printf("  Default code: 0x%02X\n", info.defaultWiperCode);

  out.println("=== Cache ===");
  out.printf("  Wiper: %s 0x%02X\n",
             s.cachedWiperKnown ? "known" : "unknown",
             s.cachedWiper);
  out.printf("  TCON: %s 0x%02X\n",
             s.cachedTconKnown ? "known" : "unknown",
             s.cachedTcon);
  out.printf("  Address pointer: %s 0x%02X\n",
             s.addressPointerKnown ? "known" : "unknown",
             s.addressPointer);
}

}  // namespace health_diag
