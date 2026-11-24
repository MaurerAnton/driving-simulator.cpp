// SPDX-License-Identifier: AGPL-3.0-only
/** This machine's window.confirm and bytes downloaded (ui/hud_parts.h): what a test sets (native.h). */
#include "platform/native/native.h"
#include "ui/hud_parts.h"

namespace fs::plat {
bool confirm(const std::string& text) {
  native::confirmed.push_back(text);
  return native::confirmAnswer;
}
}  // namespace fs::plat

namespace fs {
double downloaded() { return plat::native::bytesDownloaded; }
}  // namespace fs
