#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <cassert>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <err.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/mman.h>
#include <sysexits.h>
#include <unistd.h>

#include "base/string.h"
#include "substrings.h"

namespace {

int print_version;
int print_help;

bool stdout_is_tty;

CommonSubstringFinder csf;

struct option long_options[] = {
    {"color", no_argument, &csf.do_color, 1},
    {"cover", no_argument, &csf.do_cover, 1},
    {"cover-threshold", required_argument, nullptr, 'c'},
    {"documents", no_argument, &csf.do_document, 1},
    {"no-equal-sets", no_argument, &csf.allow_equal_sets, 0},
    {"no-filter", no_argument, &csf.filter_redundant_features, 0},
    {"prior-bias", required_argument, nullptr, 'p'},
    {"skip-prefixes", no_argument, &csf.skip_samecount_prefixes, 1},
    {"threshold", required_argument, nullptr, 't'},
    {"threshold-percent", required_argument, nullptr, 'P'},
    {"threshold-count", required_argument, nullptr, 'T'},
    {"words", no_argument, &csf.do_words, 1},
    {"version", no_argument, &print_version, 1},
    {"help", no_argument, &print_help, 1},
    {0, 0, 0, 0}};

// Memory maps the file specified by path.  Prints an error message and exits
// on failure.
void* MapFile(const char* path, size_t* ret_size) {
  off_t size;
  void* map = nullptr;
  int fd;

  if (-1 == (fd = open(path, O_RDONLY)))
    err(EX_NOINPUT, "Could not open '%s' for reading", path);

  if (-1 == (size = lseek(fd, 0, SEEK_END)))
    err(EX_IOERR, "Could not seek to end of '%s'", path);

  if (size &&
      MAP_FAILED == (map = mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0)))
    err(EX_IOERR, "Could not memory-map '%s'", path);

  close(fd);

  if (ret_size) *ret_size = size;

  return map;
}

void PrintString(const ev::StringRef& string) {
  const unsigned char* ch = (const unsigned char*)string.data();
  auto length = string.size();

  for (; length--; ++ch) {
    if (csf.do_color && length) {
      if (stdout_is_tty)
        printf("\033[%d;1m", *ch - 'A' + 30);
      else
        putchar(*ch);

      ++ch;
      --length;
    }

    if (*ch >= ' ' && *ch != '\\') {
      putchar(*ch);

      continue;
    }

    putchar('\\');

    switch (*ch) {
      case '\a':
        putchar('a');
        break;
      case '\b':
        putchar('b');
        break;
      case '\t':
        putchar('t');
        break;
      case '\n':
        putchar('n');
        break;
      case '\v':
        putchar('v');
        break;
      case '\f':
        putchar('f');
        break;
      case '\r':
        putchar('r');
        break;
      case '\\':
        putchar('\\');
        break;
      default:
        printf("%03o", (unsigned char)*ch);
    }
  }

  if (csf.do_color) printf("\033[00m");
}

void PrintResult(size_t input0_count, size_t input1_count, double log_odds,
                 const ev::StringRef& substring) {
  printf("%.3f\t%zu\t%zu\t", log_odds, input0_count, input1_count);
  PrintString(substring);
  putchar('\n');
  fflush(stdout);
}

}  // namespace

int main(int argc, char** argv) {
  char* endptr;
  int i;

  while ((i = getopt_long(argc, argv, "", long_options, 0)) != -1) {
    switch (i) {
      case 0:

        break;

      case 'c':
        csf.cover_threshold = strtol(optarg, &endptr, 0);

        if (*endptr || csf.cover_threshold < 0)
          errx(EX_USAGE,
               "Parse error in cover threshold, expected non-negative integer");
        break;

      case 'p':
        csf.prior_bias = strtod(optarg, &endptr);

        if (*endptr)
          errx(EX_USAGE,
               "Parse error in prior bias, expected decimal fraction");
        break;

      case 'P':
        csf.threshold_percent = strtol(optarg, &endptr, 0);

        if (*endptr || csf.threshold_percent < 0 || csf.threshold_percent > 100)
          errx(EX_USAGE,
               "Parse error in threshold percentage, expected integer between "
               "0 and 100");
        break;

      case 't':
        csf.threshold = strtod(optarg, &endptr);

        if (*endptr)
          errx(EX_USAGE,
               "Parse error in probability threshold, expected decimal "
               "fraction");
        break;

      case 'T':
        csf.threshold_count = strtol(optarg, &endptr, 0);

        if (*endptr || csf.threshold_count < 0)
          errx(EX_USAGE,
               "Parse error in threshold count, expected non-negative integer");
        break;

      case '?':
        fprintf(stderr, "Try `%s --help' for more information.\n", argv[0]);
        return EXIT_FAILURE;
    }
  }

  if (print_help) {
    printf(
        "Usage: %s [OPTION]... INPUT1 INPUT2 [INPUT1-MIN [INPUT2-MAX]]\n"
        "\n"
        "      --document             count each prefix only once per "
        "document\n"
        "                             documents are delimited by NUL "
        "characters\n"
        "      --skip-prefixes        skip prefixes with identical positive "
        "counts\n"
        "      --probability          give probability¹ rather than counts\n"
        "      --prior-bias=BIAS      assign BIAS to prior probability\n"
        "      --threshold=PROB       set minimum probability for output\n"
        "      --cover                suppress normal output and print only "
        "the\n"
        "                             unique substrings that meet the "
        "required\n"
        "                             threshold, and that are necessary to "
        "cover\n"
        "                             all input documents.\n"
        "                             Implies --document\n"
        "      --no-filter            don't attempt to filter redundant "
        "features\n"
        "      --help     display this help and exit\n"
        "      --version  display version information\n"
        "\n"
        "1. The probability returned is the probability that a given N-gram "
        "belongs in\n"
        "   INPUT-1.  If the input sample is incomplete, you may want to "
        "assign some\n"
        "   bias in favor of the prior (i.e. additive smoothing).  A bias of 1 "
        "is a\n"
        "   good starting point.\n"
        "\n"
        "Report bugs to <morten.hustveit@gmail.com>\n",
        argv[0]);

    return EXIT_SUCCESS;
  }

  if (print_version) errx(EXIT_SUCCESS, "%s", PACKAGE_STRING);

  if (optind + 2 != argc)
    errx(EX_USAGE, "Usage: %s [OPTION]... INPUT1 INPUT2", argv[0]);

  // --cover implies --unique and --document.
  if (csf.do_cover) {
    csf.do_document = 1;
  }

  stdout_is_tty = isatty(STDOUT_FILENO);

  csf.input0 =
      reinterpret_cast<const char*>(MapFile(argv[optind++], &csf.input0_size));
  csf.input1 =
      reinterpret_cast<const char*>(MapFile(argv[optind++], &csf.input1_size));

  csf.output = PrintResult;

  csf.FindSubstringFrequencies();
}
