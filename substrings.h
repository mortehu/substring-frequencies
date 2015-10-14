#ifndef SUBSTRINGS_H_
#define SUBSTRINGS_H_ 1

#include <climits>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_set>
#include <vector>

#include "base/stringref.h"
#include "libdivsufsort/divsufsort.h"

class CommonSubstringFinder {
 public:
  void FindSubstringFrequencies();

  std::function<void(size_t input0_count, size_t input1_count, double log_odds,
                     const ev::StringRef& substring)> output;

  const char* input0, *input1;
  size_t input0_size, input1_size;

  int skip_samecount_prefixes = 0;
  int do_probability = 0;
  int do_document = 0;
  int do_color = 0;
  int do_cover = 0;
  int do_words = 0;

  int filter_redundant_features = 1;

  double prior_bias = 1.0;
  double threshold = 0.0;
  int cover_threshold = 0;

  // Minimum fraction of documents to contain a substring for inclusion.  Used
  // only in document mode.
  size_t threshold_percent = 5;

  // Minimum count of substring for inclusion.  Used when threshold_percent
  // isn't used.
  int threshold_count = -1;

  // If set to false, does not produce two substrings corresponding to the same
  // set of document.
  int allow_equal_sets = 1;

 private:
  struct Feature {
    Feature(const ev::StringRef& substring, float log_odds, size_t input0_hits,
            size_t input1_hits)
        : substring(substring),
          log_odds(log_odds),
          input0_hits(input0_hits),
          input1_hits(input1_hits) {}

    uint64_t set_hash = 0;
    ev::StringRef substring;
    float log_odds;
    size_t input0_hits;
    size_t input1_hits;
  };

  struct CompareLength {
    bool operator()(const Feature& lhs, const Feature& rhs) const {
      if (lhs.substring.size() != rhs.substring.size())
        return lhs.substring.size() < rhs.substring.size();

      return !(rhs.substring < lhs.substring);
    }
  };

  struct StringComparator {
    bool operator()(const Feature& lhs, const Feature& rhs) const {
      return lhs.substring < rhs.substring;
    }
  };

  void AddSuffix(const ev::StringRef& suffix, const saidx_t* offsets,
                 size_t count, size_t input0_threshold,
                 size_t input1_threshold);

  void BuildLCPArray(std::vector<size_t>& result, const char* text,
                     size_t text_length, const saidx_t* suffixes,
                     size_t suffix_count);

  void FindSubstrings();

  std::vector<size_t> CountNGrams(const char* text, size_t text_size);

  void FindCover(void);

  void OutputUnique();

  void FindDocumentBounds(const char* text, size_t text_size);

  size_t FilterSuffixes(saidx_t* input, const char* text, size_t count);

  // Returns the document index corresponding to a given offset in the input.
  size_t DocIdxForOffset(saidx_t offset) const;

  std::mutex output_mutex_;

  std::vector<char> buffer_;

  std::vector<saidx_t> suffixes_;

  std::vector<size_t> input0_n_gram_counts_;
  std::vector<size_t> input1_n_gram_counts_;

  std::vector<saidx_t> document_starts_;

  // ceil(log2(document_starts_.size())) -- Number of iterations in binary
  // search of document_starts_.
  size_t document_binsearch_count_;
  size_t document_binsearch_first_mid_;

  size_t input0_doc_count_ = 0;
  size_t input1_doc_count_ = 0;

  size_t max_suffix_size_ = 32;

  // List of suffixes collected so far.
  std::vector<Feature> features_;
};

#endif  // !SUBSTRINGS_H_
