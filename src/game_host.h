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

#ifndef AURA_GAME_HOST_H_
#define AURA_GAME_HOST_H_

#include "socket.h"
#include "includes.h"
#include "list.h"
#include "protocol/game_protocol.h"


struct GameHost
{
  sockaddr_storage          m_Address;
  uint32_t                  m_Identifier;
  uint32_t                  m_EntryKey;

  GameHost();
  GameHost(const sockaddr_storage& address, uint32_t identifier);
  GameHost(const sockaddr_storage& address, uint32_t identifier, uint32_t entryKey);
  GameHost(const std::string& hexInput);
  ~GameHost();

  inline void SetAddress(const sockaddr_storage& address) { memcpy(&m_Address, &address, sizeof(sockaddr_storage)); }
  inline void SetIdentifier(uint32_t identifier) { m_Identifier = identifier; }
  inline void SetEntryKey(uint32_t entryKey) { m_EntryKey = entryKey; }

  [[nodiscard]] inline const sockaddr_storage*           GetAddress() const { return &m_Address; }
  [[nodiscard]] inline uint32_t                          GetIdentifier() const { return m_Identifier; }
  [[nodiscard]] inline uint32_t                          GetEntryKey() const { return m_EntryKey; }
  [[nodiscard]] inline bool                              GetHasEntryKey() const { return m_EntryKey != 0; }

  static std::optional<GameHost> Parse(const std::string& hexInput);
};

struct GameInfo
{
  uint16_t                  m_GameType;
  uint32_t                  m_GameStatus;
  uint32_t                  m_GameFlags;
  uint16_t                  m_MapWidth;
  uint16_t                  m_MapHeight;
  std::array<uint8_t, 4>    m_MapScriptsBlizzHash;
  std::optional<std::array<uint8_t, 20>> m_MapScriptsSHA1;

  std::string               m_MapPath;

  GameInfo()
   : m_GameType(0),
     m_GameStatus(0),
     m_GameFlags(0),
     m_MapWidth(0),
     m_MapHeight(0)
  {
    m_MapScriptsBlizzHash.fill(0);
  }

  ~GameInfo()
  {
  }

  [[nodiscard]] inline bool GetHasSHA1() const { return m_MapScriptsSHA1.has_value(); }
  [[nodiscard]] inline const std::array<uint8_t, 20>& GetMapScriptsSHA1() const { return m_MapScriptsSHA1.value(); }
};

//
// NetworkGameInfo
//

struct NetworkGameInfo
{
  bool                    m_IsValid;
  GameHost                m_Host;
  GameInfo                m_Info;
  std::string             m_GameName;
  std::string             m_GamePassWord;
  std::string             m_HostName;

  NetworkGameInfo()
   : m_IsValid(true)
  {
  }

  ~NetworkGameInfo()
  {
  }

  void SetAddress(const sockaddr_storage& address);
  void SetGameType(uint16_t gameType);
  void SetStatus(uint32_t status);
  void SetGameName(std::string_view gameName);
  void SetPassword(std::string_view passWord);
  bool SetBNETGameInfo(std::string_view gameInfo, const Version& war3Version);

  [[nodiscard]] inline bool GetIsValid() const { return m_IsValid; }
  [[nodiscard]] std::string GetIPString() const;
  [[nodiscard]] inline const sockaddr_storage* GetAddress() const { return m_Host.GetAddress(); }
  [[nodiscard]] inline uint32_t GetIdentifier() const { return m_Host.m_Identifier; }
  [[nodiscard]] inline uint32_t GetEntryKey() const { return m_Host.m_EntryKey; }
  [[nodiscard]] inline uint32_t GetGameFlags() const { return m_Info.m_GameFlags; }
  [[nodiscard]] inline bool GetHasSHA1() const { return m_Info.GetHasSHA1(); }
  [[nodiscard]] inline const std::array<uint8_t, 20>& GetMapScriptsSHA1() const { return m_Info.GetMapScriptsSHA1(); }
  [[nodiscard]] inline std::string_view GetGameName() const { return m_GameName; }
  [[nodiscard]] inline std::string_view GetHostName() const { return m_HostName; }
  [[nodiscard]] std::string GetHostDetails() const;
  [[nodiscard]] inline bool GetIsGProxy() const { return m_Info.m_MapWidth == m_Info.m_MapHeight && m_Info.m_MapHeight == 1984; }
  [[nodiscard]] inline const std::array<uint8_t, 4>& GetMapScriptsBlizzHash() const { return m_Info.m_MapScriptsBlizzHash; }
  [[nodiscard]] inline std::string_view GetMapClientPath() const { return m_Info.m_MapPath; }
  [[nodiscard]] std::string GetMapClientFileName() const;
};

#endif // AURA_GAME_HOST_H_
