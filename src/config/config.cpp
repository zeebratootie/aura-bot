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

#include "config.h"
#include "../includes.h"
#include "../parser.h"
#include "../util.h"
#include "../net.h"
#include "../json.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>

#include <utf8/utf8.h>

using namespace std;

#define SUCCESS(T) \
    do { \
        m_ErrorLast = false; \
        return T; \
    } while(0);


#define CONFIG_ERROR(K, T) \
    do { \
        m_ErrorLast = true; \
        if (m_StrictMode) m_CriticalError = true; \
        Print(string("[CONFIG] Error - Invalid value provided for <") + K + string(">.")); \
        return T; \
    } while(0);


#define CONFIG_ERROR_ALLOWED_VALUES(K, T, U) \
    do { \
        m_ErrorLast = true; \
        if (m_StrictMode) m_CriticalError = true; \
        Print(string("[CONFIG] Error - Invalid value provided for <") + K + string(">. Allowed values: ") + JoinStrings(U, false) + "."); \
        return T; \
    } while(0);


#define END(K, T) \
    do { \
        if (errored) Print(string("[CONFIG] Error - Invalid value provided for <") + K + string(">.")); \
        m_ErrorLast = errored; \
        if (errored && m_StrictMode) m_CriticalError = true; \
        return T; \
    } while(0);


#define GET_KEY(K, T, U) \
    m_ValidKeys.insert(K);\
    auto _it = m_CFG.find(K);\
    if (_it == end(m_CFG)) {\
      SUCCESS(U)\
    }\
    string T = _it->second;\


#define TRY_JSON_STRING(K, T, U, V) \
    do {\
      if (CConfig::GetIsJSONValue(T)) {\
        optional<string> maybeResult = JSONAPI::ParseString(T.substr(5));\
        if (!maybeResult.has_value()) {\
          CONFIG_ERROR(K, U);\
        }\
        V.swap(*maybeResult);\
      }\
    } while (0);

//
// CConfig
//

CConfig::CConfig()
 : m_ErrorLast(false),
   m_CriticalError(false),
   m_StrictMode(false),
   m_IsModified(false)
{
}

CConfig::~CConfig() = default;

bool CConfig::Read(const filesystem::path& file, CConfig* adapterConfig)
{
  m_File = file;

  ifstream in;
  in.open(file.native().c_str(), ios::in);

  if (in.fail()) {
#ifdef _WIN32
    uint32_t errorCode = (uint32_t)GetLastOSError();
    string errorCodeString = (
      errorCode == 2 ? "file not found" : (
      (errorCode == 32 || errorCode == 33) ? "file is currently opened by another process." : (
      "error code " + to_string(errorCode)
      ))
    );
    Print("[CONFIG] warning - unable to read file [" + PathToString(file) + "] - " + errorCodeString);
#else
    Print("[CONFIG] warning - unable to read file [" + PathToString(file) + "] - " + string(strerror(errno)));
#endif
    return false;
  }

  Print("[CONFIG] loading file [" + PathToString(file) + "]");

  string RawLine;
  int lineCount = 0;

  while (!in.eof()) {
    lineCount++;
    getline(in, RawLine);

    // Strip UTF-8 BOM
    if (lineCount == 1 && RawLine.length() >= 3 && RawLine[0] == '\xEF' && RawLine[1] == '\xBB' && RawLine[2] == '\xBF') {
      RawLine = RawLine.substr(3);
    }

    // ignore blank lines and comments
    if (RawLine.empty() || RawLine[0] == '#' || RawLine[0] == ';' || RawLine == "\n") {
      continue;
    }

    // remove CR
    RawLine.erase(remove(begin(RawLine), end(RawLine), '\r'), end(RawLine));

    string Line = RawLine;
    string::size_type Split = Line.find('=');

    if (Split == string::npos || Split == 0) {
      continue;
    }

    string::size_type KeyStart   = Line.find_first_not_of(' ');
    string::size_type KeyEnd     = Line.find_last_not_of(' ', Split - 1) + 1;
    string::size_type ValueStart = Line.find_first_not_of(' ', Split + 1);
    string::size_type ValueEnd   = Line.find_last_not_of(' ') + 1;

    if (ValueStart == string::npos) {
      continue;
    }

    string Key = Line.substr(KeyStart, KeyEnd - KeyStart);
    if (adapterConfig) {
      Key = adapterConfig->GetString(Key, Key);
    }
    m_CFG[Key] = Line.substr(ValueStart, ValueEnd - ValueStart);
  }

  in.close();
  return true;
}

