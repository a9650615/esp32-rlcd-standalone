#pragma once

#include <string>

namespace ota {

// Compares two dotted numeric versions, tolerating a leading "v" and any
// trailing suffix (so "v1.2.3" and "1.2.3-rc1" both parse). Missing components
// count as zero, making "1.2" and "1.2.0" equal.
//
// Returns <0 when a is older, 0 when equal, >0 when a is newer.
int compare_versions(const std::string& a, const std::string& b);

// True when `candidate` is strictly newer than `running`.
//
// This is the only thing standing between a periodic update check and an
// endless reflash loop: the image validator deliberately does not gate on
// version - re-flashing the installed build by hand is a legitimate rescue -
// so an automated path has to decide for itself, and "not older" is not good
// enough. Anything unparseable answers false, because the failure mode of a
// false positive here is a device that reinstalls the same firmware forever.
bool is_newer(const std::string& candidate, const std::string& running);

}  // namespace ota
