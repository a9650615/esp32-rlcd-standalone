#pragma once

#include <string>

namespace ota {

struct ReleaseInfo {
  bool ok = false;
  // Tag as published, e.g. "v0.2.0". Empty when the query failed.
  std::string version;
  // Download URL of the release's firmware asset.
  std::string firmware_url;
  // True only when `version` parsed and is strictly newer than the running
  // image. Never inferred from "the tag differs" - see ota_version.hpp.
  bool update_available = false;
  // ASCII, safe for the panel and the settings page.
  std::string message;
};

// Asks a GitHub repository for its latest release and reports whether it
// carries firmware newer than what is running.
//
// Read-only: it never installs anything. Handing the returned firmware_url to
// pull_from_url is a separate, deliberate step, so a check cannot turn into an
// unattended reflash by accident.
//
// Blocking, holds a TLS session and a response buffer; call from a task with
// room for both, never from the LVGL thread.
ReleaseInfo check_latest_release();

}  // namespace ota
