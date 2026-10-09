# cpu_monitor

Command-line tool that prints the load of every CPU core on request and,
optionally, appends it to a file every N seconds. Linux, C++17, Bazel.
See [DECISIONS.md](DECISIONS.md) for design decisions and justifications.

## Build & test

    bazel build //:cpu_monitor
    bazel test  //:cpu_load_reader_test

Without Bazel:

    g++ -std=c++17 -O2 -Wall -Wextra src/*.cpp -o cpu_monitor

## Usage

    cpu_monitor                      # interactive: type commands on stdin
    cpu_monitor -i 5 -o cpu.log      # additionally log every 5 s to cpu.log
    echo load | cpu_monitor          # one-shot

Commands: `load` (`l`), `help` (`h`), `quit` (`q`). Ctrl+C also exits.

Example:

    $ ./cpu_monitor
    load
    cpu0:  12.3%
    cpu1:   3.0%

Log file format: `2026-10-09T12:00:00Z cpu0=12.3 cpu1=3.0`

## Layout

    src/fd.hpp              RAII file descriptor, WriteAll
    src/cpu_load_reader.*   /proc/stat parser + load computation
    src/report.*            allocation-free formatting
    src/app.*               Init/Run state machine, command loop, logging
    src/main.cpp            argument parsing (POSIX getopt)
    tests/                  unit tests
