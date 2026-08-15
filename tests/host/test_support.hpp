#pragma once

#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace host_tests {
using Test = std::pair<const char*, std::function<void()>>;
std::vector<Test>& all_tests();
struct Register {
  Register(const char* name, std::function<void()> test);
};
}  // namespace host_tests

#define HOST_TEST(name) \
  static void name(); \
  static host_tests::Register register_##name(#name, name); \
  static void name()

#define EXPECT_TRUE(condition)                                                   \
  do {                                                                            \
    if (!(condition)) {                                                           \
      throw std::runtime_error(std::string("expected true: ") + #condition);     \
    }                                                                             \
  } while (false)

#define EXPECT_EQ(actual, expected)                                               \
  do {                                                                            \
    if (!((actual) == (expected))) {                                              \
      throw std::runtime_error(std::string("expected equality: ") + #actual +    \
                               " == " + #expected);                              \
    }                                                                             \
  } while (false)
