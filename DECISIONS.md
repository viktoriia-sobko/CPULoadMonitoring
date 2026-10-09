# Design decisions and justifications

The task statement leaves several points open. This file lists every decision I
made, and why.

## 1. "POSIX-compliant APIs only" vs. reading per-core CPU load

**Problem.** POSIX defines no API that returns per-core CPU load or CPU time
counters (`getrusage`/`times` are per-process; `sysconf(_SC_NPROCESSORS_*)`
only gives the *number* of cores). A strictly portable solution is therefore
impossible. Alternatives (`host_processor_info` on macOS, `sysctl` on BSD) are
not POSIX either.

**Decision.** The *only* non-POSIX element is the data source: the Linux file
`/proc/stat`. Everything else is POSIX: `open`, `pread`, `read`, `write`,
`close`, `poll`, `sigaction`, `clock_gettime`, `gmtime_r`, `sysconf`, `getopt`.
No GNU extensions (`getopt_long`, `epoll`, `sched_getcpu`, ...) are used, and
`/proc/stat` is read through plain POSIX file I/O.

**Why.** It is the standard, dependency-free source on Linux (used by `top`,
`htop`, `mpstat`). The data source is isolated in one class
(`CpuLoadReader`), so porting to another OS means replacing one file.

## 2. How "load" is defined

Load of a core = `busy / total` over the interval between two samples, in %.
`busy = user + nice + system + irq + softirq + steal`;
`total = busy + idle + iowait`. `guest`/`guest_nice` are *not* added because
the kernel already includes them in `user`/`nice` (adding them would double
count). `iowait` is treated as idle, as most tools do.

Because counters are cumulative since boot, a single reading is meaningless.
Therefore the interval is: *since the previous request* (or since Init for the
first request), and *since the previous log record* for the file output.
The two measurements use **two independent `CpuLoadReader` instances**, so a
manual `load` request never shortens or distorts the periodic log interval
(and vice versa). Cost: a few KB of extra memory, allocated in Init.

Edge cases: if no kernel tick elapsed between two samples (calls within ~10 ms)
the previous value is kept (no division by zero); counter wrap/reset yields 0;
a core that appears after start (hotplug) shows 0 until its second sample;
cores that disappear are simply not printed.

## 3. Two states, Init and Run

`Application::Execute()` is an explicit state machine
(`Init -> Run -> Done`); `DoInit()` returns and the machine moves to `Run`
automatically, as required. Init failures (cannot open `/proc/stat` or the log
file) throw, are reported by `main` and result in exit code 1 *before* the app
ever enters Run.

## 4. Dynamic memory only in Init

All `std::vector` / `std::string` sizing happens in Init:
- `CpuLoadReader` allocates its read buffer and snapshot arrays, sized from
  `sysconf(_SC_NPROCESSORS_CONF)` (the maximum possible number of cores, so
  hotplug can never require growth);
- the output buffer is sized `capacity * 48 + 64` bytes;
- the log file is opened in Init.

Run-state code is allocation-free: `pread` into the preallocated buffer, a
hand-written parser (no `istringstream`, `stoi`, `std::string`), a hand-written
number formatter (no `printf("%f")`, which may call `malloc` in glibc),
`vector::swap` instead of copy, a fixed `std::array` for command input, and
raw `write()` instead of `iostream`/`stdio`. No exceptions are thrown in Run
(all Run-path functions are `noexcept`).

Verified with an `LD_PRELOAD` malloc counter: 5 requests and 2000 requests give
the same number of `malloc` calls (all from startup).

## 5. How "by request" works

The program is interactive: it reads commands from stdin line by line.
`load` (or `l`) prints the load of every core to stdout; `help`; `quit`
(also Ctrl+C / SIGTERM / EOF). Hints and errors go to **stderr**, so stdout
contains only data and can be piped (`echo load | cpu_monitor`).

Output format (stdout):

    cpu0:  12.3%
    cpu1: 100.0%

I chose stdin commands over alternatives (signal-triggered, socket) because it
is the simplest portable "request" mechanism for a CMD-line tool and is easy
to test and script.

## 6. Periodic file output (nice-to-have)

`cpu_monitor -i N -o FILE`: every N seconds (1..86400, integer) one line is
appended:

    2026-10-09T12:00:00Z cpu0=12.3 cpu1=100.0

- Both `-i` and `-o` are required together (one without the other is an error,
  exit code 2).
- The file is opened with `O_APPEND` so an existing log is continued, not
  destroyed; each record is a single `write()` (no stdio buffering), so data is
  not lost on a crash or kill.
- ISO-8601 UTC timestamps make logs unambiguous and sortable.
- One `poll()` waits for stdin *and* the next log deadline (monotonic clock),
  so there are no threads, no locks and no busy-waiting. If stdin is closed
  (e.g. started as a service with `< /dev/null`) the program keeps logging
  until SIGINT/SIGTERM; without a log file it exits on EOF.
- Missed deadlines (e.g. suspend) do not cause a burst of catch-up records.

## 7. RAII

- `UniqueFd` owns every file descriptor (`/proc/stat`, log file): move-only,
  closes in the destructor, no leaks on exceptions.
- Containers (`std::vector`, `std::optional<CpuLoadReader>`) own all memory;
  there is no `new`/`delete`/`malloc`/`free` in the code.
- `Application` owns its readers and the log descriptor; destruction order
  releases everything automatically.

## 8. Language level and portability

C++17, compiled with `-Wall -Wextra -Wpedantic` without warnings (GCC 13).
C++17 features used: `std::optional`, `std::exchange` (C++14), inline
functions in headers. Signal handling is async-signal-safe: the handler only
sets a `volatile sig_atomic_t`, and `sigaction` is installed *without*
`SA_RESTART` so `poll()` returns `EINTR` and the loop exits promptly.

## 9. Build (Bazel)

`MODULE.bazel` (bzlmod), `.bazelversion` pinned to 7.4.1 (use Bazelisk),
`.bazelrc` sets `-std=c++17`. Targets: `//:cpu_monitor` (binary),
`//:cpumon_lib` (library), `//:cpu_load_reader_test` (test). The tests use a
tiny built-in `CHECK` macro instead of GoogleTest so the build needs no
external downloads besides `rules_cc`.

## 10. Testing

`tests/cpu_load_reader_test.cpp` covers the parser (guest columns, capacity,
incomplete lines), load arithmetic, formatting, the log-line format and the
"no tick elapsed" case. `CpuLoadReader` accepts a file path, so tests feed it
a fake `/proc/stat` they rewrite between samples.

## 11. Known limitations

- Linux only (see decision 1).
- Resolution is the kernel tick (usually 10 ms); very short intervals are noisy.
- Command lines longer than 127 bytes are rejected with a message.
- Not Windows/macOS compatible.