bool CConfig::Exists(const string& key)
{
  m_ValidKeys.insert(key);
  return m_CFG.find(key) != end(m_CFG);
}

void CConfig::Accept(const string& key)
{
  m_ValidKeys.insert(key);
}

void CConfig::Delete(const string& key)
{
  m_ValidKeys.insert(key);
  m_CFG.erase(key);
}

uint8_t CConfig::CheckRealmKey(const string& key) const
{
  if (key.length() < 9 || key.substr(0, 6) != "realm_") {
    return 0xFF;
  }
  size_t dotIndex = key.find('.');
  if (dotIndex == string::npos) {
    return 0xFF;
  }
  string realmNum = key.substr(6, dotIndex - 6);
  if (realmNum.empty() || realmNum.length() > 3) {
    return 0xFF;
  }
  int32_t value = 0;
  try {
    value = stoi(realmNum);
  } catch (...) {
  }
  if (1 <= value && value <= 120)
    return static_cast<uint8_t>(value - 1);

  return 0xFF;
}

vector<string> CConfig::GetInvalidKeys(const bitset<120> definedRealms) const
{
  vector<string> invalidKeys;
  for (const auto& entry : m_CFG) {
    if (m_ValidKeys.find(entry.first) == m_ValidKeys.end()) {
      uint8_t realmKey = CheckRealmKey(entry.first);
      if (realmKey == 0xFF || definedRealms.test(realmKey)) {
        invalidKeys.push_back("<" + entry.first + ">");
      }
    }
  }
  return invalidKeys;
}

string CConfig::GetString(const string& key)
{
  return GetString(key, string());
}

string CConfig::GetString(const string& key, const string& defaultValue)
{
  GET_KEY(key, value, defaultValue)
  TRY_JSON_STRING(key, value, defaultValue, value)
  SUCCESS(value)
}

string CConfig::GetString(const string& key, const uint32_t minLength, const uint32_t maxLength, const string& defaultValue)
{
  GET_KEY(key, value, defaultValue)
  TRY_JSON_STRING(key, value, defaultValue, value)

  if (value.length() < minLength) {
    CONFIG_ERROR(key, defaultValue)
  }

  if (value.length() > maxLength) {
    CONFIG_ERROR(key, defaultValue)
  }

  SUCCESS(value)
}

string CConfig::GetKeyValue(const string& key)
{
  return "<" + key + " = " + GetString(key) + ">";
}

string CConfig::GetGameCounterTemplate(const string& key, const string& defaultValue)
{
  GET_KEY(key, value, defaultValue)
  TRY_JSON_STRING(key, value, defaultValue, value)

  size_t nonTokenSize = CountTemplateFixedChars(value).value_or(MAX_GAME_NAME_SIZE);
  if (nonTokenSize >= MAX_GAME_NAME_SIZE) {
    CONFIG_ERROR(key, defaultValue)
  }

  multiset<string> tokens = GetTemplateTokens(value);
  size_t countCount = tokens.count("COUNT");
  if (countCount != 1 || tokens.size() != countCount) {
    Print("[CONFIG] <" + key + "> allows placeholder {COUNT} once.");
    CONFIG_ERROR(key, defaultValue)
  }

  SUCCESS(value)
}

string CConfig::GetGameNameTemplate(const string& key, const string& defaultValue)
{
  GET_KEY(key, value, defaultValue)
  TRY_JSON_STRING(key, value, defaultValue, value)

  size_t nonTokenSize = CountTemplateFixedChars(value).value_or(MAX_GAME_NAME_SIZE);
  if (nonTokenSize >= MAX_GAME_NAME_SIZE) {
    CONFIG_ERROR(key, defaultValue)
  }
  multiset<string> tokens = GetTemplateTokens(value);
  size_t nameCount = tokens.count("NAME");
  size_t modeCount = tokens.count("MODE");
  size_t counterCount = tokens.count("COUNTER");
  if (nameCount > 1 || modeCount > 1 || counterCount > 1 || tokens.size() != (nameCount + modeCount + counterCount)) {
    Print("[CONFIG] <" + key + "> allows placeholders {NAME}, {MODE}, and {COUNTER} once each.");
    CONFIG_ERROR(key, defaultValue)
  }
  if (nameCount == 0) {
    value.append("{NAME}");
  }
  if (counterCount == 0) {
    value.append("{COUNTER}");
  }

  SUCCESS(value)
}

