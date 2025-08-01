/*

  Copyright [2024-2025] [Leonardo Julca]

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

/*

   Copyright [2010] [Josko Nikolic]

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

   CODE PORTED FROM THE ORIGINAL GHOST PROJECT

 */

#ifndef AURA_UTIL_H_
#define AURA_UTIL_H_

#include "includes.h"
#include "flat_map.h"
#include "hash.h"
#include "parser.h"

#include <fstream>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <utility>

#pragma once

#define TO_ARRAY(...) StringArray({__VA_ARGS__})

template <size_t N>
constexpr std::array<std::string, N> StringArray(const char* const (&strings)[N]) {
  std::array<std::string, N> arr;
  for (size_t i = 0; i < N; ++i) {
    arr[i] = strings[i];
  }
  return arr;
}

template <typename T>
[[nodiscard]] T SubtractClampZero(const T& minuend, const T& subtrahend) noexcept
{
  if (minuend < subtrahend) return 0;
  return minuend - subtrahend;
}

[[nodiscard]] uint8_t ToBaseZero(const uint8_t slotIndex);
[[nodiscard]] uint8_t ToBaseOne(const uint8_t slotIndex);
[[nodiscard]] std::string ToDecString(const uint8_t byte);
[[nodiscard]] std::string ToHexString(uint32_t i);

#ifdef _WIN32
[[nodiscard]] PLATFORM_STRING_TYPE ToDecStringCPlatform(const size_t value);
#else
[[nodiscard]] PLATFORM_STRING_TYPE ToDecStringCPlatform(const size_t value);
#endif

[[nodiscard]] std::string ToDecStringPadded(const int64_t num, const std::string::size_type padding);
[[nodiscard]] std::string TrimString(const std::string& str);
[[nodiscard]] std::string TrimStringExtended(const std::string& str);
void TrimStringView(std::string_view& str);
[[nodiscard]] PLATFORM_STRING_TYPE TrimPlatformString(const PLATFORM_STRING_TYPE& str);
[[nodiscard]] PLATFORM_STRING_TYPE TrimPlatformStringExtended(const PLATFORM_STRING_TYPE& str);
[[nodiscard]] std::string RemoveDuplicateWhiteSpace(const std::string& str);
void EllideEmptyElementsInPlace(std::vector<std::string>& list);

template <typename Container>
[[nodiscard]] std::string_view GetContainerView(const Container& container);

template <size_t N>
[[nodiscard]] std::string_view GetContainerView(const std::array<uint8_t, N>& container);

[[nodiscard]] inline std::string U8ToString(const std::string &s) {
  return s;
}
[[nodiscard]] inline std::string U8ToString(std::string &&s) {
  return std::move(s);
}
#if defined(__cpp_lib_char8_t)
[[nodiscard]] inline std::string U8ToString(const std::u8string &s) {
  return std::string(s.begin(), s.end());
}
#endif

[[nodiscard]] inline std::string_view ToStringView(const char* s) {
  return s ? std::string_view{s} : std::string_view{};
}

[[nodiscard]] inline std::string_view ToStringView(const std::string& s) {
  return std::string_view{s};
}

[[nodiscard]] inline std::string_view ToStringView(std::string_view sv) {
  return sv;
}

template <typename T>
std::string_view ToStringViewChecked(T&& arg) {
  static_assert(
    std::is_convertible_v<T, std::string_view>,
    "Concat() only accepts string-like arguments or char"
  );
  return ToStringView(std::forward<T>(arg));
}

[[nodiscard]] inline std::string ConcatInner(std::initializer_list<std::string_view> views) {
  size_t totalSize = 0;
  for (auto sv : views) totalSize += sv.size();

  std::string result;
  result.reserve(totalSize);
  for (auto sv : views) result.append(sv);
  return result;
}

template <typename... Args>
[[nodiscard]] std::string Concat(Args&&... args) {
  /*static_assert((std::is_lvalue_reference_v<Args> && ...),
    "All arguments must be lvalues (no temporaries allowed)");*/
  return ConcatInner({
    ToStringViewChecked(std::forward<Args>(args))...
  });
}

