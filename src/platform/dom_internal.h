// SPDX-License-Identifier: AGPL-3.0-only
/** Between dom_common.cpp and the hosts' backends. */
#pragma once

#include "platform/dom.h"

namespace fs::dom::detail {

/** run a handler (called by the host when an event arrives) */
void dispatch(int listener, Event& e);
/** how many handlers are alive (tests, and the leak check) */
int listenerCount();

}  // namespace fs::dom::detail
