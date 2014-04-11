#include <algorithm>
#include <cassert>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <list>
#include <set>
#include <vector>

#include <err.h>
#include <sysexits.h>

#include "substrings.h"

#define DELIMITER '\0'

namespace {

struct Match {
  const char* string;
  size_t string_length;

  double score;

  Match(const char* string, size_t string_length, double score)
      : string(string), string_length(string_length), score(score) {}
};

std::vector<Match> matches;

struct Substring {
  const char* text;
  size_t length;
  size_t count;
};

struct CompareInBlock {
  CompareInBlock(size_t length, const char* data, size_t size)
      : data_(data), size_(size) {}

  // Returns true if the string at offset `lhs' is less than `rhs'.  Used by
  // std::lower_bound().
  bool operator()(const saidx_t& lhs, const Substring& rhs) const {
    size_t lhs_length = size_ - lhs;

    int cmp = memcmp(data_ + lhs, rhs.text, std::min(rhs.length, lhs_length));

    return cmp < 0 || (cmp == 0 && lhs_length < rhs.length);
  }

  // Returns true if `lhs' is less than the string at offset `rhs'.  Used by
  // upper_bound().
  bool operator()(const Substring& lhs, const saidx_t& rhs) const {
    size_t rhs_length = size_ - rhs;
    int cmp = memcmp(lhs.text, data_ + rhs, std::min(lhs.length, rhs_length));

    return cmp < 0 ||
           (cmp == 0 && lhs.length < rhs_length && data_[rhs + lhs.length]);
  }

 private:
  const char* data_;
  size_t size_;
};

struct CompareScore {
  bool operator()(const Match& lhs, const Match& rhs) const {
    if (lhs.score != rhs.score) return lhs.score > rhs.score;

    if (lhs.string_length != rhs.string_length)
      return lhs.string_length > rhs.string_length;

    return 0 >= memcmp(lhs.string, rhs.string, rhs.string_length);
  }
};

struct CompareSuperstrings {
  CompareSuperstrings(const Match& base) : base(base) {}

  bool operator()(const Match& rhs) const {
    return nullptr != memmem(base.string, base.string_length, rhs.string,
                             rhs.string_length);
  }

 private:
  Match base;
};

struct CompareLength {
  bool operator()(const Match& lhs, const Match& rhs) const {
    if (lhs.string_length != rhs.string_length)
      return lhs.string_length < rhs.string_length;

    return 0 >= memcmp(lhs.string, rhs.string, lhs.string_length);
  }
};

struct StringComparator {
  bool operator()(const Match& lhs, const Match& rhs) const {
    int ret;

    ret = memcmp(lhs.string, rhs.string,
                 std::min(lhs.string_length, rhs.string_length));

    if (ret == 0)
      return (lhs.string_length < rhs.string_length);
    else
      return (ret < 0);
  }
};

}  // namespace

// Adds the document containing the character at `offset' to the `documents'
// set.  The end point of each document is defined by `document_ends', which
// must be sorted.
void CommonSubstringFinder::AddDocument(
    std::set<size_t>* documents, const std::vector<size_t>& document_ends,
    size_t offset) {
  std::vector<size_t>::const_iterator i;

  i = std::lower_bound(document_ends.begin(), document_ends.end(), offset);

  documents->insert(std::distance(document_ends.begin(), i));
}

void CommonSubstringFinder::BuildLCPArray(std::vector<size_t>& result,
                                          const char* text, size_t text_length,
                                          const saidx_t* suffixes,
                                          size_t suffix_count) {
  static const size_t kInvalidOffset = static_cast<size_t>(-1);

  const char* end = text + text_length;

  std::vector<size_t> inverse;

  inverse.resize(text_length, kInvalidOffset);

  for (size_t i = 0; i + 1 < suffix_count; ++i) inverse[suffixes[i]] = i;

  result.resize(suffix_count);

  size_t h = 0;

  for (size_t i = 0; i < text_length; ++i) {
    size_t x = inverse[i];

    if (x == kInvalidOffset) {
      if (h > 0) --h;

      continue;
    }

    size_t j = suffixes[x + 1];

    /* The shared prefix of a string starting at offset X is at least as long
     * as the one starting at offset X-1, minus 1.  */

    const char* p1 = text + i + h;
    const char* p0 = text + j + h;

    while (p1 != end && p0 != end && (*p1 != DELIMITER) && *p1++ == *p0++) ++h;

    result[x] = h;

    if (h > 0) --h;
  }
}

