#include "cpu_load_reader.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace cpumon {
namespace {

constexpr std::size_t kBytesPerCore = 256;  // generous upper bound for a cpu line
constexpr std::size_t kExtraBytes = 4096;

inline bool IsDigit(char c) noexcept { return c >= '0' && c <= '9'; }

// Parses one unsigned number after optional spaces. Returns nullptr if none.
const char* ParseU64(const char* p, const char* end, std::uint64_t& v) noexcept {
  while (p < end && *p == ' ') ++p;
  if (p >= end || !IsDigit(*p)) return nullptr;
  std::uint64_t r = 0;
  while (p < end && IsDigit(*p)) {
    r = r * 10 + static_cast<std::uint64_t>(*p - '0');
    ++p;
  }
  v = r;
  return p;
}

}  // namespace

std::size_t ParseProcStat(const char* data, std::size_t len, CpuTimes* out,
                          std::size_t capacity) noexcept {
  const char* p = data;
  const char* const end = data + len;
  std::size_t n = 0;

  while (p < end && n < capacity) {
    const char* nl = static_cast<const char*>(std::memchr(p, '\n', static_cast<std::size_t>(end - p)));
    if (nl == nullptr) break;  // incomplete trailing line

    if (nl - p >= 4 && p[0] == 'c' && p[1] == 'p' && p[2] == 'u' && IsDigit(p[3])) {
      const char* q = p + 3;
      int id = 0;
      while (q < nl && IsDigit(*q)) {
        id = id * 10 + (*q - '0');
        ++q;
      }
      // user nice system idle iowait irq softirq steal guest guest_nice
      std::uint64_t f[10] = {};
      std::size_t k = 0;
      while (k < 10) {
        const char* r = ParseU64(q, nl, f[k]);
        if (r == nullptr) break;
        q = r;
        ++k;
      }
      if (k >= 4) {  // guest/guest_nice are already counted in user/nice
        const std::uint64_t busy = f[0] + f[1] + f[2] + f[5] + f[6] + f[7];
        const std::uint64_t idle = f[3] + f[4];
        out[n++] = CpuTimes{id, busy, busy + idle};
      }
    } else if (n > 0) {
      break;  // cpu lines are contiguous; the first non-cpu line ends them
    }
    p = nl + 1;  // (the aggregate "cpu " line is skipped by the branch above)
  }
  return n;
}

CpuLoadReader::CpuLoadReader(const std::string& path, std::size_t max_cores) {
  if (max_cores == 0) {
    const long conf = ::sysconf(_SC_NPROCESSORS_CONF);
    max_cores = conf > 0 ? static_cast<std::size_t>(conf) : 1;
  }
  capacity_ = max_cores;

  fd_.reset(::open(path.c_str(), O_RDONLY | O_CLOEXEC));
  if (!fd_.valid()) {
    throw std::system_error(errno, std::generic_category(), "cannot open " + path);
  }

  buf_.resize(capacity_ * kBytesPerCore + kExtraBytes);
  prev_.resize(capacity_);
  cur_.resize(capacity_);
  load_.assign(capacity_, 0.0);

  count_ = ReadSnapshot(prev_.data());  // baseline for the first Sample()
  if (count_ == 0) {
    throw std::runtime_error("no per-core 'cpuN' lines found in " + path);
  }
}

std::size_t CpuLoadReader::ReadSnapshot(CpuTimes* out) noexcept {
  ssize_t n;
  do {
    n = ::pread(fd_.get(), buf_.data(), buf_.size(), 0);
  } while (n < 0 && errno == EINTR);
  if (n <= 0) return 0;
  return ParseProcStat(buf_.data(), static_cast<std::size_t>(n), out, capacity_);
}

bool CpuLoadReader::Sample() noexcept {
  const std::size_t cnt = ReadSnapshot(cur_.data());
  if (cnt == 0) return false;

  for (std::size_t i = 0; i < cnt; ++i) {
    const bool same_core = i < count_ && prev_[i].id == cur_[i].id;
    if (!same_core) {  // core appeared / re-ordered (hotplug): no baseline yet
      load_[i] = 0.0;
      continue;
    }
    const CpuTimes& a = prev_[i];
    const CpuTimes& b = cur_[i];
    if (b.total < a.total || b.busy < a.busy) {  // counter reset / wrap
      load_[i] = 0.0;
    } else if (b.total == a.total) {
      // Less than one kernel tick elapsed: keep the previous value.
    } else {
      const std::uint64_t dt = b.total - a.total;
      std::uint64_t db = b.busy - a.busy;
      if (db > dt) db = dt;
      load_[i] = 100.0 * static_cast<double>(db) / static_cast<double>(dt);
    }
  }
  std::swap(prev_, cur_);  // O(1), no allocation
  count_ = cnt;
  return true;
}

}  // namespace cpumon
