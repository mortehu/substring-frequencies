#include <algorithm>
#include <cassert>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <set>
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
static int do_unique;
static int do_document;
static int do_color;
static int do_cover;
static double prior_bias = 1.0;
static double threshold;
static int threshold_count;
static int cover_threshold;

static int stdout_is_tty;

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
  { "version",           no_argument,       &print_version,             1 },
  { "help",              no_argument,       &print_help,                1 },
  { 0, 0, 0, 0 }
};

static const char *input0, *input1;
static size_t input0_size, input1_size;

static saidx_t *input0_suffixes, *input1_suffixes;
size_t input0_suffix_count, input1_suffix_count;

static std::vector<size_t> input0_n_gram_counts;
static std::vector<size_t> input1_n_gram_counts;

static std::vector<size_t> input0_document_ends;
static std::vector<size_t> input1_document_ends;

struct match
{
  const char *string;
  size_t string_length;

  double score;

  match (const char *string, size_t string_length, double score)
    : string (string),
      string_length (string_length),
      score (score)
  {
  }
};

static std::vector<match> matches;

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
print_string (const char *string, size_t length)
{
  const unsigned char *ch = (const unsigned char *) string;

  for (; length--; ++ch)
    {
      if (do_color && length)
        {
          if (stdout_is_tty)
            printf ("\033[%d;1m", *ch - 'A' + 30);
          else
            putchar (*ch);

          ++ch;
          --length;
        }

      if (isprint (*ch) || (*ch & 0x80))
        {
          putchar (*ch);

          continue;
        }

      putchar ('\\');

      switch (*ch)
        {
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

  if (do_color)
    printf ("\033[00m");
}

struct compare_in_input1
{
  compare_in_input1 (size_t length)
    : length(length)
  {
  }

  /* Used by lower_bound.  Behaves like strcmp with regard to length */
  bool operator()(const saidx_t &lhs, const char *rhs) const
    {
      size_t lhs_length = input1_size - lhs;
      int cmp;

      cmp = memcmp (input1 + lhs, rhs, std::min (length, lhs_length));

      return (cmp < 0 || (cmp == 0 && lhs_length < length));
    }

  /* Used by upper_bound.  Only compares equality of prefix */
  bool operator()(const char *lhs, const saidx_t &rhs) const
    {
      size_t rhs_length = input1_size - rhs;

      if (rhs_length < length)
        return true;

      return 0 != memcmp (lhs, input1 + rhs, length);
    }

private:

  size_t length;
};

static void
add_document (std::set<size_t> &documents,
              const std::vector<size_t> document_ends,
              size_t offset)
{
  std::vector<size_t>::const_iterator i;

  i = std::lower_bound (document_ends.begin (), document_ends.end (), offset);

  documents.insert (std::distance (document_ends.begin (), i));
}

struct score_comparator
{
  bool operator() (const match &lhs, const match &rhs) const
    {
      if (lhs.score != rhs.score)
        return lhs.score > rhs.score;

      if (lhs.string_length != rhs.string_length)
        return lhs.string_length > rhs.string_length;

      return 0 >= memcmp (lhs.string, rhs.string, rhs.string_length);
    }
};

static void
find_substrings (size_t input0_threshold, size_t input1_threshold)
{
  size_t input1_offset = 0;

  const char *input0_end;
  size_t previous_prefix = 0;

  if (!input0_suffix_count)
    return;

  input0_end = input0 + input0_size;

  std::vector<size_t> shared_prefixes;

  shared_prefixes.reserve (input0_suffix_count);

  for (size_t i = 0; i + 1 < input0_suffix_count; ++i)
    {
      const char *lhs, *rhs;
      size_t shared_prefix;

      lhs = input0 + input0_suffixes[i];
      rhs = input0 + input0_suffixes[i + 1];

      shared_prefix = common_prefix (lhs, rhs,
                                     std::min ((size_t) (input0_end - lhs),
                                               (size_t) (input0_end - rhs)));

      if (do_color)
        shared_prefix &= ~1;

      shared_prefixes.push_back (shared_prefix);
    }

  std::vector<substring> stack;
  std::set<size_t> matching_documents;

  for (size_t i = 1; i < input0_suffix_count; ++i)
    {
      size_t prefix;

      prefix = shared_prefixes[i - 1];

      if (prefix > previous_prefix)
        {
          matching_documents.clear ();

          size_t count = 2;
          size_t j = i + 1;

          if (do_document)
            {
              add_document (matching_documents,
                            input0_document_ends,
                            input0_suffixes[i - 1]);

              add_document (matching_documents,
                            input0_document_ends,
                            input0_suffixes[i]);
            }

          for (size_t length = prefix; j <= input0_suffix_count && length > previous_prefix; )
            {
              if (shared_prefixes[j - 1] < length)
                {
                  substring s;

                  if (stack.empty () || (!skip_samecount_prefixes || stack.back ().count != count))
                    {
                      s.count = do_document ? matching_documents.size () : count;
                      s.length = length;
                      s.text = input0 + input0_suffixes[i];

                      stack.push_back (s);
                    }

                  if (do_color)
                    length -= 2;
                  else
                    --length;

                  continue;
                }

              if (do_document)
                {
                  add_document (matching_documents,
                                input0_document_ends,
                                input0_suffixes[j]);
                }

              ++count;
              ++j;
            }

          while (!stack.empty ())
            {
              size_t input1_substring_count = 0;
              size_t input1_match_end;

              double P_A, P_A_Bx = 0;

              substring s = stack.back ();
              stack.pop_back ();

              if (s.count < input0_threshold)
                continue;

              saidx_t *end, *search_result;

              do
                {
                  end = input1_suffixes + std::min (input1_offset + 1024, input1_suffix_count);

                  search_result = std::lower_bound (input1_suffixes + input1_offset, end,
                                                    s.text, compare_in_input1 (s.length));

                  input1_offset = search_result - input1_suffixes;
                }
              while (search_result == end && end != input1_suffixes + input1_suffix_count);

              input1_match_end = input1_offset;

              do
                {
                  end = input1_suffixes + std::min (input1_match_end + 1024, input1_suffix_count);

                  search_result = std::upper_bound (input1_suffixes + input1_match_end, end,
                                                    s.text, compare_in_input1 (s.length));

                  input1_match_end = search_result - input1_suffixes;
                }
              while (search_result == end && end != input1_suffixes + input1_suffix_count);

              if (do_document)
                {
                  matching_documents.clear ();

                  for (size_t i = input1_offset; i != input1_match_end; ++i)
                    {
                      add_document (matching_documents,
                                    input1_document_ends,
                                    input1_suffixes[i]);
                    }

                  input1_substring_count = matching_documents.size ();
                }
              else
                input1_substring_count = input1_match_end - input1_offset;

              if (input1_substring_count > input1_threshold)
                continue;

              if (do_probability || threshold)
                {
                  size_t n_gram_count0 = 0, n_gram_count1 = 0;

                  if (s.length < input0_n_gram_counts.size ())
                    n_gram_count0 = input0_n_gram_counts[s.length];

                  if (s.length < input1_n_gram_counts.size ())
                    n_gram_count1 = input1_n_gram_counts[s.length];

                  /* A = Random N-gram belongs in set 0
                   * Bx = N-gram `x' is observed
                   */

                  /* P(A) */
                  P_A = (double) n_gram_count0 / (n_gram_count0 + n_gram_count1);

                  /* P(A|Bx + prior bias) */
                  P_A_Bx = (double) (s.count + prior_bias) / (s.count + input1_substring_count + prior_bias / P_A);

                  if (P_A_Bx < threshold)
                    continue;

                  if (do_probability && !do_unique)
                    printf ("%.9f\t", P_A_Bx);
                }

              if (!do_probability)
                {
                  P_A_Bx = s.count;

                  if (!do_unique)
                    printf ("%zu\t%zu\t", s.count, input1_substring_count);
                }

              if (do_unique)
                {
                  if (threshold_count > 0
                      && matches.size () >= (size_t) threshold_count)
                    {
                      if (matches.front ().score > P_A_Bx)
                        continue;

                      std::pop_heap (matches.begin (), matches.end (), score_comparator ());
                      matches.pop_back ();
                    }

                  matches.push_back (match (s.text, s.length, P_A_Bx));

                  if (threshold_count > 0)
                    std::push_heap (matches.begin (), matches.end (), score_comparator ());

                  continue;
                }

              print_string (s.text, s.length);
              putchar ('\n');
            }
        }

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

      if (result.size () <= (size_t) (next - ch))
        result.resize (next - ch + 1);

      for (size_t i = 1; i <= (size_t) (next - ch); ++i)
        ++result[i];

      if (next == text_end)
        break;

      ch = next + 1;
    }

  return result;
}

static void
find_cover (void)
{
  std::list<std::pair<const char *, size_t> > remaining_documents;
  const char *start = input0, *end;
  size_t i;

  std::sort (matches.begin (), matches.end (), score_comparator ());

  for (i = 0; i < input0_document_ends.size (); ++i)
    {
      end = input0 + input0_document_ends[i];

      remaining_documents.push_back (std::make_pair (start, end - start));

      start = end + 1;
    }

  std::vector<match>::const_iterator j = matches.begin ();

  for (j = matches.begin ();
       j != matches.end () && !remaining_documents.empty (); ++j)
    {
      const char *string_begin = j->string;
      size_t string_length = j->string_length;

      std::list<std::pair<const char *, size_t> >::iterator k;
      int hits = 0;

      for (k = remaining_documents.begin ();
           k != remaining_documents.end (); )
        {
          if (memmem (k->first, k->second,
                      string_begin, string_length))
            {
              k = remaining_documents.erase (k);

              ++hits;
            }
          else
            ++k;
        }

      if (hits > cover_threshold)
        {
          printf ("%d\t", hits);
          print_string (string_begin, string_length);
          putchar ('\n');
        }
    }
}

struct superstring_comparator
{
  superstring_comparator (const match &base)
    : base(base)
    {
    }

  bool operator() (const match &rhs) const
    {
      return NULL != memmem (base.string, base.string_length,
                             rhs.string, rhs.string_length);
    }

private:

  match base;
};

struct length_comparator
{
  bool operator() (const match &lhs, const match &rhs) const
    {
      if (lhs.string_length != rhs.string_length)
        return lhs.string_length < rhs.string_length;

      return 0 >= memcmp (lhs.string, rhs.string, lhs.string_length);
    }
};

struct string_comparator
{
  bool operator() (const match &lhs, const match &rhs) const
    {
      int ret;

      ret = memcmp (lhs.string, rhs.string, std::min (lhs.string_length, rhs.string_length));

      if (ret == 0)
        return (lhs.string_length < rhs.string_length);
      else
        return (ret < 0);
    }
};

static void
print_unique (void)
{
  std::vector<match> unique_substrings;

  std::sort (matches.begin (), matches.end (), length_comparator ());

  std::vector<match>::const_iterator i;

  for (i = matches.begin (); i != matches.end (); ++i)
    {
      if (unique_substrings.end () != std::find_if (unique_substrings.begin (), unique_substrings.end (),
                                                    superstring_comparator (*i)))
        continue;

      unique_substrings.push_back (*i);
    }

  std::sort (unique_substrings.begin (), unique_substrings.end (), string_comparator ());

  std::vector<match>::const_iterator j;

  for (j = unique_substrings.begin (); j != unique_substrings.end (); ++j)
    {
      print_string (j->string, j->string_length);
      putchar ('\n');
    }
}

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

static void
find_document_bounds (std::vector<size_t>& document_ends,
                      const char *text, size_t text_size)
{
  const char *text_end = text + text_size;
  const char *ch, *next;

  ch = text;

  while (ch != text_end)
    {
      if (!(next = (const char *) memchr (ch, DELIMITER, text_end - ch)))
        next = text_end;

      document_ends.push_back (next - text);

      if (next == text_end)
        break;

      ch = next + 1;
    }

  assert (!document_ends.empty ());
}

static size_t
filter_suffixes (saidx_t *input, const char *text, size_t count)
{
  saidx_t *output, *i, *end;

  i = input;
  end = i + count;

  output = input;

  for (; i != end; ++i)
    {
      int ch;

      if (do_color && (*i & 1))
        continue;

      ch = (unsigned char) text[*i];

      if (ch == DELIMITER)
        continue;

      if ((ch & 0xc0) == 0x80)
        continue;

      *output++ = *i;
    }

  return output - input;
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

  if (!(input0_suffixes = (saidx_t *) calloc (sizeof (*input0_suffixes), input0_size)))
    errx (EX_OSERR, "calloc failed");

  if (!(input1_suffixes = (saidx_t *) calloc (sizeof (*input1_suffixes), input1_size)))
    errx (EX_OSERR, "calloc failed");

  if (do_probability || threshold)
    {
      input0_n_gram_counts = count_n_grams (input0, input0_size);
      input1_n_gram_counts = count_n_grams (input1, input1_size);
    }

  if (do_document)
    {
      find_document_bounds (input0_document_ends, input0, input0_size);
      find_document_bounds (input1_document_ends, input1, input1_size);
    }

  divsufsort ((const sauchar_t *) input0, input0_suffixes, input0_size);
  divsufsort ((const sauchar_t *) input1, input1_suffixes, input1_size);

  input0_suffix_count = filter_suffixes (input0_suffixes, input0, input0_size);
  input1_suffix_count = filter_suffixes (input1_suffixes, input1, input1_size);

  if (do_unique)
    skip_samecount_prefixes = 1;

  find_substrings (input0_threshold, input1_threshold);

  if (do_cover)
    find_cover ();
  else if (do_unique)
    print_unique ();

  return EXIT_SUCCESS;
}
