/*

  Copyright [2025] [Leonardo Julca]

  Permission is hereby granted, free of charge, to any person obtaining
  a copy of this software and associated documentation files (the
  "Software"), to deal in the Software without restriction, including
  without limitation the rights to use, copy, modify, merge, publish,
  distribute, sublicense, and/or sell copies of the Software, and to
  permit persons to whom the Software is furnished to do so, subject to
  the following conditions:

  The above copyright notice and this permission notice shall be
  included in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
  LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
  OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
  WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#ifndef AURA_PARSER_H_
#define AURA_PARSER_H_

#include "includes.h"
#include "hash.h"

#include <cfloat>
#include <filesystem>
#include <variant>

[[nodiscard]] inline std::optional<uint32_t> ParseUint32Hex(const std::string& hexString)
{
  if (hexString.empty() || hexString.size() > 8) {
    return std::nullopt;
  }

  // Initialize a uint32_t value
  uint32_t result;

  std::istringstream stream(hexString);
  stream >> std::hex >> result;

  if (stream.fail() || !stream.eof()) {
    return std::nullopt;
  }

  return result;
}

[[nodiscard]] inline std::optional<Version> ParseGameVersion(std::string versionString) {
  std::optional<Version> result;
  std::string::size_type periodIndex = versionString.find('.');
  std::string::size_type periodIndex2;
  if (periodIndex == std::string::npos) {
    return ParseGameVersion("1." + versionString);
  }

  periodIndex = versionString.find('.');
  periodIndex2 = versionString.find('.', periodIndex + 1);
  if (periodIndex2 == std::string::npos) {
    periodIndex2 = versionString.size();
  }

  std::string majorVersionString = versionString.substr(0, periodIndex);
  std::string minorVersionString = versionString.substr(periodIndex + 1, periodIndex2 - (periodIndex + 1));

  if (!IsBase10NaturalOrZero(majorVersionString)) {
    return result;
  }

  if (!IsBase10NaturalOrZero(minorVersionString)) {
    return result;
  }

  uint32_t majorVersion = stol(majorVersionString);
  uint32_t minorVersion = stol(minorVersionString);
  result = Version((uint8_t)majorVersion, (uint8_t)minorVersion);
  return result;
}

[[nodiscard]] inline std::optional<bool> ParseBoolean(const std::string& input, uint8_t parseFlags = PARSER_BOOLEAN_ALLOW_ALL)
{
  std::optional<bool> result;
  if (input.empty()) {
    if (parseFlags & PARSER_BOOLEAN_EMPTY_USE_DEFAULT) {
      result = (parseFlags & PARSER_BOOLEAN_DEFAULT_FALSE) == 0;
    }
    return result;
  }
  std::string lowerInput = ToLowerCase(input);
  switch (HashCode(input)) {
    case HashCode("0"):
    case HashCode("no"):
    case HashCode("false"):
    case HashCode("off"):
    case HashCode("none"):
      result = false;
      break;
    case HashCode("never"):
      if (!(parseFlags & PARSER_BOOLEAN_ALLOW_TIME)) {
        break;
      }
      result = false;
      break;
    case HashCode("disable"):
    if (!(parseFlags & PARSER_BOOLEAN_ALLOW_ENABLE)) {
        break;
      }
      result = false;
      break;

    case HashCode("1"):
    case HashCode("yes"):
    case HashCode("true"):
    case HashCode("on"):
      result = true;
      break;
    case HashCode("always"):
      if (!(parseFlags & PARSER_BOOLEAN_ALLOW_TIME)) {
        break;
      }
      result = true;
      break;
    case HashCode("enable"):
      if (!(parseFlags & PARSER_BOOLEAN_ALLOW_ENABLE)) {
        break;
      }
      result = true;
      break;
  }
  return result;
}

[[nodiscard]] inline std::string ParseFileName(const std::string& inputPath) {
  std::filesystem::path filePath = inputPath;
  return filePath.filename().string();
}

[[nodiscard]] inline std::string ParseFileExtension(const std::string& inputPath) {
  std::string fileName = ParseFileName(inputPath);
  size_t extIndex = fileName.find_last_of(".");
  if (extIndex == std::string::npos) return std::string();
  std::string extension = fileName.substr(extIndex);
  std::transform(std::begin(extension), std::end(extension), std::begin(extension), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return extension;
}

[[nodiscard]] inline std::optional<int8_t> ParseInt8(const std::string& input, bool decimalOnly = false)
{
  std::optional<int8_t> result;
  try {
    size_t parseEnd;
    int base = (!decimalOnly && input.size() > 2 && (input[0] == '0' && (input[1] == 'x' || input[1] == 'X'))) ? 16 : 10;
    int64_t userValue = stoll(input, &parseEnd, base);
    if (parseEnd != input.size() || userValue < (int64_t)(std::numeric_limits<int8_t>::min()) || 0x7F < userValue) {
      return result;
    }
    result = static_cast<int8_t>(userValue);
  } catch (...) {
  }
  return result;
}

[[nodiscard]] inline std::optional<uint8_t> ParseUInt8(const std::string& input, bool decimalOnly = false)
{
  std::optional<uint8_t> result;
  try {
    size_t parseEnd;
    int base = (!decimalOnly && input.size() > 2 && (input[0] == '0' && (input[1] == 'x' || input[1] == 'X'))) ? 16 : 10;
    int64_t userValue = stoll(input, &parseEnd, base);
    if (parseEnd != input.size() || userValue < 0 || 0xFF < userValue) {
      return result;
    }
    result = static_cast<uint8_t>(userValue);
  } catch (...) {
  }
  return result;
}

[[nodiscard]] inline std::optional<int16_t> ParseInt16(const std::string& input, bool decimalOnly = false)
{
  std::optional<int16_t> result;
  try {
    size_t parseEnd;
    int base = (!decimalOnly && input.size() > 2 && (input[0] == '0' && (input[1] == 'x' || input[1] == 'X'))) ? 16 : 10;
    int64_t userValue = stoll(input, &parseEnd, base);
    if (parseEnd != input.size() || userValue < (int64_t)(std::numeric_limits<int16_t>::min()) || 0x7FFF < userValue) {
      return result;
    }
    result = static_cast<int16_t>(userValue);
  } catch (...) {
  }
  return result;
}

[[nodiscard]] inline std::optional<uint16_t> ParseUInt16(const std::string& input, bool decimalOnly = false)
{
  std::optional<uint16_t> result;
  try {
    size_t parseEnd;
    int base = (!decimalOnly && input.size() > 2 && (input[0] == '0' && (input[1] == 'x' || input[1] == 'X'))) ? 16 : 10;
    int64_t userValue = stoll(input, &parseEnd, base);
    if (parseEnd != input.size() || userValue < 0 || 0xFFFF < userValue) {
      return result;
    }
    result = static_cast<uint16_t>(userValue);
  } catch (...) {
  }
  return result;
}

[[nodiscard]] inline std::optional<int32_t> ParseInt32(const std::string& input, bool decimalOnly = false)
{
  std::optional<int32_t> result;
  try {
    size_t parseEnd;
    int base = (!decimalOnly && input.size() > 2 && (input[0] == '0' && (input[1] == 'x' || input[1] == 'X'))) ? 16 : 10;
    int64_t userValue = stoll(input, &parseEnd, base);
    if (parseEnd != input.size() || userValue < (int64_t)(std::numeric_limits<int32_t>::min()) || 0x7FFFFFFF < userValue) {
      return result;
    }
    result = static_cast<int32_t>(userValue);
  } catch (...) {
  }
  return result;
}

[[nodiscard]] inline std::optional<uint32_t> ParseUInt32(const std::string& input, bool decimalOnly = false)
{
  std::optional<uint32_t> result;
  try {
    size_t parseEnd;
    int base = (!decimalOnly && input.size() > 2 && (input[0] == '0' && (input[1] == 'x' || input[1] == 'X'))) ? 16 : 10;
    int64_t userValue = stoll(input, &parseEnd, base);
    if (parseEnd != input.size() || userValue < 0 || 0xFFFFFFFF < userValue) {
      return result;
    }
    result = static_cast<uint32_t>(userValue);
  } catch (...) {
  }
  return result;
}

[[nodiscard]] inline std::optional<int64_t> ParseInt64(const std::string& input, bool decimalOnly = false)
{
  std::optional<int64_t> result;
  try {
    size_t parseEnd;
    int base = (!decimalOnly && input.size() > 2 && (input[0] == '0' && (input[1] == 'x' || input[1] == 'X'))) ? 16 : 10;
    int64_t userValue = stoll(input, &parseEnd, base);
    if (parseEnd != input.size()) {
      return result;
    }
    result = userValue;
  } catch (...) {
  }
  return result;
}

[[nodiscard]] inline std::optional<uint64_t> ParseUInt64(const std::string& input, bool decimalOnly = false)
{
  std::optional<uint64_t> result;
  try {
    size_t parseEnd;
    int base = (!decimalOnly && input.size() > 2 && (input[0] == '0' && (input[1] == 'x' || input[1] == 'X'))) ? 16 : 10;
    int64_t userValue = stoull(input, &parseEnd, base);
    if (parseEnd != input.size()) {
      return result;
    }
    result = userValue;
  } catch (...) {
  }
  return result;
}

[[nodiscard]] inline std::optional<float> ParseFloat(const std::string& input)
{
  std::optional<float> result;
  try {
    size_t parseEnd;
    float userValue = stof(input, &parseEnd);
    if (parseEnd != input.size() || std::isnan(userValue) || !std::isfinite(userValue)) {
      return result;
    }
    if (userValue == 0.f) {
      result = 0.f;
    } else {
      result = userValue;
    }
  } catch (...) {
  }
  return result;
}

[[nodiscard]] inline std::optional<double> ParseDouble(const std::string& input)
{
  std::optional<double> result;
  try {
    size_t parseEnd;
    double userValue = stod(input, &parseEnd);
    if (parseEnd != input.size() || std::isnan(userValue) || !std::isfinite(userValue)) {
      return result;
    }
    if (userValue == 0.0) {
      result = 0.0;
    } else {
      result = userValue;
    }
  } catch (...) {
  }
  return result;
}

[[nodiscard]] inline std::variant<std::monostate, double, std::pair<double, double>> ParseDoubleOrRange(const std::string& input)
{
   std::variant<std::monostate, double, std::pair<double, double>> result;
  {
    std::optional<double> maybeDouble = ParseDouble(input);
    if (maybeDouble.has_value()) {
      result = *maybeDouble;
      return result;
    }
  }

  {
    std::string::size_type hyphenPos = input.find('-');
    if (hyphenPos == std::string::npos) {
      return result;
    }
    std::string front = input.substr(0, hyphenPos);
    std::string back = input.substr(hyphenPos + 1);
    std::optional<double> maybeMin = ParseDouble(front);
    if (!maybeMin.has_value()) {
      return result;
    }
    std::optional<double> maybeMax = ParseDouble(back);
    if (!maybeMax.has_value()) {
      return result;
    }
    result = std::make_pair<double, double>(std::move(maybeMin.value()), std::move(maybeMax.value()));
    return result;
  }
}

[[nodiscard]] inline std::optional<uint8_t> ParseUint8(const std::string& input, bool decimalOnly = false) { return ParseUInt8(input, decimalOnly); }
[[nodiscard]] inline std::optional<uint16_t> ParseUint16(const std::string& input, bool decimalOnly = false) { return ParseUInt16(input, decimalOnly); }
[[nodiscard]] inline std::optional<uint32_t> ParseUint32(const std::string& input, bool decimalOnly = false) { return ParseUInt32(input, decimalOnly); }
[[nodiscard]] inline std::optional<uint64_t> ParseUint64(const std::string& input, bool decimalOnly = false) { return ParseUInt64(input, decimalOnly); }

#endif // AURA_PARSER_H_
