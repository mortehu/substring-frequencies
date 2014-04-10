#include <cassert>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <err.h>
#include <fcntl.h>
#include <getopt.h>
#include <sysexits.h>
#include <sys/mman.h>
#include <unistd.h>

#include "substrings.h"

static int print_version;
static int print_help;

static struct option long_options[] =
{
  { "color",             no_argument,       &do_color,                  1 },
  { "cover",             no_argument,       &do_cover,                  1 },
  { "cover-threshold",   required_argument, NULL,                     'c' },
  { "documents",         no_argument,       &do_document,               1 },
  { "prior-bias",        required_argument, NULL,                     'p' },
  { "probability",       no_argument,       &do_probability,            1 },
  { "skip-prefixes",     no_argument,       &skip_samecount_prefixes,   1 },
  { "threshold",         required_argument, NULL,                     't' },
  { "threshold-count",   required_argument, NULL,                     'T' },
  { "unique-substrings", no_argument,       &do_unique,                 1 },
  { "words",             no_argument,       &do_words,                  1 },
  { "version",           no_argument,       &print_version,             1 },
  { "help",              no_argument,       &print_help,                1 },
  { 0, 0, 0, 0 }
};

// Memory maps the file specified by path.  Prints an error message and exits
// on failure.
static void *
map_file (const char *path, size_t *ret_size)
{
  off_t size;
  void *map = NULL;
  int fd;

  if (-1 == (fd = open (path, O_RDONLY)))
    err (EX_NOINPUT, "Could not open '%s' for reading", path);

  if (-1 == (size = lseek (fd, 0, SEEK_END)))
    err (EX_IOERR, "Could not seek to end of '%s'", path);

  if (size && MAP_FAILED == (map = mmap (NULL, size, PROT_READ, MAP_SHARED, fd, 0)))
    err (EX_IOERR, "Could not memory-map '%s'", path);

  close (fd);

  if (ret_size)
    *ret_size = size;

  return map;
}

// This program is liable to consume a lot of memory, and its purpose is rarely
// system critical, so we elect to be killed first by Linux' OOM killer.
static void
become_oom_friendly (void)
{
  static const char *setting = "1000";

  int fd;

  if (-1 == (fd = open ("/proc/self/oom_score_adj", O_WRONLY)))
    return;

  write (fd, setting, strlen (setting));

  close (fd);
}

int
main (int argc, char **argv)
{
  long input0_threshold = 2, input1_threshold = LONG_MAX;
  char *endptr;
  int i;

  while ((i = getopt_long (argc, argv, "", long_options, 0)) != -1)
    {
      switch (i)
        {
        case 0:

          break;

        case 'c':

          cover_threshold = strtol (optarg, &endptr, 0);

          if (*endptr || cover_threshold < 0)
            errx (EX_USAGE, "Parse error in cover threshold, expected non-negative integer");

          break;

        case 'p':

          prior_bias = strtod (optarg, &endptr);

          if (*endptr)
            errx (EX_USAGE, "Parse error in prior bias, expected decimal fraction");

          break;

        case 't':

          threshold = strtod (optarg, &endptr);

          if (*endptr)
            errx (EX_USAGE, "Parse error in probability threshold, expected decimal fraction");

          break;

        case 'T':

          threshold_count = strtol (optarg, &endptr, 0);

          if (*endptr || threshold_count < 0)
            errx (EX_USAGE, "Parse error in threshold count, expected non-negative integer");

          break;

        case '?':

          fprintf (stderr, "Try `%s --help' for more information.\n", argv[0]);

          return EXIT_FAILURE;
        }
    }

  if (print_help)
    {
      printf ("Usage: %s [OPTION]... INPUT1 INPUT2 [INPUT1-MIN [INPUT2-MAX]]\n"
              "\n"
              "      --document             count each prefix only once per document\n"
              "                             documents are delimited by NUL characters\n"
              "      --skip-prefixes        skip prefixes with identical positive counts\n"
              "      --probability          give probability¹ rather than counts\n"
              "      --prior-bias=BIAS      assign BIAS to prior probability\n"
              "      --threshold=PROB       set minimum probability for output\n"
              "      --unique-substrings    suppress normal output and print only the\n"
              "                             unqiue substrings that meet the required\n"
              "                             threshold\n"
              "      --cover                suppress normal output and print only the\n"
              "                             unique substrings that meet the required\n"
              "                             threshold, and that are necessary to cover\n"
              "                             all input documents.\n"
              "                             Implies --document\n"
              "      --help     display this help and exit\n"
              "      --version  display version information\n"
              "\n"
              "1. The probability returned is the probability that a given N-gram belongs in\n"
              "   INPUT-1.  If the input sample is incomplete, you may want to assign some\n"
              "   bias in favor of the prior (i.e. additive smoothing).  A bias of 1 is a\n"
              "   good starting point.\n"
              "\n"
              "Report bugs to <morten.hustveit@gmail.com>\n",
              argv[0]);

      return EXIT_SUCCESS;
    }

  if (print_version)
    errx (EXIT_SUCCESS, "%s", PACKAGE_STRING);

  if (optind + 2 > argc || optind + 4 < argc)
    errx (EX_USAGE, "Usage: %s [OPTION]... INPUT1 INPUT2 [INPUT1-MIN [INPUT2-MAX]]", argv[0]);

  if (do_cover)
    {
      do_unique = 1;
      do_document = 1;
    }

  become_oom_friendly ();

  stdout_is_tty = isatty (1);

  input0 = (const char *) map_file (argv[optind++], &input0_size);
  input1 = (const char *) map_file (argv[optind++], &input1_size);

  if (optind < argc)
    {
      input0_threshold = strtol (argv[optind++], &endptr, 0);

      if (*endptr || input0_threshold < 2)
        errx (EX_USAGE, "Parse error in INPUT1-MIN.  Expected integer greater than or equal to 2");
    }

  if (optind < argc)
    {
      input1_threshold = strtol (argv[optind++], &endptr, 0);

      if (*endptr || input1_threshold < 0)
        errx (EX_USAGE, "Parse error in INPUT2-MAX.  Expected non-negative integer");
    }

  find_substring_frequencies(input0_threshold, input1_threshold);

  return EXIT_SUCCESS;
}
