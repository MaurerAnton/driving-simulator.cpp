// SPDX-License-Identifier: AGPL-3.0-only
/**
 * gzip, inflated: the map's documents arrive compressed (`world.json` as the
 * browser fetches it, `addresses.json.gz` on disk) and are read here, so the
 * game itself never cares how they travelled.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fs {

/** Inflate a gzip stream.  False when it is not one (or is broken). */
bool gunzip(const std::vector<uint8_t>& in, std::string& out);

}  // namespace fs
