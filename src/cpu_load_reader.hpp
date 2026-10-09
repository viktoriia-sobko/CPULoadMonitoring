// Reads per-core CPU time counters and converts them into load percentages.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "fd.hpp"

namespace cpumon {

struct CpuTimes {
  int id = -1;                 // N from the "cpuN" label
  std::uint64_t busy = 0;      // user+nice+system+irq+softirq+steal (jiffies)
  std::uint64_t total = 0;     // busy + idle + iowait
};

// Parses the leading "cpuN ..." lines of /proc/stat-formatted text.
// Allocation-free. Only complete (newline-terminated) lines are parsed.
// Returns the number of cores written to `out` (at most `capacity`).
std::size_t ParseProcStat(const char* data, std::size_t len, CpuTimes* out,
                          std::size_t capacity) noexcept;

class CpuLoadReader {
 public:
  // INIT-phase constructor: every dynamic allocation happens here.
  // `max_cores == 0` means "autodetect with sysconf(_SC_NPROCESSORS_CONF)".
  // Throws std::system_error / std::runtime_error on failure.
  explicit CpuLoadReader(const std::string& path = "/proc/stat",
                         std::size_t max_cores = 0);

  // RUN-phase: re-reads the counters and updates loads relative to the
  // previous Sample() (or to construction for the first call).
  // Allocation-free, never throws.
  bool Sample() noexcept;

  std::size_t CoreCount() const noexcept { return count_; }
  std::size_t Capacity() const noexcept { return capacity_; }
  int CoreId(std::size_t i) const noexcept { return prev_[i].id; }
  double Load(std::size_t i) const noexcept { return load_[i]; }  // 0..100 %

 private:
  std::size_t ReadSnapshot(CpuTimes* out) noexcept;

  UniqueFd fd_;
  std::size_t capacity_ = 0;
  std::size_t count_ = 0;
  std::vector<char> buf_;
  std::vector<CpuTimes> prev_;  // latest snapshot after every Sample()
  std::vector<CpuTimes> cur_;   // scratch
  std::vector<double> load_;
};

}  // namespace cpumon
