// clang++ --std=c++17 -Wall -Wextra -Wpedantic -O3 libuuid_iostream.cpp -o libuuid_iostream -l uuid
#include <cstdlib>
#include <climits>
#include <cstring>
#include <utility>
#include <algorithm>
#include <vector>
#include <iostream>
#include <uuid/uuid.h>

namespace uuid_bulk {

extern "C" int main() {
  while (true) {
    ::uuid_t uu;
    ::uuid_generate_random(uu);

    std::array<char, 37> buf;
    ::uuid_unparse(uu, std::data(buf));

    std::cout.write(std::data(buf), 36);
    std::cout.put('\n');
  }

  return 0;
}

}