void CommonSubstringFinder::FindSubstrings(size_t input0_threshold,
                                           size_t input1_threshold) {
  size_t input1_offset = 0;

  size_t previous_prefix = 0;

  if (!input0_suffix_count_) return;

  std::vector<size_t> shared_prefixes;

  shared_prefixes.reserve(input0_suffix_count_);

  BuildLCPArray(shared_prefixes, input0, input0_size, input0_suffixes_,
                input0_suffix_count_);

  std::vector<Substring> stack;
  std::set<size_t> matching_documents;

  for (size_t i = 1; i < input0_suffix_count_; ++i) {
    size_t prefix;

    prefix = shared_prefixes[i - 1];

    if (prefix > previous_prefix) {
      matching_documents.clear();

      size_t count = 2;
      size_t j = i + 1;

      if (do_document) {
        AddDocument(&matching_documents, input0_document_ends_,
                    input0_suffixes_[i - 1]);

        AddDocument(&matching_documents, input0_document_ends_,
                    input0_suffixes_[i]);
      }

      for (size_t length = prefix;
           j <= input0_suffix_count_ && length > previous_prefix;) {
        if (shared_prefixes[j - 1] < length) {
          Substring s;

          if (stack.empty() ||
              (!skip_samecount_prefixes || stack.back().count != count)) {
            s.count = do_document ? matching_documents.size() : count;
            s.length = length;
            s.text = input0 + input0_suffixes_[i];

            stack.push_back(s);
          }

          if (do_color) --length;

          --length;

          continue;
        }

        if (do_document) {
          AddDocument(&matching_documents, input0_document_ends_,
                      input0_suffixes_[j]);
        }

        ++count;
        ++j;
      }

      while (!stack.empty()) {
        size_t input1_substring_count = 0;
        size_t input1_match_end;

        double P_A, P_A_Bx = 0;

        Substring s = stack.back();
        stack.pop_back();

        if (s.count < input0_threshold) continue;

        saidx_t* end, *search_result;

        // Look for first suffix in input1 that is greater than or equal to the
        // currently processed suffix.
        //
        // Instead of a linear search or a binary search, we perform a series
        // of binary searches over at most 1024 elements.
        do {
          end = input1_suffixes_ +
                std::min(input1_offset + 1024, input1_suffix_count_);

          search_result =
              std::lower_bound(input1_suffixes_ + input1_offset, end, s,
                               CompareInBlock(s.length, input1, input1_size));

          input1_offset = search_result - input1_suffixes_;
        } while (search_result == end &&
                 end != input1_suffixes_ + input1_suffix_count_);

        input1_match_end = input1_offset;

        do {
          end = input1_suffixes_ +
                std::min(input1_match_end + 1024, input1_suffix_count_);

          search_result =
              std::upper_bound(input1_suffixes_ + input1_match_end, end, s,
                               CompareInBlock(s.length, input1, input1_size));

          input1_match_end = search_result - input1_suffixes_;
        } while (search_result == end &&
                 end != input1_suffixes_ + input1_suffix_count_);

        if (do_document) {
          matching_documents.clear();

          for (size_t i = input1_offset; i != input1_match_end; ++i) {
            AddDocument(&matching_documents, input1_document_ends_,
                        input1_suffixes_[i]);
          }

          input1_substring_count = matching_documents.size();
        } else {
          input1_substring_count = input1_match_end - input1_offset;
        }

        if (input1_substring_count > input1_threshold) continue;

        if (do_words) {
          if (s.text > input0 && !isspace(s.text[-1])) continue;

          if (s.text + s.length < input0 + input0_size &&
              !isspace(s.text[s.length]))
            continue;
        }

        if (do_probability || threshold) {
          size_t n_gram_count0 = 0, n_gram_count1 = 0;

          if (s.length < input0_n_gram_counts_.size())
            n_gram_count0 = input0_n_gram_counts_[s.length];

          if (s.length < input1_n_gram_counts_.size())
            n_gram_count1 = input1_n_gram_counts_[s.length];

          /* A = Random N-gram belongs in set 0
           * Bx = N-gram `x' is observed
           */

          /* P(A) */
          P_A = (double)n_gram_count0 / (n_gram_count0 + n_gram_count1);

          /* P(A|Bx + prior bias) */
          P_A_Bx = (double)(s.count + prior_bias) /
                   (s.count + input1_substring_count + prior_bias / P_A);

          if (P_A_Bx < threshold) continue;
        }

        if (!do_probability) P_A_Bx = s.count;

        if (do_unique) {
          if (threshold_count > 0 &&
              matches.size() >= (size_t)threshold_count) {
            if (matches.front().score > P_A_Bx) continue;

            std::pop_heap(matches.begin(), matches.end(), CompareScore());
            matches.pop_back();
          }

          matches.push_back(Match(s.text, s.length, P_A_Bx));

          if (threshold_count > 0)
            std::push_heap(matches.begin(), matches.end(), CompareScore());

          continue;
        }

        output(P_A_Bx, input1_substring_count, s.text, s.length);
      }
    }

    previous_prefix = prefix;
  }
}