uint8_t CConfig::GetStringIndex(const string& key, const vector<string>& fromList, const uint8_t defaultValue)
{
  GET_KEY(key, value, defaultValue)
  value = ToLowerCase(value);
  auto match = find(fromList.begin(), fromList.end(), value);
  if (match != fromList.end()) {
    SUCCESS((uint8_t)distance(fromList.begin(), match))
  }
  CONFIG_ERROR_ALLOWED_VALUES(key, defaultValue, fromList)
}

uint8_t CConfig::GetStringIndexSensitive(const string& key, const vector<string>& fromList, const uint8_t defaultValue)
{
  GET_KEY(key, value, defaultValue)
  auto match = find(fromList.begin(), fromList.end(), value);
  if (match != fromList.end()) {
    SUCCESS((uint8_t)distance(fromList.begin(), match))
  }
  CONFIG_ERROR_ALLOWED_VALUES(key, defaultValue, fromList)
}

bool CConfig::GetBool(const string& key, bool defaultValue)
{
  GET_KEY(key, value, defaultValue)
  optional<bool> parsedToggle = ParseBoolean(value);
  if (parsedToggle.has_value()) {
    SUCCESS(parsedToggle.value());
  }
  CONFIG_ERROR(key, defaultValue)
}

uint8_t CConfig::GetUint8(const string& key, uint8_t defaultValue)
{
  GET_KEY(key, value, defaultValue)
  optional<uint8_t> maybeResult = ParseUInt8(value);
  if (!maybeResult.has_value()) {
    CONFIG_ERROR(key, defaultValue)
  }
  SUCCESS(maybeResult.value())
}

uint16_t CConfig::GetUint16(const string& key, uint16_t defaultValue)
{
  GET_KEY(key, value, defaultValue)
  optional<uint16_t> maybeResult = ParseUInt16(value);
  if (!maybeResult.has_value()) {
    CONFIG_ERROR(key, defaultValue)
  }
  SUCCESS(maybeResult.value())
}

int32_t CConfig::GetInt32(const string& key, int32_t defaultValue)
{
  GET_KEY(key, value, defaultValue)
  optional<int32_t> maybeResult = ParseInt32(value);
  if (!maybeResult.has_value()) {
    CONFIG_ERROR(key, defaultValue)
  }
  SUCCESS(maybeResult.value())
}

uint32_t CConfig::GetUint32(const string& key, uint32_t defaultValue)
{
  GET_KEY(key, value, defaultValue)
  optional<uint32_t> maybeResult = ParseUInt32(value);
  if (!maybeResult.has_value()) {
    CONFIG_ERROR(key, defaultValue)
  }
  SUCCESS(maybeResult.value())
}

int64_t CConfig::GetInt64(const string& key, int64_t defaultValue)
{
  GET_KEY(key, value, defaultValue)
  optional<int64_t> maybeResult = ParseInt64(value);
  if (!maybeResult.has_value()) {
    CONFIG_ERROR(key, defaultValue)
  }
  SUCCESS(maybeResult.value())
}

uint16_t CConfig::GetNonZeroPort(const string& key, uint16_t defaultValue)
{
  GET_KEY(key, value, defaultValue)
  optional<uint16_t> maybeResult = ParseUInt16(value);
  if (!maybeResult.has_value() || maybeResult.value() == 0) {
    CONFIG_ERROR(key, defaultValue)
  }
  SUCCESS(maybeResult.value())
}

uint8_t CConfig::GetSlot(const string& key, uint8_t defaultValue)
{
  return GetSlot(key, MAX_SLOTS_MODERN, defaultValue);
}

uint8_t CConfig::GetSlot(const string& key, uint8_t maxSlots, uint8_t defaultValue)
{
  GET_KEY(key, value, defaultValue)
  optional<uint8_t> maybeResult = ParseUInt8(value);
  if (!maybeResult.has_value()) {
    CONFIG_ERROR(key, defaultValue)
  }
  if (maybeResult.value() <= 0 || maxSlots < maybeResult.value()) {
    CONFIG_ERROR(key, defaultValue)
  }
  SUCCESS(maybeResult.value() - 1)
}

