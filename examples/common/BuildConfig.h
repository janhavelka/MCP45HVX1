/**
 * @file BuildConfig.h
 * @brief Build-time configuration for examples.
 *
 * Define LOG_LEVEL via build_flags in platformio.ini, or accept defaults here.
 */

#pragma once

/// @brief Log level: 0=off, 1=error, 2=info, 3=debug, 4=trace
#ifndef LOG_LEVEL
#define LOG_LEVEL 2
#endif

/// @brief Compile-time master switch for ANSI color in example CLIs.
#ifndef MCP45HVX1_CLI_ENABLE_COLOR
#define MCP45HVX1_CLI_ENABLE_COLOR 1
#endif

/// @brief Runtime default for ANSI color when color support is compiled in.
#ifndef MCP45HVX1_CLI_COLOR_DEFAULT
#define MCP45HVX1_CLI_COLOR_DEFAULT 1
#endif
