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

#ifndef AURA_BNETPROTOCOL_H_
#define AURA_BNETPROTOCOL_H_

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

#include "../includes.h"
#include "../config/config.h"
#include "../hash.h"
#include "../game_host.h"

namespace BNETProtocol
{
  namespace Magic
  { 
    constexpr uint8_t ZERO                   = 0u;   // 0x0
    constexpr uint8_t STOPADV                = 2u;   // 0x2
    constexpr uint8_t GETADVLISTEX           = 9u;   // 0x9
    constexpr uint8_t ENTERCHAT              = 10u;  // 0xA
    constexpr uint8_t JOINCHANNEL            = 12u;  // 0xC
    constexpr uint8_t CHATMESSAGE            = 14u;  // 0xE - PvPGN: CLIENT_MESSAGE
    constexpr uint8_t CHATEVENT              = 15u;  // 0xF
    constexpr uint8_t CHECKAD                = 21u;  // 0x15
    constexpr uint8_t PUBLICHOST             = 27u;  // 0x1B
    constexpr uint8_t STARTADVEX3            = 28u;  // 0x1C
    constexpr uint8_t DISPLAYAD              = 33u;  // 0x21
    constexpr uint8_t NOTIFYJOIN             = 34u;  // 0x22
    constexpr uint8_t PING                   = 37u;  // 0x25
    constexpr uint8_t LOGONRESPONSE          = 41u;  // 0x29
    constexpr uint8_t AUTH_ACCOUNTSIGNUP     = 42u;  // 0x2A
    constexpr uint8_t AUTH_ACCOUNTSIGNUP2    = 61u;  // 0x3D
    constexpr uint8_t NETGAMEPORT            = 69u;  // 0x45
    constexpr uint8_t AUTH_INFO              = 80u;  // 0x50
    constexpr uint8_t AUTH_CHECK             = 81u;  // 0x51
    constexpr uint8_t AUTH_ACCOUNTLOGON      = 83u;  // 0x53
    constexpr uint8_t AUTH_ACCOUNTLOGONPROOF = 84u;  // 0x54
    constexpr uint8_t WARDEN                 = 94u;  // 0x5E
    constexpr uint8_t FRIENDLIST             = 101u; // 0x65
    constexpr uint8_t FRIENDSUPDATE          = 102u; // 0x66
    constexpr uint8_t CLANMEMBERLIST         = 125u; // 0x7D
    constexpr uint8_t CLANMEMBERSTATUSCHANGE = 127u; // 0x7F
    constexpr uint8_t GETGAMEINFO            = 131u; // 0x83
    constexpr uint8_t HOSTGAME               = 132u; // 0x84

    // Orthogonal to above
    constexpr uint8_t BNET_HEADER            = 255u; // 0xFF
  };

  namespace KeyResult
  {
    constexpr uint32_t GOOD             = 0u;
    constexpr uint32_t BAD              = 1u;
    constexpr uint32_t OLD_GAME_VERSION = 256u;
    constexpr uint32_t INVALID_VERSION  = 257u;
    constexpr uint32_t ROC_KEY_IN_USE   = 513u;
    constexpr uint32_t TFT_KEY_IN_USE   = 529u;
  };

  namespace IncomingChatEvent
  {
    constexpr uint32_t SHOWUSER            = 1u;  // received when you join a channel (includes users in the channel and their information)
    constexpr uint32_t JOIN                = 2u;  // received when someone joins the channel you're currently in
    constexpr uint32_t LEAVE               = 3u;  // received when someone leaves the channel you're currently in
    constexpr uint32_t WHISPER             = 4u;  // received a whisper message
    constexpr uint32_t TALK                = 5u;  // received when someone talks in the channel you're currently in
    constexpr uint32_t BROADCAST           = 6u;  // server broadcast
    constexpr uint32_t CHANNEL             = 7u;  // received when you join a channel (includes the channel's nameu; flags)
    constexpr uint32_t USERFLAGS           = 9u;  // user flags updates
    constexpr uint32_t WHISPERSENT         = 10u; // sent a whisper message
    constexpr uint32_t CHANNELFULL         = 13u; // channel is full
    constexpr uint32_t CHANNELDOESNOTEXIST = 14u; // channel does not exist
    constexpr uint32_t CHANNELRESTRICTED   = 15u; // channel is restricted
    constexpr uint32_t INFO                = 18u; // broadcast/information message
    constexpr uint32_t NOTICE              = 19u; // notice/error message
    constexpr uint32_t EMOTE               = 23u; // emote
  };