uint8_t CConfig::GetPlayerCount(const string& key, uint8_t defaultValue)
{
  return GetPlayerCount(key, MAX_SLOTS_MODERN, defaultValue);
}

uint8_t CConfig::GetPlayerCount(const string& key, uint8_t maxSlots, uint8_t defaultValue)
{
  GET_KEY(key, value, defaultValue)
  optional<uint8_t> maybeResult = ParseUInt8(value);
  if (!maybeResult.has_value()) {
    CONFIG_ERROR(key, defaultValue)
  }
  if (maxSlots < maybeResult.value()) {
    CONFIG_ERROR(key, defaultValue)
  }
  SUCCESS(maybeResult.value())
}

float CConfig::GetFloat(const string& key, float defaultValue)
{
  GET_KEY(key, value, defaultValue)
  optional<float> maybeResult = ParseFloat(value);
  if (!maybeResult.has_value()) {
    CONFIG_ERROR(key, defaultValue)
  }
  SUCCESS(maybeResult.value())
}

int32_t CConfig::GetInt(const string& key, int32_t defaultValue)
{
  return GetInt32(key, defaultValue);
}

vector<string> CConfig::GetList(const string& key, char separator, bool allowEmptyElements, const vector<string> defaultValue)
{
  GET_KEY(key, value, defaultValue)

  vector<string> entries;
  if (CConfig::GetIsJSONValue(value)) {
    optional<vector<string>> maybeResult = JSONAPI::ParseStringArray(value.substr(5));
    if (!maybeResult.has_value()) {
      CONFIG_ERROR(key, defaultValue);
    }
    entries.swap(*maybeResult);
  } else {
    stringstream ss(value);
    while (ss.good()) {
      string element;
      getline(ss, element, separator);
      entries.push_back(element);
    }
  }

  if (!allowEmptyElements) EllideEmptyElementsInPlace(entries);

  SUCCESS(entries)
}

set<string> CConfig::GetSetBase(const string& key, char separator, bool trimElements, bool caseSensitive, bool allowEmptyElements, const set<string> defaultValue)
{
  GET_KEY(key, value, defaultValue)

  vector<string> entries;
  if (CConfig::GetIsJSONValue(value)) {
    optional<vector<string>> maybeResult = JSONAPI::ParseStringArray(value.substr(5));
    if (!maybeResult.has_value()) {
      CONFIG_ERROR(key, defaultValue);
    }
    entries.swap(*maybeResult);
  } else {
    stringstream ss(value);
    while (ss.good()) {
      string element;
      getline(ss, element, separator);
      entries.push_back(element);
    }
  }

  bool errored = false;
  set<string> uniqueEntries;
  for (auto& element : entries) {
    if (trimElements) element = TrimString(element);
    if (!caseSensitive) element = ToLowerCase(element);
    if (!allowEmptyElements && element.empty()) {
      continue;
    }
    if (!uniqueEntries.insert(element).second) {
      errored = true;
    }
  }

  END(key, uniqueEntries)
}

set<string> CConfig::GetSetSensitive(const string& key, char separator, bool trimElements, bool allowEmptyElements, const set<string> defaultValue)
{
  return GetSetBase(key, separator, trimElements, true, allowEmptyElements, defaultValue);
}

set<string> CConfig::GetSet(const string& key, char separator, bool trimElements, bool allowEmptyElements, const set<string> defaultValue)
{
  return GetSetBase(key, separator, trimElements, false, allowEmptyElements, defaultValue);
}

vector<uint8_t> CConfig::GetUint8Vector(const string& key, const uint32_t count)
{
  GET_KEY(key, value, vector<uint8_t>())

  vector<uint8_t> Output = ExtractNumbers(value, count);
  if (Output.size() != count) {
    CONFIG_ERROR(key, vector<uint8_t>())
  }

  SUCCESS(Output)
}

set<uint8_t> CConfig::GetUint8Set(const string& key, char separator)
{
  set<uint64_t> userValues = GetUint64Set(key, separator);
  set<uint8_t> uniqueEntries;
  bool errored = false;
  for (const auto& value : userValues) {
    if (value > 0xFF) {
      errored = true;
    } else {
      uniqueEntries.insert(static_cast<uint8_t>(value));
    }
  }
  END(key, uniqueEntries)
}

