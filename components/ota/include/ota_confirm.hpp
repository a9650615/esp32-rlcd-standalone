#pragma once

#include <cstdint>
#include <string>

namespace ota {

// A network push waits here for someone at the board to accept it.
//
// The upload endpoint listens whenever the board is on the network, so nothing
// has to be pressed to make a push possible - but a device that reflashes
// itself because a request arrived is a device anyone on the LAN can reflash.
// This is the authorisation, and it is deliberately a physical one: the board
// shows what is being offered and two buttons answer.
//
// Nothing is erased and nothing is written while this is pending. A rejection
// or a timeout leaves the running firmware untouched.
enum class ConfirmResult : uint8_t {
  Accepted,
  Rejected,
  TimedOut,
};

// Blocks the calling task - the HTTP handler - until answered or the timeout
// elapses. `peer` is shown on the prompt so the answer is about a specific
// request rather than an abstract one.
//
// One at a time: a second call while another is pending is rejected outright
// rather than queued, so two pushes cannot race for one confirmation.
ConfirmResult request_confirm(const std::string& peer,
                              const std::string& version,
                              uint32_t timeout_ms);

// True while a prompt is on screen. The input layer checks this to know that
// the buttons currently mean yes and no.
bool confirm_pending();

// Answers the pending prompt. Called from the LVGL thread on a button event;
// a no-op when nothing is pending.
void answer_confirm(bool accepted);

// Set once at startup, to whatever puts the prompt on the panel and takes it
// away again. Same indirection as the progress handler: the ota component must
// not depend on the snapshot owner.
void set_confirm_prompt_handler(void (*handler)(bool showing,
                                                const std::string& peer,
                                                const std::string& version));

}  // namespace ota
