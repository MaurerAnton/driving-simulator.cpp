// SPDX-License-Identifier: AGPL-3.0-only
/** The parts of plat.h that are the same on every host. */
#include <zlib.h>

#include "platform/plat.h"

namespace fs::plat {

DateFields localFields(double ms) { return utcFields(ms - timezoneOffsetMin(ms) * 60000.0); }

Bytes gunzip(const Bytes& gz) {
  Bytes out;
  if (!isGzip(gz)) return out;
  z_stream zs{};
  if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK) return out;
  zs.next_in = const_cast<Bytef*>(gz.data());
  zs.avail_in = static_cast<uInt>(gz.size());
  // (the last four bytes of a gzip file say how long it is unpacked – a good first guess)
  size_t guess = gz.size() * 4;
  if (gz.size() >= 4) {
    const uint8_t* t = gz.data() + gz.size() - 4;
    size_t isize = static_cast<size_t>(t[0]) | (static_cast<size_t>(t[1]) << 8) | (static_cast<size_t>(t[2]) << 16) | (static_cast<size_t>(t[3]) << 24);
    if (isize >= gz.size() / 2 && isize < (static_cast<size_t>(1) << 30)) guess = isize;
  }
  out.resize(guess > 64 ? guess : 64);
  size_t have = 0;
  for (;;) {
    zs.next_out = out.data() + have;
    zs.avail_out = static_cast<uInt>(out.size() - have);
    int rc = inflate(&zs, Z_NO_FLUSH);
    have = out.size() - zs.avail_out;
    if (rc == Z_STREAM_END) {
      // (a file of several members: the next one follows)
      if (zs.avail_in > 2 && zs.next_in[0] == 0x1f && zs.next_in[1] == 0x8b) {
        if (inflateReset(&zs) != Z_OK) break;
        continue;
      }
      break;
    }
    if (rc != Z_OK && rc != Z_BUF_ERROR) {
      inflateEnd(&zs);
      return {};
    }
    if (zs.avail_out == 0) out.resize(out.size() * 2);
    else if (zs.avail_in == 0) {
      // the input ended in the middle of the stream
      inflateEnd(&zs);
      return {};
    }
  }
  inflateEnd(&zs);
  out.resize(have);
  return out;
}

}  // namespace fs::plat