set<uint64_t> CConfig::GetUint64Set(const string& key, char separator)
{
  set<uint64_t> uniqueEntries;
  GET_KEY(key, value, uniqueEntries)

  vector<uint64_t> entries;
  if (CConfig::GetIsJSONValue(value)) {
    optional<vector<uint64_t>> maybeResult = JSONAPI::ParseUnsignedIntArray(value.substr(5));
    if (!maybeResult.has_value()) {
      CONFIG_ERROR(key, uniqueEntries);
    }
    entries.swap(*maybeResult);
  } else {
    stringstream ss(value);
    while (ss.good()) {
      string element;
      getline(ss, element, separator);
      if (element.empty())
        continue;
      optional<uint64_t> maybeUint64 = ParseUint64(element);
      if (!maybeUint64.has_value()) {
        CONFIG_ERROR(key, uniqueEntries)
      }
      entries.push_back(*maybeUint64);
    }
  }

  bool errored = false;
  for (auto& element : entries) {
    if (!uniqueEntries.insert(element).second) {
      errored = true;
    }
  }

  END(key, uniqueEntries)
}

vector<uint8_t> CConfig::GetIPv4(const string& key, const array<uint8_t, 4> &defaultValue)
{
  GET_KEY(key, value, vector<uint8_t>(defaultValue.begin(), defaultValue.end()))
  vector<uint8_t> Output = ExtractIPv4(value);
  if (Output.empty()) {
    CONFIG_ERROR(key, vector<uint8_t>(defaultValue.begin(), defaultValue.end()))
  }

  SUCCESS(Output)
}

set<string> CConfig::GetIPStringSet(const string& key, char separator)
{
  set<string> uniqueEntries;
  GET_KEY(key, value, uniqueEntries)

  vector<string> entries;
  if (CConfig::GetIsJSONValue(value)) {
    optional<vector<string>> maybeResult = JSONAPI::ParseStringArray(value.substr(5));
    if (!maybeResult.has_value()) {
      CONFIG_ERROR(key, uniqueEntries);
    }
    entries.swap(*maybeResult);
  } else {
    stringstream ss(value);
    while (ss.good()) {
      string element;
      getline(ss, element, separator);
      entries.push_back(element);
    }
  }

  bool errored = false;
  for (auto& element : entries) {
    element = TrimString(element);
    if (element.empty()) {
      continue;
    }
    optional<sockaddr_storage> result = CNet::ParseAddress(element, ACCEPT_ANY);
    if (!result.has_value()) {
      errored = true;
      continue;
    }
    string normalIp = AddressToString(result.value());
    if (!uniqueEntries.insert(normalIp).second) {
      errored = true;
    }
  }

  END(key, uniqueEntries)
}

vector<sockaddr_storage> CConfig::GetHostListWithImplicitPort(const string& key, const uint16_t defaultPort, char separator)
{
  GET_KEY(key, value, {})
  bool errored = false;
  vector<sockaddr_storage> Output;
  stringstream ss(value);
  while (ss.good()) {
    string element;
    getline(ss, element, separator);
    if (element.empty())
      continue;

    string ip;
    uint16_t port;
    if (!SplitIPAddressAndPortOrDefault(element, defaultPort, ip, port) || port == 0) {
      errored = true;
      continue;
    }
    optional<sockaddr_storage> result = CNet::ParseAddress(ip, ACCEPT_ANY);
    if (!result.has_value()) {
      errored = true;
      continue;
    }
    SetAddressPort(&(result.value()), port);
    Output.push_back(std::move(result.value()));
    result.reset();
  }
  END(key, Output)
}


filesystem::path CConfig::GetPath(const string &key, const filesystem::path &defaultValue)
{
  GET_KEY(key, value, defaultValue)

#ifdef _WIN32
  if (!utf8::is_valid(value.begin(), value.end())) {
    CONFIG_ERROR(key, defaultValue)
  }

  wstring widePath;
  utf8::utf8to16(value.begin(), value.end(), back_inserter(widePath));

  filesystem::path result = widePath;
#else
  filesystem::path result = value;
#endif
  if (result.is_absolute()) {
    SUCCESS(result)
  }

  SUCCESS(filesystem::path(GetHomeDir() / result).lexically_normal())
}