[[nodiscard]] std::string ToFormattedString(const double d, const uint8_t precision = 2);
[[nodiscard]] std::string ToFormattedRealm();
[[nodiscard]] std::string ToFormattedRealm(const std::string& hostName);
[[nodiscard]] std::string ToFormattedTimeStampHHMMSS(const int64_t hh, const int64_t mm, const int64_t ss);
[[nodiscard]] std::string ToDurationStringHHMMSS(const int64_t hh, const int64_t mm, const int64_t ss);
template <typename T>
[[nodiscard]] std::string ToFormattedTimeStamp(const T seconds);
template <typename T>
[[nodiscard]] std::string ToDurationString(const T seconds);
[[nodiscard]] std::string ToVersionString(const Version& version);
[[nodiscard]] uint32_t ToVersionFlattened(const Version& version); // MDNS protocol
[[nodiscard]] uint8_t ToVersionOrdinal(const Version& version); // Aura internal
[[nodiscard]] bool GetIsValidVersion(const Version& version);
[[nodiscard]] std::string ToOrdinalName(const size_t number);
template <Endianness endianness>
void WriteUint16(std::vector<uint8_t>& buffer, const uint16_t value, const size_t offset);
template <Endianness endianness>
void WriteUint32(std::vector<uint8_t>& buffer, const uint32_t value, const size_t offset);
[[nodiscard]] std::vector<uint8_t> CopyBytes(const uint8_t* a, const size_t size);
[[nodiscard]] std::vector<uint8_t> CreateByteArray(const uint8_t c);
template <Endianness endianness>
[[nodiscard]] std::vector<uint8_t> CreateByteArray(const uint16_t i);
template <Endianness endianness>
[[nodiscard]] std::vector<uint8_t> CreateByteArray(const uint32_t i);
template <Endianness endianness>
[[nodiscard]] std::vector<uint8_t> CreateByteArrayLossy(const int64_t i);
template <Endianness endianness>
[[nodiscard]] std::vector<uint8_t> CreateByteArray(const float i);
template <Endianness endianness>
[[nodiscard]] std::vector<uint8_t> CreateByteArray(const double i);
[[nodiscard]] std::array<uint8_t, 1> CreateFixedByteArray(const uint8_t c);
template <Endianness endianness>
[[nodiscard]] std::array<uint8_t, 2> CreateFixedByteArray(const uint16_t i);
template <Endianness endianness>
[[nodiscard]] std::array<uint8_t, 4> CreateFixedByteArray(const uint32_t i);
template <Endianness endianness>
[[nodiscard]] std::array<uint8_t, 4> CreateFixedByteArrayLossy(const int64_t i);
template <Endianness endianness>
[[nodiscard]] std::array<uint8_t, 8> CreateFixedByteArray64(const uint64_t i);
template <Endianness endianness>
[[nodiscard]] std::array<uint8_t, 8> CreateFixedByteArray(const double i);
void EnsureFixedByteArray(std::optional<std::array<uint8_t, 1>>& optArray, const uint8_t c);
template <Endianness endianness>
void EnsureFixedByteArray(std::optional<std::array<uint8_t, 2>>& optArray, const uint16_t i);
template <Endianness endianness>
void EnsureFixedByteArray(std::optional<std::array<uint8_t, 4>>& optArray, const uint32_t i);
template <Endianness endianness>
void EnsureFixedByteArrayLossy(std::optional<std::array<uint8_t, 4>>& optArray, const int64_t i);
template <Endianness endianness>
void EnsureFixedByteArray(std::optional<std::array<uint8_t, 8>>& optArray, const double i);
template <Endianness endianness>
[[nodiscard]] uint16_t ByteArrayToUInt16(const std::vector<uint8_t>& b, const size_t start);
template <Endianness endianness>
[[nodiscard]] uint32_t ByteArrayToUInt32(const std::vector<uint8_t>& b, const size_t start);
template <Endianness endianness>
[[nodiscard]] uint16_t ByteArrayToUInt16(const uint8_t* b);
template <Endianness endianness>
[[nodiscard]] uint32_t ByteArrayToUInt32(const uint8_t* b);
[[nodiscard]] uint8_t GetByteAt(const char* b, const size_t pos);
[[nodiscard]] uint8_t GetByteAt(const std::string_view b, const size_t pos);
template <Endianness endianness>
[[nodiscard]] uint16_t ByteArrayToUInt16(const std::string_view b, const size_t start);
template <Endianness endianness>
[[nodiscard]] uint32_t ByteArrayToUInt32(const std::string_view b, const size_t start);
template <Endianness endianness>
[[nodiscard]] uint64_t ByteArrayToUInt64(const std::string_view b, const size_t start);
[[nodiscard]] std::string ByteArrayToDecString(const std::vector<uint8_t>& b);
[[nodiscard]] std::string ByteArrayToDecString(const uint8_t* start, const size_t size);
template <size_t SIZE>
[[nodiscard]] std::string ByteArrayToDecString(const std::array<uint8_t, SIZE>& b);
[[nodiscard]] std::string ByteArrayToHexString(const std::vector<uint8_t>& b);
[[nodiscard]] std::string ByteArrayToHexString(const uint8_t* start, const size_t size);
template <size_t SIZE>
[[nodiscard]] std::string ByteArrayToHexString(const std::array<uint8_t, SIZE>& b);
[[nodiscard]] std::string GetStringBytesHex(const std::string_view b);
[[nodiscard]] std::string ReverseByteArrayToDecString(const std::vector<uint8_t>& b);
[[nodiscard]] std::string ReverseByteArrayToDecString(const uint8_t* start, const size_t size);
template <size_t SIZE>
[[nodiscard]] std::string ReverseByteArrayToDecString(const std::array<uint8_t, SIZE>& b);
[[nodiscard]] std::string ReverseByteArrayToHexString(const std::vector<uint8_t>& b);
template <size_t SIZE>
[[nodiscard]] std::string ReverseByteArrayToHexString(const std::array<uint8_t, SIZE>& b);
void AppendContainer(std::vector<uint8_t>& b, const std::vector<uint8_t>& append);
template <size_t SIZE>
void AppendContainer(std::vector<uint8_t>& b, const std::array<uint8_t, SIZE>& append);
void AppendBytes(std::vector<uint8_t>& b, const uint8_t* a, const size_t size);
void AppendByteArrayString(std::vector<uint8_t>& b, std::string_view append, bool terminator = true);
template <Endianness endianness>
void AppendNumber(std::vector<uint8_t>& b, const uint16_t i);
template <Endianness endianness>
void AppendNumber(std::vector<uint8_t>& b, const uint32_t i);
template <Endianness endianness>
void AppendNumberLossy(std::vector<uint8_t>& b, const int64_t i);
template <Endianness endianness>
void AppendNumber(std::vector<uint8_t>& b, const float i);
template <Endianness endianness>
void AppendNumber(std::vector<uint8_t>& b, const double i);
void AppendSwapString(std::string& fromString, std::string& toString);
void AppendProtoBufferFromLengthDelimitedS2S(std::vector<uint8_t>& b, const std::string& key, const std::string& value);
void AppendProtoBufferFromLengthDelimitedS2C(std::vector<uint8_t>& b, const std::string& key, const uint8_t value);
[[nodiscard]] bool IsAllZeroes(const uint8_t* start, const uint8_t* end);
template <OOBPolicy oobPolicy>
[[nodiscard]] size_t FindNullDelimiterOrStart(const std::vector<uint8_t>& b, const size_t start);
[[nodiscard]] const uint8_t* FindNullDelimiterInRangeOrStart(const uint8_t* start, const uint8_t* end);
template <OOBPolicy oobPolicy>
[[nodiscard]] size_t FindNullDelimiterOrEnd(const std::vector<uint8_t>& b, const size_t start);
template <OOBPolicy oobPolicy>
[[nodiscard]] size_t FindNullDelimiterOrEnd(const std::string_view b, const size_t start);
[[nodiscard]] const uint8_t* FindNullDelimiterInRangeOrEnd(const uint8_t* start, const uint8_t* end);
[[nodiscard]] std::string GetStringAddressRange(const uint8_t* start, const uint8_t* end);
[[nodiscard]] std::string GetStringAddressRange(const std::vector<uint8_t>& b, const size_t start, const size_t end);
template <OOBPolicy oobPolicy, NullTerminatorPolicy nullPolicy, StringEncoding encoding>
[[nodiscard]] std::string_view ExtractStringView(const std::vector<uint8_t>& b, const size_t start, const size_t maxSize);
template <OOBPolicy oobPolicy, NullTerminatorPolicy nullPolicy, StringEncoding encoding>
[[nodiscard]] std::string_view ExtractStringView(const std::string_view b, const size_t start, const size_t maxSize);
[[nodiscard]] std::string_view ExtractUTF8View(const std::vector<uint8_t>& b, const size_t start, const size_t maxSize);
[[nodiscard]] std::string_view ExtractUTF8View(const std::string_view b, const size_t start, const size_t maxSize);
[[nodiscard]] std::vector<uint8_t> ExtractNumbers(const std::string& s, const uint32_t maxCount);
[[nodiscard]] std::vector<uint8_t> ExtractHexNumbers(const std::string& s);
[[nodiscard]] std::vector<uint8_t> ExtractIPv4(const std::string& s);
[[nodiscard]] std::string ToUpperCase(const std::string& input);
[[nodiscard]] std::vector<uint8_t> SplitNumeral(std::string_view input);
[[nodiscard]] std::vector<std::string> SplitArgs(const std::string& s, const uint8_t expectedCount);
[[nodiscard]] std::vector<std::string> SplitArgs(const std::string& s, const uint8_t minCount, const uint8_t maxCount);
[[nodiscard]] std::vector<uint32_t> SplitNumericArgs(const std::string& s, const uint8_t expectedCount);
[[nodiscard]] std::vector<uint32_t> SplitNumericArgs(const std::string& s, const uint8_t minCount, const uint8_t maxCount);
void AssignLength(std::vector<uint8_t>& content);
[[nodiscard]] bool ValidateLength(const std::vector<uint8_t>& content);
[[nodiscard]] bool ValidateLength(const std::string_view content);
[[nodiscard]] std::string AddPathSeparator(const std::string& path);
[[nodiscard]] std::vector<uint8_t> EncodeStatString(const std::vector<uint8_t>& data);
[[nodiscard]] std::vector<uint8_t> DecodeStatString(std::string_view data);
template <typename T>
[[nodiscard]] std::vector<uint8_t> DecodeStatString(const T& data);
[[nodiscard]] std::vector<std::string_view> SplitTokens(std::string_view s, const char delim);
[[nodiscard]] std::string::size_type GetLevenshteinDistance(const std::string& s1, const std::string& s2);
[[nodiscard]] std::string::size_type GetLevenshteinDistanceForSearch(const std::string& s1, const std::string& s2, const std::string::size_type bestDistance);
[[nodiscard]] std::string CheckIsValidHCLStandard(const std::string& s);
[[nodiscard]] std::string CheckIsValidHCLSmall(const std::string& s);
[[nodiscard]] std::string DurationLeftToString(int64_t remainingSeconds);
[[nodiscard]] std::string FourCCToString(uint32_t fourCC);
[[nodiscard]] std::string RemoveNonAlphanumeric(const std::string& s);
[[nodiscard]] std::string RemoveNonAlphanumericNorHyphen(const std::string& s);
[[nodiscard]] size_t GetNearestMultiple(size_t around, size_t divisor);
[[nodiscard]] RangeSizeType ResolveSyncLimits(size_t latency, double deltaTicksMs);
[[nodiscard]] RangeSizeType ResolveSyncLimits(size_t latency, const std::pair<double, double>& deltaTicksMs);
void RangeEnsureMinMaxSorted(size_t* minValue, size_t* maxValue);
void CheckOverflowMultiples(size_t divisor, size_t* minMultiple, size_t* maxMultiple);
[[nodiscard]] bool IsValidMapName(const std::string& s);
[[nodiscard]] bool IsValidCFGName(const std::string& s);
[[nodiscard]] std::string TrimTrailingSlash(const std::string s);
[[nodiscard]] std::string MaybeBase10(const std::string s);
template<typename Container>
[[nodiscard]] std::string JoinStrings(const Container& list, const std::string connector);
template<typename Container>
[[nodiscard]] std::string JoinStrings(const Container& list);
[[nodiscard]] std::string IPv4ToString(const std::array<uint8_t, 4> ip);
[[nodiscard]] bool SplitIPAddressAndPortOrDefault(const std::string& input, const uint16_t defaultPort, std::string& ip, uint16_t& port);

