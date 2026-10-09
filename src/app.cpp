#include "app.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <system_error>
#include <utility>

#include "report.hpp"

namespace cpumon {
namespace {

volatile std::sig_atomic_t g_stop = 0;
void OnSignal(int) { g_stop = 1; }

std::int64_t NowMs() noexcept {
  timespec ts;
  ::clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<std::int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

void Say(const char* msg) noexcept {  // hints / diagnostics -> stderr
  WriteAll(STDERR_FILENO, msg, std::strlen(msg));
}

const char kHelp[] =
    "Commands:\n"
    "  load | l      print the load of every CPU core (since the previous request)\n"
    "  help | h | ?  show this help\n"
    "  quit | q      exit\n";

}  // namespace

Application::Application(Config cfg) : cfg_(std::move(cfg)) {}

int Application::Execute() {
  int rc = 0;
  for (;;) {
    switch (state_) {
      case State::Init:
        DoInit();
        state_ = State::Run;  // automatic transition
        break;
      case State::Run:
        rc = DoRun();
        state_ = State::Done;
        break;
      case State::Done:
        return rc;
    }
  }
}

// ---------------------------------------------------------------- Init ----
void Application::DoInit() {
  request_reader_.emplace();

  if (cfg_.log_enabled) {
    log_reader_.emplace();
    log_fd_.reset(::open(cfg_.log_path.c_str(),
                         O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644));
    if (!log_fd_.valid()) {
      throw std::system_error(errno, std::generic_category(),
                              "cannot open log file '" + cfg_.log_path + "'");
    }
  }

  out_buf_.resize(request_reader_->Capacity() * kReportBytesPerCore + kReportExtraBytes);

  struct sigaction sa;
  std::memset(&sa, 0, sizeof sa);
  sa.sa_handler = OnSignal;  // no SA_RESTART: poll() must return EINTR
  sigemptyset(&sa.sa_mask);
  ::sigaction(SIGINT, &sa, nullptr);
  ::sigaction(SIGTERM, &sa, nullptr);

  Say("[init] done, entering run state. ");
  Say(kHelp);
}

// ----------------------------------------------------------------- Run ----
int Application::DoRun() {
  const std::int64_t interval_ms = static_cast<std::int64_t>(cfg_.interval_s) * 1000;
  std::int64_t next_log = cfg_.log_enabled ? NowMs() + interval_ms : 0;
  bool stdin_open = true;

  while (!g_stop && !quit_) {
    if (!stdin_open && !cfg_.log_enabled) break;

    int timeout = -1;
    if (cfg_.log_enabled) {
      const std::int64_t left = next_log - NowMs();
      timeout = left <= 0 ? 0 : (left > INT_MAX ? INT_MAX : static_cast<int>(left));
    }

    pollfd pfd;
    pfd.fd = stdin_open ? STDIN_FILENO : -1;  // negative fd is ignored by poll()
    pfd.events = POLLIN;
    pfd.revents = 0;

    const int rc = ::poll(&pfd, 1, timeout);
    if (rc < 0) {
      if (errno == EINTR) continue;
      Say("poll() failed\n");
      return 1;
    }

    if (rc > 0 && (pfd.revents & (POLLIN | POLLHUP | POLLERR))) {
      char chunk[256];
      const ssize_t n = ::read(STDIN_FILENO, chunk, sizeof chunk);
      if (n > 0) {
        Feed(chunk, static_cast<std::size_t>(n));
      } else if (n == 0 || (errno != EINTR && errno != EAGAIN)) {
        stdin_open = false;
        if (cfg_.log_enabled) Say("[run] stdin closed; logging continues (Ctrl+C to stop)\n");
      }
    }

    if (cfg_.log_enabled) {
      const std::int64_t now = NowMs();
      if (now >= next_log) {
        WriteLogRecord();
        next_log += interval_ms;
        if (next_log <= now) next_log = now + interval_ms;  // don't burst after a stall
      }
    }
  }
  return 0;
}

void Application::Feed(const char* data, std::size_t n) noexcept {
  for (std::size_t i = 0; i < n; ++i) {
    const char c = data[i];
    if (c == '\n') {
      if (line_overflow_) {
        Say("command too long\n");
      } else {
        while (line_len_ > 0 && (line_[line_len_ - 1] == '\r' || line_[line_len_ - 1] == ' ')) {
          --line_len_;
        }
        line_[line_len_] = '\0';
        HandleCommand(line_.data());
      }
      line_len_ = 0;
      line_overflow_ = false;
    } else if (line_len_ + 1 < line_.size()) {
      line_[line_len_++] = c;
    } else {
      line_overflow_ = true;
    }
  }
}

void Application::HandleCommand(const char* line) noexcept {
  if (*line == '\0') return;
  if (!std::strcmp(line, "load") || !std::strcmp(line, "l")) {
    PrintLoad();
  } else if (!std::strcmp(line, "help") || !std::strcmp(line, "h") || !std::strcmp(line, "?")) {
    Say(kHelp);
  } else if (!std::strcmp(line, "quit") || !std::strcmp(line, "q") || !std::strcmp(line, "exit")) {
    quit_ = true;
  } else {
    Say("unknown command (type 'help')\n");
  }
}

void Application::PrintLoad() noexcept {
  if (!request_reader_->Sample()) {
    Say("failed to read CPU statistics\n");
    return;
  }
  const std::size_t n = FormatLoadReport(*request_reader_, out_buf_.data(), out_buf_.size());
  if (!WriteAll(STDOUT_FILENO, out_buf_.data(), n)) quit_ = true;  // stdout is gone
}

void Application::WriteLogRecord() noexcept {
  if (!log_reader_->Sample()) {
    Say("failed to read CPU statistics for the log\n");
    return;
  }
  const std::size_t n =
      FormatLogLine(*log_reader_, std::time(nullptr), out_buf_.data(), out_buf_.size());
  if (!WriteAll(log_fd_.get(), out_buf_.data(), n)) Say("failed to write to the log file\n");
}

}  // namespace cpumon
