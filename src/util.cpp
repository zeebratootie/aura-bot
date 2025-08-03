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

#include "util.h"

#include <random>
#include <regex>
#include <utf8/utf8.h>

using namespace std;

uint8_t ToBaseZero(const uint8_t num)
{
  return static_cast<uint8_t>(num - TINY_ONE);
}

uint8_t ToBaseOne(const uint8_t num)
{
  return static_cast<uint8_t>(num + TINY_ONE);
}

string ToDecString(const uint8_t byte)
{
  return to_string(static_cast<uint16_t>(byte));
}

string ToHexString(uint32_t i)
{
  string       result;
  stringstream SS;
  SS << hex << i;
  SS >> result;
  return result;
}

#ifdef _WIN32
PLATFORM_STRING_TYPE ToDecStringCPlatform(const size_t value)
{
  PLATFORM_STRING_TYPE platformNumeral;
  string numeral = to_string(value);
  utf8::utf8to16(numeral.begin(), numeral.end(), back_inserter(platformNumeral));
  return platformNumeral;
}
#else
PLATFORM_STRING_TYPE ToDecStringCPlatform(const size_t value)
{
  return to_string(value);
}
#endif

string ToDecStringPadded(const int64_t num, const string::size_type padding)
{
  string numeral = to_string(num);
  if (numeral.size() >= padding) return numeral;
  string padded = string(padding, '0');
  string::size_type replacePos = static_cast<string::size_type>(padding - numeral.size());
  padded.replace(padded.begin() + static_cast<string::difference_type>(replacePos), padded.end(), numeral);
  return padded;
}

string TrimString(const string& str)
{
  if (str.empty()) return string();

  string::size_type firstNonSpace = str.find_first_not_of(" ");
  string::size_type lastNonSpace = str.find_last_not_of(" ");

  if (firstNonSpace != string::npos && lastNonSpace != string::npos) {
    return str.substr(firstNonSpace, lastNonSpace - firstNonSpace + 1);
  } else {
    return string();
  }
}

void TrimStringView(string_view& str)
{
  if (str.empty()) return;

  string_view::size_type firstNonSpace = str.find_first_not_of(" ");
  string_view::size_type lastNonSpace = str.find_last_not_of(" ");

  if (firstNonSpace != string_view::npos && lastNonSpace != string_view::npos) {
    str.remove_suffix(str.size() - 1 - lastNonSpace);
    str.remove_prefix(firstNonSpace);
  } else {
    str.remove_prefix(str.size());
  }
}

PLATFORM_STRING_TYPE TrimPlatformString(const PLATFORM_STRING_TYPE& str)
{
  if (str.empty()) return PLATFORM_STRING_TYPE();

  PLATFORM_STRING_TYPE::size_type firstNonSpace = str.find_first_not_of(PLATFORM_STRING(" "));
  PLATFORM_STRING_TYPE::size_type lastNonSpace = str.find_last_not_of(PLATFORM_STRING(" "));

  if (firstNonSpace != PLATFORM_STRING_TYPE::npos && lastNonSpace != PLATFORM_STRING_TYPE::npos) {
    return str.substr(firstNonSpace, lastNonSpace - firstNonSpace + 1);
  } else {
    return PLATFORM_STRING_TYPE();
  }
}

string TrimStringExtended(const string& str)
{
  if (str.empty()) return string();

  size_t firstNonSpace = str.find_first_not_of(" \r\n");
  size_t lastNonSpace = str.find_last_not_of(" \r\n");

  if (firstNonSpace != string::npos && lastNonSpace != string::npos) {
    return str.substr(firstNonSpace, lastNonSpace - firstNonSpace + 1);
  } else {
    return string();
  }
}

PLATFORM_STRING_TYPE TrimPlatformStringExtended(const PLATFORM_STRING_TYPE& str)
{
  if (str.empty()) return PLATFORM_STRING_TYPE();

  PLATFORM_STRING_TYPE::size_type firstNonSpace = str.find_first_not_of(PLATFORM_STRING(" \r\n"));
  PLATFORM_STRING_TYPE::size_type lastNonSpace = str.find_last_not_of(PLATFORM_STRING(" \r\n"));

  if (firstNonSpace != PLATFORM_STRING_TYPE::npos && lastNonSpace != PLATFORM_STRING_TYPE::npos) {
    return str.substr(firstNonSpace, lastNonSpace - firstNonSpace + 1);
  } else {
    return PLATFORM_STRING_TYPE();
  }
}

string RemoveDuplicateWhiteSpace(const string& str)
{
  string result;
  result.reserve(str.size());

  bool wasSpace = false;
  for (char c : str) {
    bool isSpace = c == ' ';
    if (!(isSpace && wasSpace)) {
      result += c;
    }
    wasSpace = isSpace;
  }

  return result;
}

void EllideEmptyElementsInPlace(vector<string>& list)
{
  auto it = list.rbegin();
  while (it != list.rend()) {
    if (it->empty()) {
      it = vector<string>::reverse_iterator(list.erase((++it).base()));
    } else {
      it++;
    }
  }
}

template <typename Container>
string_view GetContainerView(const Container& container) {
  return string_view(reinterpret_cast<const char*>(container.data()), container.size());
}
template string_view GetContainerView(const std::string& container);
template string_view GetContainerView(const std::vector<uint8_t>& container);

template <size_t N>
string_view GetContainerView(const std::array<uint8_t, N>& container) {
  return string_view(reinterpret_cast<const char*>(container.data()), N);
}
template string_view GetContainerView(const std::array<uint8_t, 2>& container);
template string_view GetContainerView(const std::array<uint8_t, 4>& container);
template string_view GetContainerView(const std::array<uint8_t, 8>& container);
template string_view GetContainerView(const std::array<uint8_t, 20>& container);
template string_view GetContainerView(const std::array<uint8_t, 32>& container);

string ToFormattedString(const double d, const uint8_t precision)
{
  ostringstream out;
  out << fixed << setprecision(precision) << d;
  return out.str();
}

string ToFormattedRealm()
{
  return "@@LAN/VPN";
}

string ToFormattedRealm(const string& hostName)
{
  if (hostName.empty()) return ToFormattedRealm();
  return hostName;
}

string ToFormattedTimeStampHHMMSS(const int64_t hh, const int64_t mm, const int64_t ss)
{
  if (hh > 0) return ToDecStringPadded(hh, 2) + ":" + ToDecStringPadded(mm, 2) + ":" + ToDecStringPadded(ss, 2);
  return ToDecStringPadded(mm, 2) + ":" + ToDecStringPadded(ss, 2);
}

string ToDurationStringHHMMSS(const int64_t hh, const int64_t mm, const int64_t ss)
{
  string result;
  if (hh > 0) result.append(to_string(hh) + " h ");
  if (mm > 0) result.append(to_string(mm) + " min ");
  if (ss > 0) result.append(to_string(ss) + " s ");
  if (result.empty()) return "Now";
  return TrimString(result);
}

template <typename T>
string ToFormattedTimeStamp(const T seconds)
{
  T ss, mm, hh;
  ss = seconds;

  mm = ss / 60;
  ss = ss % 60;
  hh = mm / 60;
  mm = mm % 60;

  return ToFormattedTimeStampHHMMSS((const int64_t)hh, (const int64_t)mm, (const int64_t)ss);
}

template <typename T>
string ToDurationString(const T seconds)
{
  T ss, mm, hh;
  ss = seconds;

  mm = ss / 60;
  ss = ss % 60;
  hh = mm / 60;
  mm = mm % 60;

  return ToDurationStringHHMMSS((const int64_t)hh, (const int64_t)mm, (const int64_t)ss);
}

template string ToFormattedTimeStamp(const int64_t seconds);
template string ToFormattedTimeStamp(const uint64_t seconds);
template string ToDurationString(const int64_t seconds);
template string ToDurationString(const uint64_t seconds);

string ToVersionString(const Version& version)
{
  return ToDecString(version.first) + "." + ToDecString(version.second);
}

uint32_t ToVersionFlattened(const Version& version) // MDNS protocol
{
  return (uint32_t)10000u + (uint32_t)100u * ((uint32_t)version.first - 1u) + (uint32_t)(version.second);
}

uint8_t ToVersionOrdinal(const Version& version) // Aura internal
{
  return static_cast<uint8_t>((uint32_t)(version.first - TINY_ONE) * (uint32_t)TINY_37 + (uint32_t)version.second);
}

bool GetIsValidVersion(const Version& version)
{
  switch (version.first) {
    case 2: return true;
    case 1: return version.second <= TINY_36;
    default: return false;
  }
}

string ToOrdinalName(const size_t number)
{
  switch (number % 10) {
    case 1: return to_string(number) + "st";
    case 2: return to_string(number) + "nd";
    case 3: return to_string(number) + "rd";
    default: return to_string(number) + "th";
  }
}

template <Endianness endianness>
void WriteUint16(vector<uint8_t>& buffer, const uint16_t value, const size_t offset)
{
  if constexpr (endianness == Endianness::kLittle) {
    buffer[offset] = static_cast<uint8_t>(value);
    buffer[offset + 1] = static_cast<uint8_t>(value >> 8);
  } else {
    buffer[offset] = static_cast<uint8_t>(value >> 8);
    buffer[offset + 1] = static_cast<uint8_t>(value);
  }
}

template void WriteUint16<Endianness::kLittle>(vector<uint8_t>& buffer, const uint16_t value, const size_t offset);

template <Endianness endianness>
void WriteUint32(vector<uint8_t>& buffer, const uint32_t value, const size_t offset)
{
  if constexpr (endianness == Endianness::kLittle) {
    buffer[offset] = static_cast<uint8_t>(value);
    buffer[offset + 1] = static_cast<uint8_t>(value >> 8);
    buffer[offset + 2] = static_cast<uint8_t>(value >> 16);
    buffer[offset + 3] = static_cast<uint8_t>(value >> 24);
  } else {
    buffer[offset] = static_cast<uint8_t>(value >> 24);
    buffer[offset + 1] = static_cast<uint8_t>(value >> 16);
    buffer[offset + 2] = static_cast<uint8_t>(value >> 8);
    buffer[offset + 3] = static_cast<uint8_t>(value);
  }
}

template void WriteUint32<Endianness::kLittle>(vector<uint8_t>& buffer, const uint32_t value, const size_t offset);

vector<uint8_t> CopyBytes(const uint8_t* a, const size_t size)
{
  return vector<uint8_t>(a, a + size);
}

vector<uint8_t> CreateByteArray(const uint8_t c)
{
  return vector<uint8_t>{c};
}