filesystem::path CConfig::GetDirectory(const string &key, const filesystem::path &defaultValue)
{
  m_ValidKeys.insert(key);
  auto it = m_CFG.find(key);
  if (it == end(m_CFG)) {
    filesystem::path defaultDirectory = defaultValue;
    NormalizeDirectory(defaultDirectory);
    SUCCESS(defaultDirectory)
  }

  string value = it->second;

#ifdef _WIN32
  if (!utf8::is_valid(value.begin(), value.end())) {
    filesystem::path defaultDirectory = defaultValue;
    NormalizeDirectory(defaultDirectory);
    CONFIG_ERROR(key, defaultDirectory)
  }

  wstring widePath;
  utf8::utf8to16(value.begin(), value.end(), back_inserter(widePath));

  filesystem::path result = widePath;
#else
  filesystem::path result = value;
#endif
  if (result.is_absolute()) {
    NormalizeDirectory(result);
    SUCCESS(result)
  }

  result = GetHomeDir() / result;
  NormalizeDirectory(result);
  SUCCESS(result)
}

sockaddr_storage CConfig::GetAddressOfType(const string& key, const uint8_t acceptMode, const string& defaultValue)
{
  m_ValidKeys.insert(key);
  auto it = m_CFG.find(key);
  vector<string> tryAddresses;
  if (it != end(m_CFG)) tryAddresses.push_back(it->second);
  tryAddresses.push_back(defaultValue);

  for (uint8_t i = 0; i < 2; ++i) {
    optional<sockaddr_storage> result = CNet::ParseAddress(tryAddresses[i], acceptMode);
    if (result.has_value()) {
      if (i == 0) {
        SUCCESS(result.value())
      } else {
        // usable but display a warning
        CONFIG_ERROR(key, result.value());
      }
    }
  }

  struct sockaddr_storage fallback;
  memset(&fallback, 0, sizeof(fallback));
  CONFIG_ERROR(key, fallback)
}

sockaddr_storage CConfig::GetAddressIPv4(const string& key, const string& defaultValue)
{
  return GetAddressOfType(key, ACCEPT_IPV4, defaultValue);
}

sockaddr_storage CConfig::GetAddressIPv6(const string& key, const string& defaultValue)
{
  return GetAddressOfType(key, ACCEPT_IPV6, defaultValue);
}

sockaddr_storage CConfig::GetAddress(const string& key, const string& defaultValue)
{
  return GetAddressOfType(key, ACCEPT_ANY, defaultValue);
}

optional<bool> CConfig::GetMaybeBool(const string& key)
{
  optional<bool> result;
  GET_KEY(key, value, result)
  result = ParseBoolean(value);
  if (result.has_value()) {
    SUCCESS(result);
  }
  CONFIG_ERROR(key, result)
}

optional<uint8_t> CConfig::GetMaybeUint8(const string& key)
{
  optional<uint8_t> result;
  GET_KEY(key, value, result)
  result = ParseUint8(value);
  if (!result.has_value()) {
    CONFIG_ERROR(key, result)
  }
  SUCCESS(result)
}

optional<uint16_t> CConfig::GetMaybeUint16(const string& key)
{
  optional<uint16_t> result;
  GET_KEY(key, value, result);
  result = ParseUint16(value);
  if (!result.has_value()) {
    CONFIG_ERROR(key, result)
  }
  SUCCESS(result)
}

optional<uint32_t> CConfig::GetMaybeUint32(const string& key)
{
  optional<uint32_t> result;
  GET_KEY(key, value, result);
  result = ParseUint32(value);
  if (!result.has_value()) {
    CONFIG_ERROR(key, result)
  }
  SUCCESS(result)
}

optional<int64_t> CConfig::GetMaybeInt64(const string& key)
{
  optional<int64_t> result;
  GET_KEY(key, value, result);
  result = ParseInt64(value);
  if (!result.has_value()) {
    CONFIG_ERROR(key, result)
  }
  SUCCESS(result)
}

optional<uint64_t> CConfig::GetMaybeUint64(const string& key)
{
  optional<uint64_t> result;
  GET_KEY(key, value, result);
  result = ParseUInt64(value);
  if (!result.has_value()) {
    CONFIG_ERROR(key, result)
  }
  SUCCESS(result)
}