  namespace WhoisTexts
  {
    namespace enUS
    {
      constexpr const char* CLIENT_WC3 = "is using Warcraft III";
      constexpr const char* CLIENT_TFT = " Frozen Throne";
      constexpr const char* LOCATION_PREFIX = " and is currently ";
      constexpr const char* LOCATION_PRIVATE_GAME = "in private game \"";
      constexpr const char* LOCATION_PUBLIC_GAME = "in  game \"";
      constexpr const char* LOCATION_CHAT = "in channel \"";
      constexpr const char* LAST_SEEN_PREFIX = "User was last seen on: ";
      constexpr const char* USER_UNKNOWN = "Unknown user.";
      constexpr const char* USER_OFFLINE = "User is offline";
      constexpr const char* LOCATION_SUFFIX = "\".";
    };

    namespace esES
    {
      constexpr const char* CLIENT_WC3 = "está usando Warcraft III";
      constexpr const char* CLIENT_TFT = " Frozen Throne";
      constexpr const char* LOCATION_PREFIX = " y está actualmente dentro ";
      constexpr const char* LOCATION_PRIVATE_GAME = "de privado juego \"";
      constexpr const char* LOCATION_PUBLIC_GAME = "de  juego \"";
      constexpr const char* LOCATION_CHAT = "del canal \"";
      constexpr const char* LAST_SEEN_PREFIX = "Usuario última vez visto el: ";
      constexpr const char* USER_UNKNOWN = "Usuario desconocido.";
      constexpr const char* USER_OFFLINE = "El usuario está desconectado";
      constexpr const char* LOCATION_SUFFIX = "\".";
    };

    namespace deDE
    {
      constexpr const char* CLIENT_WC3 = "ist nutzt Warcraft III";
      constexpr const char* CLIENT_TFT = " Frozen Throne";
      constexpr const char* LOCATION_PREFIX = " und ist ist momentan ";
      constexpr const char* LOCATION_PRIVATE_GAME = "im privat Spiel \"";
      constexpr const char* LOCATION_PUBLIC_GAME = "im  Spiel \"";
      constexpr const char* LOCATION_CHAT = "im Kanal \"";
      constexpr const char* LAST_SEEN_PREFIX = "Nutzer zuletzt gesehen am: ";
      constexpr const char* USER_UNKNOWN = "Unbekannter Nutzer.";
      constexpr const char* USER_OFFLINE = "Nutzer ist offline.";
      constexpr const char* LOCATION_SUFFIX = "\""; // trailing period missing in German
    };
  };
  
  namespace WhoisInfoStatus
  {
    constexpr uint8_t UNKNOWN                = 0u;  // received when you join a channel (includes users in the channel and their information)
    constexpr uint8_t OFFLINE_GENERIC        = 1u;  // received when someone joins the channel you're currently in
    constexpr uint8_t OFFLINE_LAST_SEEN      = 2u;  // received when someone leaves the channel you're currently in
    constexpr uint8_t ONLINE                 = 3u;
    constexpr uint8_t ONLINE_IN_GAME_PUBLIC  = 4u;
    constexpr uint8_t ONLINE_IN_GAME_PRIVATE = 5u;
    constexpr uint8_t ONLINE_IN_CHAT         = 6u;
  };

  namespace WhoisInfoClientTag
  {
    constexpr uint8_t None                   = 0u;
    constexpr uint8_t ROC                    = 1u;
    constexpr uint8_t TFT                    = 2u;
  };

  struct AuthInfoResult
  {
    const bool success;
    const uint8_t* logonType;
    const uint8_t* serverToken;
    const uint8_t* mpqFileTime;
    const uint8_t* verFileNameStart;
    const uint8_t* verFileNameEnd;
    const uint8_t* valueStringFormulaStart;
    const uint8_t* valueStringFormulaEnd;

    AuthInfoResult(const bool nSuccess, const uint8_t* nLogonType, const uint8_t* nServerToken, const uint8_t* nMPQFileTime, const uint8_t* nVerFileNameStart, const uint8_t* nVerFileNameEnd, const uint8_t* nValueStringFormulaStart, const uint8_t* nValueStringFormulaEnd)
    : success(nSuccess),
      logonType(nLogonType),
      serverToken(nServerToken),
      mpqFileTime(nMPQFileTime),
      verFileNameStart(nVerFileNameStart),
      verFileNameEnd(nVerFileNameEnd),
      valueStringFormulaStart(nValueStringFormulaStart),
      valueStringFormulaEnd(nValueStringFormulaEnd)
      {};

    ~AuthInfoResult() = default;
  };

  struct AuthCheckResult
  {
    const uint32_t state;
    std::string_view description;

    AuthCheckResult()
     : state(BNETProtocol::KeyResult::BAD)
    {}

    AuthCheckResult(const uint32_t nState, std::string_view nDescription)
     : state(nState),
       description(nDescription)
    {}

    ~AuthCheckResult() = default;
  };

  struct AuthLoginResult
  {
    const bool success;
    const uint8_t* salt;
    const uint8_t* serverPublicKey;