template <Endianness endianness>
vector<uint8_t> CreateByteArray(const uint16_t i)
{
  if constexpr (endianness == Endianness::kLittle) {
    return vector<uint8_t>{static_cast<uint8_t>(i), static_cast<uint8_t>(i >> 8)};
  } else {
    return vector<uint8_t>{static_cast<uint8_t>(i >> 8), static_cast<uint8_t>(i)};
  }
}

template vector<uint8_t> CreateByteArray<Endianness::kLittle>(const uint16_t i);

template <Endianness endianness>
vector<uint8_t> CreateByteArray(const uint32_t i)
{
  if constexpr (endianness == Endianness::kLittle) {
    return vector<uint8_t>{static_cast<uint8_t>(i), static_cast<uint8_t>(i >> 8), static_cast<uint8_t>(i >> 16), static_cast<uint8_t>(i >> 24)};
  } else {
    return vector<uint8_t>{static_cast<uint8_t>(i >> 24), static_cast<uint8_t>(i >> 16), static_cast<uint8_t>(i >> 8), static_cast<uint8_t>(i)};
  }
}

template vector<uint8_t> CreateByteArray<Endianness::kLittle>(const uint32_t i);

template <Endianness endianness>
vector<uint8_t> CreateByteArrayLossy(const int64_t i)
{
  // FIXME: CreateByteArray int64_t overload loses 4 bytes
  if constexpr (endianness == Endianness::kLittle) {
    return vector<uint8_t>{
      static_cast<uint8_t>(i), static_cast<uint8_t>(i >> 8), static_cast<uint8_t>(i >> 16), static_cast<uint8_t>(i >> 24)/*,
      static_cast<uint8_t>(i >> 32), static_cast<uint8_t>(i >> 40), static_cast<uint8_t>(i >> 48), static_cast<uint8_t>(i >> 56)*/
    };
  } else {
    return vector<uint8_t>{
      /*static_cast<uint8_t>(i >> 56), static_cast<uint8_t>(i >> 48), static_cast<uint8_t>(i >> 40), static_cast<uint8_t>(32),*/
      static_cast<uint8_t>(i >> 24), static_cast<uint8_t>(i >> 16), static_cast<uint8_t>(i >> 8), static_cast<uint8_t>(i)
    };
  }
}

template vector<uint8_t> CreateByteArrayLossy<Endianness::kLittle>(const int64_t i);

template <Endianness endianness>
vector<uint8_t> CreateByteArray(const float i)
{
  vector<uint8_t> bytes(4, 0);
  if (numeric_limits<float>::is_iec559) {
    memcpy(bytes.data(), &i, sizeof(float));
    if constexpr (endianness == Endianness::kLittle) {
      if (GetIsHostBigEndian()) {
        reverse(bytes.begin(), bytes.end());
      }
    } else {
      if (!GetIsHostBigEndian()) {
        reverse(bytes.begin(), bytes.end());
      }
    }
  }
  return bytes;
}

template vector<uint8_t> CreateByteArray<Endianness::kLittle>(const float i);

template <Endianness endianness>
vector<uint8_t> CreateByteArray(const double i)
{
  vector<uint8_t> bytes(8, 0);
  if (numeric_limits<double>::is_iec559) {
    memcpy(bytes.data(), &i, sizeof(double));
    if constexpr (endianness == Endianness::kLittle) {
      if (GetIsHostBigEndian()) {
        reverse(bytes.begin(), bytes.end());
      }
    } else {
      if (!GetIsHostBigEndian()) {
        reverse(bytes.begin(), bytes.end());
      }
    }
  }
  return bytes;
}

template vector<uint8_t> CreateByteArray<Endianness::kLittle>(const double i);

array<uint8_t, 1> CreateFixedByteArray(const uint8_t c)
{
  return array<uint8_t, 1>{c};
}

template <Endianness endianness>
array<uint8_t, 2> CreateFixedByteArray(const uint16_t i)
{
  if constexpr (endianness == Endianness::kLittle) {
    return array<uint8_t, 2>{static_cast<uint8_t>(i), static_cast<uint8_t>(i >> 8)};
  } else {
    return array<uint8_t, 2>{static_cast<uint8_t>(i >> 8), static_cast<uint8_t>(i)};
  }
}

template array<uint8_t, 2> CreateFixedByteArray<Endianness::kBig>(const uint16_t i);
template array<uint8_t, 2> CreateFixedByteArray<Endianness::kLittle>(const uint16_t i);

template <Endianness endianness>
array<uint8_t, 4> CreateFixedByteArray(const uint32_t i)
{
  if constexpr (endianness == Endianness::kLittle) {
    return array<uint8_t, 4>{static_cast<uint8_t>(i), static_cast<uint8_t>(i >> 8), static_cast<uint8_t>(i >> 16), static_cast<uint8_t>(i >> 24)};
  } else {
    return array<uint8_t, 4>{static_cast<uint8_t>(i >> 24), static_cast<uint8_t>(i >> 16), static_cast<uint8_t>(i >> 8), static_cast<uint8_t>(i)};
  }
}

template array<uint8_t, 4> CreateFixedByteArray<Endianness::kBig>(const uint32_t i);
template array<uint8_t, 4> CreateFixedByteArray<Endianness::kLittle>(const uint32_t i);

template <Endianness endianness>
array<uint8_t, 4> CreateFixedByteArrayLossy(const int64_t i)
{
  // FIXME: CreateFixedByteArray int64_t overload loses 4 bytes
  if constexpr (endianness == Endianness::kLittle) {
    return array<uint8_t, 4>{
      static_cast<uint8_t>(i), static_cast<uint8_t>(i >> 8), static_cast<uint8_t>(i >> 16), static_cast<uint8_t>(i >> 24)/*,
      static_cast<uint8_t>(i >> 32), static_cast<uint8_t>(i >> 40), static_cast<uint8_t>(i >> 48), static_cast<uint8_t>(i >> 56)*/
    };
  } else {
    return array<uint8_t, 4>{
      /*static_cast<uint8_t>(i >> 56), static_cast<uint8_t>(i >> 48), static_cast<uint8_t>(i >> 40), static_cast<uint8_t>(32),*/
      static_cast<uint8_t>(i >> 24), static_cast<uint8_t>(i >> 16), static_cast<uint8_t>(i >> 8), static_cast<uint8_t>(i)
    };
  }
}

template array<uint8_t, 4> CreateFixedByteArrayLossy<Endianness::kLittle>(const int64_t i);

template <Endianness endianness>
array<uint8_t, 8> CreateFixedByteArray64(const uint64_t i)
{
  if constexpr (endianness == Endianness::kLittle) {
    return array<uint8_t, 8>{
      static_cast<uint8_t>(i), static_cast<uint8_t>(i >> 8), static_cast<uint8_t>(i >> 16), static_cast<uint8_t>(i >> 24),
      static_cast<uint8_t>(i >> 32), static_cast<uint8_t>(i >> 40), static_cast<uint8_t>(i >> 48), static_cast<uint8_t>(i >> 56)
    };
  } else {
    return array<uint8_t, 8>{
      static_cast<uint8_t>(i >> 56), static_cast<uint8_t>(i >> 48), static_cast<uint8_t>(i >> 40), static_cast<uint8_t>(32),
      static_cast<uint8_t>(i >> 24), static_cast<uint8_t>(i >> 16), static_cast<uint8_t>(i >> 8), static_cast<uint8_t>(i)
    };
  }
}

template array<uint8_t, 8> CreateFixedByteArray64<Endianness::kLittle>(const uint64_t i);

template <Endianness endianness>
array<uint8_t, 8> CreateFixedByteArray(const double i)
{
  array<uint8_t, 8> bytes;
  bytes.fill(0);
  if (numeric_limits<double>::is_iec559) {
    memcpy(bytes.data(), &i, sizeof(double));
    if constexpr (endianness == Endianness::kLittle) {
      if (GetIsHostBigEndian()) {
        reverse(bytes.begin(), bytes.end());
      }
    } else {
      if (!GetIsHostBigEndian()) {
        reverse(bytes.begin(), bytes.end());
      }
    }
  }
  return bytes;
}

template array<uint8_t, 8> CreateFixedByteArray<Endianness::kLittle>(const double i);

void EnsureFixedByteArray(optional<array<uint8_t, 1>>& optArray, const uint8_t c)
{
  array<uint8_t, 1> val = CreateFixedByteArray(c);
  optArray.emplace();
  optArray->swap(val);
}

template <Endianness endianness>
void EnsureFixedByteArray(optional<array<uint8_t, 2>>& optArray, const uint16_t i)
{
  array<uint8_t, 2> val = CreateFixedByteArray<endianness>(i);
  optArray.emplace();
  optArray->swap(val);
}

template void EnsureFixedByteArray<Endianness::kLittle>(optional<array<uint8_t, 2>>& optArray, const uint16_t i);

template <Endianness endianness>
void EnsureFixedByteArray(optional<array<uint8_t, 4>>& optArray, const uint32_t i)
{
  array<uint8_t, 4> val = CreateFixedByteArray<endianness>(i);
  optArray.emplace();
  optArray->swap(val);
}

template void EnsureFixedByteArray<Endianness::kLittle>(optional<array<uint8_t, 4>>& optArray, const uint32_t i);
template void EnsureFixedByteArray<Endianness::kBig>(optional<array<uint8_t, 4>>& optArray, const uint32_t i);

template <Endianness endianness>
void EnsureFixedByteArrayLossy(optional<array<uint8_t, 4>>& optArray, const int64_t i)
{
  // FIXME: EnsureFixedByteArray int64_t overload loses 4 bytes
  // NOTE: This one is apparently unused.
  array<uint8_t, 4> val = CreateFixedByteArrayLossy<endianness>(i);
  optArray.emplace();
  optArray->swap(val);
}

template void EnsureFixedByteArrayLossy<Endianness::kLittle>(optional<array<uint8_t, 4>>& optArray, const int64_t i);

template <Endianness endianness>
void EnsureFixedByteArray(optional<array<uint8_t, 8>>& optArray, const double i)
{
  array<uint8_t, 8> val = CreateFixedByteArray<endianness>(i);
  optArray.emplace();
  optArray->swap(val);
}

template void EnsureFixedByteArray<Endianness::kLittle>(optional<array<uint8_t, 8>>& optArray, const double i);

template <Endianness endianness>
uint16_t ByteArrayToUInt16(const vector<uint8_t>& b, const size_t start)
{
  if (b.size() < start + 2)
    return 0;

  if constexpr (endianness == Endianness::kLittle) {
    return static_cast<uint16_t>(b[start + 1] << 8 | b[start]);
  } else {
    return static_cast<uint16_t>(b[start] << 8 | b[start + 1]);
  }
}

