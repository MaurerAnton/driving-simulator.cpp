// SPDX-License-Identifier: AGPL-3.0-only
#include "core/task.h"

#include <cstdio>
#include <stdexcept>

namespace fs::detail {

void taskFailedUnobserved(std::exception_ptr e) {
  // (what the browser prints as "Uncaught (in promise)")
  try {
    if (e) std::rethrow_exception(e);
  } catch (const std::exception& ex) {
    std::fprintf(stderr, "fahrsim: uncaught in a task: %s\n", ex.what());
  } catch (...) {
    std::fprintf(stderr, "fahrsim: uncaught in a task\n");
  }
}

}  // namespace fs::detail
