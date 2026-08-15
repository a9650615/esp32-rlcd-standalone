#include "test_support.hpp"

#include <cstddef>
#include <iostream>

namespace host_tests {
std::vector<Test>& all_tests() {
  static std::vector<Test> tests;
  return tests;
}
Register::Register(const char* name, std::function<void()> test) {
  all_tests().emplace_back(name, std::move(test));
}
}  // namespace host_tests

int main() {
  int failures = 0;
  for (const auto& [name, test] : host_tests::all_tests()) {
    try {
      test();
      std::cout << "PASS " << name << '\n';
    } catch (const std::exception& error) {
      ++failures;
      std::cout << "FAIL " << name << ": " << error.what() << '\n';
    }
  }
  const std::size_t case_count = host_tests::all_tests().size();
  std::cout << case_count << " cases, " << failures
            << " failures\n";
  return failures == 0 ? 0 : 1;
}
