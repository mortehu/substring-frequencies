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
#include <sysexits.h>

#include <divsufsort.h>

#include "substrings.h"

#define DELIMITER '\0'

const char *input0, *input1;
size_t input0_size, input1_size;

int skip_samecount_prefixes;
int do_probability;
int do_unique;
int do_document;
int do_color;
int do_cover;
int do_words;
double prior_bias = 1.0;
double threshold;
int threshold_count;
int cover_threshold;

int stdout_is_tty;

namespace {

saidx_t *input0_suffixes, *input1_suffixes;
size_t input0_suffix_count, input1_suffix_count;

std::vector<size_t> input0_n_gram_counts;
std::vector<size_t> input1_n_gram_counts;

std::vector<size_t> input0_document_ends;
std::vector<size_t> input1_document_ends;

struct match {
  const char *string;
  size_t string_length;

  double score;

  match(const char *string, size_t string_length, double score)
      : string(string), string_length(string_length), score(score) {}
};

std::vector<match> matches;

struct substring {
  const char *text;
  size_t length;
  size_t count;
};

void PrintString(const char *string, size_t length) {
  const unsigned char *ch = (const unsigned char *)string;

  for (; length--; ++ch) {
    if (do_color && length) {
      if (stdout_is_tty)
        printf("\033[%d;1m", *ch - 'A' + 30);
      else
        putchar(*ch);

      ++ch;
      --length;
    }

    if (isprint(*ch) || (*ch & 0x80)) {
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

  if (do_color) printf("\033[00m");
}

struct CompareInInput1 {
  CompareInInput1(size_t length) : length(length) {}

  // Used by lower_bound.  Behaves like strcmp with regard to length.
  bool operator()(const saidx_t &lhs, const char *rhs) const {
    size_t lhs_length = input1_size - lhs;
    int cmp;

    cmp = memcmp(input1 + lhs, rhs, std::min(length, lhs_length));

    return (cmp < 0 || (cmp == 0 && lhs_length < length));
  }

  // Used by upper_bound.  Only compares equality of prefix.
  bool operator()(const char *lhs, const saidx_t &rhs) const {
    size_t rhs_length = input1_size - rhs;

    if (rhs_length < length) return true;

    return 0 != memcmp(lhs, input1 + rhs, length);
  }

 private:
  size_t length;
};

// Adds the document containing the character at `offset' to the `documents'
// set.  The end point of each document is defined by `document_ends', which
// must be sorted.
void AddDocument(std::set<size_t> *documents,
                 const std::vector<size_t> document_ends, size_t offset) {
  std::vector<size_t>::const_iterator i;

  i = std::lower_bound(document_ends.begin(), document_ends.end(), offset);

  documents->insert(std::distance(document_ends.begin(), i));
}

struct CompareScore {
  bool operator()(const match &lhs, const match &rhs) const {
    if (lhs.score != rhs.score) return lhs.score > rhs.score;

    if (lhs.string_length != rhs.string_length)
      return lhs.string_length > rhs.string_length;

    return 0 >= memcmp(lhs.string, rhs.string, rhs.string_length);
  }
};

void BuildLCPArray(std::vector<size_t> &result, const char *text,
                   size_t text_length, const saidx_t *suffixes,
                   size_t suffix_count) {
  const char *end = text + text_length;

  std::vector<size_t> inverse;

  inverse.resize(text_length, (size_t) - 1);

  for (size_t i = 0; i < suffix_count; ++i) inverse[suffixes[i]] = i;

  result.resize(suffix_count);

  size_t h = 0;

  for (size_t i = 0; i < text_length; ++i) {
    size_t x = inverse[i];

    if (x == (size_t) - 1) {
      if (h > 0) --h;

      continue;
    }

    size_t j = suffixes[x + 1];

    /* The shared prefix of a string starting at offset X is at least as long
     * as the one starting at offset X-1, minus 1.  */

    const char *p1 = text + i + h;
    const char *p0 = text + j + h;

    while (p1 != end && p0 != end && (*p1 != DELIMITER) && *p1++ == *p0++) ++h;

    result[x] = h;

    if (h > 0) --h;
  }
}

void FindSubstrings(size_t input0_threshold, size_t input1_threshold) {
  size_t input1_offset = 0;

  size_t previous_prefix = 0;

  if (!input0_suffix_count) return;

  std::vector<size_t> shared_prefixes;

  shared_prefixes.reserve(input0_suffix_count);

  BuildLCPArray(shared_prefixes, input0, input0_size, input0_suffixes,
                input0_suffix_count);

  std::vector<substring> stack;
  std::set<size_t> matching_documents;

  for (size_t i = 1; i < input0_suffix_count; ++i) {
    size_t prefix;

    prefix = shared_prefixes[i - 1];

    if (prefix > previous_prefix) {
      matching_documents.clear();

      size_t count = 2;
      size_t j = i + 1;

      if (do_document) {
        AddDocument(&matching_documents, input0_document_ends,
                    input0_suffixes[i - 1]);

        AddDocument(&matching_documents, input0_document_ends,
                    input0_suffixes[i]);
      }

      for (size_t length = prefix;
           j <= input0_suffix_count && length > previous_prefix;) {
        if (shared_prefixes[j - 1] < length) {
          substring s;

          if (stack.empty() ||
              (!skip_samecount_prefixes || stack.back().count != count)) {
            s.count = do_document ? matching_documents.size() : count;
            s.length = length;
            s.text = input0 + input0_suffixes[i];

            stack.push_back(s);
          }

          if (do_color) --length;

          --length;

          continue;
        }

        if (do_document) {
          AddDocument(&matching_documents, input0_document_ends,
                      input0_suffixes[j]);
        }

        ++count;
        ++j;
      }

      while (!stack.empty()) {
        size_t input1_substring_count = 0;
        size_t input1_match_end;

        double P_A, P_A_Bx = 0;

        substring s = stack.back();
        stack.pop_back();

        if (s.count < input0_threshold) continue;

        saidx_t *end, *search_result;

        do {
          end = input1_suffixes +
                std::min(input1_offset + 1024, input1_suffix_count);

          search_result = std::lower_bound(input1_suffixes + input1_offset, end,
                                           s.text, CompareInInput1(s.length));

          input1_offset = search_result - input1_suffixes;
        } while (search_result == end &&
                 end != input1_suffixes + input1_suffix_count);

        input1_match_end = input1_offset;

        do {
          end = input1_suffixes +
                std::min(input1_match_end + 1024, input1_suffix_count);

          search_result =
              std::upper_bound(input1_suffixes + input1_match_end, end, s.text,
                               CompareInInput1(s.length));

          input1_match_end = search_result - input1_suffixes;
        } while (search_result == end &&
                 end != input1_suffixes + input1_suffix_count);

        if (do_document) {
          fprintf(stderr, "Clear!\n");
          matching_documents.clear();

          for (size_t i = input1_offset; i != input1_match_end; ++i) {
            AddDocument(&matching_documents, input1_document_ends,
                        input1_suffixes[i]);
          }

          input1_substring_count = matching_documents.size();
        } else
          input1_substring_count = input1_match_end - input1_offset;

        if (input1_substring_count > input1_threshold) continue;

        if (do_words) {
          if (s.text > input0 && !isspace(s.text[-1])) continue;

          if (s.text + s.length < input0 + input0_size &&
              !isspace(s.text[s.length]))
            continue;
        }

        if (do_probability || threshold) {
          size_t n_gram_count0 = 0, n_gram_count1 = 0;

          if (s.length < input0_n_gram_counts.size())
            n_gram_count0 = input0_n_gram_counts[s.length];

          if (s.length < input1_n_gram_counts.size())
            n_gram_count1 = input1_n_gram_counts[s.length];

          /* A = Random N-gram belongs in set 0
           * Bx = N-gram `x' is observed
           */

          /* P(A) */
          P_A = (double)n_gram_count0 / (n_gram_count0 + n_gram_count1);

          /* P(A|Bx + prior bias) */
          P_A_Bx = (double)(s.count + prior_bias) /
                   (s.count + input1_substring_count + prior_bias / P_A);

          if (P_A_Bx < threshold) continue;

          if (do_probability && !do_unique) printf("%.9f\t", P_A_Bx);
        }

        if (!do_probability) {
          P_A_Bx = s.count;

          if (!do_unique) printf("%zu\t%zu\t", s.count, input1_substring_count);
        }

        if (do_unique) {
          if (threshold_count > 0 &&
              matches.size() >= (size_t)threshold_count) {
            if (matches.front().score > P_A_Bx) continue;

            std::pop_heap(matches.begin(), matches.end(), CompareScore());
            matches.pop_back();
          }

          matches.push_back(match(s.text, s.length, P_A_Bx));

          if (threshold_count > 0)
            std::push_heap(matches.begin(), matches.end(), CompareScore());

          continue;
        }

        PrintString(s.text, s.length);
        putchar('\n');
      }
    }

    previous_prefix = prefix;
  }
}

std::vector<size_t> CountNGrams(const char *text, size_t text_size) {
  const char *text_end = text + text_size;
  const char *ch, *next;

  std::vector<size_t> result;

  ch = text;

  while (ch != text_end) {
    if (!(next = (const char *)memchr(ch, DELIMITER, text_end - ch)))
      next = text_end;

    if (result.size() <= (size_t)(next - ch)) result.resize(next - ch + 1);

    for (size_t i = 1; i <= (size_t)(next - ch); ++i) ++result[i];

    if (next == text_end) break;

    ch = next + 1;
  }

  return result;
}

void FindCover(void) {
  std::list<std::pair<const char *, size_t> > remaining_documents;
  const char *start = input0, *end;
  size_t i;

  std::sort(matches.begin(), matches.end(), CompareScore());

  for (i = 0; i < input0_document_ends.size(); ++i) {
    end = input0 + input0_document_ends[i];

    remaining_documents.push_back(std::make_pair(start, end - start));

    start = end + 1;
  }

  std::vector<match>::const_iterator j = matches.begin();

  for (j = matches.begin(); j != matches.end() && !remaining_documents.empty();
       ++j) {
    const char *string_begin = j->string;
    size_t string_length = j->string_length;

    std::list<std::pair<const char *, size_t> >::iterator k;
    int hits = 0;

    for (k = remaining_documents.begin(); k != remaining_documents.end();) {
      if (memmem(k->first, k->second, string_begin, string_length)) {
        k = remaining_documents.erase(k);

        ++hits;
      } else
        ++k;
    }

    if (hits > cover_threshold) {
      printf("%d\t", hits);
      PrintString(string_begin, string_length);
      putchar('\n');
    }
  }
}

struct superstring_comparator {
  superstring_comparator(const match &base) : base(base) {}

  bool operator()(const match &rhs) const {
    return NULL != memmem(base.string, base.string_length, rhs.string,
                          rhs.string_length);
  }

 private:
  match base;
};

struct length_comparator {
  bool operator()(const match &lhs, const match &rhs) const {
    if (lhs.string_length != rhs.string_length)
      return lhs.string_length < rhs.string_length;

    return 0 >= memcmp(lhs.string, rhs.string, lhs.string_length);
  }
};

struct string_comparator {
  bool operator()(const match &lhs, const match &rhs) const {
    int ret;

    ret = memcmp(lhs.string, rhs.string,
                 std::min(lhs.string_length, rhs.string_length));

    if (ret == 0)
      return (lhs.string_length < rhs.string_length);
    else
      return (ret < 0);
  }
};

void PrintUnique(void) {
  std::vector<match> unique_substrings;

  std::sort(matches.begin(), matches.end(), length_comparator());

  std::vector<match>::const_iterator i;

  for (i = matches.begin(); i != matches.end(); ++i) {
    if (unique_substrings.end() != std::find_if(unique_substrings.begin(),
                                                unique_substrings.end(),
                                                superstring_comparator(*i)))
      continue;

    unique_substrings.push_back(*i);
  }

  std::sort(unique_substrings.begin(), unique_substrings.end(),
            string_comparator());

  std::vector<match>::const_iterator j;

  for (j = unique_substrings.begin(); j != unique_substrings.end(); ++j) {
    PrintString(j->string, j->string_length);
    putchar('\n');
  }
}

void FindDocumentBounds(std::vector<size_t> &document_ends, const char *text,
                        size_t text_size) {
  const char *text_end = text + text_size;
  const char *ch, *next;

  ch = text;

  while (ch != text_end) {
    if (!(next = (const char *)memchr(ch, DELIMITER, text_end - ch)))
      next = text_end;

    document_ends.push_back(next - text);

    if (next == text_end) break;

    ch = next + 1;
  }

  assert(!document_ends.empty());
}

size_t FilterSuffixes(saidx_t *input, const char *text, size_t count) {
  saidx_t *output, *i, *end;

  i = input;
  end = i + count;

  output = input;

  for (; i != end; ++i) {
    int ch;

    if (do_color && (*i & 1)) continue;

    ch = (unsigned char)text[*i];

    if (ch == DELIMITER) continue;

    if ((ch & 0xc0) == 0x80) continue;

    *output++ = *i;
  }

  return output - input;
}

}  // namespace

void FindSubstringFrequencies(size_t input0_threshold,
                              size_t input1_threshold) {
  if (!(input0_suffixes =
            (saidx_t *)calloc(sizeof(*input0_suffixes), input0_size)))
    errx(EX_OSERR, "calloc failed");

  if (!(input1_suffixes =
            (saidx_t *)calloc(sizeof(*input1_suffixes), input1_size)))
    errx(EX_OSERR, "calloc failed");

  if (do_probability || threshold) {
    input0_n_gram_counts = CountNGrams(input0, input0_size);
    input1_n_gram_counts = CountNGrams(input1, input1_size);
  }

  if (do_document) {
    FindDocumentBounds(input0_document_ends, input0, input0_size);
    FindDocumentBounds(input1_document_ends, input1, input1_size);
  }

  divsufsort((const sauchar_t *)input0, input0_suffixes, input0_size);
  divsufsort((const sauchar_t *)input1, input1_suffixes, input1_size);

  input0_suffix_count = FilterSuffixes(input0_suffixes, input0, input0_size);
  input1_suffix_count = FilterSuffixes(input1_suffixes, input1, input1_size);

  if (do_unique) skip_samecount_prefixes = 1;

  FindSubstrings(input0_threshold, input1_threshold);

  if (do_cover)
    FindCover();
  else if (do_unique)
    PrintUnique();
}