    AuthLoginResult(const bool nSuccess, const uint8_t* nSalt, const uint8_t* nServerPublicKey)
    : success(nSuccess),
      salt(nSalt),
      serverPublicKey(nServerPublicKey)
    {};
    ~AuthLoginResult() = default;
  };

  struct EnterChatResult
  {
    const bool success;
    std::string_view uniqueName;

    EnterChatResult()
    : success(false)
    {}

    EnterChatResult(const bool nSuccess, std::string_view nUniqueName)
    : success(nSuccess),
      uniqueName(nUniqueName)
    {}

    ~EnterChatResult() = default;
  };

  //
  // IncomingChatResult
  //

  struct IncomingChatResult
  {
    const bool success;
    const uint32_t type;
    std::string_view userName;
    std::string_view message;

    IncomingChatResult()
     : success(false),
       type(0)
     {};

    IncomingChatResult(const bool nSuccess, const uint32_t nType, std::string_view nUserName, std::string_view nMessage)
     : success(nSuccess),
       type(nType),
       userName(nUserName),
       message(nMessage)
     {};
    ~IncomingChatResult() = default;
  };

  //
  // WhoisInfo
  //

  struct WhoisInfo
  {
    const uint8_t status;
    const uint8_t tag;
    const std::string name;
    const std::string location;

    WhoisInfo(const uint8_t nStatus)
     : status(nStatus),
       tag(BNETProtocol::WhoisInfoClientTag::None)
     {};

    WhoisInfo(const uint8_t nStatus, const uint8_t nTag)
     : status(nStatus),
       tag(nTag)
     {};

    WhoisInfo(const uint8_t nStatus, const uint8_t nTag, const std::string& nName, const std::string& nLocation)
     : status(nStatus),
       tag(nTag),
       name(nName),
       location(nLocation)
     {};

    ~WhoisInfo() = default;

    inline bool GetIsInGame() { return status == BNETProtocol::WhoisInfoStatus::ONLINE_IN_GAME_PRIVATE || status == BNETProtocol::WhoisInfoStatus::ONLINE_IN_GAME_PUBLIC; }
  };

  [[nodiscard]] inline bool GetIsCommandConflictsWithWhisper(const uint64_t hashCode, bool hasArgument) {
    switch (hashCode) {
      case HashCode("kick"):
      case HashCode("kill"):
      case HashCode("serverban"):
        return true;
      case HashCode("netinfo"):
        return hasArgument;
      default:
        return false;
    }
  }

  [[nodiscard]] inline size_t GetMessageSize(const std::vector<uint8_t> message) { return message.size(); }
  [[nodiscard]] inline size_t GetWhisperSize(const std::vector<uint8_t> message, const std::vector<uint8_t> name) { return message.size() + name.size(); }
 
      
  // receive functions

  [[nodiscard]] bool RECEIVE_SID_ZERO(const std::vector<uint8_t>& data);
  [[nodiscard]] std::vector<NetworkGameInfo> RECEIVE_SID_GETADVLISTEX(const Version& war3Version, const std::vector<uint8_t>& data);
  [[nodiscard]] BNETProtocol::EnterChatResult RECEIVE_SID_ENTERCHAT(const std::vector<uint8_t>& data);
  [[nodiscard]] BNETProtocol::IncomingChatResult RECEIVE_SID_CHATEVENT(const std::vector<uint8_t>& data);
  [[nodiscard]] bool RECEIVE_SID_CHECKAD(const std::vector<uint8_t>& data);
  [[nodiscard]] bool RECEIVE_SID_STARTADVEX3(const std::vector<uint8_t>& data);
  [[nodiscard]] std::array<uint8_t, 4> RECEIVE_SID_PING(const std::vector<uint8_t>& data);
  [[nodiscard]] BNETProtocol::AuthInfoResult RECEIVE_SID_AUTH_INFO(const std::vector<uint8_t>& data);
  [[nodiscard]] BNETProtocol::AuthCheckResult RECEIVE_SID_AUTH_CHECK(const std::vector<uint8_t>& data);
  [[nodiscard]] BNETProtocol::AuthLoginResult RECEIVE_SID_AUTH_ACCOUNTLOGON(const std::vector<uint8_t>& data);
  [[nodiscard]] bool RECEIVE_SID_AUTH_ACCOUNTLOGONPROOF(const std::vector<uint8_t>& data);
  [[nodiscard]] bool RECEIVE_SID_AUTH_ACCOUNTSIGNUP(const std::vector<uint8_t>& data);
  [[nodiscard]] std::vector<std::string> RECEIVE_SID_FRIENDLIST(const std::vector<uint8_t>& data);
  [[nodiscard]] std::vector<std::string> RECEIVE_SID_CLANMEMBERLIST(const std::vector<uint8_t>& data);
  [[nodiscard]] std::optional<CConfig> RECEIVE_HOSTED_GAME_CONFIG(const std::vector<uint8_t>& data);