[[nodiscard]] std::string EncodeURIComponent(const std::string& s);
[[nodiscard]] std::string DecodeURIComponent(const std::string& encoded);
[[nodiscard]] bool CaseInsensitiveEquals(const std::string& nameOne, const std::string& nameTwo);
[[nodiscard]] bool FileNameEquals(const std::string& nameOne, const std::string& nameTwo);
[[nodiscard]] bool HasNullOrBreak(const std::string& unsafeInput);
[[nodiscard]] bool PathHasNullBytes(const std::filesystem::path& filePath);
[[nodiscard]] bool IsUnsafeCodePoint(char32_t codePoint);
[[nodiscard]] bool IsASCII(std::string_view unsafeInput);
[[nodiscard]] bool HasUnsafeUTF8CodePoints(std::string_view unsafeUTF8Input);
[[nodiscard]] std::string ToLowerCasePreserveUTF8(std::string_view input);
[[nodiscard]] bool IsArbitraryStringUTF8Safe(std::string_view unsafeInput);
[[nodiscard]] std::string_view SanitizeUTF8(std::string_view unsafeInput, std::string_view = {});
[[nodiscard]] std::string_view SanitizeASCII(std::string_view unsafeInput, std::string_view = {});
[[nodiscard]] std::string SanitizeWrapUTF8(std::string_view unsafeInput, std::string_view = "REDACTED");
[[nodiscard]] std::string SanitizeWrapASCII(std::string_view unsafeInput, std::string_view = "REDACTED");
[[nodiscard]] uint32_t ASCIIHexToNum(const std::array<uint8_t, 8>& data, bool reverse);
[[nodiscard]] std::array<uint8_t, 8> NumToASCIIHex(uint32_t num, bool reverse);
[[nodiscard]] std::string PreparePatternForFuzzySearch(const std::string& rawPattern);
[[nodiscard]] std::string PrepareMapPatternForFuzzySearch(const std::string& rawPattern);
[[nodiscard]] std::vector<std::string> ReadChatTemplate(const std::filesystem::path& filePath);
[[nodiscard]] std::string GetNormalizedAlias(std::string_view alias);
void NormalizeDirectory(std::filesystem::path& filePath);
[[nodiscard]] bool FindNextMissingElementBack(uint8_t& element, std::vector<uint8_t> counters);
[[nodiscard]] std::optional<uint32_t> ToUint32(std::string_view input);
[[nodiscard]] std::optional<uint32_t> ToUint32(const std::string& input);
[[nodiscard]] std::optional<int32_t> ToInt32(const std::string& input);
[[nodiscard]] std::optional<double> ToDouble(const std::string& input);
[[nodiscard]] std::pair<std::string, std::string> SplitAddress(const std::string& fqName);
[[nodiscard]] bool CheckTargetGameSyntax(const std::string& rawInput);
bool ReplaceText(std::string& input, const std::string& fragment, const std::string& replacement);
[[nodiscard]] std::optional<size_t> CountTemplateFixedChars(const std::string& input);
[[nodiscard]] std::multiset<std::string> GetTemplateTokens(const std::string& input);
[[nodiscard]] std::string ReplaceTemplate(const std::string& input, const FlatMap<int64_t, bool>* boolCache, const FlatMap<int64_t, std::string>* textCache, const FlatMap<int64_t, std::function<bool()>>* boolFuncsMap, const FlatMap<int64_t, std::function<std::string()>>* textFuncsMap, bool tolerant = false);
[[nodiscard]] std::string ReplaceTemplate(const std::string& input, std::unordered_map<int64_t, bool>* boolCache, std::unordered_map<int64_t, std::string>* textCache, const FlatMap<int64_t, std::function<bool()>>* boolFuncsMap, const FlatMap<int64_t, std::function<std::string()>>* textFuncsMap, bool tolerant = false);
[[nodiscard]] float LinearInterpolation(const float x, const float x1, const float x2, const float y1, const float y2);
[[nodiscard]] size_t DoubleToSize(double x);
[[nodiscard]] uint64_t DoubleToUnsigned(double x);

/*
[[nodiscard]] float HyperbolicInterpolation(const float x, const float x1, const float x2, const float y1, const float y2);
[[nodiscard]] float ExponentialInterpolation(const float x, const float x1, const float x2, const float y1, const float y2);
*/

[[nodiscard]] uint32_t GetRandomUInt32();

#endif // AURA_UTIL_H_