template uint16_t ByteArrayToUInt16<Endianness::kLittle>(const vector<uint8_t>& b, const size_t start);
template uint16_t ByteArrayToUInt16<Endianness::kBig>(const vector<uint8_t>& b, const size_t start);

template <Endianness endianness>
uint32_t ByteArrayToUInt32(const vector<uint8_t>& b, const size_t start)
{
  if (b.size() < start + 4)
    return 0;

  if constexpr (endianness == Endianness::kLittle) {
    return static_cast<uint32_t>(b[start + 3] << 24 | b[start + 2] << 16 | b[start + 1] << 8 | b[start]);
  } else {
    return static_cast<uint32_t>(b[start] << 24 | b[start + 1] << 16 | b[start + 2] << 8 | b[start + 3]);
  }
}

template uint32_t ByteArrayToUInt32<Endianness::kLittle>(const vector<uint8_t>& b, const size_t start);
template uint32_t ByteArrayToUInt32<Endianness::kBig>(const vector<uint8_t>& b, const size_t start);

template <Endianness endianness>
uint16_t ByteArrayToUInt16(const uint8_t* b)
{
  if constexpr (endianness == Endianness::kLittle) {
    return static_cast<uint16_t>(
      ((const uint16_t)(b[1]) << 8) |
      ((const uint16_t)(b[0]))
    );
  } else {
    return static_cast<uint16_t>(
      ((const uint16_t)(b[2]) << 8) |
      ((const uint16_t)(b[3]))
    );
  }
}

template uint16_t ByteArrayToUInt16<Endianness::kLittle>(const uint8_t* b);

template <Endianness endianness>
uint32_t ByteArrayToUInt32(const uint8_t* b)
{
  if constexpr (endianness == Endianness::kLittle) {
    return static_cast<uint32_t>(
      ((const uint32_t)(b[3]) << 24) |
      ((const uint32_t)(b[2]) << 16) |
      ((const uint32_t)(b[1]) << 8) |
      ((const uint32_t)(b[0]))
    );
  } else {
    return static_cast<uint32_t>(
      ((const uint32_t)(b[0]) << 24) |
      ((const uint32_t)(b[1]) << 16) |
      ((const uint32_t)(b[2]) << 8) |
      ((const uint32_t)(b[3]))
    );
  }
}

template uint32_t ByteArrayToUInt32<Endianness::kLittle>(const uint8_t* b);

uint8_t GetByteAt(const char* b, const size_t pos)
{
  return (uint8_t)(static_cast<unsigned char>(b[pos]));
}

uint8_t GetByteAt(const string_view b, const size_t pos)
{
  return (uint8_t)(static_cast<unsigned char>(b[pos]));
}

template <Endianness endianness>
uint16_t ByteArrayToUInt16(const string_view b, const size_t start)
{
  if (b.size() < start + 2)
    return 0;

  if constexpr (endianness == Endianness::kLittle) {
    return static_cast<uint16_t>(
      ((uint16_t)(static_cast<unsigned char>(b[start + 1])) << 8) |
      ((uint16_t)(static_cast<unsigned char>(b[start])))
    );
  } else {
    return static_cast<uint16_t>(
      ((uint16_t)(static_cast<unsigned char>(b[start])) << 8) |
      ((uint16_t)(static_cast<unsigned char>(b[start + 1])))
    );
  }
}

template uint16_t ByteArrayToUInt16<Endianness::kLittle>(const string_view b, const size_t start);
template uint16_t ByteArrayToUInt16<Endianness::kBig>(const string_view b, const size_t start);

template <Endianness endianness>
uint32_t ByteArrayToUInt32(const string_view b, const size_t start)
{
  if (b.size() < start + 4)
    return 0;

  if constexpr (endianness == Endianness::kLittle) {
    return static_cast<uint32_t>(
      ((uint32_t)(static_cast<unsigned char>(b[start + 3])) << 24) |
      ((uint32_t)(static_cast<unsigned char>(b[start + 2])) << 16) |
      ((uint32_t)(static_cast<unsigned char>(b[start + 1])) << 8) |
      ((uint32_t)(static_cast<unsigned char>(b[start])))
    );
  } else {
    return static_cast<uint32_t>(
      ((uint32_t)(static_cast<unsigned char>(b[start])) << 24) |
      ((uint32_t)(static_cast<unsigned char>(b[start + 1])) << 16) |
      ((uint32_t)(static_cast<unsigned char>(b[start + 2])) << 8) |
      ((uint32_t)(static_cast<unsigned char>(b[start + 3])))
    );
  }
}

template uint32_t ByteArrayToUInt32<Endianness::kLittle>(const string_view b, const size_t start);
template uint32_t ByteArrayToUInt32<Endianness::kBig>(const string_view b, const size_t start);

template <Endianness endianness>
uint64_t ByteArrayToUInt64(const string_view b, const size_t start)
{
  if (b.size() < start + 4)
    return 0;

  if constexpr (endianness == Endianness::kLittle) {
    return static_cast<uint64_t>(
      ((uint64_t)(static_cast<unsigned char>(b[start + 7])) << 56) |
      ((uint64_t)(static_cast<unsigned char>(b[start + 6])) << 48) |
      ((uint64_t)(static_cast<unsigned char>(b[start + 5])) << 40) |
      ((uint64_t)(static_cast<unsigned char>(b[start + 4])) << 32) |
      ((uint64_t)(static_cast<unsigned char>(b[start + 3])) << 24) |
      ((uint64_t)(static_cast<unsigned char>(b[start + 2])) << 16) |
      ((uint64_t)(static_cast<unsigned char>(b[start + 1])) << 8) |
      ((uint64_t)(static_cast<unsigned char>(b[start])))
    );
  } else {
    return static_cast<uint64_t>(
      ((uint64_t)(static_cast<unsigned char>(b[start])) << 56) |
      ((uint64_t)(static_cast<unsigned char>(b[start + 1])) << 48) |
      ((uint64_t)(static_cast<unsigned char>(b[start + 2])) << 40) |
      ((uint64_t)(static_cast<unsigned char>(b[start + 3])) << 32) |
      ((uint64_t)(static_cast<unsigned char>(b[start + 4])) << 24) |
      ((uint64_t)(static_cast<unsigned char>(b[start + 5])) << 16) |
      ((uint64_t)(static_cast<unsigned char>(b[start + 6])) << 8) |
      ((uint64_t)(static_cast<unsigned char>(b[start + 7])))
    );
  }
}

template uint64_t ByteArrayToUInt64<Endianness::kLittle>(const string_view b, const size_t start);
template uint64_t ByteArrayToUInt64<Endianness::kBig>(const string_view b, const size_t start);

string ByteArrayToDecString(const vector<uint8_t>& b)
{
  if (b.empty())
    return string();

  string result = to_string(b[0]);

  for (auto i = cbegin(b) + 1; i != cend(b); ++i)
    result += " " + to_string(*i);

  return result;
}

string ByteArrayToDecString(const uint8_t* start, const size_t size)
{
  if (size == 0)
    return string();

  string result = to_string(start[0]);
  for (size_t i = 1; i < size; ++i) {
    result += " " + to_string(start[i]);
  }

  return result;
}

template <size_t SIZE>
string ByteArrayToDecString(const array<uint8_t, SIZE>& b)
{
  string result = to_string(b[0]);

  for (auto i = cbegin(b) + 1; i != cend(b); ++i)
    result += " " + to_string(*i);

  return result;
}

template string ByteArrayToDecString(const array<uint8_t, 2>& b);
template string ByteArrayToDecString(const array<uint8_t, 4>& b);
template string ByteArrayToDecString(const array<uint8_t, 20>& b);

string ByteArrayToHexString(const vector<uint8_t>& b)
{
  if (b.empty())
    return string();

  string result = ToHexString(b[0]);

  for (auto i = cbegin(b) + 1; i != cend(b); ++i)
  {
    if (*i < 0x10)
      result += " 0" + ToHexString(*i);
    else
      result += " " + ToHexString(*i);
  }

  return result;
}

string ByteArrayToHexString(const uint8_t* start, const size_t size)
{
  if (size == 0)
    return string();

  string result = ToHexString(start[0]);
  for (size_t i = 1; i < size; ++i) {
    if (start[i] < 0x10) 
      result += " 0" + ToHexString(start[i]);
    else
      result += " " + ToHexString(start[i]);
  }

  return result;
}

template <size_t SIZE>
string ByteArrayToHexString(const array<uint8_t, SIZE>& b)
{
  string result = ToHexString(b[0]);

  for (auto i = cbegin(b) + 1; i != cend(b); ++i)
  {
    if (*i < 0x10)
      result += " 0" + ToHexString(*i);
    else
      result += " " + ToHexString(*i);
  }

  return result;
}

template string ByteArrayToHexString(const array<uint8_t, 4>& b);
template string ByteArrayToHexString(const array<uint8_t, 20>& b);

string GetStringBytesHex(string_view sv)
{
  if (sv.empty()) return string();
  string result;
  {
    unsigned char b = static_cast<unsigned char>(sv[0]);
    if (b < 0x10)
      result += "0" + ToHexString((uint16_t)b);
    else
      result += ToHexString((uint16_t)b);
  }

  for (auto i = cbegin(sv) + 1; i != cend(sv); ++i) {
    unsigned char b = static_cast<unsigned char>(*i);
    if (b < 0x10)
      result += " 0" + ToHexString((uint16_t)b);
    else
      result += " " + ToHexString((uint16_t)b);
  }

  return result;
}

string ReverseByteArrayToDecString(const vector<uint8_t>& b)
{
  if (b.empty())
    return string();

  string result = to_string(b.back());

  for (auto i = b.crbegin() + 1; i != b.crend(); ++i)
    result += " " + to_string(*i);

  return result;
}

string ReverseByteArrayToDecString(const uint8_t* start, const size_t size)
{
  if (size == 0)
    return string();

  size_t i = size - 1;
  string result = to_string(start[i]);

  while (i--) {
    result += " " + to_string(start[i]);
  }

  return result;
}

template <size_t SIZE>
string ReverseByteArrayToDecString(const array<uint8_t, SIZE>& b)
{
  string result = to_string(b.back());

  for (auto i = b.crbegin() + 1; i != b.crend(); ++i)
    result += " " + to_string(*i);

  return result;
}
template string ReverseByteArrayToDecString(const array<uint8_t, 4>& b);
template string ReverseByteArrayToDecString(const array<uint8_t, 20>& b);

