#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <err.h>
#include <fcntl.h>
#include <getopt.h>
#include <sysexits.h>
#include <sys/mman.h>
#include <unistd.h>

#include <divsufsort.h>

#define DELIMITER '\0'

static int print_version;
static int print_help;
static int skip_samecount_prefixes;
static int do_probability;
static double prior_bias;

static struct option long_options[] =
{
  { "skip-prefixes",     no_argument,       &skip_samecount_prefixes, 1 },
  { "probability",       no_argument,       &do_probability,          1 },
  { "prior-bias",        required_argument, NULL,                     'p' },
  { "version",           no_argument,       &print_version,           1 },
  { "help",              no_argument,       &print_help,              1 },
  { 0, 0, 0, 0 }
};

static const char *input0;
static saidx_t *input0_suffixes;
static off_t input0_size;

static const char *input1;
static saidx_t *input1_suffixes;
static off_t input1_size;

static std::vector<size_t> input0_n_gram_counts;
static std::vector<size_t> input1_n_gram_counts;

static void *
map_file (const char *path, off_t *ret_size)
{
  off_t size;
  void *map;
  int fd;

  if (-1 == (fd = open (path, O_RDONLY)))
    err (EX_NOINPUT, "Could not open '%s' for reading", path);

  if (-1 == (size = lseek (fd, 0, SEEK_END)))
    err (EX_IOERR, "Could not seek to end of '%s'", path);

  if (MAP_FAILED == (map = mmap (NULL, size, PROT_READ, MAP_SHARED, fd, 0)))
    err (EX_IOERR, "Could not memory-map '%s'", path);

  close (fd);

  if (ret_size)
    *ret_size = size;

  return map;
}

static size_t
common_prefix (const char *lhs, const char *rhs, size_t length)
{
  size_t result = 0;

  while (result < length && *lhs != DELIMITER && *lhs++ == *rhs++)
    ++result;

  return result;
}

struct substring
{
  const char *text;
  size_t length;
  size_t count;
};

