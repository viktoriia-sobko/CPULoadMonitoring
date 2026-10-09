#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <utility>

#include "src/app.hpp"

namespace {

void PrintUsage(std::FILE* out, const char* prog) {
  std::fprintf(out,
               "Usage: %s [-i SECONDS -o FILE] [-h]\n"
               "  -i SECONDS  write all-core load to FILE every SECONDS (1..86400)\n"
               "  -o FILE     log file (appended); must be used together with -i\n"
               "  -h          show this help\n"
               "\nWithout -i/-o the program only answers requests typed on stdin\n"
               "('load', 'help', 'quit').\n",
               prog);
}

}  // namespace

int main(int argc, char** argv) {
  cpumon::Config cfg;
  bool have_i = false;
  bool have_o = false;

  int opt;
  while ((opt = ::getopt(argc, argv, "i:o:h")) != -1) {
    switch (opt) {
      case 'i': {
        errno = 0;
        char* end = nullptr;
        const unsigned long v = std::strtoul(optarg, &end, 10);
        if (optarg[0] < '0' || optarg[0] > '9' || errno != 0 || *end != '\0' || v < 1 ||
            v > 86400) {
          std::fprintf(stderr, "error: invalid interval '%s' (expected 1..86400)\n", optarg);
          return 2;
        }
        cfg.interval_s = static_cast<unsigned>(v);
        have_i = true;
        break;
      }
      case 'o':
        cfg.log_path = optarg;
        have_o = true;
        break;
      case 'h':
        PrintUsage(stdout, argv[0]);
        return 0;
      default:
        PrintUsage(stderr, argv[0]);
        return 2;
    }
  }
  if (optind < argc) {
    std::fprintf(stderr, "error: unexpected argument '%s'\n", argv[optind]);
    PrintUsage(stderr, argv[0]);
    return 2;
  }
  if (have_i != have_o) {
    std::fprintf(stderr, "error: -i and -o must be given together\n");
    return 2;
  }
  cfg.log_enabled = have_i && have_o;

  try {
    cpumon::Application app(std::move(cfg));
    return app.Execute();
  } catch (const std::exception& e) {
    std::fprintf(stderr, "fatal: %s\n", e.what());
    return 1;
  }
}
