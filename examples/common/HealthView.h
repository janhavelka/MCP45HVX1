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

inline void printSummary(const MCP45HVX1::MCP45HVX1& dev, Print& out = Serial) {
  const uint32_t totalSuccess = dev.totalSuccess();
  const uint32_t totalFailures = dev.totalFailures();
  const uint32_t total = totalSuccess + totalFailures;
  const float pct = (total > 0U)
                        ? (100.0f * static_cast<float>(totalSuccess) /
                           static_cast<float>(total))
                        : 0.0f;
  out.printf("Health: state=%s%s%s online=%s%s%s consec=%s%u%s ok=%s%lu%s fail=%s%lu%s rate=%s%.1f%%%s lastErr=%u\n",
             LOG_COLOR_STATE(dev.isOnline(), dev.consecutiveFailures()),
             stateName(dev.state()),
             LOG_COLOR_RESET,
             boolColor(dev.isOnline()),
             dev.isOnline() ? "true" : "false",
             LOG_COLOR_RESET,
             failureColor(dev.consecutiveFailures()),
             static_cast<unsigned>(dev.consecutiveFailures()),
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
             static_cast<unsigned>(dev.lastError().code));
}

}  // namespace health_view
