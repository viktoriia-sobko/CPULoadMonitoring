#include "report.hpp"

#include <cstdint>
#include <cstring>

namespace cpumon {
namespace {

// Bounded output cursor; silently truncates on overflow.
class Sink {
 public:
  Sink(char* buf, std::size_t cap) noexcept : buf_(buf), cap_(cap) {}
  void Put(char c) noexcept {
    if (len_ < cap_) buf_[len_++] = c;
  }
  void Str(const char* s) noexcept {
    while (*s != '\0') Put(*s++);
  }
  void Uint(std::uint64_t v) noexcept {
    char tmp[20];
    int n = 0;
    do {
      tmp[n++] = static_cast<char>('0' + v % 10);
      v /= 10;
    } while (v != 0);
    while (n > 0) Put(tmp[--n]);
  }
  void Percent(double load, bool pad) noexcept {
    unsigned t = 0;
    if (load >= 100.0) {
      t = 1000;
    } else if (load > 0.0) {
      t = static_cast<unsigned>(load * 10.0 + 0.5);
    }
    if (pad) {
      if (t < 100) Put(' ');
      if (t < 1000) Put(' ');
    }
    Uint(t / 10);
    Put('.');
    Uint(t % 10);
  }
  std::size_t len() const noexcept { return len_; }

 private:
  char* buf_;
  std::size_t cap_;
  std::size_t len_ = 0;
};

}  // namespace

std::size_t FormatLoadReport(const CpuLoadReader& r, char* out, std::size_t cap) noexcept {
  Sink s(out, cap);
  for (std::size_t i = 0; i < r.CoreCount(); ++i) {
    s.Str("cpu");
    s.Uint(static_cast<std::uint64_t>(r.CoreId(i)));
    s.Str(": ");
    s.Percent(r.Load(i), true);
    s.Str("%\n");
  }
  return s.len();
}

std::size_t FormatLogLine(const CpuLoadReader& r, std::time_t now, char* out,
                          std::size_t cap) noexcept {
  Sink s(out, cap);
  char ts[32] = "0000-00-00T00:00:00Z";
  std::tm tm_utc;
  if (::gmtime_r(&now, &tm_utc) != nullptr) {
    ::strftime(ts, sizeof ts, "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
  }
  s.Str(ts);
  for (std::size_t i = 0; i < r.CoreCount(); ++i) {
    s.Str(" cpu");
    s.Uint(static_cast<std::uint64_t>(r.CoreId(i)));
    s.Put('=');
    s.Percent(r.Load(i), false);
  }
  s.Put('\n');
  return s.len();
}

}  // namespace cpumon
