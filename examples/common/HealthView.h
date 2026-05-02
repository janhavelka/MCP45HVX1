/**
 * @file HealthView.h
 * @brief Compact health-state formatter for MCP45HVX1 examples.
 */

#pragma once

#include <Arduino.h>

#include "MCP45HVX1/MCP45HVX1.h"

namespace health_view {

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
  out.printf("state=%s failures=%u totalFail=%lu totalOk=%lu lastErr=%u\n",
             stateName(dev.state()),
             static_cast<unsigned>(dev.consecutiveFailures()),
             static_cast<unsigned long>(dev.totalFailures()),
             static_cast<unsigned long>(dev.totalSuccess()),
             static_cast<unsigned>(dev.lastError().code));
}

}  // namespace health_view

