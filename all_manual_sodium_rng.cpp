// clang++ --std=c++17 -Wall -Wextra -Wpedantic -O3 all_manual_sodium_rng.cpp -o all_manual_sodium_rng -l sodium
#include <sys/random.h>
#include <unistd.h>

#include <cstdlib>
#include <climits>
#include <cstring>

#include <utility>
#include <algorithm>
#include <vector>

#include <sodium/crypto_stream_chacha20.h>

namespace uuid_bulk {

using byte = unsigned char;
using uuid_t = std::array<byte, 16>;
constexpr size_t uuid_str_len = 36;
constexpr size_t uuid_line_len = uuid_str_len+1;
constexpr size_t uuid_batch_size = 2000;


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

void getrandom(byte *dat, size_t len) {
  size_t off = 0;
  while (off < len) {
    auto r = ::getrandom(reinterpret_cast<void*>(dat + off), len - off, 0);
    if (r < 0 && errno == EINTR) continue;
    if (r < 0) perror("Error getting random data");
    off += r;
  }
}

class rng_chacha20 {
  static constexpr size_t block_size = 64;

  uint64_t nonce = -1;
  std::array<byte, crypto_stream_chacha20_KEYBYTES> key;
  std::array<byte, block_size> buf;
  static_assert(block_size % sizeof(uuid_t) == 0);
  // Maximum value will trigger overflow on first call
  uint8_t buf_off = block_size - sizeof(uuid_t);

  void regenerate_buf() {
    nonce++;
    if (nonce == 0) { // Rekey if nonce overflows
      getrandom(std::data(key), std::size(key));
    }

    ::crypto_stream_chacha20(
        &buf[0], std::size(buf),
        reinterpret_cast<const byte*>(&nonce), std::data(key));
  }

public:
  const uuid_t& operator()() {
    // Next in buffer
    buf_off = (buf_off + sizeof(uuid_t)) % block_size;
    if (buf_off == 0) // buffer overflows
      regenerate_buf();

    return reinterpret_cast<const uuid_t&>(buf[buf_off]);
  }
};

extern "C" int main() {


  std::vector<char> str_buf;
  str_buf.resize(uuid_line_len * uuid_batch_size);
  size_t off = 0;

  auto write_char = [&](char c) {
    str_buf[off++] = c;
  };

  constexpr char hexes[] = "0123456789abcdef";
  auto write_hex = [&](byte b) {
    write_char(hexes[b >> 4 & 0xf]);
    write_char(hexes[b & 0xf]);
  };

  auto write_uuid = [&](const uuid_t &uu) {
    write_hex(uu[0]);
    write_hex(uu[1]);
    write_hex(uu[2]);
    write_hex(uu[3]);
    write_char('-');
    write_hex(uu[4]);
    write_hex(uu[5]);
    write_char('-');
    write_hex((uu[6] & 0x0f) | 0x40); // version 4
    write_hex(uu[7]);
    write_char('-');
    write_hex((uu[8] & 0x3f) | 0x80); // variant
    write_hex(uu[9]);
    write_char('-');
    write_hex(uu[10]);
    write_hex(uu[11]);
    write_hex(uu[12]);
    write_hex(uu[13]);
    write_hex(uu[14]);
    write_hex(uu[15]);
    write_char('\n');
  };

  auto flush = [&]() {
    write_all(1, reinterpret_cast<const byte*>(std::data(str_buf)), off);
    off = 0;
  };

  rng_chacha20 uuid_rng;
  while (true) {
    // Convert to strings
    for (size_t idx=0; idx < uuid_batch_size; idx++)
      write_uuid(uuid_rng());

    flush();
  }

  return 0;
}

}
