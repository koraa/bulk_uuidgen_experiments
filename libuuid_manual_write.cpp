#include <unistd.h>
#include <cstdlib>
#include <climits>
#include <cstring>
#include <utility>
#include <algorithm>
#include <vector>
#include <uuid/uuid.h>

namespace uuid_bulk {

using byte = unsigned char;

size_t write(int fd, const byte *dat, size_t len) {
  if (len == 0) return 0;
  while (true) {
    auto r = ::write(fd, dat, std::min(size_t(SSIZE_MAX), len));
    if (r < 0 && errno == EINTR) continue;
    if (r < 0) perror("Error");
    return r;
  }
}

void write_all(int fd, const byte *dat, size_t len) {
  size_t off = 0;
  while (off < len)
    off += write(fd, dat + off, len - off);
}

extern "C" int main() {
  constexpr size_t uuid_line_len = 37;
  constexpr size_t uuid_batch_size = 2000;

  std::vector<char> str_buf;
  str_buf.resize(uuid_line_len * uuid_batch_size);

  while (true) {
    size_t off = 0;
    // Convert to strings
    for (size_t idx=0; idx < uuid_batch_size; idx++) {
      ::uuid_t uu;
      ::uuid_generate_random(uu);
      ::uuid_unparse(uu, std::data(str_buf)+off);
      str_buf[off+36] = '\n';
      off += 37;
    }

    // Flush
    write_all(1, reinterpret_cast<const byte*>(std::data(str_buf)), off);
  }

  return 0;
}

}
