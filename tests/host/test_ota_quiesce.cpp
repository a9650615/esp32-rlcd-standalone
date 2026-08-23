#include "ota_quiesce.hpp"

#include "test_support.hpp"

namespace {

int g_calls_a = 0;
int g_calls_b = 0;
int g_order_next = 0;
int g_order_a = 0;
int g_order_b = 0;

void hook_a() {
  ++g_calls_a;
  g_order_a = ++g_order_next;
}
void hook_b() {
  ++g_calls_b;
  g_order_b = ++g_order_next;
}
void hook_noop() {}

void reset_counters() {
  g_calls_a = 0;
  g_calls_b = 0;
  g_order_next = 0;
  g_order_a = 0;
  g_order_b = 0;
  ota::reset_quiesce_hooks_for_test();
}

}  // namespace

HOST_TEST(quiesce_runs_every_hook_in_registration_order) {
  reset_counters();
  EXPECT_TRUE(ota::register_quiesce_hook(&hook_a));
  EXPECT_TRUE(ota::register_quiesce_hook(&hook_b));
  EXPECT_EQ(ota::quiesce_hook_count(), 2);

  EXPECT_EQ(ota::run_quiesce_hooks(), 2);
  EXPECT_EQ(g_calls_a, 1);
  EXPECT_EQ(g_calls_b, 1);
  // Order is part of the contract, not an accident: audio drops the amplifier
  // and anything registered later runs against a board that is already quiet.
  EXPECT_EQ(g_order_a, 1);
  EXPECT_EQ(g_order_b, 2);
}

HOST_TEST(quiesce_is_safe_with_nothing_registered) {
  reset_counters();
  EXPECT_EQ(ota::quiesce_hook_count(), 0);
  // A board built with no module that needs quiescing must still be able to
  // take a firmware write - this is the path that must not need a special case
  // at the call site in ota_session.cpp.
  EXPECT_EQ(ota::run_quiesce_hooks(), 0);
}

HOST_TEST(quiesce_hooks_run_again_on_a_retried_push) {
  reset_counters();
  EXPECT_TRUE(ota::register_quiesce_hook(&hook_a));
  EXPECT_EQ(ota::run_quiesce_hooks(), 1);
  EXPECT_EQ(ota::run_quiesce_hooks(), 1);
  // Twice, because a push that failed and was retried erases the slot again.
  // The registered hooks have to be idempotent and this asserts they are
  // called as though they are.
  EXPECT_EQ(g_calls_a, 2);
}

HOST_TEST(quiesce_registration_refuses_null_and_reports_a_full_table) {
  reset_counters();
  // Null is refused rather than stored: a null slot would be a crash at the
  // worst possible moment, immediately before an erase.
  EXPECT_TRUE(!ota::register_quiesce_hook(nullptr));
  EXPECT_EQ(ota::quiesce_hook_count(), 0);

  for (int index = 0; index < ota::kMaxQuiesceHooks; ++index) {
    EXPECT_TRUE(ota::register_quiesce_hook(&hook_noop));
  }
  EXPECT_EQ(ota::quiesce_hook_count(), ota::kMaxQuiesceHooks);
  // Refused, not silently dropped. A module whose quiesce never runs is
  // exactly the state this component exists to prevent, so registration has to
  // be able to fail loudly at startup rather than at the next firmware write.
  EXPECT_TRUE(!ota::register_quiesce_hook(&hook_a));
  EXPECT_EQ(ota::quiesce_hook_count(), ota::kMaxQuiesceHooks);
}