string ReverseByteArrayToHexString(const vector<uint8_t>& b)
{
  if (b.empty())
    return string();

  string result = ToHexString(b.back());

  for (auto i = b.crbegin() + 1; i != b.crend(); ++i)
  {
    if (*i < 0x10)
      result += " 0" + ToHexString(*i);
    else
      result += " " + ToHexString(*i);
  }

  return result;
}

template <size_t SIZE>
string ReverseByteArrayToHexString(const array<uint8_t, SIZE>& b)
{
  string result = ToHexString(b.back());

  for (auto i = b.crbegin() + 1; i != b.crend(); ++i)
  {
    if (*i < 0x10)
      result += " 0" + ToHexString(*i);
    else
      result += " " + ToHexString(*i);
  }

  return result;
}

template string ReverseByteArrayToHexString(const array<uint8_t, 4>& b);
template string ReverseByteArrayToHexString(const array<uint8_t, 20>& b);

void AppendContainer(vector<uint8_t>& b, const vector<uint8_t>& append)
{
  b.insert(end(b), begin(append), end(append));
}

template <size_t SIZE>
void AppendContainer(vector<uint8_t>& b, const array<uint8_t, SIZE>& append)
{
  b.insert(end(b), begin(append), end(append));
}

template void AppendContainer(vector<uint8_t>& b, const array<uint8_t, 2>& append);
template void AppendContainer(vector<uint8_t>& b, const array<uint8_t, 4>& append);
template void AppendContainer(vector<uint8_t>& b, const array<uint8_t, 8>& append);
template void AppendContainer(vector<uint8_t>& b, const array<uint8_t, 20>& append);
template void AppendContainer(vector<uint8_t>& b, const array<uint8_t, 32>& append);

void AppendBytes(vector<uint8_t>& b, const uint8_t* a, const size_t size)
{
  size_t cursor = b.size();
  b.resize(cursor + size);
  copy_n(a, size, b.data() + cursor);
}

void AppendByteArrayString(vector<uint8_t>& b, string_view append, bool terminator)
{
  // append the string plus a null terminator

  b.insert(end(b), begin(append), end(append));

  if (terminator)
    b.push_back(0);
}

template <Endianness endianness>
void AppendNumber(vector<uint8_t>& b, const uint16_t i)
{
  size_t offset = b.size();
  b.resize(offset + 2);
  uint8_t* cursor = b.data() + offset;
  if constexpr (endianness == Endianness::kBig) {
    cursor[0] = static_cast<uint8_t>(i >> 8);
    cursor[1] = static_cast<uint8_t>(i);
  } else {
    cursor[0] = static_cast<uint8_t>(i);
    cursor[1] = static_cast<uint8_t>(i >> 8);
  }
}

template void AppendNumber<Endianness::kLittle>(vector<uint8_t>& b, const uint16_t i);
template void AppendNumber<Endianness::kBig>(vector<uint8_t>& b, const uint16_t i);

template <Endianness endianness>
void AppendNumber(vector<uint8_t>& b, const uint32_t i)
{
  size_t offset = b.size();
  b.resize(offset + 4);
  uint8_t* cursor = b.data() + offset;
  if constexpr (endianness == Endianness::kBig) {
    cursor[0] = static_cast<uint8_t>(i >> 24);
    cursor[1] = static_cast<uint8_t>(i >> 16);
    cursor[2] = static_cast<uint8_t>(i >> 8);
    cursor[3] = static_cast<uint8_t>(i);
  } else {
    cursor[0] = static_cast<uint8_t>(i);
    cursor[1] = static_cast<uint8_t>(i >> 8);
    cursor[2] = static_cast<uint8_t>(i >> 16);
    cursor[3] = static_cast<uint8_t>(i >> 24);
  }
}

template void AppendNumber<Endianness::kLittle>(vector<uint8_t>& b, const uint32_t i);

template <Endianness endianness>
void AppendNumberLossy(vector<uint8_t>& b, const int64_t i)
{
  // FIXME: AppendNumber int64_t overload loses 4 bytes
  AppendContainer(b, CreateByteArrayLossy<endianness>(i));
}

template void AppendNumberLossy<Endianness::kLittle>(vector<uint8_t>& b, const int64_t i);

template <Endianness endianness>
void AppendNumber(vector<uint8_t>& b, const float i)
{
  size_t offset = b.size();
  b.resize(offset + 4);
  if constexpr (numeric_limits<float>::is_iec559 && sizeof(float) == 4) {
    uint8_t* cursor = b.data() + offset;
    memcpy(cursor, &i, 4);
    if constexpr (endianness == Endianness::kLittle) {
      if (GetIsHostBigEndian()) {
        reverse(cursor, cursor + 4);
      }
    } else {
      if (!GetIsHostBigEndian()) {
        reverse(cursor, cursor + 4);
      }
    }
  } else {
  }
}

template void AppendNumber<Endianness::kLittle>(vector<uint8_t>& b, const float i);

template <Endianness endianness>
void AppendNumber(vector<uint8_t>& b, const double i)
{
  size_t offset = b.size();
  b.resize(offset + 8);
  if constexpr (numeric_limits<double>::is_iec559 && sizeof(double) == 8) {
    uint8_t* cursor = b.data() + offset;
    memcpy(cursor, &i, 8);
    if constexpr (endianness == Endianness::kLittle) {
      if (GetIsHostBigEndian()) {
        reverse(cursor, cursor + 8);
      }
    } else {
      if (!GetIsHostBigEndian()) {
        reverse(cursor, cursor + 8);
      }
    }
  }
}

template void AppendNumber<Endianness::kLittle>(vector<uint8_t>& b, const double i);

void AppendSwapString(string& fromString, string& toString)
{
  if (toString.empty()) {
    fromString.swap(toString);
  } else {
    toString.append(fromString);
    fromString.clear();
  }
}

void AppendProtoBufferFromLengthDelimitedS2S(vector<uint8_t>& b, const string& key, const string& value)
{
  size_t thisSize = key.size() + value.size() + 4;
  b.reserve(b.size() + thisSize + 2);
  b.push_back(0x1A);
  b.push_back((uint8_t)thisSize);
  b.push_back(0x0A);
  b.push_back((uint8_t)key.size());
  AppendByteArrayString(b, key, false);
  b.push_back(0x12);
  b.push_back((uint8_t)value.size());
  AppendByteArrayString(b, value, false);
}

void AppendProtoBufferFromLengthDelimitedS2C(vector<uint8_t>& b, const string& key, const uint8_t value)
{
  size_t thisSize = key.size() + 5;
  b.reserve(b.size() + thisSize + 2);
  b.push_back(0x1A);
  b.push_back((uint8_t)thisSize);
  b.push_back(0x0A);
  b.push_back((uint8_t)key.size());
  AppendByteArrayString(b, key, false);
  b.push_back(0x12);
  b.push_back(1);
  b.push_back(value);
}

bool IsAllZeroes(const uint8_t* start, const uint8_t* end)
{
  const uint8_t* needle = start;
  while (needle < end) {
    if (*needle != 0) {
      return false;
    }
    ++needle;
  }
  return true;
}

template <OOBPolicy oobPolicy>
size_t FindNullDelimiterOrStart(const vector<uint8_t>& b, const size_t start)
{
  size_t end = b.size();
  if constexpr (oobPolicy == OOBPolicy::kCheck) {
    if (start >= end) return start;
  }
  assert((end > start) && "Out of bounds access searching for null delimiter.");
  for (size_t i = start; i < end; ++i) {
    if (b[i] == 0) {
      return i;
    }
  }
  return start;
}
template size_t FindNullDelimiterOrStart<OOBPolicy::kCheck>(const vector<uint8_t>& b, const size_t start);
template size_t FindNullDelimiterOrStart<OOBPolicy::kUnsafe>(const vector<uint8_t>& b, const size_t start);

const uint8_t* FindNullDelimiterInRangeOrStart(const uint8_t* start, const uint8_t* end)
{
  const uint8_t* needle = start;
  while (needle < end) {
    if (*needle == 0) {
      return needle;
    }
    ++needle;
  }
  return start;
}

template <OOBPolicy oobPolicy>
size_t FindNullDelimiterOrEnd(const vector<uint8_t>& b, const size_t start)
{
  // start searching the byte array at position 'start' for the first null value
  // if found, return the subarray from 'start' to the null value but not including the null value

  size_t end = b.size();
  if constexpr (oobPolicy == OOBPolicy::kCheck) {
    if (start >= end) return end;
  }
  assert((end > start) && "Out of bounds access searching for null delimiter.");
  for (size_t i = start; i < end; ++i) {
    if (b[i] == 0) {
      return i;
    }
  }
  return end;
}

template size_t FindNullDelimiterOrEnd<OOBPolicy::kCheck>(const vector<uint8_t>& b, const size_t start);
template size_t FindNullDelimiterOrEnd<OOBPolicy::kUnsafe>(const vector<uint8_t>& b, const size_t start);

template <OOBPolicy oobPolicy>
size_t FindNullDelimiterOrEnd(const string_view b, const size_t start)
{
  // start searching the byte array at position 'start' for the first null value
  // if found, return the subarray from 'start' to the null value but not including the null value

  size_t end = b.size();
  if constexpr (oobPolicy == OOBPolicy::kCheck) {
    if (start >= end) return end;
  }
  assert((end > start) && "Out of bounds access searching for null delimiter.");
  for (size_t i = start; i < end; ++i) {
    if (b[i] == '\x00') {
      return i;
    }
  }
  return end;
}

template size_t FindNullDelimiterOrEnd<OOBPolicy::kCheck>(const string_view b, const size_t start);
template size_t FindNullDelimiterOrEnd<OOBPolicy::kUnsafe>(const string_view b, const size_t start);

const uint8_t* FindNullDelimiterInRangeOrEnd(const uint8_t* start, const uint8_t* end)
{
  const uint8_t* needle = start;
  while (needle < end) {
    if (*needle == 0) {
      return needle;
    }
    ++needle;
  }
  return end;
}

string GetStringAddressRange(const uint8_t* start, const uint8_t* end)
{
  if (end == start) return string();
  return string(reinterpret_cast<const char*>(start), static_cast<size_t>(end - start));
}

string GetStringAddressRange(const vector<uint8_t>& b, const size_t start, const size_t end)
{
  if (end == start) return string();
  return string(reinterpret_cast<const char*>(b.data() + start), end - start);
}

