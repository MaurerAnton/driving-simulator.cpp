// SPDX-License-Identifier: AGPL-3.0-only
/** gzip, inflated (zlib's own inflate, window bits 16 + gzip header). */
#include "platform/gunzip.h"

#include <zlib.h>

namespace fs {

bool gunzip(const std::vector<uint8_t>& in, std::string& out) {
  if (in.size() < 3 || in[0] != 0x1f || in[1] != 0x8b) return false;
  z_stream zs{};
  if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK) return false;
  zs.next_in = const_cast<Bytef*>(in.data());
  zs.avail_in = static_cast<uInt>(in.size());
  std::vector<uint8_t> buf(1 << 16);
  int rc;
  do {
    zs.next_out = buf.data();
    zs.avail_out = static_cast<uInt>(buf.size());
    rc = inflate(&zs, Z_NO_FLUSH);
    if (rc != Z_OK && rc != Z_STREAM_END) {
      inflateEnd(&zs);
      return false;
    }
    out.append(reinterpret_cast<char*>(buf.data()), buf.size() - zs.avail_out);
  } while (rc != Z_STREAM_END);
  inflateEnd(&zs);
  return true;
}

}  // namespace fs
