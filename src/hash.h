#ifndef AURA_HASH_H_
#define AURA_HASH_H_

#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <cstdint>

// compile time functions for char*

static constexpr uint64_t HASH_FACTOR = 31u;

static constexpr uint64_t HashCode(const char* str, const size_t pos, const uint64_t hash)
{
  return str[pos] ? (HashCode(str, pos + 1u, HASH_FACTOR * hash + static_cast<uint64_t>(str[pos]))) : hash;
}

static constexpr uint64_t HashCode(const char* str)
{
  return HashCode(str, 0, 7);
}

template <size_t N>
static constexpr uint32_t FourCC(const char (&str)[N])
{
  // Note: Some parts of WC3 accept FourCC codes with less than 4 characters
  static_assert(N <= 5, "FourCC requires a string literal of exactly 4 characters.");
  if constexpr (N == 5) {
    return static_cast<uint32_t>(str[3]) | (static_cast<uint32_t>(str[2]) << 8) | (static_cast<uint32_t>(str[1]) << 16) | (static_cast<uint32_t>(str[0]) << 24);
  }
  if constexpr (N == 4) {
    return static_cast<uint32_t>(str[2]) | (static_cast<uint32_t>(str[1]) << 8) | (static_cast<uint32_t>(str[0]) << 16);
  }
  if constexpr (N == 3) {
    return static_cast<uint32_t>(str[1]) | (static_cast<uint32_t>(str[0]) << 8);
  }
  if constexpr (N == 2) {
    return static_cast<uint32_t>(str[0]);
  }
  return 0;
}

template<typename K, typename V, size_t N>
constexpr std::array<std::pair<K, V>, N> SortPairs(std::array<std::pair<K, V>, N> input)
{
  for (size_t i = 0; i < N; ++i) {
    for (size_t j = i + 1; j < N; ++j) {
      if (input[j].first < input[i].first) {
        auto tmp = input[i];
        input[i] = input[j];
        input[j] = tmp;
      }
    }
  }
  return input;
}

// runtime function for std::string

[[nodiscard]] static uint64_t HashCode(const std::string& str)
{
  return HashCode(str.c_str());
}

[[nodiscard]] static uint32_t FourCC(const std::string& str)
{
  if (str.size() != 4) return 0;
  return FourCC(str.c_str());
}

template<typename K, typename V, size_t N>
[[nodiscard]] static std::array<std::pair<K, V>, N>& SortPairs(std::array<std::pair<K, V>, N>& input)
{
  std::sort(std::begin(input), std::end(input), [](const std::pair<K, V>& a, const std::pair<K, V>& b) {
    return a.first < b.first;
  });
  return input;
}

#endif // AURA_HASH_H_
