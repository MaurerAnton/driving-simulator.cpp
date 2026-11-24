// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/netdiff.ts @ v1.7.7
/**
 * A change to the road network as a small diff over the published one
 * (world/netfile.h) – so a map part can carry a network change without
 * shipping the whole ~10 MB net (the width corrections of 2026-11-20: 66
 * `wayKind` records and 375 `across` values against 570 996 and 1 002 713).
 *
 * Why a diff and not the file: the net is one file for the whole region, while
 * a part holds 500 m tiles – a whole-file replacement would put ~10 MB in every
 * part that declares it, and two parts could each claim a different net with no
 * way to say which wins.  A diff is ~2 KB for a correction like that.
 *
 * A diff is only safe where the net has the **same structure**: same edges in
 * the same order, same ways, same turns.  A structural change (a road added,
 * re-routed, split) shifts every later index, so an index diff would list
 * nearly every entry – or worse, apply values to the wrong roads.  So a diff
 * carries a **fingerprint** of the structure it was made against, and
 * `applyNetDiff` refuses to apply it to a base whose fingerprint differs
 * (the caller then falls back to the whole file).
 *
 *   { v: 1, kind: 'netdiff', edges, ways, hash, set: { wayKind: [[i, v]…], across: [[i, v]…] } }
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "world/netfile.h"

namespace fs {

/** A list of (index, value) pairs – the changed entries of one array. */
using NetChanges = std::vector<std::pair<double, double>>;

/**
 * The changed entries per array (`Changes`): of the arrays whose *values* a correction may
 * change (never their length or order) – wayKind, waySpeed, turn, across, edgeCls –, in the
 * order they were put in.
 */
struct Changes {
  std::vector<std::pair<std::string, NetChanges>> entries;
  /** `set[name]`: null if the array has no changes */
  const NetChanges* find(std::string_view name) const;
  /** `set[name] = pairs` */
  void set(std::string_view name, NetChanges pairs);
};

/** A network diff as it is stored in a part. */
struct NetDiff {
  int v = 1;
  std::string kind = "netdiff";
  /** the number of edges of the net it was made against */
  double edges = 0;
  /** a fingerprint of the structure (see fingerprint) – must match the base to apply */
  std::string hash;
  /** the changed entries, per array */
  Changes set;
};

/** A small string hash of the structure: lengths + a rolling sum of the positional arrays. */
std::string fingerprint(const NetData& d);

/** Is `diff` made against the structure of `base`? */
bool matchesFingerprint(const NetData& base, const NetDiff& diff);

/**
 * The diff of `neu` against `base`: only the entries of the value sections
 * that differ.  Structural differences are not expressible this way – the
 * caller compares `fingerprint` first and ships the whole file instead.
 * (Throws std::runtime_error where an array changed its length.)
 */
NetDiff netDiff(const NetData& base, const NetData& neu);

/**
 * The net of `base` with `diff` applied: a copy of every section, the value
 * sections overwritten at the changed indices.  Throws (std::runtime_error) when the diff was made
 * against another structure (never applies values to the wrong roads).
 *
 * (`base` is taken by value: hand it over with std::move where the old network is not needed any more –
 * it is tens of megabytes.)
 */
NetData applyNetDiff(NetData base, const NetDiff& diff);

/** Pack a diff for a part (the bytes a part stores). */
std::vector<uint8_t> packNetDiff(const NetDiff& diff);

/** Read a diff out of a part's bytes; null when it is not one. */
std::optional<NetDiff> unpackNetDiff(const std::vector<uint8_t>& bytes);

}  // namespace fs
