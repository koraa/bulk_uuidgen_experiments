// clang++ --std=c++17 -Wall -Wextra -Wpedantic -O3 all_manual_xorshiro_vmsplice.cpp -o all_manual_xorshiro_vmsplice;
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sys/random.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdlib>
#include <climits>
#include <cstring>
#include <utility>
#include <algorithm>
#include <vector>
#include <thread>

namespace uuid_bulk {

using byte = unsigned char;
using uuid_t = std::array<byte, 16>;
constexpr size_t uuid_str_len = 36;
constexpr size_t uuid_line_len = uuid_str_len+1;
constexpr size_t batch_size = 2000;

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

template<typename T>
T* mmap(T *addr, size_t length, int prot, int flags, int fd, off_t offset) {
  void* r = ::mmap(reinterpret_cast<void*>(addr), length, prot, flags, fd, offset);
  if (r == MAP_FAILED)
    perror("Could not map memory region");
  return reinterpret_cast<T*>(r);
}

void vmsplice(int fd, const struct iovec *iov, unsigned long nr_segs, unsigned int flags) {
  ssize_t r = ::vmsplice(fd, iov, nr_segs, flags);
  if (r == -1)
    perror("Could not donate pages");
}

class rng_xorshiro {
  uint64_t state[2];

  __attribute__((always_inline)) static uint64_t rotl(const uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
  }

public:
  rng_xorshiro() {
    getrandom(reinterpret_cast<byte*>(&state[0]), sizeof(state));
  }

  __attribute__((flatten)) __attribute__((always_inline)) const uuid_t& operator()() {
    const uint64_t s0 = state[0];
    uint64_t s1 = state[1];
    s1 ^= s0;
    state[0] = rotl(s0, 55) ^ s1 ^ (s1 << 14); // a, b
    state[1] = rotl(s1, 36); // c

    static_assert(sizeof(state) == sizeof(uuid_t));
    return reinterpret_cast<const uuid_t&>(state);
  }
};

// NOTE: This is duplicated because down below again as a lambda,
// because if we use this function in the hot loop we loose a lot
// of performance; if I had to guess this is probably due to pointer
// aliasing rules/optimization/vectorization something something…
__attribute__((flatten)) void write_uuid(const uuid_t &uu, char *buf, size_t &off) {
  auto write_char = [&](char c) {
    buf[off++] = c;
  };

  constexpr char hexes[] = "0123456789abcdef";
  auto write_hex = [&](byte b) {
    write_char(hexes[b >> 4 & 0xf]);
    write_char(hexes[b & 0xf]);
  };

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
}

__attribute__((flatten)) void worker() {
  constexpr size_t buf_size = batch_size * uuid_line_len;
  char *buf = mmap<char>(nullptr, buf_size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  size_t off = 0;

  rng_xorshiro uuid_rng;
  while (true) {
    for (size_t idx=0; idx < batch_size; idx++)
      write_uuid(uuid_rng(), buf, off);

    ::iovec vec{buf, buf_size};
    uuid_bulk::vmsplice(1, &vec, 1, SPLICE_F_GIFT);
    mmap<char>(buf, buf_size, PROT_READ|PROT_WRITE, MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    off = 0;
  }
}

extern "C" __attribute__((flatten)) int main() {
  std::array<std::thread, 2> threads;
  for (auto &t : threads)
    t = std::thread{ worker };
  worker();
  return 0;
}

}
