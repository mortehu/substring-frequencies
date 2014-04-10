#ifndef SUBSTRINGS_H_
#define SUBSTRINGS_H_ 1

#include <cstddef>
#include <set>
#include <vector>

#include <divsufsort.h>

class CommonSubstringFinder {
 public:
  typedef void (*OutputFunction)(double input0_count, size_t input1_count, const char *string, size_t length);

  void FindSubstringFrequencies();

  OutputFunction output;

  const char *input0, *input1;
  size_t input0_size, input1_size;

  int skip_samecount_prefixes = 0;
  int do_probability = 0;
  int do_unique = 0;
  int do_document = 0;
  int do_color = 0;
  int do_cover = 0;
  int do_words = 0;
  double prior_bias = 1.0;
  double threshold = 0.0;
  int threshold_count = 0;
  int cover_threshold = 0;

  size_t input0_threshold = 2;
  size_t input1_threshold = LONG_MAX;

 private:
  // Adds the document containing the character at `offset' to the `documents'
  // set.  The end point of each document is defined by `document_ends', which
  // must be sorted.
  void AddDocument(std::set<size_t> *documents,
                   const std::vector<size_t> document_ends, size_t offset);

  void BuildLCPArray(std::vector<size_t> &result, const char *text,
                     size_t text_length, const saidx_t *suffixes,
                     size_t suffix_count);

  void FindSubstrings(size_t input0_threshold, size_t input1_threshold);

  std::vector<size_t> CountNGrams(const char *text, size_t text_size);

  void FindCover(void);

  void OutputUnique();

  void FindDocumentBounds(std::vector<size_t> &document_ends, const char *text,
                          size_t text_size);

  size_t FilterSuffixes(saidx_t *input, const char *text, size_t count);

  saidx_t *input0_suffixes_;
  saidx_t *input1_suffixes_;
  size_t input0_suffix_count_;
  size_t input1_suffix_count_;

  std::vector<size_t> input0_n_gram_counts_;
  std::vector<size_t> input1_n_gram_counts_;

  std::vector<size_t> input0_document_ends_;
  std::vector<size_t> input1_document_ends_;
};

#endif  // !SUBSTRINGS_H_
