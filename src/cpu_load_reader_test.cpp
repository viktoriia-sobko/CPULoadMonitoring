// Dependency-free tests (plain asserts) so they build offline under Bazel.
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "../src/cpu_load_reader.hpp"
#include "../src/report.hpp"

static int g_failed = 0;
#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failed;                                                      \
    }                                                                  \
  } while (0)

using cpumon::CpuTimes;

static const char kSample[] =
    "cpu  100 0 100 800 0 0 0 0 0 0\n"
    "cpu0 50 10 40 400 5 1 2 3 99 99\n"
    "cpu1 50 0 60 400 0 0 0 0 0 0\n"
    "intr 1 2 3\nctxt 5\n";

static void TestParser() {
  CpuTimes t[4];
  std::size_t n = cpumon::ParseProcStat(kSample, sizeof kSample - 1, t, 4);
  CHECK(n == 2);
  CHECK(t[0].id == 0 && t[1].id == 1);
  CHECK(t[0].busy == 50 + 10 + 40 + 1 + 2 + 3);  // guest columns are not added
  CHECK(t[0].total == t[0].busy + 400 + 5);
  CHECK(cpumon::ParseProcStat(kSample, sizeof kSample - 1, t, 1) == 1);  // capacity
  CHECK(cpumon::ParseProcStat("cpu0 1 2 3 4", 12, t, 4) == 0);           // no '\n'
  CHECK(cpumon::ParseProcStat("", 0, t, 4) == 0);
}

static void Rewrite(int fd, const std::string& text) {
  CHECK(::ftruncate(fd, 0) == 0);
  CHECK(::pwrite(fd, text.data(), text.size(), 0) == static_cast<ssize_t>(text.size()));
}

static void TestLoad() {
  const char* dir = std::getenv("TEST_TMPDIR");
  std::string path = std::string(dir ? dir : "/tmp") + "/cpumon_test_XXXXXX";
  int fd = ::mkstemp(&path[0]);
  CHECK(fd >= 0);
  Rewrite(fd, "cpu  0 0 0 0\ncpu0 0 0 0 0\ncpu1 0 0 0 0\nintr 0\n");

  cpumon::CpuLoadReader r(path, 8);
  CHECK(r.CoreCount() == 2);

  // cpu0: 50 busy of 100 total; cpu1: fully idle; cpu1 -> 100% busy below.
  Rewrite(fd, "cpu  0 0 0 0\ncpu0 50 0 0 50\ncpu1 0 0 0 100\nintr 0\n");
  CHECK(r.Sample());
  CHECK(r.Load(0) > 49.99 && r.Load(0) < 50.01);
  CHECK(r.Load(1) == 0.0);

  Rewrite(fd, "cpu  0 0 0 0\ncpu0 50 0 0 150\ncpu1 100 0 0 100\nintr 0\n");
  CHECK(r.Sample());
  CHECK(r.Load(0) == 0.0);                       // 0 busy / 100 total
  CHECK(r.Load(1) > 99.99 && r.Load(1) <= 100.0);  // 100 busy / 100 total

  char buf[512];
  std::size_t n = cpumon::FormatLoadReport(r, buf, sizeof buf);
  buf[n] = '\0';
  CHECK(std::strcmp(buf, "cpu0:   0.0%\ncpu1: 100.0%\n") == 0);

  n = cpumon::FormatLogLine(r, 0, buf, sizeof buf);
  buf[n] = '\0';
  CHECK(std::strcmp(buf, "1970-01-01T00:00:00Z cpu0=0.0 cpu1=100.0\n") == 0);

  // Unchanged counters: previous values are kept (no division by zero).
  CHECK(r.Sample());
  CHECK(r.Load(1) > 99.99);

  ::close(fd);
  ::unlink(path.c_str());
}

int main() {
  TestParser();
  TestLoad();
  if (g_failed == 0) std::puts("all tests passed");
  return g_failed == 0 ? 0 : 1;
}
