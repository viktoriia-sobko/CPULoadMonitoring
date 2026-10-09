// Application: explicit two-state machine (Init -> Run).
#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "cpu_load_reader.hpp"
#include "fd.hpp"

namespace cpumon {

struct Config {
  bool log_enabled = false;
  unsigned interval_s = 0;   // N seconds between log records
  std::string log_path;
};

enum class State { Init, Run, Done };

class Application {
 public:
  explicit Application(Config cfg);

  // Drives the state machine: Init runs first and hands over to Run
  // automatically when it finishes. Returns the process exit code.
  int Execute();

 private:
  void DoInit();  // ALL dynamic allocations live here
  int DoRun();    // allocation-free main loop

  void Feed(const char* data, std::size_t n) noexcept;
  void HandleCommand(const char* line) noexcept;
  void PrintLoad() noexcept;
  void WriteLogRecord() noexcept;

  Config cfg_;
  State state_ = State::Init;
  bool quit_ = false;

  std::optional<CpuLoadReader> request_reader_;  // baseline = previous request
  std::optional<CpuLoadReader> log_reader_;      // baseline = previous log record
  UniqueFd log_fd_;
  std::vector<char> out_buf_;

  std::array<char, 128> line_{};
  std::size_t line_len_ = 0;
  bool line_overflow_ = false;
};

}  // namespace cpumon
