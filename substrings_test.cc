#include <algorithm>
#include <set>
#include <string>

#include "substrings.h"

namespace {

std::set<std::string> unique_strings;

void CollectUnique(double input0_count, size_t input1_count, const char* string,
                   size_t length) {
  unique_strings.insert(std::string(string, length));
}

void CompareSets(const std::string& input0, const std::string& input1,
                 const std::set<std::string>& expected,
                 const std::set<std::string>& got) {
  bool ok = true;

  std::set<std::string> missing;
  std::set_difference(expected.begin(), expected.end(), got.begin(), got.end(),
                      std::inserter(missing, missing.end()));

  for (const std::string& s : missing) {
    fprintf(stderr, "Missing output string: \"%.*s\"\n",
            static_cast<int>(s.size()), s.data());
    ok = false;
  }

  std::set<std::string> unexpected;
  std::set_difference(unique_strings.begin(), unique_strings.end(),
                      expected.begin(), expected.end(),
                      std::inserter(unexpected, unexpected.end()));

  for (const std::string& s : unexpected) {
    fprintf(stderr, "Unexpected output string: \"%.*s\" (%zu bytes)\n",
            static_cast<int>(s.size()), s.data(), s.size());
    ok = false;
  }

  if (!ok) {
    fprintf(stderr, "Input 0 was: \"%.*s\"\n", static_cast<int>(input0.size()),
            input0.data());
    fprintf(stderr, "Input 1 was: \"%.*s\"\n", static_cast<int>(input1.size()),
            input1.data());
    abort();
  }
}

void TestUniqueStrings(const std::string& input0, const std::string& input1,
                       const std::set<std::string>& expected) {
  CommonSubstringFinder csf;

  csf.input0 = input0.data();
  csf.input0_size = input0.size();
  csf.input1 = input1.data();
  csf.input1_size = input1.size();

  csf.input1_threshold = 0;

  csf.output = CollectUnique;

  unique_strings.clear();
  csf.FindSubstringFrequencies();

  CompareSets(input0, input1, expected, unique_strings);
}

std::string MakeDocuments(const std::string& input, char sep) {
  std::string result;
  for (char ch : input) {
    if (ch == sep)
      result.push_back(0);
    else
      result.push_back(ch);
  }

  return result;
}

void TestDocuments(const std::string& input0, const std::string& input1,
                   const std::set<std::string>& expected) {
  CommonSubstringFinder csf;

  csf.input0 = input0.data();
  csf.input0_size = input0.size();
  csf.input1 = input1.data();
  csf.input1_size = input1.size();

  csf.input1_threshold = 0;

  csf.do_document = 1;

  csf.output = CollectUnique;

  unique_strings.clear();
  csf.FindSubstringFrequencies();

  CompareSets(input0, input1, expected, unique_strings);
}

}  // namespace

int main(int argc, char** argv) {
  TestUniqueStrings("aa aaz", "", {"a", "aa"});

  TestUniqueStrings("aa aa", "", {"a", "aa"});

  TestUniqueStrings("aa aa", "xyz", {"a", "aa"});

  TestUniqueStrings("aa aa", "a", {"aa"});

  TestUniqueStrings("cccAcccBcccCccc", "ccd dcc ccd dcc dcd", {"ccc"});

  TestUniqueStrings("cccAcccBcccCccc", "cccAcccBcccCccc", {});

  TestDocuments(MakeDocuments("ccc|ccc|ccc|ccc", '|'),
                MakeDocuments("ccd|dcc|ccd|dcc|dcd", '|'), {"ccc"});

  TestDocuments(MakeDocuments("ccc|ccc|ccc|ccc", '|'),
                MakeDocuments("ccc|ccc|ccc|ccc|ccc", '|'), {});

  TestDocuments(MakeDocuments("ccc|ccc|ccc|ccc", '|'),
                MakeDocuments("ccc|ccc|ccc|ccc|", '|'), {});

  TestUniqueStrings("abcabc", "", {"a", "ab", "abc", "bc", "b", "c"});

  TestUniqueStrings("abcabc", "abx", {"abc", "bc", "c"});

  return EXIT_SUCCESS;
}