optional<Version> CConfig::GetMaybeVersion(const string& key)
{
  optional<Version> result;
  GET_KEY(key, value, result);

  optional<Version> userValue = ParseGameVersion(value);
  if (!userValue.has_value()) {
    CONFIG_ERROR(key, result);
  }

  if (userValue->first == 0 || userValue->first > 2) {
    Print("[CONFIG] Bad version. It must be 1.x or 2.x");
    CONFIG_ERROR(key, result);
  }

  if (userValue->second >= 100) { // Values >= 100 break ToVersionFlattened
    Print("[CONFIG] Bad version. It must be 1.x or 2.x");
    CONFIG_ERROR(key, result);
  }

  result.swap(userValue);
  SUCCESS(result)
}

optional<uint16_t> CConfig::GetMaybeNonZeroPort(const string& key)
{
  optional<uint16_t> result;
  GET_KEY(key, value, result);
  result = ParseUint16(value);
  if (!result.has_value() || result.value() == 0) {
    CONFIG_ERROR(key, result)
  }
  SUCCESS(result)
}

optional<vector<uint8_t>> CConfig::GetMaybeUint8Vector(const string &key, const uint32_t count)
{
  optional<vector<uint8_t>> result;
  GET_KEY(key, value, result);

  vector<uint8_t> bytes = ExtractNumbers(value, count);
  if (bytes.size() != count) {
    CONFIG_ERROR(key, result)
  }

  result = bytes;
  SUCCESS(result)
}

optional<vector<uint8_t>> CConfig::GetMaybeIPv4(const string &key)
{
  optional<vector<uint8_t>> result;
  GET_KEY(key, value, result);

  vector<uint8_t> networkOrderBytes = ExtractIPv4(value);
  if (networkOrderBytes.empty()) {
    CONFIG_ERROR(key, result)
  }

  result = networkOrderBytes;
  SUCCESS(result)
}

optional<filesystem::path> CConfig::GetMaybePath(const string &key)
{
  optional<filesystem::path> result;
  GET_KEY(key, value, result);

#ifdef _WIN32
  if (!utf8::is_valid(value.begin(), value.end())) {
    CONFIG_ERROR(key, result)
  }

  wstring widePath;
  utf8::utf8to16(value.begin(), value.end(), back_inserter(widePath));

  result = filesystem::path(widePath);
#else
  result = filesystem::path(value);
#endif
  if (result.value().is_absolute()) {
    SUCCESS(result)
  }
  result = (GetHomeDir() / result.value()).lexically_normal();
  SUCCESS(result)
}

optional<filesystem::path> CConfig::GetMaybeDirectory(const string &key)
{
  optional<filesystem::path> result;
  GET_KEY(key, value, result);

#ifdef _WIN32
  if (!utf8::is_valid(value.begin(), value.end())) {
    CONFIG_ERROR(key, result)
  }

  wstring widePath;
  utf8::utf8to16(value.begin(), value.end(), back_inserter(widePath));

  result = filesystem::path(widePath);
#else
  result = filesystem::path(value);
#endif

  if (result.value().is_absolute()) {
    NormalizeDirectory(result.value());
    SUCCESS(result)
  }
  result = GetHomeDir() / result.value();
  NormalizeDirectory(result.value());
  SUCCESS(result)
}

optional<sockaddr_storage> CConfig::GetMaybeAddressOfType(const string& key, const uint8_t acceptMode)
{
  optional<sockaddr_storage> result;
  GET_KEY(key, value, result);

  result = CNet::ParseAddress(value, acceptMode);
  if (result.has_value()) {
    SUCCESS(result);
  }
  CONFIG_ERROR(key, result)
}

optional<sockaddr_storage> CConfig::GetMaybeAddressIPv4(const string& key)
{
  return GetMaybeAddressOfType(key, ACCEPT_IPV4);
}

optional<sockaddr_storage> CConfig::GetMaybeAddressIPv6(const string& key)
{
  return GetMaybeAddressOfType(key, ACCEPT_IPV6);
}

optional<sockaddr_storage> CConfig::GetMaybeAddress(const string& key)
{
  return GetMaybeAddressOfType(key, ACCEPT_ANY);
}

