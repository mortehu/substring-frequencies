#include <algorithm>
#include <cassert>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <list>
#include <unordered_set>
#include <vector>

#include <err.h>
#include <sysexits.h>

#include "base/string.h"
#include "substrings.h"

namespace {

// Character used to separate documents in document mode.
static const char kDocumentDelimiter = '\0';

// Counts the number of bits set in an 32 bit integer.
unsigned int BitCount(uint32_t n) {
  n = ((0xaaaaaaaa & n) >> 1) + (0x55555555 & n);
  n = ((0xcccccccc & n) >> 2) + (0x33333333 & n);
  n = ((0xf0f0f0f0 & n) >> 4) + (0x0f0f0f0f & n);
  n = ((0xff00ff00 & n) >> 8) + (0x00ff00ff & n);
  n = ((0xffff0000 & n) >> 16) + (0x0000ffff & n);
  return n;
}

}  // namespace

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

    while (p1 != end && p0 != end && (*p1 != kDocumentDelimiter) &&
           *p1++ == *p0++)
      ++h;

    result[x] = h;

    if (h > 0) --h;
  }
}

void CommonSubstringFinder::FindSubstrings() {
  size_t input0_threshold, input1_threshold;

  if (do_document && threshold_count == -1) {
    input0_threshold = input0_doc_count_ * threshold_percent / 100;
    input1_threshold = input1_doc_count_ * threshold_percent / 100;
  } else {
    input0_threshold = threshold_count;
    input1_threshold = threshold_count;
  }

  if (suffixes_.empty()) return;

  std::vector<size_t> shared_prefixes;

  shared_prefixes.reserve(suffixes_.size());

  BuildLCPArray(shared_prefixes, input0, input0_size + input1_size + 1,
                &suffixes_[0], suffixes_.size());

  // TODO(mortehu): Check word end boundary when do_words is set.

  for (size_t i = 1; i < suffixes_.size(); ++i) {
    const auto previous_prefix_length = (i > 1) ? shared_prefixes[i - 2] : 0;
    const auto prefix_length = shared_prefixes[i - 1];

    // Skip shared prefixes that are duplicates, or shorter versions of
    // previous previous ones.
    if (prefix_length <= previous_prefix_length) continue;

    if (do_words && suffixes_[i] > 0 && !std::isspace(input0[suffixes_[i] - 1]))
      continue;

    // Index of the first suffix matching the current prefix.
    const auto first_match = i - 1;

    auto first_occurence = std::min(suffixes_[i - 1], suffixes_[i]);

    size_t j = i + 1;
    size_t count = 2;

    // Collect counts for all prefixes of the current prefix.  Shorter prefixes
    // are guaranteed to have higher counts than longer prefixes.
    for (size_t prefix_prefix_length = prefix_length;
         j <= suffixes_.size() &&
             prefix_prefix_length > previous_prefix_length;) {
      if (shared_prefixes[j - 1] < prefix_prefix_length) {
        AddSuffix(ev::StringRef(input0 + first_occurence, prefix_prefix_length),
                  &suffixes_[first_match], count, input0_threshold,
                  input1_threshold);

        if (!skip_samecount_prefixes) {
          --prefix_prefix_length;
        } else {
          prefix_prefix_length = shared_prefixes[j - 1];
        }

        // Skip odd lengths when we're doing coloring.
        if (do_color) prefix_prefix_length &= ~1ULL;

        continue;
      }

      if (suffixes_[j] < first_occurence) first_occurence = suffixes_[j];

      ++count;
      ++j;
    }
  }
}

