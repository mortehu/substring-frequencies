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

void TestUniqueStrings(const std::string& input0, const std::string& input1,
                       const std::set<std::string>& expected) {
  CommonSubstringFinder csf;
  bool ok = true;

  csf.input0 = input0.data();
  csf.input0_size = input0.size();
  csf.input1 = input1.data();
  csf.input1_size = input1.size();

  csf.input1_threshold = 0;

  csf.output = CollectUnique;

  unique_strings.clear();
  csf.FindSubstringFrequencies();

  std::set<std::string> missing;
  std::set_difference(expected.begin(), expected.end(), unique_strings.begin(),
                      unique_strings.end(),
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
    fprintf(stderr, "Input 0 was: \"%.*s\"\n",
            static_cast<int>(csf.input0_size), csf.input0);
    fprintf(stderr, "Input 1 was: \"%.*s\"\n",
            static_cast<int>(csf.input1_size), csf.input1);
    abort();
  }
}

}  // namespace

int main(int argc, char** argv) {
  TestUniqueStrings("aa aaz", "", {"a", "aa"});

  TestUniqueStrings("aa aa", "", {"a", "aa"});

  TestUniqueStrings("aa aa", "xyz", {"a", "aa"});

  TestUniqueStrings("aa aa", "a", {"aa"});

  return EXIT_SUCCESS;
}
