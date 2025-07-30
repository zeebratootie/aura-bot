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

#ifndef AURA_GAME_STAT_H_
#define AURA_GAME_STAT_H_

#include "includes.h"
#include "util.h"

struct GameStat
{
  bool                                            m_IsValid;
  uint32_t                                        m_GameFlags;
  uint16_t                                        m_MapWidth;
  uint16_t                                        m_MapHeight;
  std::array<uint8_t, 4>                          m_MapScriptsBlizzHash;
  std::string                                     m_MapPath;
  std::string                                     m_HostName;
  std::optional<std::array<uint8_t, 20>>          m_MapScriptsSHA1;

  GameStat();
  GameStat(uint32_t gameFlags, uint16_t mapWidth, uint16_t mapHeight, std::string_view mapPath, std::string_view hostName, const std::array<uint8_t, 4>& mapBlizzHash, const std::optional<std::array<uint8_t, 20>>& mapSHA1);
  GameStat(const uint8_t* ptr, size_t size);

  ~GameStat();
  std::vector<uint8_t> Encode() const;
  std::string GetMapClientFileName() const;

  [[nodiscard]] static GameStat Parse(std::string_view statString);
  template <typename Container>
  [[nodiscard]] static GameStat Parse(const Container& statString);

  [[nodiscard]] static GameStat Read(const std::vector<uint8_t>& rawData) {
    return GameStat(rawData.data(), rawData.size());
  }

  [[nodiscard]] inline bool GetIsValid() const { return m_IsValid; }

  [[nodiscard]] inline uint32_t GetGameFlags() const { return m_GameFlags; }
  [[nodiscard]] inline uint16_t GetMapWidth() const { return m_MapWidth; }
  [[nodiscard]] inline uint16_t GetMapHeight() const { return m_MapHeight; }
  [[nodiscard]] inline bool GetHasSHA1() const { return m_MapScriptsSHA1.has_value(); }
  [[nodiscard]] inline const std::array<uint8_t, 20>& GetMapScriptsSHA1() const { return m_MapScriptsSHA1.value(); }
  [[nodiscard]] inline std::string_view GetHostName() const { return m_HostName; }
  [[nodiscard]] inline const std::array<uint8_t, 4>& GetMapScriptsBlizzHash() const { return m_MapScriptsBlizzHash; }
  [[nodiscard]] inline std::string_view GetMapClientPath() const { return m_MapPath; }
};

#endif // AURA_GAME_STAT_H_