void CommonSubstringFinder::AddSuffix(const ev::StringRef& substring,
                                      const saidx_t* offsets, size_t count,
                                      size_t input0_threshold,
                                      size_t input1_threshold) {
  if (substring.size() > max_suffix_size_) return;

  if (count < input0_threshold && count < input1_threshold) return;

  size_t input0_hits = 0;

  for (size_t i = 0; i < count; ++i) {
    if (offsets[i] < static_cast<saidx_t>(input0_size)) ++input0_hits;
  }

  auto input1_hits = count - input0_hits;

  if (input0_hits < input0_threshold && input1_hits < input1_threshold) return;

  uint64_t set_hash = 0;

  if (do_document) {
    const auto doc_bits_size = (document_starts_.size() + 31) / 32;
    uint32_t doc_bits[doc_bits_size];
    memset(doc_bits, 0, sizeof(doc_bits));

    input0_hits = 0;
    input1_hits = 0;

    for (size_t i = 0; i < count; ++i) {
      const auto doc_idx = DocIdxForOffset(offsets[i]);
      doc_bits[doc_idx >> 5] |= 1 << (doc_idx & 31);
    }

    size_t j;

    for (j = 0; j + 32 < input0_doc_count_; j += 32)
      input0_hits += BitCount(doc_bits[j >> 5]);

    for (; j < input0_doc_count_; ++j) {
      if (doc_bits[j >> 5] & (1 << (j & 31))) ++input0_hits;
    }
    for (; j & 31; ++j) {
      if (doc_bits[j >> 5] & (1 << (j & 31))) ++input1_hits;
    }
    for (j >>= 5; j < doc_bits_size; ++j) input1_hits += BitCount(doc_bits[j]);

    if (input0_hits < input0_threshold && input1_hits < input1_threshold)
      return;

    std::minstd_rand rng(123);
    std::uniform_int_distribution<uint64_t> dist;

    for (size_t i = 0; i < doc_bits_size; ++i)
      set_hash += (doc_bits[i] ^ dist(rng)) + (doc_bits[i] << 24ULL);
  }

  // Assuming input0_hits and input1_hits are numerators, these are the
  // denominator for the same dimension.
  double input0_denominator = 0.0, input1_denominator = 0.0;

  if (do_document) {
    input0_denominator = input0_doc_count_;
    input1_denominator = input1_doc_count_;
  } else {
    if (substring.size() < input0_n_gram_counts_.size())
      input0_denominator = input0_n_gram_counts_[substring.size()];

    if (substring.size() < input1_n_gram_counts_.size())
      input1_denominator = input1_n_gram_counts_[substring.size()];
  }

  const auto A_given_K_odds =
      (input0_hits + prior_bias) / (input1_hits + prior_bias);
  const auto prior_odds =
      (input0_denominator + prior_bias) / (input1_denominator + prior_bias);
  const auto log_odds = std::log(A_given_K_odds / prior_odds);

  if (threshold && std::fabs(log_odds) < std::log(threshold / (1 - threshold)))
    return;

  std::lock_guard<std::mutex> lk(output_mutex_);

  if (filter_redundant_features) {
    for (auto i = features_.begin(); i != features_.end(); ++i) {
      auto& feature = *i;

      if ((feature.log_odds > 0) != (log_odds > 0)) continue;

      const auto feature_str = feature.substring;

      if (!allow_equal_sets && feature.set_hash == set_hash) {
        // We discard this feature if the existing feature is longer, or
        // alphanumerically lower.
        if (feature_str.size() > substring.size() ||
            (feature_str.size() == substring.size() &&
             feature_str < substring))
          return;
      }

      if (substring.begin() != feature_str.begin() &&
          !substring.contains(feature_str) &&
          !feature_str.contains(substring))
        continue;

      // Existing feature is more predictive.
      if (std::fabs(feature.log_odds) > std::fabs(log_odds)) return;

      // Existing feature is equally predictive, but longer.
      if (std::fabs(feature.log_odds) == std::fabs(log_odds) &&
          feature.substring.size() > substring.size())
        return;

      feature.substring = substring;
      feature.log_odds = log_odds;
      feature.input0_hits = input0_hits;
      feature.input1_hits = input1_hits;
      feature.set_hash = set_hash;

      return;
    }
  }

  if (do_cover || filter_redundant_features) {
    features_.emplace_back(substring, log_odds, input0_hits, input1_hits);
  } else {
    output(input0_hits, input1_hits, log_odds, substring);
  }
}

size_t CommonSubstringFinder::DocIdxForOffset(saidx_t offset) const {
  // This implementation runs a binary search with a fixed number of iterations,
  // enough to guarantee a correct results.  This removes a conditional branch
  // from the innter loop.

  auto first = document_starts_.data();
  auto len = document_starts_.size();

  auto mid = first + len - document_binsearch_first_mid_;

  if (*mid < offset) first = mid;
  len = document_binsearch_first_mid_;

  for (auto i = document_binsearch_count_; i; --i) {
    len >>= 1;
    if (first[len] < offset) first += len;
  }

  return first - document_starts_.data();
}