template <OOBPolicy oobPolicy, NullTerminatorPolicy nullPolicy, StringEncoding encoding>
string_view ExtractStringView(const vector<uint8_t>& b, const size_t start, const size_t maxSize)
{
  if constexpr (oobPolicy == OOBPolicy::kCheck) {
    if (start >= b.size()) return string_view();
  }
  assert((b.size() > start) && "Out of bounds access searching for null delimiter.");

  size_t nullPos = FindNullDelimiterOrEnd<OOBPolicy::kUnsafe>(b, start);
  string_view sv;
  if (nullPos == b.size()) {
    if constexpr (nullPolicy == NullTerminatorPolicy::kRequired) {
      return string_view();
    } else {
      sv = string_view(reinterpret_cast<const char*>(b.data() + start), b.size() - start);
    }
  } else {
    sv = string_view(reinterpret_cast<const char*>(b.data() + start), nullPos - start);
  }
  if (0 < maxSize && maxSize < sv.size()) {
    return string_view();
  }
  if constexpr (encoding == StringEncoding::kUTF8) {
    if (!utf8::is_valid(sv)) {
      return string_view();
    }
  }
  return sv;
}

template <OOBPolicy oobPolicy, NullTerminatorPolicy nullPolicy, StringEncoding encoding>
string_view ExtractStringView(const string_view b, const size_t start, const size_t maxSize)
{
  if constexpr (oobPolicy == OOBPolicy::kCheck) {
    if (start >= b.size()) return string_view();
  }
  assert((b.size() > start) && "Out of bounds access searching for null delimiter.");

  size_t nullPos = FindNullDelimiterOrEnd<OOBPolicy::kUnsafe>(b, start);
  string_view sv;
  if (nullPos == b.size()) {
    if constexpr (nullPolicy == NullTerminatorPolicy::kRequired) {
      return string_view();
    } else {
      sv = b.substr(start, b.size() - start);
    }
  } else {
    sv = b.substr(start, nullPos - start);
  }
  if (0 < maxSize && maxSize < sv.size()) {
    return string_view();
  }
  if constexpr (encoding == StringEncoding::kUTF8) {
    if (!utf8::is_valid(sv)) {
      return string_view();
    }
  }
  return sv;
}

template string_view ExtractStringView<OOBPolicy::kUnsafe, NullTerminatorPolicy::kRequired, StringEncoding::kUTF8>(const vector<uint8_t>& b, const size_t start, const size_t maxSize);
template string_view ExtractStringView<OOBPolicy::kUnsafe, NullTerminatorPolicy::kRequired, StringEncoding::kNone>(const vector<uint8_t>& b, const size_t start, const size_t maxSize);
template string_view ExtractStringView<OOBPolicy::kUnsafe, NullTerminatorPolicy::kRequired, StringEncoding::kUTF8>(const string_view b, const size_t start, const size_t maxSize);
template string_view ExtractStringView<OOBPolicy::kUnsafe, NullTerminatorPolicy::kRequired, StringEncoding::kNone>(const string_view b, const size_t start, const size_t maxSize);
template string_view ExtractStringView<OOBPolicy::kCheck, NullTerminatorPolicy::kRequired, StringEncoding::kNone>(const string_view b, const size_t start, const size_t maxSize);

string_view ExtractUTF8View(const vector<uint8_t>& b, const size_t start, const size_t maxSize)
{
  return ExtractStringView<OOBPolicy::kUnsafe, NullTerminatorPolicy::kRequired, StringEncoding::kUTF8>(b, start, maxSize);
}

string_view ExtractUTF8View(const string_view b, const size_t start, const size_t maxSize)
{
  return ExtractStringView<OOBPolicy::kUnsafe, NullTerminatorPolicy::kRequired, StringEncoding::kUTF8>(b, start, maxSize);
}

vector<uint8_t> ExtractNumbers(const string& s, const size_t maxCount)
{
  // consider the string to contain a bytearray in dec-text form, e.g. "52 99 128 1"

  vector<uint8_t> result;
  uint32_t c;
  stringstream SS;
  SS << s;

  for (size_t i = 0; i < maxCount; ++i) {
    if (SS.eof())
      break;

    SS >> c;

    if (SS.fail() || c > 0xFF)
      break;

    result.push_back(static_cast<uint8_t>(c));
  }

  return result;
}

vector<uint8_t> ExtractHexNumbers(const string& s)
{
  // consider the string to contain a bytearray in hex-text form, e.g. "4e 17 b7 e6"

  vector<uint8_t> result;
  uint32_t             c;
  stringstream    SS;
  SS << s;

  while (!SS.eof())
  {
    SS >> hex >> c;
    if (c > 0xFF)
      break;

    result.push_back(static_cast<uint8_t>(c));
  }

  return result;
}

vector<uint8_t> ExtractIPv4(const string& s)
{
  vector<uint8_t> Output;
  stringstream ss(s);
  while (ss.good()) {
    string element;
    getline(ss, element, '.');
    if (element.empty())
      break;

    int32_t parsedElement = 0;
    try {
      parsedElement = stoi(element);
    } catch (...) {
      break;
    }
    if (parsedElement < 0 || parsedElement > 0xFF)
      break;

    Output.push_back(static_cast<uint8_t>(parsedElement));
  }

  if (Output.size() != 4)
    Output.clear();

  return Output;
}

string ToUpperCase(const string& input)
{
  string output = input;
  transform(begin(output), end(output), begin(output), [](char c) { return static_cast<char>(toupper(c)); });
  return output;
}

vector<uint8_t> SplitNumeral(string_view input)
{
  vector<uint8_t> result;
  result.reserve(input.size());
  for (const auto& c : input) {
    if (isdigit(c)) {
      result.push_back((uint8_t)(c - 0x30));
    } else {
      return {};
    }
  }
  return result;
}

vector<string> SplitArgs(const string& s, const uint8_t expectedCount)
{
  uint8_t parsedCount = 0;
  stringstream SS(s);
  string nextItem;
  vector<string> output;
  do {
    getline(SS, nextItem, ',');
    if (SS.fail()) {
      break;
    }
    output.push_back(TrimString(nextItem));
    ++parsedCount;
  } while (!SS.eof() && parsedCount < expectedCount);

  if (parsedCount != expectedCount)
    output.clear();

  return output;
}

vector<string> SplitArgs(const string& s, const uint8_t minCount, const uint8_t maxCount)
{
  uint8_t parsedCount = 0;
  stringstream SS(s);
  string nextItem;
  vector<string> output;
  do {
    getline(SS, nextItem, ',');
    if (SS.fail()) {
      break;
    }
    output.push_back(TrimString(nextItem));
    ++parsedCount;
  } while (!SS.eof() && parsedCount < maxCount);

  if (!(minCount <= parsedCount && parsedCount <= maxCount))
    output.clear();

  return output;
}

vector<uint32_t> SplitNumericArgs(const string& s, const uint8_t expectedCount)
{
  uint8_t parsedCount = 0;
  stringstream SS(s);
  string nextString;
  vector<uint32_t> output;
  output.reserve(expectedCount);
  do {
    getline(SS, nextString, ',');
    if (SS.fail()) {
      break;
    }
    optional<uint32_t> nextItem = ToUint32(TrimString(nextString));
    if (!nextItem.has_value()) {
      output.clear();
      break;
    }
    output.push_back(*nextItem);
    ++parsedCount;
  } while (!SS.eof() && parsedCount < expectedCount);

  if (parsedCount != expectedCount)
    output.clear();

  return output;
}

vector<uint32_t> SplitNumericArgs(const string& s, const uint8_t minCount, const uint8_t maxCount)
{
  uint8_t parsedCount = 0;
  stringstream SS(s);
  string nextString;
  vector<uint32_t> output;
  output.reserve(minCount);
  do {
    getline(SS, nextString, ',');
    if (SS.fail()) {
      break;
    }
    optional<uint32_t> nextItem = ToUint32(TrimString(nextString));
    if (!nextItem.has_value()) {
      output.clear();
      break;
    }
    output.push_back(*nextItem);
    ++parsedCount;
  } while (!SS.eof() && parsedCount < maxCount);

  if (!(minCount <= parsedCount && parsedCount <= maxCount))
    output.clear();

  return output;
}

void AssignLength(vector<uint8_t>& content)
{
  // insert the actual length of the content array into bytes 3 and 4 (indices 2 and 3)

  const uint16_t size = static_cast<uint16_t>(content.size());
  assert((size >= 4) && "Protocol requires packets at least 4 bytes long.");
  content[2] = static_cast<uint8_t>(size);
  content[3] = static_cast<uint8_t>(size >> 8);
}

bool ValidateLength(const vector<uint8_t>& content)
{
  // verify that bytes 3 and 4 (indices 2 and 3) of the content array describe the length

  size_t size = content.size();
  if (size >= 4 && size <= 0xFFFF) {
    return ByteArrayToUInt16<Endianness::kLittle>(content, 2) == static_cast<uint16_t>(size);
  }
  return false;
}

bool ValidateLength(const string_view content)
{
  // verify that bytes 3 and 4 (indices 2 and 3) of the content array describe the length

  size_t size = content.size();
  if (size >= 4 && size <= 0xFFFF) {
    return ByteArrayToUInt16<Endianness::kLittle>(content, 2) == static_cast<uint16_t>(size);
  }
  return false;
}

string AddPathSeparator(const string& path)
{
  if (path.empty())
    return string();

#ifdef _WIN32
  const char Separator = '\\';
#else
  const char Separator = '/';
#endif

  if (*(end(path) - 1) == Separator)
    return path;
  else
    return path + string(1, Separator);
}

vector<uint8_t> EncodeStatString(const vector<uint8_t>& data)
{
  uint8_t b = 0, mask = 1;
  size_t i = 0, j = 0, w = 1;
  size_t len = data.size();
  size_t fullBlockCount = len / 7u;
  size_t fullBlockLen = fullBlockCount * 7u;
  size_t outSize = fullBlockCount * 8u;
  if (fullBlockLen < len) {
    outSize += (len % 7u) + 1u;
  }
  vector<uint8_t> result;
  result.resize(outSize);

  for (; i < fullBlockLen; i += 7, w++) {
    mask = 1;
    for (j = 0; j < 7; j++, w++) {
      b = data[i + j];
      if (b % 2 == 0) {
        result[w] = (uint8_t)(b + TINY_ONE);
      } else {
        result[w] = b;
        mask |= (1u << (j + 1u));
      }
    }
    result[w - 8] = mask;
  }

  if (fullBlockLen < len) {
    mask = 1;
    for (i = fullBlockLen; i < len; i++, w++) {
      b = data[i];
      if (b % 2 == 0) {
        result[w] = (uint8_t)(b + TINY_ONE);
      } else {
        result[w] = b;
        mask |= (1u << ((i % 7u) + 1u));
      }
    }
    result[fullBlockCount * 8] = mask;
  }


  return result;
}

