// Host behavior tests compile the actual IDF input loop and parsers extracted
// by tools/test_contract_tools.py. SDK peripheral behavior is covered by HIL.
#include <cassert>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "MCP45HVX1/MCP45HVX1.h"

namespace {
std::string incoming;
size_t cursor = 0;
size_t readCalls = 0;
unsigned prompts = 0;
unsigned diagnostics = 0;
int emptyError = EAGAIN;
bool streamError = false;
std::vector<std::string> commands;

char* fakeFgets(char* buffer, int size, FILE*) {
  assert(size == 2);
  ++readCalls;
  if (streamError || cursor == incoming.size()) {
    errno = emptyError;
    streamError = true;
    return nullptr;
  }
  buffer[0] = incoming[cursor++];
  buffer[1] = '\0';
  return buffer;
}

void fakeClearerr(FILE*) { streamError = false; }

int fakePrintf(const char* format, ...) {
  if (std::strcmp(format, "> ") == 0) {
    ++prompts;
  } else {
    ++diagnostics;
  }
  return 0;
}

int fakePuts(const char*) {
  ++diagnostics;
  return 0;
}

void handleCommand(char* line) { commands.emplace_back(line); }

using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_ERR_TIMEOUT = 0x107;
constexpr esp_err_t ESP_ERR_INVALID_ARG = 0x102;
constexpr esp_err_t ESP_ERR_INVALID_STATE = 0x103;
constexpr esp_err_t ESP_ERR_NOT_FOUND = 0x105;
constexpr esp_err_t ESP_ERR_INVALID_RESPONSE = 0x108;

#define fgets fakeFgets
#define clearerr fakeClearerr
#define printf fakePrintf
#define puts fakePuts
#include "idf_console_under_test.h"
#undef puts
#undef printf
#undef clearerr
#undef fgets

void feed(ConsoleInput& input, const std::string& bytes) {
  incoming += bytes;
  do {
    const size_t before = readCalls;
    assert(pollConsole(input));
    assert(readCalls - before <= LINE_LEN);
  } while (cursor < incoming.size());
}

void testInput() {
  ConsoleInput input;
  feed(input, "ze");
  assert(commands.empty());
  assert(prompts == 0);
  for (unsigned i = 0; i < 8; ++i) {
    feed(input, "");
  }
  assert(commands.empty() && prompts == 0 && diagnostics == 0);
  feed(input, "ro\r");
  assert(commands == std::vector<std::string>{"zero"});
  assert(prompts == 1);
  feed(input, "\nhelp\nversion\r\n");
  assert((commands == std::vector<std::string>{"zero", "help", "version"}));
  assert(prompts == 3);
  feed(input, "\n \t\r\n");
  assert(commands.size() == 3);
  assert(prompts == 5);
  feed(input, "\bhelx\bp\x7f" "p\n");
  assert(commands.back() == "help");

  const size_t baseline = commands.size();
  feed(input, "zero " + std::string(LINE_LEN, ' '));
  assert(commands.size() == baseline && input.discard);
  feed(input, "\b\nhelp\n");
  assert(commands.size() == baseline + 1 && commands.back() == "help");
  assert(diagnostics == 1);
  feed(input, std::string("zero\0\n", 6));
  feed(input, "max\x01\n");
  assert(commands.size() == baseline + 1 && diagnostics == 3);

  // The longest complete line fits; a subsequent byte invalidates the whole line.
  feed(input, std::string(LINE_LEN - 1, 'a') + "\n");
  assert(commands.back().size() == LINE_LEN - 1);
  const size_t afterBoundary = commands.size();
  feed(input, std::string(LINE_LEN, 'a') + "\n");
  assert(commands.size() == afterBoundary && diagnostics == 4);

  emptyError = EINTR;
  feed(input, "mi");
  assert(!streamError);
  emptyError = EAGAIN;
  feed(input, "d\n");
  assert(commands.back() == "mid");

  feed(input, "max");
  const size_t beforeFatal = commands.size();
  emptyError = EIO;
  assert(!pollConsole(input));
  assert(commands.size() == beforeFatal);
  fakeClearerr(stdin);
  emptyError = 0;  // EOF also terminates; it must not spin or dispatch a prefix.
  assert(!pollConsole(input));
  assert(commands.size() == beforeFatal);
}

void testParsers() {
  uint32_t value = 99;
  assert(parseU32Bounded("0x3c", 255, &value) && value == 60);
  assert(!parseU32Bounded("60 junk", 255, &value));
  assert(!parseU32Bounded("60 7", 255, &value));
  assert(!parseU32Bounded("4294967296", 255, &value));
  float fraction = 0.0f;
  assert(parseFloatRangeArg("0.5", 0.0f, 1.0f, &fraction));
  assert(!parseFloatRangeArg("0.5 junk", 0.0f, 1.0f, &fraction));
  assert(!parseFloatRangeArg("nan", 0.0f, 1.0f, &fraction));
  assert(!parseFloatRangeArg("inf", 0.0f, 1.0f, &fraction));
  bool enabled = false;
  assert(parseBoolArg("on", &enabled) && enabled);
  assert(!parseBoolArg("on junk", &enabled));

  using MCP45HVX1::TerminalMode;
  const struct {
    const char* alias;
    TerminalMode expected;
  } modes[] = {
      {"pot", TerminalMode::Potentiometer},
      {"potentiometer", TerminalMode::Potentiometer},
      {"bw", TerminalMode::RheostatBToW}, {"b-w", TerminalMode::RheostatBToW},
      {"rheostat-bw", TerminalMode::RheostatBToW},
      {"rheostat_bw", TerminalMode::RheostatBToW},
      {"aw", TerminalMode::RheostatAToW}, {"a-w", TerminalMode::RheostatAToW},
      {"rheostat-aw", TerminalMode::RheostatAToW},
      {"rheostat_aw", TerminalMode::RheostatAToW},
      {"float", TerminalMode::WiperFloating},
      {"floating", TerminalMode::WiperFloating},
      {"wiper-floating", TerminalMode::WiperFloating},
      {"shutdown", TerminalMode::Shutdown},
  };
  for (const auto& item : modes) {
    TerminalMode parsed = TerminalMode::Custom;
    assert(parseTerminalMode(item.alias, &parsed) && parsed == item.expected);
    assert(!parseTerminalMode((std::string(item.alias) + " junk").c_str(), &parsed));
  }
  MCP45HVX1::Resolution resolution = MCP45HVX1::Resolution::Bits7;
  assert(parseResolutionText("hv51", &resolution));
  assert(!parseResolutionText("hv51 junk", &resolution));
  MCP45HVX1::ResistanceOption resistance{};
  assert(parseResistance("10k", &resistance));
  assert(!parseResistance("10k junk", &resistance));
}

void testI2cMapping() {
  using MCP45HVX1::Err;
  assert(mapI2c(ESP_OK, "test").ok());
  assert(mapI2c(ESP_ERR_TIMEOUT, "test").code == Err::I2C_TIMEOUT);
  assert(mapI2c(ESP_ERR_INVALID_ARG, "test").code == Err::INVALID_PARAM);
  const auto response = mapI2c(ESP_ERR_INVALID_RESPONSE, "test");
  assert(response.code == Err::I2C_ERROR && response.detail == ESP_ERR_INVALID_RESPONSE);
  assert(mapI2c(ESP_ERR_INVALID_STATE, "test").code == Err::I2C_BUS);
  assert(mapI2c(ESP_ERR_NOT_FOUND, "test").code == Err::I2C_BUS);
}
}  // namespace

int main() {
  testInput();
  testParsers();
  testI2cMapping();
  std::puts("PASS: IDF console input, parsers, and conservative transport mapping");
}