std::vector<size_t> CommonSubstringFinder::CountNGrams(const char* text,
                                                       size_t text_size) {
  const char* text_end = text + text_size;
  const char* ch, *next;

  std::vector<size_t> result;

  ch = text;

  while (ch != text_end) {
    if (!(next = (const char*)memchr(ch, kDocumentDelimiter, text_end - ch)))
      next = text_end;

    if (result.size() <= (size_t)(next - ch)) result.resize(next - ch + 1);

    for (size_t i = 1; i <= (size_t)(next - ch); ++i) ++result[i];

    if (next == text_end) break;

    ch = next + 1;
  }

  return result;
}

void CommonSubstringFinder::FindCover(void) {
  std::list<std::pair<const char*, size_t>> remaining_documents;
  const char* start = input0, *end;
  size_t i;

  std::sort(features_.begin(), features_.end(),
            [](const auto& lhs,
               const auto& rhs) { return lhs.log_odds > rhs.log_odds; });

  for (i = 0; i < document_starts_.size(); ++i) {
    if (document_starts_[i] >= static_cast<saidx_t>(input0_size)) break;

    end = input0 + document_starts_[i];

    remaining_documents.push_back(std::make_pair(start, end - start));

    start = end + 1;
  }

  for (auto j = features_.begin();
       j != features_.end() && !remaining_documents.empty(); ++j) {
    const char* string_begin = j->substring.data();
    size_t string_length = j->substring.size();

    std::list<std::pair<const char*, size_t>>::iterator k;
    int hits = 0;

    for (k = remaining_documents.begin(); k != remaining_documents.end();) {
      if (memmem(k->first, k->second, string_begin, string_length)) {
        k = remaining_documents.erase(k);

        ++hits;
      } else
        ++k;
    }

    if (hits > cover_threshold)
      output(hits, 0, j->log_odds, ev::StringRef(string_begin, string_length));
  }
}

void CommonSubstringFinder::FindDocumentBounds(const char* text,
                                               size_t text_size) {
  const char* text_end = text + text_size;
  const char* ch, *next;

  document_starts_.emplace_back(0);
  ch = text;

  while (ch != text_end) {
    if (!(next = (const char*)memchr(ch, kDocumentDelimiter, text_end - ch)))
      next = text_end;

    document_starts_.emplace_back(next - text);

    if (next < input1)
      ++input0_doc_count_;
    else
      ++input1_doc_count_;

    if (next == text_end) break;

    ch = next + 1;
  }

  if (document_starts_.size() > 1) document_starts_.pop_back();

  const auto size = document_starts_.size();
  document_binsearch_count_ = std::ceil(std::log2(size)) - 1;
  document_binsearch_first_mid_ =
      std::pow(2.0, std::ceil(std::log2(document_starts_.size())) - 1.0);
}

size_t CommonSubstringFinder::FilterSuffixes(saidx_t* input, const char* text,
                                             size_t count) {
  auto end = input + count;

  auto output = input;

  for (auto i = input; i != end; ++i) {
    if (do_color && (*i & 1)) continue;

    auto ch = static_cast<unsigned char>(text[*i]);

    if (ch == kDocumentDelimiter) continue;

    // Skip UTF-8 continuation bytes; we're not interested in substrings
    // starting inside characters.
    if ((ch & 0xc0) == 0x80) continue;

    *output++ = *i;
  }

  return output - input;
}

void CommonSubstringFinder::FindSubstringFrequencies() {
  if (input1 != input0 + input0_size + 1 ||
      input0[input0_size] != kDocumentDelimiter) {
    buffer_.resize(input0_size + input1_size + 1);
    std::copy(input0, input0 + input0_size, buffer_.begin());
    buffer_[input0_size] = kDocumentDelimiter;
    std::copy(input1, input1 + input1_size, buffer_.begin() + input0_size + 1);

    input0 = &buffer_[0];
    input1 = &buffer_[input0_size + 1];
  }

  suffixes_.resize(input0_size + input1_size + 1);

  if (do_probability || threshold) {
    input0_n_gram_counts_ = CountNGrams(input0, input0_size);
    input1_n_gram_counts_ = CountNGrams(input1, input1_size);
  }

  if (do_document) {
    FindDocumentBounds(input0, input0_size + input1_size + 1);
  }

  divsufsort(reinterpret_cast<const sauchar_t*>(input0), &suffixes_[0],
             input0_size + input1_size + 1);

  suffixes_.resize(
      FilterSuffixes(&suffixes_[0], input0, input0_size + input1_size + 1));

  FindSubstrings();

  if (do_cover) {
    FindCover();
  } else {
    for (const auto& feature : features_) {
      output(feature.input0_hits, feature.input1_hits, feature.log_odds,
             feature.substring);
    }
  }
}