vector<uint8_t> DecodeStatString(string_view data)
{
  uint8_t mask = 1u;
  uint8_t bitOrder = 0u;
  size_t i = 0, j = 0;
  size_t len = data.size();
  size_t fullBlockCount = len / 8u;
  size_t fullBlockLen = fullBlockCount * 8u;
  size_t outSize = fullBlockCount * 7u;
  if (fullBlockLen < len) {
    outSize += (len % 8u) - 1u;
  }
  vector<uint8_t> result;
  result.reserve(outSize);

  for (i = 0; i < fullBlockLen; i += 8) {
    mask = GetByteAt(data, i);
    for (j = 1; j < 8; j++) {
      bitOrder = (uint8_t)j;
      if (((mask >> bitOrder) & 0x1) == 0) {
        result.push_back(ToBaseZero(GetByteAt(data, i + j)));
      } else {
        result.push_back(GetByteAt(data, i + j));
      }
    }
  }

  if (fullBlockLen < len) {
    mask = GetByteAt(data, fullBlockLen);
    for (i = fullBlockLen + 1; i < len; i++) {
      bitOrder = (uint8_t)i % 8;
      if (((mask >> bitOrder) & 0x1) == 0) {
        result.push_back(ToBaseZero(GetByteAt(data, i)));
      } else {
        result.push_back(GetByteAt(data, i));
      }
    }
  }

  return result;
}

template <typename T>
vector<uint8_t> DecodeStatString(const T& data)
{
  string_view sv = GetContainerView(data);
  return DecodeStatString(sv);
}

template vector<uint8_t> DecodeStatString(const vector<uint8_t>& data);
template vector<uint8_t> DecodeStatString(const string& data);

vector<string_view> SplitTokens(string_view s, const char delim)
{
  vector<string_view> tokens;
  size_t start = 0;

  while (start < s.size()) {
    size_t end = s.find(delim, start);

    if (end == string_view::npos) {
      end = s.size();
    }

    if (end > start) {
      tokens.push_back(s.substr(start, end - start));
    }

    start = end + 1;
  }

  return tokens;
}

string::size_type GetLevenshteinDistance(const string& s1, const string& s2)
{
  string::size_type m = s1.length();
  string::size_type n = s2.length();

  // Create a 2D vector to store the distances
  vector<vector<string::size_type>> dp(m + 1, vector<string::size_type>(n + 1, 0));

  for (string::size_type i = 0; i <= m; ++i) {
    for (string::size_type j = 0; j <= n; ++j) {
      if (i == 0) {
          dp[i][j] = j;
      } else if (j == 0) {
          dp[i][j] = i;
      } else if (s1[i - 1] == s2[j - 1]) {
          dp[i][j] = dp[i - 1][j - 1];
      } else {
        string::size_type cost = (s1[i - 1] == s2[j - 1]) ? 0 : (isdigit(s1[i - 1]) || isdigit(s2[j - 1])) ? 3 : 1;
        dp[i][j] = min({ dp[i - 1][j] + 1, dp[i][j - 1] + 1, dp[i - 1][j - 1] + cost });
      }
    }
  }

  return dp[m][n];
}

string::size_type GetLevenshteinDistanceForSearch(const string& s1, const string& s2, const string::size_type bestDistance)
{
  string::size_type m = s1.length();
  string::size_type n = s2.length();

  if (m > n + bestDistance) {
    return m - n;
  }

  if (n > m + bestDistance) {
    return n - m;
  }

  // Create a 2D vector to store the distances
  vector<vector<string::size_type>> dp(m + 1, vector<string::size_type>(n + 1, 0));

  for (string::size_type i = 0; i <= m; ++i) {
    for (string::size_type j = 0; j <= n; ++j) {
      if (i == 0) {
          dp[i][j] = j;
      } else if (j == 0) {
          dp[i][j] = i;
      } else if (s1[i - 1] == s2[j - 1]) {
          dp[i][j] = dp[i - 1][j - 1];
      } else {
        string::size_type cost = (s1[i - 1] == s2[j - 1]) ? 0 : (isdigit(s1[i - 1]) || isdigit(s2[j - 1])) ? 3 : 1;
        dp[i][j] = min({ dp[i - 1][j] + 1, dp[i][j - 1] + 1, dp[i - 1][j - 1] + cost });
      }
    }
  }

  return dp[m][n];
}

string CheckIsValidHCLStandard(const string& s)
{
  string HCLChars = HCL_CHARSET_STANDARD;
  if (s.find_first_not_of(HCLChars) != string::npos) {
    return "[" + s + "] is not a standard HCL string.";
  }
  return string();
}

string CheckIsValidHCLSmall(const string& s)
{
  string HCLChars = HCL_CHARSET_SMALL;
  if (s.find_first_not_of(HCLChars) != string::npos) {
    return "[" + s + "] is not a short HCL string.";
  }
  return string();
}

string DurationLeftToString(int64_t remainingSeconds)
{
  if (remainingSeconds < 0)
    remainingSeconds = 0;
  int64_t remainingMinutes = remainingSeconds / 60;
  remainingSeconds = remainingSeconds % 60;
  if (remainingMinutes == 0) {
    return to_string(remainingSeconds) + " seconds";
  } else if (remainingSeconds == 0) {
    return to_string(remainingMinutes) + " minutes";
  } else {
    return to_string(remainingMinutes) + " min " + to_string(remainingSeconds) + "s";
  }
}

string FourCCToString(uint32_t fourCC)
{
  string result;
  result.reserve(4);
  result += static_cast<char>(static_cast<uint8_t>(fourCC >> 24));
  result += static_cast<char>(static_cast<uint8_t>(fourCC >> 16));
  result += static_cast<char>(static_cast<uint8_t>(fourCC >> 8));
  result += static_cast<char>(static_cast<uint8_t>(fourCC));
  return result;
}

string RemoveNonAlphanumeric(const string& s)
{
  regex nonAlphanumeric("[^a-zA-Z0-9]");
  return regex_replace(s, nonAlphanumeric, "");
}

string RemoveNonAlphanumericNorHyphen(const string& s)
{
  regex nonAlphanumericNorHyphen("[^a-zA-Z0-9-]");
  return regex_replace(s, nonAlphanumericNorHyphen, "");
}

[[nodiscard]] size_t GetNearestMultiple(size_t around, size_t divisor)
{
  double quotientRound = round((double)around / (double)divisor);
  size_t nearest = (size_t)quotientRound * divisor;
  if (quotientRound > (double)around && nearest < around) { // Overflow
    nearest = (around / divisor) * divisor;
  }
  return nearest;
}

RangeSizeType ResolveSyncLimits(size_t latency, double deltaTicksMs)
{
  size_t baseSync = DoubleToSize(deltaTicksMs);
  size_t minSync = baseSync < latency ? latency : GetNearestMultiple(baseSync, latency);
  size_t maxSync = minSync + latency;
  CheckOverflowMultiples(latency, &minSync, &maxSync);
  return make_pair<size_t, size_t>(move(minSync), move(maxSync));
}

RangeSizeType ResolveSyncLimits(size_t latency, const pair<double, double>& deltaTicksMs)
{
  size_t minSync = GetNearestMultiple(DoubleToSize(deltaTicksMs.first), latency);
  size_t maxSync = GetNearestMultiple(DoubleToSize(deltaTicksMs.second), latency);
  if (minSync == maxSync) {
    return ResolveSyncLimits(latency, (double)minSync);
  }
  RangeEnsureMinMaxSorted(&minSync, &maxSync);

  if (minSync < latency) {
    minSync = latency;
    if (maxSync <= minSync) {
      maxSync = minSync + latency;
    }
  }

  return make_pair<size_t, size_t>(move(minSync), move(maxSync));
}

void RangeEnsureMinMaxSorted(size_t* minValue, size_t* maxValue)
{
  if (*maxValue < *minValue) {
    size_t temp = *minValue;
    *minValue = *maxValue;
    *maxValue = temp;
  }
}

void CheckOverflowMultiples(size_t divisor, size_t* minMultiple, size_t* maxMultiple)
{
  // After adding to *maxMultiple
  if (*maxMultiple < *minMultiple) {
    *maxMultiple = *minMultiple;
    *minMultiple = *maxMultiple - divisor;
  }
}

bool IsValidMapName(const string& s)
{
  if (!s.length()) return false;
  if (s[0] == '.') return false;
  regex invalidChars("[^a-zA-Z0-9_ ().~-]");
  regex validExtensions("\\.w3(m|x)$");
  return !regex_search(s, invalidChars) && regex_search(s, validExtensions);
}

bool IsValidCFGName(const string& s)
{
  if (!s.length()) return false;
  if (s[0] == '.') return false;
  regex invalidChars("[^a-zA-Z0-9_ ().~-]");
  regex validExtensions("\\.ini$");
  return !regex_search(s, invalidChars) && regex_search(s, validExtensions);
}

string TrimTrailingSlash(const string s)
{
  if (s.empty()) return s;
  if (s[s.length() - 1] == '/') return s.substr(0, s.length() - 1);
  return s;
}

string MaybeBase10(const string s)
{
  return IsBase10NaturalOrZero(s) ? s : string();
}

template<typename Container>
string JoinStrings(const Container& list, const string connector)
{
  using T = typename Container::value_type;
  static_assert(is_same_v<T, string> || is_same_v<T, string_view> || is_same_v<T, uint16_t> || is_same_v<T, uint32_t>, "Container must contain string or uint16_t or uint32_t");

  size_t size = 0;
  string results;
  for (const auto& element : list) {
    if constexpr (is_same_v<T, string> || is_same_v<T, string_view>) {
      size += element.size();
    } else {
      size += 4;
    }
  }

  if (size == 0) {
    return results;
  }

  size += connector.size() * (list.size() - 1);
  results.reserve(size);

  auto it = list.cbegin();
  auto itEnd = list.cend();
  if constexpr (is_same_v<T, string> || is_same_v<T, string_view>) {
    results.append(*it);
  } else {
    results.append(to_string(*it));
  }  
  ++it;

  while (it != itEnd) {
    results.append(connector);
    if constexpr (is_same_v<T, string> || is_same_v<T, string_view>) {
      results.append(*it);
    } else {
      results.append(to_string(*it));
    }
    ++it;
  }

  return results;
}

template<typename Container>
string JoinStrings(const Container& list)
{
  return JoinStrings(list, ", ");
}

#define INSTANTIATE_JOIN_STRINGLIKE(T)\
template string JoinStrings(const vector<T>& list, const string connector);\
template string JoinStrings(const vector<T>& list);\
template string JoinStrings(const set<T>& list, const string connector);\
template string JoinStrings(const set<T>& list);

#define INSTANTIATE_JOIN_STRINGS_ARRAY(SIZE)\
template string JoinStrings(const array<string, SIZE>& list, const string connector);\
template string JoinStrings(const array<string, SIZE>& list);

