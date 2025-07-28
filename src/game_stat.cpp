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

#include "game_stat.h"
#include "util.h"

using namespace std;

// GameStat

GameStat::GameStat()
: m_IsValid(false),
  m_GameFlags(0),
  m_MapWidth(0),
  m_MapHeight(0)
{
  m_MapScriptsBlizzHash.fill(0);
}

GameStat::GameStat(uint32_t gameFlags, uint16_t mapWidth, uint16_t mapHeight, string_view mapPath, string_view hostName, const array<uint8_t, 4>& mapBlizzHash, const optional<array<uint8_t, 20>>& maybeSHA1)
: m_IsValid(true),
  m_GameFlags(gameFlags),
  m_MapWidth(mapWidth),
  m_MapHeight(mapHeight),
  m_MapPath(mapPath),
  m_HostName(hostName)
{
  m_MapScriptsBlizzHash.fill(0);
  copy_n(mapBlizzHash.begin(), 4, m_MapScriptsBlizzHash.begin());

  if (maybeSHA1.has_value() && !IsAllZeroes(maybeSHA1->data(), maybeSHA1->data() + 20)) {
    m_MapScriptsSHA1.emplace();
    copy_n(maybeSHA1->begin(), 20, m_MapScriptsSHA1->begin());
  }
}

GameStat::GameStat(const uint8_t* ptr, size_t size)
: m_IsValid(false),
  m_GameFlags(0),
  m_MapWidth(0),
  m_MapHeight(0)
{
  const uint8_t* dataEnd = ptr + size;
  const uint8_t* cursorStart = ptr;
  const uint8_t* cursorEnd = cursorStart;

  m_MapScriptsBlizzHash.fill(0);

  cursorEnd = cursorStart + 4;
  if (cursorEnd > dataEnd) return;
  m_GameFlags = ByteArrayToUInt32LE(cursorStart);
  cursorStart = cursorEnd + 1;

  cursorEnd = cursorStart + 2;
  if (cursorEnd > dataEnd) return;
  m_MapWidth = ByteArrayToUInt16LE(cursorStart);
  cursorStart = cursorEnd;

  cursorEnd = cursorStart + 2;
  if (cursorEnd > dataEnd) return;
  m_MapHeight = ByteArrayToUInt16LE(cursorStart);
  cursorStart = cursorEnd;

  cursorEnd = cursorStart + 4;
  if (cursorEnd > dataEnd) return;
  copy_n(cursorStart, 4, m_MapScriptsBlizzHash.begin());
  cursorStart = cursorEnd;

  cursorEnd = FindNullDelimiterInRangeOrStart(cursorStart, dataEnd);
  if (cursorEnd == cursorStart) return;

  m_MapPath = GetStringAddressRange(cursorStart, cursorEnd);

  cursorStart = cursorEnd + 1;
  cursorEnd = FindNullDelimiterInRangeOrStart(cursorStart, dataEnd);
  if (cursorEnd == cursorStart) return;

  m_HostName = GetStringAddressRange(cursorStart, cursorEnd);

  cursorStart = cursorEnd + 2;
  cursorEnd = cursorStart + 20;

  // Since v1.23, savefiles for LAN games store an all-zeroes SHA1
  if (cursorEnd <= dataEnd && !IsAllZeroes(cursorStart, cursorEnd)) {
    m_MapScriptsSHA1.emplace();
    copy_n(cursorStart, 20, m_MapScriptsSHA1->begin());
  }

  m_IsValid = true;
}

GameStat::~GameStat()
{
}

vector<uint8_t> GameStat::Encode() const
{
  vector<uint8_t> encoded;
  encoded.reserve(13 + m_MapPath.size() + 1 + m_HostName.size() + 2 + (m_MapScriptsSHA1.has_value() ? 20 : 0));
  AppendNumberLE(encoded, m_GameFlags);
  encoded.push_back(0);
  AppendNumberLE(encoded, m_MapWidth);
  AppendNumberLE(encoded, m_MapHeight);
  AppendContainer(encoded, m_MapScriptsBlizzHash);
  AppendByteArrayString(encoded, m_MapPath, true);
  AppendByteArrayString(encoded, m_HostName, true);
  encoded.push_back(0);
  if (m_MapScriptsSHA1.has_value()) {
    AppendContainer(encoded, *m_MapScriptsSHA1);
  }
  return EncodeStatString(encoded);
}

string GameStat::GetMapClientFileName() const
{
  size_t LastSlash = m_MapPath.rfind('\\');
  if (LastSlash == string::npos) {
    return m_MapPath;
  }
  return m_MapPath.substr(LastSlash + 1);
}

template <typename Container>
GameStat GameStat::Parse(const Container& statString) {
  std::vector<uint8_t> decoded = DecodeStatString(statString);
  return GameStat(decoded.data(), decoded.size());
}

template GameStat GameStat::Parse(const vector<uint8_t>& statString);
template GameStat GameStat::Parse(const string& statString);
