// Allocation-free text formatting of load values into caller-provided buffers.
#pragma once

#include <cstddef>
#include <ctime>

#include "src/cpu_load_reader.hpp"

namespace cpumon {

// Upper bound of bytes needed per core by either formatter.
constexpr std::size_t kReportBytesPerCore = 48;
constexpr std::size_t kReportExtraBytes = 64;

// "cpu0:  12.3%\ncpu1: 100.0%\n..."  (for stdout)
std::size_t FormatLoadReport(const CpuLoadReader& r, char* out, std::size_t cap) noexcept;

// "2026-10-09T12:00:00Z cpu0=12.3 cpu1=100.0 ...\n"  (for the log file)
std::size_t FormatLogLine(const CpuLoadReader& r, std::time_t now, char* out,
                          std::size_t cap) noexcept;

}  // namespace cpumon