  [[nodiscard]] std::optional<BNETProtocol::WhoisInfo> PARSE_WHOIS_INFO(const std::string& message, const PvPGNLocale realmLocale);

  // send functions

  [[nodiscard]] std::vector<uint8_t> SEND_PROTOCOL_INITIALIZE_SELECTOR();
  [[nodiscard]] std::vector<uint8_t> SEND_SID_ZERO();
  [[nodiscard]] std::vector<uint8_t> SEND_SID_STOPADV();
  [[nodiscard]] std::vector<uint8_t> SEND_SID_GETADVLISTEX();
  [[nodiscard]] std::vector<uint8_t> SEND_SID_GETADVLISTEX(const std::string& gameName, const std::string& gamePassword);
  [[nodiscard]] std::vector<uint8_t> SEND_SID_ENTERCHAT();
  [[nodiscard]] std::vector<uint8_t> SEND_SID_JOINCHANNEL(const std::string& channel);
  [[nodiscard]] std::vector<uint8_t> SEND_SID_CHAT_PUBLIC(const std::string& message);
  [[nodiscard]] std::vector<uint8_t> SEND_SID_CHAT_WHISPER(const std::string& message, const std::string& user);
  [[nodiscard]] std::vector<uint8_t> SEND_SID_CHAT_PUBLIC(const std::vector<uint8_t>& message);
  [[nodiscard]] std::vector<uint8_t> SEND_SID_CHAT_WHISPER(const std::vector<uint8_t>& message, const std::vector<uint8_t>& user);
  [[nodiscard]] std::vector<uint8_t> SEND_SID_CHECKAD();
  [[nodiscard]] std::vector<uint8_t> SEND_SID_PUBLICHOST(const std::array<uint8_t, 4> address, uint16_t port);
  [[nodiscard]] std::vector<uint8_t> SEND_SID_STARTADVEX3(uint8_t state, const uint32_t mapGameType, const uint32_t gameFlags, const std::array<uint8_t, 2>& mapWidth, const std::array<uint8_t, 2>& mapHeight, const std::string& gameName, const std::string& hostName, uint32_t upTime, const std::string& mapPath, const std::array<uint8_t, 4>& mapBlizzHash, const std::optional<std::array<uint8_t, 20>>& mapSHA1, uint32_t hostCounter, uint8_t maxSupportedSlots);
  [[nodiscard]] std::vector<uint8_t> SEND_SID_NOTIFYJOIN(const std::string& gameName);
  [[nodiscard]] std::vector<uint8_t> SEND_SID_PING(const std::array<uint8_t, 4>& pingValue);
  [[nodiscard]] std::vector<uint8_t> SEND_SID_LOGONRESPONSE(const std::vector<uint8_t>& clientToken, const std::vector<uint8_t>& serverToken, const std::vector<uint8_t>& passwordHash, const std::string& accountName);
  [[nodiscard]] std::vector<uint8_t> SEND_SID_NETGAMEPORT(uint16_t serverPort);
  [[nodiscard]] std::vector<uint8_t> SEND_SID_AUTH_INFO(const bool isExpansion, const Version& war3Version, uint32_t localeID, uint32_t languageID, const std::array<uint8_t, 4>& localeShort, const std::string& countryShort, const std::string& country);
  [[nodiscard]] std::vector<uint8_t> SEND_SID_AUTH_CHECK(const std::array<uint8_t, 4>& clientToken, const bool isExpansion, const std::array<uint8_t, 4>& exeVersion, const std::array<uint8_t, 4>& exeVersionHash, const std::vector<uint8_t>& keyInfoROC, const std::vector<uint8_t>& keyInfoTFT, const std::string& exeInfo, const std::string& keyOwnerName);
  [[nodiscard]] std::vector<uint8_t> SEND_SID_AUTH_ACCOUNTLOGON(const std::array<uint8_t, 32>& clientPublicKey, const std::string& accountName);
  [[nodiscard]] std::vector<uint8_t> SEND_SID_AUTH_ACCOUNTLOGONPROOF(const std::array<uint8_t, 20>& clientPasswordProof);
  [[nodiscard]] std::vector<uint8_t> SEND_SID_AUTH_ACCOUNTSIGNUP(const std::string& userName, const std::array<uint8_t, 20>& clientPasswordProof);
  [[nodiscard]] std::vector<uint8_t> SEND_SID_FRIENDLIST();
  [[nodiscard]] std::vector<uint8_t> SEND_SID_CLANMEMBERLIST();
};

#endif // AURA_BNETPROTOCOL_H_