std::vector<size_t> CommonSubstringFinder::CountNGrams(const char* text,
                                                       size_t text_size) {
  const char* text_end = text + text_size;
  const char* ch, *next;

  std::vector<size_t> result;

  ch = text;

  while (ch != text_end) {
    if (!(next = (const char*)memchr(ch, DELIMITER, text_end - ch)))
      next = text_end;

    if (result.size() <= (size_t)(next - ch)) result.resize(next - ch + 1);

    for (size_t i = 1; i <= (size_t)(next - ch); ++i) ++result[i];

    if (next == text_end) break;

    ch = next + 1;
  }

  return result;
}

void CommonSubstringFinder::FindCover(void) {
  std::list<std::pair<const char*, size_t> > remaining_documents;
  const char* start = input0, *end;
  size_t i;

  std::sort(matches.begin(), matches.end(), CompareScore());

  for (i = 0; i < input0_document_ends_.size(); ++i) {
    end = input0 + input0_document_ends_[i];

    remaining_documents.push_back(std::make_pair(start, end - start));

    start = end + 1;
  }

  std::vector<Match>::const_iterator j = matches.begin();

  for (j = matches.begin(); j != matches.end() && !remaining_documents.empty();
       ++j) {
    const char* string_begin = j->string;
    size_t string_length = j->string_length;

    std::list<std::pair<const char*, size_t> >::iterator k;
    int hits = 0;

    for (k = remaining_documents.begin(); k != remaining_documents.end();) {
      if (memmem(k->first, k->second, string_begin, string_length)) {
        k = remaining_documents.erase(k);

        ++hits;
      } else
        ++k;
    }

    if (hits > cover_threshold) output(hits, 0, string_begin, string_length);
  }
}

void CommonSubstringFinder::OutputUnique() {
  std::vector<Match> unique_substrings;

  std::sort(matches.begin(), matches.end(), CompareLength());

  std::vector<Match>::const_iterator i;

  for (i = matches.begin(); i != matches.end(); ++i) {
    if (unique_substrings.end() != std::find_if(unique_substrings.begin(),
                                                unique_substrings.end(),
                                                CompareSuperstrings(*i)))
      continue;

    unique_substrings.push_back(*i);
  }

  std::sort(unique_substrings.begin(), unique_substrings.end(),
            StringComparator());

  std::vector<Match>::const_iterator j;

  for (j = unique_substrings.begin(); j != unique_substrings.end(); ++j)
    output(0.0, 0, j->string, j->string_length);
}

void CommonSubstringFinder::FindDocumentBounds(
    std::vector<size_t>& document_ends, const char* text, size_t text_size) {
  const char* text_end = text + text_size;
  const char* ch, *next;

  ch = text;

  while (ch != text_end) {
    if (!(next = (const char*)memchr(ch, DELIMITER, text_end - ch)))
      next = text_end;

    document_ends.push_back(next - text);

    if (next == text_end) break;

    ch = next + 1;
  }

  assert(!document_ends.empty());
}

size_t CommonSubstringFinder::FilterSuffixes(saidx_t* input, const char* text,
                                             size_t count) {
  saidx_t* output, *i, *end;

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

void CommonSubstringFinder::FindSubstringFrequencies() {
  if (!(input0_suffixes_ =
            (saidx_t*)calloc(sizeof(*input0_suffixes_), input0_size)))
    errx(EX_OSERR, "calloc failed");

  if (!(input1_suffixes_ =
            (saidx_t*)calloc(sizeof(*input1_suffixes_), input1_size)))
    errx(EX_OSERR, "calloc failed");

  if (do_probability || threshold) {
    input0_n_gram_counts_ = CountNGrams(input0, input0_size);
    input1_n_gram_counts_ = CountNGrams(input1, input1_size);
  }

  if (do_document) {
    FindDocumentBounds(input0_document_ends_, input0, input0_size);
    FindDocumentBounds(input1_document_ends_, input1, input1_size);
  }

  divsufsort((const sauchar_t*)input0, input0_suffixes_, input0_size);
  divsufsort((const sauchar_t*)input1, input1_suffixes_, input1_size);

  input0_suffix_count_ = FilterSuffixes(input0_suffixes_, input0, input0_size);
  input1_suffix_count_ = FilterSuffixes(input1_suffixes_, input1, input1_size);

  if (do_unique) skip_samecount_prefixes = 1;

  FindSubstrings(input0_threshold, input1_threshold);

  if (do_cover)
    FindCover();
  else if (do_unique)
    OutputUnique();
}
