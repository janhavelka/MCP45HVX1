/**
 * @file HealthDiag.h
 * @brief Detailed health/settings printer for MCP45HVX1 examples.
 */

#pragma once

#include <Arduino.h>

#include "MCP45HVX1/MCP45HVX1.h"
#include "examples/common/HealthView.h"

namespace health_diag {

inline void printSettings(const MCP45HVX1::MCP45HVX1& dev, Print& out = Serial) {
  const MCP45HVX1::SettingsSnapshot s = dev.getSettings();
  out.printf("Driver: %s initialized=%s online=%s\n",
             health_view::stateName(s.state),
             s.initialized ? "yes" : "no",
             dev.isOnline() ? "yes" : "no");
  out.printf("I2C: addr=0x%02X timeout=%lu ms resolution=%u-bit\n",
             s.config.i2cAddress,
             static_cast<unsigned long>(s.config.i2cTimeoutMs),
             static_cast<unsigned>(s.config.resolution));
  out.printf("Health: consecutive=%u totalFail=%lu totalOk=%lu lastErr=%u detail=%ld\n",
             static_cast<unsigned>(s.consecutiveFailures),
             static_cast<unsigned long>(s.totalFailures),
             static_cast<unsigned long>(s.totalSuccess),
             static_cast<unsigned>(s.lastError.code),
             static_cast<long>(s.lastError.detail));
  out.printf("Cache: wiper=%s 0x%02X tcon=%s 0x%02X pointer=%s 0x%02X\n",
             s.cachedWiperKnown ? "known" : "unknown",
             s.cachedWiper,
             s.cachedTconKnown ? "known" : "unknown",
             s.cachedTcon,
             s.addressPointerKnown ? "known" : "unknown",
             s.addressPointer);
}

}  // namespace health_diag

