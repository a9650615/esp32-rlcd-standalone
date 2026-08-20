#pragma once

#include <cstddef>

namespace ota {

// Fixed capacity, no dynamic allocation - same shape as
// app_core::kMaxTrayIndicators. Two registrations exist today (modules/audio
// and modules/airplay); four leaves room without needing revisiting, and
// register_quiesce_hook() enforces it rather than assuming callers count.
inline constexpr int kMaxQuiesceHooks = 4;

// Called on the task that is about to write firmware, immediately before
// esp_ota_begin(), so a module can put its hardware somewhere safe first.
//
// The incident this exists for: a push sent while AirPlay was playing produced
// loud noise from the speaker and then failed. The mechanism is the amplifier
// being left enabled with an undriven input - see
// docs/design/2026-08-20-quiescing-modules-before-an-ota-write.md, and note
// that a hook which merely stops feeding the codec reproduces the fault it was
// added to prevent. GPIO46 has to drop before the codec stops being fed.
//
// Contract:
//   - Runs synchronously, on the writer's task, before the first erase.
//   - Must return promptly. kQuiesceBudgetMs below is the whole budget for
//     every hook together, and a hook that overruns it does not delay the
//     write - it is simply still running when the write begins, which is
//     worse for that module than returning early.
//   - Must not start network I/O. The socket the firmware is arriving on is
//     the one thing that must keep working.
//   - Must be safe to call when the module is idle, and safe to call twice:
//     a retried push calls every hook again.
using QuiesceHook = void (*)();

// Registers a hook. Returns false when the table is full or `hook` is null,
// rather than silently dropping it - a module whose quiesce never runs is
// exactly the state this component exists to prevent, so it must be able to
// find out at registration time.
//
// Not thread-safe and not meant to be: registration happens once per module
// during startup, from app_main's single init path, the same as
// register_tray_indicator().
bool register_quiesce_hook(QuiesceHook hook);

// How many hooks are registered. For tests and for the log line that says
// what was quiesced, so a push that quiesced nothing is visible as such.
int quiesce_hook_count();

// Runs every registered hook in registration order, and returns how many were
// called. Safe to call with none registered.
//
// The budget is advisory by construction. A hook is a plain synchronous call,
// so a hook that hangs cannot be interrupted from here - and the ADR requires
// that a slow hook must never prevent the write, because the write is the
// recovery path for broken firmware. What this function can do, and does, is
// refuse to *start* further hooks once the budget is spent, and log the
// overrun so a module that misbehaves is named rather than merely suspected.
// Bounding a single hook would need a task per hook and the ability to abandon
// one mid-call; that is not worth its own failure modes for two callbacks whose
// whole job is to drop a GPIO and close a socket.
int run_quiesce_hooks();

// Total budget for all hooks together. Sized against what the registered ones
// actually do: audio's closes a stream, which writes a short trailing silence
// and waits out one I2S drain, and airplay's tears down an RTSP session. Both
// are tens of milliseconds; 500 ms is generous enough that hitting it means
// something is wrong rather than merely slow.
inline constexpr int kQuiesceBudgetMs = 500;

// Test seam. Clears the table so a host test can register into a known state;
// never called by firmware.
void reset_quiesce_hooks_for_test();

}  // namespace ota