void CConfig::Set(const string& key, const string& value)
{
  m_CFG[key] = value;
}

void CConfig::SetString(const string& key, const string& value)
{
  m_CFG[key] = value;
}

void CConfig::SetString(const std::string& key, const char* start, const std::string::size_type& size)
{
  m_CFG[key] = string(start, size);
}

void CConfig::SetString(const std::string& key, const unsigned char* start, const std::string::size_type& size)
{
  m_CFG[key] = string(reinterpret_cast<const char*>(start), size);
}

void CConfig::SetBool(const string& key, const bool& value)
{
  m_CFG[key] = value ? "yes" : "no";
}

void CConfig::SetInt32(const string& key, const int32_t& value)
{
  m_CFG[key] = to_string(value);
}

void CConfig::SetInt64(const string& key, const int64_t& value)
{
  m_CFG[key] = to_string(value);
}

void CConfig::SetUint32(const string& key, const uint32_t& value)
{
  m_CFG[key] = to_string(value);
}

void CConfig::SetUint16(const string& key, const uint16_t& value)
{
  m_CFG[key] = to_string(value);
}
void CConfig::SetUint8(const string& key, const uint8_t& value)
{
  m_CFG[key] = to_string(value);
}

void CConfig::SetFloat(const string& key, const float& value)
{
  m_CFG[key] = to_string(value);
}

void CConfig::SetUint8Vector(const string& key, const vector<uint8_t> &value)
{
  m_CFG[key] = ByteArrayToDecString(value);
}

void CConfig::SetUint8Array(const string& key, const uint8_t* start, const size_t size)
{
  m_CFG[key] = ByteArrayToDecString(start, size);
}

void CConfig::SetUint8VectorReverse(const string& key, const vector<uint8_t> &value)
{
  m_CFG[key] = ReverseByteArrayToDecString(value);
}

void CConfig::SetUint8ArrayReverse(const string& key, const uint8_t* start, const size_t size)
{
  m_CFG[key] = ReverseByteArrayToDecString(start, size);
}

std::vector<uint8_t> CConfig::Export() const
{
  std::ostringstream SS;
  for (auto it = m_CFG.begin(); it != m_CFG.end(); ++it) {
    std::string value = it->second;
    value.erase(remove(begin(value), end(value), '\n'), end(value));
    SS << (it->first + " = " + value + "\n");
  }

  std::string str = SS.str();
  std::vector<uint8_t> bytes(str.begin(), str.end());
  return bytes;
}

std::string CConfig::ReadString(const std::filesystem::path& file, const std::string& key)
{
  std::string Output;
  ifstream in;
  in.open(file.native().c_str(), ios::in);

  if (in.fail())
    return Output;

  string RawLine;

  bool isFirstLine = true;
  while (!in.eof()) {
    getline(in, RawLine);

    if (isFirstLine) {
      if (RawLine.length() >= 3 && RawLine[0] == '\xEF' && RawLine[1] == '\xBB' && RawLine[2] == '\xBF')
        RawLine = RawLine.substr(3);
      isFirstLine = false;
    }

    // ignore blank lines and comments
    if (RawLine.empty() || RawLine[0] == '#' || RawLine[0] == ';' || RawLine == "\n") {
      continue;
    }

    // remove CR
    RawLine.erase(remove(begin(RawLine), end(RawLine), '\r'), end(RawLine));

    string Line = RawLine;
    string::size_type Split = Line.find('=');

    if (Split == string::npos || Split == 0)
      continue;

    string::size_type KeyStart   = Line.find_first_not_of(' ');
    string::size_type KeyEnd     = Line.find_last_not_of(' ', Split - 1) + 1;
    string::size_type ValueStart = Line.find_first_not_of(' ', Split + 1);
    string::size_type ValueEnd   = Line.find_last_not_of(' ') + 1;

    if (ValueStart == string::npos)
      continue;

    if (Line.substr(KeyStart, KeyEnd - KeyStart) == key) {
      Output = Line.substr(ValueStart, ValueEnd - ValueStart);
      break;
    }
  }

  in.close();
  return Output;
}

bool CConfig::GetIsJSONValue(const string& value)
{
  return value.size() >= 5 && value.substr(0, 5) == "json:";
}

#undef SUCCESS
#undef CONFIG_ERROR
#undef END