INSTANTIATE_JOIN_STRINGLIKE(string)
INSTANTIATE_JOIN_STRINGLIKE(string_view)
INSTANTIATE_JOIN_STRINGLIKE(uint16_t)
INSTANTIATE_JOIN_STRINGLIKE(uint32_t)
INSTANTIATE_JOIN_STRINGS_ARRAY(2)
INSTANTIATE_JOIN_STRINGS_ARRAY(3)
INSTANTIATE_JOIN_STRINGS_ARRAY(4)
INSTANTIATE_JOIN_STRINGS_ARRAY(5)
INSTANTIATE_JOIN_STRINGS_ARRAY(6)
INSTANTIATE_JOIN_STRINGS_ARRAY(7)
INSTANTIATE_JOIN_STRINGS_ARRAY(8)
INSTANTIATE_JOIN_STRINGS_ARRAY(9)
INSTANTIATE_JOIN_STRINGS_ARRAY(10)
INSTANTIATE_JOIN_STRINGS_ARRAY(11)
INSTANTIATE_JOIN_STRINGS_ARRAY(12)

#undef INSTANTIATE_JOIN_STRINGLIKE
#undef INSTANTIATE_JOIN_STRINGS_ARRAY

string IPv4ToString(const array<uint8_t, 4> ip)
{
  return ToDecString(ip[0]) + "." + ToDecString(ip[1]) + "." + ToDecString(ip[2]) + "." + ToDecString(ip[3]);
}

bool SplitIPAddressAndPortOrDefault(const string& input, const uint16_t defaultPort, string& ip, uint16_t& port)
{
  size_t colonPos = input.rfind(':');
  
  if (colonPos == string::npos) {
    ip = input;
    port = defaultPort;
    return true;
  }

  if (input.find(']') == string::npos) {
    ip = input.substr(0, colonPos);
    int32_t parsedPort = 0;
    try {
      parsedPort = stoi(input.substr(colonPos + 1));
    } catch (...) {
      return false;
    }
    if (parsedPort < 0 || 0xFFFF < parsedPort) {
      return false;
    }
    port = static_cast<uint16_t>(parsedPort);
    return true;
  }

  // This is an IPv6 literal, expect format: [IPv6]:PORT
  size_t startBracket = input.find('[');
  size_t endBracket = input.find(']');
  if (startBracket == string::npos || endBracket == string::npos || endBracket < startBracket) {
    return false;
  }

  ip = input.substr(startBracket + 1, endBracket - startBracket - 1);
  if (colonPos > endBracket) {
    int32_t parsedPort = 0;
    try {
      parsedPort = stoi(input.substr(colonPos + 1));
    } catch (...) {
      return false;
    }
    if (parsedPort < 0 || 0xFFFF < parsedPort) {
      return false;
    }
    port = static_cast<uint16_t>(parsedPort);
  } else {
    port = defaultPort;
  }

  return true;
}

string EncodeURIComponent(const string& s) {
  ostringstream escaped;
  escaped.fill('0');
  escaped << hex;

  for (char c : s) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
        escaped << c;
    } else if (c == ' ') {
        escaped << '+';
    } else {
        escaped << '%' << setw(2) << int(static_cast<unsigned char>(c));
    }
  }

  return escaped.str();
}

string DecodeURIComponent(const string& encoded)
{
  ostringstream decoded;

  for (size_t i = 0; i < encoded.size(); ++i) {
    if (encoded[i] == '%' && i + 2 < encoded.size() &&
      isxdigit(encoded[i + 1]) && isxdigit(encoded[i + 2])) {
      int hexValue;
      istringstream hexStream(encoded.substr(i + 1, 2));
      hexStream >> hex >> hexValue;
      decoded << static_cast<char>(hexValue);
      i += 2;
    } else if (encoded[i] == '+') {
      decoded << ' ';
    } else {
      decoded << encoded[i];
    }
  }

  return decoded.str();
}

bool CaseInsensitiveEquals(const string& nameOne, const string& nameTwo)
{
  return ToLowerCase(nameOne) == ToLowerCase(nameTwo);
}

bool FileNameEquals(const string& nameOne, const string& nameTwo)
{
#ifndef _WIN32
  return nameOne == nameTwo;
#else
  return CaseInsensitiveEquals(nameOne, nameTwo);
#endif
}

bool HasNullOrBreak(const string& unsafeInput)
{
  for (const auto& c : unsafeInput) {
    if (c == '\0' || c == '\n' || c == '\r' || c == '\f') {
      return true;
    }
  }
  return false;
}

bool PathHasNullBytes(const filesystem::path& filePath)
{
  for (const auto& c : filePath.native()) {
    if (c == '\0') {
      return true;
    }
  }
  return false;
}

bool IsASCII(string_view unsafeInput)
{
  for (const auto& c : unsafeInput) {
    if ((c & 0x80) != 0) {
      return false;
    }
  }
  return true;
}

bool IsUnsafeCodePoint(char32_t cp)
{
  // C0, DEL, C1
  if (cp < 0x20 || (0x7F <= cp && cp <= 0x9F)) {
    return true;
  }
  switch (cp) {
    // bidi control characters
    case 0x200E: // LRM
    case 0x200F: // RLM
    case 0x202A: // LRE
    case 0x202B: // RLE
    case 0x202C: // PDF
    case 0x202D: // LRO
    case 0x202E: // RLO
    case 0x2066: // LRI
    case 0x2067: // RLI
    case 0x2068: // FSI
    case 0x2069: // PDI

    // zero-width and format
    case 0x200B:
    case 0x200D:
    case 0x2060:
    case 0xFEFF:
    case 0x00AD:

    // line separators
    case 0x2028: // line separator
    case 0x2029: // paragraph separator
      return true;
  }
  return false;
}

bool HasUnsafeUTF8CodePoints(string_view unsafeUTF8Input)
{
  auto it = unsafeUTF8Input.begin();
  auto end = unsafeUTF8Input.end();

  while (it != end) {
    char32_t cp = utf8::unchecked::next(it);
    if (IsUnsafeCodePoint(cp)) {
      return true;
    }
  }
  return false;
}

string ToLowerCasePreserveUTF8(string_view input)
{
  string output;
  output.reserve(input.size());
  auto it = input.begin();
  auto end = input.end();

  while (it != end) {
    char32_t cp = utf8::unchecked::next(it);
    if (cp >= 0x80) {
      u32string_view sv(&cp, 1);
      output += utf8::utf32to8(sv);
    } else {
      output += static_cast<char>(tolower(static_cast<unsigned char>(cp)));
    }
  }
  return output;
}

bool IsArbitraryStringUTF8Safe(string_view unsafeInput)
{
  if (!utf8::is_valid(unsafeInput)) {
    return false;
  }
  return !HasUnsafeUTF8CodePoints(unsafeInput);
}

string_view SanitizeUTF8(string_view unsafeInput, string_view fallback)
{
  if (IsArbitraryStringUTF8Safe(unsafeInput)) {
    return unsafeInput;
  }
  return fallback;
}

string_view SanitizeASCII(string_view unsafeInput, string_view fallback)
{
  if (IsASCII(unsafeInput)) {
    return unsafeInput;
  }
  return fallback;
}

string SanitizeWrapUTF8(string_view unsafeInput, string_view fallback)
{
  if (IsArbitraryStringUTF8Safe(unsafeInput)) {
    return "[" + string(unsafeInput) + "]";
  }
  return string(fallback);
}

string SanitizeWrapASCII(string_view unsafeInput, string_view fallback)
{
  if (IsASCII(unsafeInput)) {
    return "[" + string(unsafeInput) + "]";
  }
  return string(fallback);
}

uint32_t ASCIIHexToNum(const array<uint8_t, 8>& data, bool reverse)
{
  string ascii;
  if (reverse) {
    ascii = string(data.rbegin(), data.rend());
  } else {
    ascii = string(data.begin(), data.end());
  }
  uint32_t result = 0;
  for (size_t j = 0; j < 4; j++) {
    unsigned int c;
    stringstream SS;
    SS << ascii.substr(j * 2, 2);
    SS >> hex >> c;
    result |= c << (24 - j * 8);
  }
  return result;
}

array<uint8_t, 8> NumToASCIIHex(uint32_t num, bool reverse)
{
  array<uint8_t, 8> result;
  result.fill(0x30);

  for (size_t j = 0; j < 4; j++) {
    uint8_t c;
    if (reverse) {
      c = static_cast<uint8_t>(num >> (24 - j * 8));
    } else {
      c = static_cast<uint8_t>(num >> (j * 8));
    }
    string fragment = ToHexString(c);
    if (c > 0xF) {
      result[7 - (j * 2)] = static_cast<uint8_t>(fragment[0]);
      result[6 - (j * 2)] = static_cast<uint8_t>(fragment[1]);
    } else {
      result[6 - (j * 2)] = static_cast<uint8_t>(fragment[0]);
    }
  }
  return result;
}

string PreparePatternForFuzzySearch(const string& rawPattern)
{
  string pattern = rawPattern;
  transform(begin(pattern), end(pattern), begin(pattern), [](unsigned char c) {
    if (c == static_cast<char>(0x20)) return static_cast<char>(0x2d);
    return static_cast<char>(tolower(c));
  });
  return RemoveNonAlphanumericNorHyphen(pattern);
}

string PrepareMapPatternForFuzzySearch(const string& rawPattern)
{
  string pattern = ToLowerCase(rawPattern);
  string extension = ParseFileExtension(pattern);
  if (extension == ".w3x" || extension == ".w3m" || extension == ".ini") {
    pattern = pattern.substr(0, pattern.length() - extension.length());
  }
  return RemoveNonAlphanumeric(pattern);
}

vector<string> ReadChatTemplate(const filesystem::path& filePath) {
  ifstream in;
  in.open(filePath.native().c_str(), ios::in);
  vector<string> fileContents;
  if (!in.fail()) {
    while (!in.eof()) {
      string line;
      getline(in, line);
      if (line.empty()) {
        if (!in.eof())
          fileContents.push_back(" ");
      } else {
        fileContents.push_back(line);
      }
    }
    in.close();
  }
  return fileContents;
}