static void
find_substrings (long input0_threshold, long input1_threshold)
{
  size_t input1_offset = 0;

  const char *input0_end;
  const char *previous;
  size_t previous_prefix = 0;

  if (!input0_size)
    return;

  input0_end = input0 + input0_size;

  previous = input0 + input0_suffixes[0];

  std::vector<substring> stack;

  for (size_t i = 1; i < input0_size; ++i)
    {
      const char *current;
      size_t prefix;

      current = input0 + input0_suffixes[i];

      if (current[0] == DELIMITER)
        continue;

      prefix = common_prefix (current, previous,
                              std::min (input0_end - current, input0_end - previous));

      if (prefix > previous_prefix)
        {
          size_t count = 2;
          size_t j = i + 1;

          for (size_t length = prefix; j <= input0_size && length > previous_prefix; )
            {
              const char *test;
              size_t test_prefix;

              if (j < input0_size)
                {
                  test = input0 + input0_suffixes[j];

                  test_prefix = common_prefix (current, test,
                                               std::min ((size_t) (input0_end - test), length));
                }
              else
                test_prefix = 0;

              if (test_prefix < length)
                {
                  substring s;

                  if (stack.empty () || (!skip_samecount_prefixes || stack.back ().count != count))
                    {
                      s.count = count;
                      s.length = length;
                      s.text = input0 + input0_suffixes[i];

                      stack.push_back (s);
                    }

                  --length;

                  continue;
                }

              ++count;
              ++j;
            }

          while (!stack.empty ())
            {
              size_t input1_substring_count = 0;

              substring s = stack.back ();
              stack.pop_back ();

              if (s.count < input0_threshold)
                continue;

              while (input1_offset < input1_size)
                {
                  const char *string1;
                  size_t length1, cmp_length;
                  int cmp;

                  string1 = input1 + input1_suffixes[input1_offset];
                  length1 = input1_size - input1_suffixes[input1_offset];

                  cmp_length = std::min (s.length, length1);

                  cmp = memcmp (string1, s.text, cmp_length);

                  if (cmp > 0)
                    break;

                  if (cmp < 0 || length1 < s.length)
                    {
                      ++input1_offset;

                      continue;
                    }

                  /* Substring exists in set 1 */
                  input1_substring_count = 1;

                  for (size_t k = input1_offset + 1; k < input1_size; ++k)
                    {
                      if (input1_size - input1_suffixes[k] < s.length)
                        break;

                      if (memcmp (input1 + input1_suffixes[k], s.text, s.length))
                        break;

                      ++input1_substring_count;
                    }

                  break;
                }

              if (input1_substring_count > input1_threshold)
                continue;

              if (do_probability)
                {
                  size_t n_gram_count0 = 0, n_gram_count1 = 0;

                  if (s.length < input0_n_gram_counts.size ())
                    n_gram_count0 = input0_n_gram_counts[s.length];

                  if (s.length < input1_n_gram_counts.size ())
                    n_gram_count1 = input1_n_gram_counts[s.length];

                  /* A = N-gram belongs in set 0
                   * Bx = N-gram `x' is observed
                   */

                  /* P(A) */
                  double P_A = (double) n_gram_count0 / (n_gram_count0 + n_gram_count1);

                  /* P(A|Bx + prior bias) */
                  double P_A_Bx = (double) (s.count + prior_bias) / (s.count + input1_substring_count + prior_bias / P_A);

                  printf ("%.7f\t", P_A_Bx);
                }
              else
                printf ("%zu\t%zu\t", s.count, input1_substring_count);

              for (const char *ch = s.text; ch != s.text + s.length; ++ch)
                {
                  if (isprint (*ch))
                    {
                      putchar (*ch);

                      continue;
                    }

                  putchar ('\\');

                  switch (*ch)
                    {
                    case '\0': putchar ('0'); break;
                    case '\a': putchar ('a'); break;
                    case '\b': putchar ('b'); break;
                    case '\t': putchar ('t'); break;
                    case '\n': putchar ('n'); break;
                    case '\v': putchar ('v'); break;
                    case '\f': putchar ('f'); break;
                    case '\r': putchar ('r'); break;
                    case '\\': putchar ('\\'); break;
                    default: printf ("%03o", (unsigned char) *ch);
                    }
                }

              putchar ('\n');
            }
        }

      previous = current;
      previous_prefix = prefix;
    }
}

std::vector<size_t>
count_n_grams (const char *text, size_t text_size)
{
  const char *text_end = text + text_size;
  const char *ch, *next;

  std::vector<size_t> result;

  ch = text;

  while (ch != text_end)
    {
      if (!(next = (const char *) memchr (ch, DELIMITER, text_end - ch)))
        next = text_end;

      if (result.size () <= next - ch)
        result.resize (next - ch + 1);

      for (size_t i = 1; i <= next - ch; ++i)
        ++result[i];

      if (next == text_end)
        break;

      ch = next + 1;
    }

  return result;
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

        case 'p':

          prior_bias = strtod (optarg, &endptr);

          if (*endptr)
            errx (EX_USAGE, "Parse error in prior bias, expected decimal fraction");

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
              "      --skip-prefixes        skip prefixes with identical positive counts\n"
              "      --probability          give probability¹ rather than counts\n"
              "      --prior-bias=BIAS      assign BIAS to prior probability\n"
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

  if (!(input0_suffixes = (saidx_t *) calloc (sizeof (*input0_suffixes), input0_size)))
    errx (EX_OSERR, "calloc failed");

  if (!(input1_suffixes = (saidx_t *) calloc (sizeof (*input1_suffixes), input1_size)))
    errx (EX_OSERR, "calloc failed");

  if (do_probability)
    {
      input0_n_gram_counts = count_n_grams (input0, input0_size);
      input1_n_gram_counts = count_n_grams (input1, input1_size);
    }

  divsufsort ((const sauchar_t *) input0, input0_suffixes, input0_size);

  divsufsort ((const sauchar_t *) input1, input1_suffixes, input1_size);

  find_substrings (input0_threshold, input1_threshold);

  return EXIT_SUCCESS;
}
