//
// Copyright RIME Developers
// Distributed under the BSD License
//
// 2013-01-30 GONG Chen <chen.sst@gmail.com>
//
#include <fstream>
#include <array>
#include <cstdint>
#include <rime/algo/utilities.h>

namespace rime {

namespace {

// C++20 constexpr 编译期生成 CRC32 表以替代 booost::crc
constexpr auto kCrc32Table = []() consteval {
  std::array<uint32_t, 256> table{};
  for (uint32_t i = 0; i < 256; ++i) {
    uint32_t c = i;
    for (int j = 0; j < 8; ++j) {
      c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
    }
    table[i] = c;
  }
  return table;
}();

// slicing-by-8 用的 8 张派生表：tables[k][i] 等价于对状态 tables[k-1][i]
// 再处理一个零字节。一次处理 8 字节，结果与逐字节算法完全相同。
constexpr auto kCrc32SlicingTables = []() consteval {
  std::array<std::array<uint32_t, 256>, 8> tables{};
  tables[0] = kCrc32Table;
  for (size_t k = 1; k < tables.size(); ++k) {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = tables[k - 1][i];
      tables[k][i] = (c >> 8) ^ tables[0][c & 0xFF];
    }
  }
  return tables;
}();

}  // namespace

int CompareVersionString(const string& x, const string& y) {
  size_t i = 0, j = 0, m = x.size(), n = y.size();
  while (i < m || j < n) {
    int v1 = 0, v2 = 0;
    while (i < m && x[i] != '.')
      v1 = v1 * 10 + (int)(x[i++] - '0');
    ++i;
    while (j < n && y[j] != '.')
      v2 = v2 * 10 + (int)(y[j++] - '0');
    ++j;
    if (v1 > v2)
      return 1;
    if (v1 < v2)
      return -1;
  }
  return 0;
}

ChecksumComputer::ChecksumComputer(uint32_t initial_remainder)
    : crc_(initial_remainder) {}

void ChecksumComputer::ProcessFile(const path& file_path) {
  // C++17/20 std::ifstream 原生支持 std::filesystem::path 参数，无需 .c_str()
  std::ifstream fin(file_path, std::ios::binary);
  if (!fin)
    return;

  std::array<char, buffer_size> buffer;
  while (fin) {
    fin.read(buffer.data(), buffer_size);
    std::streamsize bytes_read = fin.gcount();
    if (bytes_read > 0) {
      const auto* p = reinterpret_cast<const uint8_t*>(buffer.data());
      std::streamsize i = 0;
      // 一次处理 8 字节（结果与逐字节相同，仅更快）
      for (; i + 8 <= bytes_read; i += 8) {
        uint32_t word = static_cast<uint32_t>(p[i]) |
                        (static_cast<uint32_t>(p[i + 1]) << 8) |
                        (static_cast<uint32_t>(p[i + 2]) << 16) |
                        (static_cast<uint32_t>(p[i + 3]) << 24);
        crc_ ^= word;
        crc_ = kCrc32SlicingTables[7][crc_ & 0xFF] ^
               kCrc32SlicingTables[6][(crc_ >> 8) & 0xFF] ^
               kCrc32SlicingTables[5][(crc_ >> 16) & 0xFF] ^
               kCrc32SlicingTables[4][(crc_ >> 24) & 0xFF] ^
               kCrc32SlicingTables[3][p[i + 4]] ^
               kCrc32SlicingTables[2][p[i + 5]] ^
               kCrc32SlicingTables[1][p[i + 6]] ^
               kCrc32SlicingTables[0][p[i + 7]];
      }
      for (; i < bytes_read; ++i) {
        crc_ = kCrc32Table[(crc_ ^ p[i]) & 0xFF] ^ (crc_ >> 8);
      }
    }
  }
}

uint32_t ChecksumComputer::Checksum() {
  return crc_;
}

}  // namespace rime