string GetNormalizedAlias(string_view alias)
{
  if (alias.empty()) return string();

  string result;
  if (!IsArbitraryStringUTF8Safe(alias)) {
    return result;
  }

  vector<unsigned short> utf16line;
  utf8::utf8to16(alias.begin(), alias.end(), back_inserter(utf16line));
  // Note that MSVC 2019 doesn't fully support unicode string literals.
  for (const auto& c : utf16line) {
    switch (c) {
      case 32: case 39: case 45: case 95: // whitespace ( ), single quote ('), hyphen (-), underscore (_)
        break;
      case 224: case 225: case 226: case 227: case 228: case 229: // à á â ã ä å
        result += 'a'; break;
      case 231: // ç
        result += 'c'; break;
      case 232: case 233: case 234: case 235: // è é ê ë
        result += 'e'; break;
      case 236: case 237: case 238: case 239: // ì í î ï
        result += 'i'; break;
      case 241: // ñ
        result += 'n'; break;
      case 242: case 243: case 244: case 245: case 246: case 248: // ò ó ô õ ö ø
        result += 'o'; break;
      case 249: case 250: case 251: case 252: // ù ú û ü
        result += 'u'; break;
      case 253: case 255: // ý ÿ
        result += 'y'; break;
      default:
        if (c <= 0x7f) {
          // Single-byte UTF-8 encoding for ASCII characters
          result += static_cast<char>(c & 0x7F);
          continue;
        } else if (c <= 0x7FF) {
          // Two-byte UTF-8 encoding
          result += static_cast<char>(0xC0 | ((c >> 6) & 0x1F));
          result += static_cast<char>(0x80 | (c & 0x3F));
        } else {
          // Three-byte UTF-8 encoding
          result += static_cast<char>(0xE0 | ((c >> 12) & 0x0F));
          result += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
          result += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
  }
  return result;
}

void NormalizeDirectory(filesystem::path& filePath)
{
  if (filePath.empty()) return;
  filePath += filePath.preferred_separator;
  filePath = filePath.lexically_normal();
}

bool FindNextMissingElementBack(uint8_t& element, vector<uint8_t> counters)
{
  if (element == 0) return false;
  do {
    --element;
  } while (counters[element] != 0 && element > 0);
  return counters[element] == 0;
}

optional<uint32_t> ToUint32(string_view input) {
  return ToUint32(string(input));
}

optional<uint32_t> ToUint32(const string& input)
{
  return ParseUInt32(input, true);
}

optional<int32_t> ToInt32(const string& input)
{
  return ParseInt32(input, true);
}

optional<double> ToDouble(const string& input)
{
  return ParseDouble(input);
}

pair<string, string> SplitAddress(const string& fqName)
{
  string::size_type atSignPos = fqName.find('@');
  if (atSignPos == string::npos) {
    return make_pair(fqName, string());
  }
  return make_pair(
    TrimString(fqName.substr(0, atSignPos)),
    TrimString(fqName.substr(atSignPos + 1))
  );
}

bool CheckTargetGameSyntax(const string& rawInput)
{
  if (rawInput.empty()) {
    return false;
  }
  string inputGame = ToLowerCase(rawInput);
  if (inputGame == "lobby" || inputGame == "game#lobby") {
    return true;
  }
  if (inputGame == "oldest" || inputGame == "game#oldest") {
    return true;
  }
  if (inputGame == "newest" || inputGame == "latest" || inputGame == "game#newest" || inputGame == "game#latest") {
    return true;
  }
  if (inputGame == "lobby#oldest") {
    return true;
  }
  if (inputGame == "lobby#newest") {
    return true;
  }
  if (inputGame.substr(0, 5) == "game#") {
    inputGame = inputGame.substr(5);
  } else if (inputGame.substr(0, 6) == "lobby#") {
    inputGame = inputGame.substr(6);
  }

  try {
    long long value = stoll(inputGame);
    return value >= 0;
  } catch (...) {
    return false;
  }
}

bool ReplaceText(string& input, const string& fragment, const string& replacement)
{
  string::size_type matchIndex = input.find(fragment);
  if (matchIndex == string::npos) return false;
  input.replace(matchIndex, fragment.size(), replacement);
  return true;
}

optional<size_t> CountTemplateFixedChars(const string& input)
{
  size_t size = 0;
  size_t pos = 0;
  size_t start = 0;

  while ((start = input.find('{', pos)) != string::npos) {
    size += start - pos;

    size_t end = input.find('}', start);
    if (end == string::npos || end == start + 1) {
      return nullopt;
    }
    pos = end + 1;
  }

  size += input.size() - pos;
  return size;
}

multiset<string> GetTemplateTokens(const string& input)
{
  multiset<string> tokens;
  size_t pos = 0;
  size_t start = 0;

  while ((start = input.find('{', pos)) != string::npos) {
    size_t end = input.find('}', start);
    if (end == string::npos || end == start + 1) {
      return multiset<string>();
    }
    tokens.insert(input.substr(start + 1, end - start - 1));
    pos = end + 1;
  }

  return tokens;
}

string ReplaceTemplate(const string& input, const FlatMap<int64_t, bool>* boolCache, const FlatMap<int64_t, string>* textCache, const FlatMap<int64_t, function<bool()>>* boolFuncsMap, const FlatMap<int64_t, function<string()>>* textFuncsMap, bool tolerant)
{
  string result;
  string::size_type pos = 0;
  string::size_type start = 0;
  const bool* boolCacheMatch = nullptr;
  const string* textCacheMatch = nullptr;
  const function<bool()>* boolFuncMatch = nullptr;
  const function<string()>* textFuncMatch = nullptr;

  while ((start = input.find('{', pos)) != string::npos) {
    result.append(input, pos, start - pos);

    size_t end = input.find('}', start);
    if (end == string::npos || end == start + 1) {
      if (!tolerant) return string();
      result.append(input, start, input.size() - start);
      return result;
    }

    string token = input.substr(start + 1, end - start - 1);
    bool isCondition = token[0] == '?' || token[0] == '!';
    if (isCondition) {
      token = token.substr(1);
    }
    int64_t cacheKey = HashCode(token);

    if (isCondition) {
      bool checkResult = false;
      if (boolCache != nullptr && ((boolCacheMatch = boolCache->find(cacheKey)) != nullptr)) {
        checkResult = *boolCacheMatch;
      } else if (boolFuncsMap != nullptr && ((boolFuncMatch = boolFuncsMap->find(cacheKey)) != nullptr)) {
        bool value = (*boolFuncMatch)();
        checkResult = value;
      } else {
        if (!tolerant) return string();
        result.append("{").append(token).append("}");
      }
      if (checkResult == (token[0] == '?')) {
        pos = end + 1;
      } else {
        pos = input.find('\n', pos);
      }
    } else {
      if (textCache != nullptr && ((textCacheMatch = textCache->find(cacheKey)) != nullptr)) {
        result.append(*textCacheMatch);
      } else if (textFuncsMap != nullptr && ((textFuncMatch = textFuncsMap->find(cacheKey)) != nullptr)) {
        string value = (*textFuncMatch)();
        result.append(value);
      } else {
        if (!tolerant) return string();
        result.append("{").append(token).append("}");
      }
      pos = end + 1;
    }
  }

  result.append(input, pos, string::npos);
  return result;
}

string ReplaceTemplate(const string& input, unordered_map<int64_t, bool>* boolCache, unordered_map<int64_t, string>* textCache, const FlatMap<int64_t, function<bool()>>* boolFuncsMap, const FlatMap<int64_t, function<string()>>* textFuncsMap, bool tolerant)
{
  string result;
  string::size_type pos = 0;
  string::size_type start = 0;
  unordered_map<int64_t, bool>::iterator boolCacheMatch;
  unordered_map<int64_t, string>::iterator textCacheMatch;
  const function<bool()>* boolFuncMatch = nullptr;
  const function<string()>* textFuncMatch = nullptr;

  while ((start = input.find('{', pos)) != string::npos) {
    result.append(input, pos, start - pos);

    size_t end = input.find('}', start);
    if (end == string::npos || end == start + 1) {
      if (!tolerant) return string();
      result.append(input, start, input.size() - start);
      return result;
    }

    string token = input.substr(start + 1, end - start - 1);
    bool isCondition = token[0] == '?' || token[0] == '!';
    if (isCondition) {
      token = token.substr(1);
    }
    int64_t cacheKey = HashCode(token);

    if (isCondition) {
      bool checkResult = false;
      if (boolCache != nullptr && ((boolCacheMatch = boolCache->find(cacheKey)) != boolCache->end())) {
        checkResult = boolCacheMatch->second;
      } else if (boolFuncsMap != nullptr && ((boolFuncMatch = boolFuncsMap->find(cacheKey)) != nullptr)) {
        bool value = (*boolFuncMatch)();
        if (boolCache != nullptr) {
          (*boolCache)[cacheKey] = value;
        }
        checkResult = value;
      } else {
        if (!tolerant) return string();
        result.append("{").append(token).append("}");
      }
      if (checkResult == (token[0] == '?')) {
        pos = end + 1;
      } else {
        pos = input.find('\n', pos);
      }
    } else {
      if (textCache != nullptr && ((textCacheMatch = textCache->find(cacheKey)) != textCache->end())) {
        result.append(textCacheMatch->second);
      } else if (textFuncsMap != nullptr && ((textFuncMatch = textFuncsMap->find(cacheKey)) != nullptr)) {
        string value = (*textFuncMatch)();
        if (textCache != nullptr) {
          (*textCache)[cacheKey] = value;
        }
        result.append(value);
      } else {
        if (!tolerant) return string();
        result.append("{").append(token).append("}");
      }
      pos = end + 1;
    }
  }

  result.append(input, pos, string::npos);
  return result;
}

float LinearInterpolation(const float x, const float x1, const float x2, const float y1, const float y2)
{
  float y = y1 + (x - x1) * (y2 - y1) / (x2 - x1);
  return y;
}

/*
float HyperbolicInterpolation(const float x, const float x1, const float x2, const float y1, const float y2)
{
  float y = y1 + (x - x1) * (y2 - y1) / (x2 - x1);
  return y;
}

float ExponentialInterpolation(const float x, const float x1, const float x2, const float y1, const float y2)
{
  float y = y1 + (x - x1) * (y2 - y1) / (x2 - x1);
  return y;
}
*/

size_t DoubleToSize(double x)
{
  constexpr double maxValue = static_cast<double>(numeric_limits<size_t>::max());
  if (x < 0) x *= -1;
  if (x > maxValue) return numeric_limits<size_t>::max();
  return static_cast<size_t>(x);
}

uint64_t DoubleToUnsigned(double x)
{
  constexpr double maxValue = static_cast<double>(numeric_limits<uint64_t>::max());
  if (x < 0) x *= -1;
  if (x > maxValue) return numeric_limits<uint64_t>::max();
  return static_cast<uint64_t>(x);
}

uint32_t GetRandomUInt32()
{
  random_device rd;
  mt19937 gen(rd());
  uniform_int_distribution<uint32_t> dis;
  return dis(gen);
}
