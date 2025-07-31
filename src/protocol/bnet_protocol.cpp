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

#include "bnet_protocol.h"

#include "../util.h"
#include "../socket.h"
#include "../game_stat.h"

#include <utility>

using namespace std;

namespace BNETProtocol
{
  ///////////////////////
  // RECEIVE FUNCTIONS //
  ///////////////////////

  bool RECEIVE_SID_ZERO(const string_view packet)
  {
    // DEBUG_Print( "RECEIVED SID_ZERO" );
    // DEBUG_Print( packet );

    // 2 bytes					-> Header
    // 2 bytes					-> Length

    return ValidateLength(packet);
  }

  vector<NetworkGameInfo> RECEIVE_SID_GETADVLISTEX(const Version& war3Version, string_view data)
  {
    vector<NetworkGameInfo> games;
    size_t byteCount = data.size();
    if (!ValidateLength(data) || byteCount < 8) {
      //Print("[BNETPROTO] Got invalid game list");
      return games;
    }

    const uint32_t totalGames = ByteArrayToUInt32LE(data, 4);
    if (totalGames == 0 || totalGames > 100) {
      //Print(Concat("[BNETPROTO] Got list of ", to_string(totalGames), " games"));
      return games;
    }

    games.resize(totalGames);
    uint32_t gameIndex = 0;
    size_t cursor = 8;

    while (gameIndex < totalGames) {
      if (byteCount < cursor + 33) {
        break;
      }
      cursor += 4;

      NetworkGameInfo& gameInfo = games[gameIndex];
      gameInfo.SetGameType(ByteArrayToUInt16LE(data, cursor));
      cursor += 2;
      cursor += 4; // <0x01 0x00 0x02 0x00>

      uint16_t port = ByteArrayToUInt16BE(data, cursor);
      cursor += 2;
      sockaddr_storage address = IPv4BytesToAddress(reinterpret_cast<const uint8_t*>(data.data() + cursor));
      cursor += 4;
      SetAddressPort(&address, port);
      gameInfo.SetAddress(address);

      cursor += 4; // zeroes
      cursor += 4; // zeroes

      gameInfo.SetStatus(ByteArrayToUInt32LE(data, cursor));
      cursor += 4;

      cursor += 4; // <0x2b 0x00 0x00 0x00>

      string_view gameName = ExtractStringView<OOBPolicy::kUnsafe, NullTerminatorPolicy::kRequired, StringEncoding::kNone>(data, cursor, 255);
      if (gameName.empty()) {
        //Print(Concat("[BNETPROTO] Game name was empty #", to_string(gameIndex + 1), " at " + gameInfo.GetHostDetails()));
        return games;
      }
      //Print(Concat("[BNETPROTO] Got game #", to_string(gameIndex + 1), " name ", SanitizeWrapUTF8(gameName), " at ", gameInfo.GetHostDetails()));
      gameInfo.SetGameName(gameName);
      cursor += gameName.size() + 1;

      string_view passWord = ExtractStringView<OOBPolicy::kCheck, NullTerminatorPolicy::kRequired, StringEncoding::kNone>(data, cursor, 255);
      gameInfo.SetPassword(passWord);
      cursor += passWord.size() + 1;

      string_view gameStat = ExtractStringView<OOBPolicy::kCheck, NullTerminatorPolicy::kRequired, StringEncoding::kNone>(data, cursor, 255);
      if (gameStat.empty()) {
        //Print("[BNETPROTO] Game info was empty");
        return games;
      }
      gameInfo.SetBNETGameInfo(gameStat, war3Version);
      cursor += gameStat.size() + 1;

      gameIndex++;
    }

    return games;
  }

  EnterChatResultView RECEIVE_SID_ENTERCHAT(const string_view packet)
  {
    // DEBUG_Print( "RECEIVED SID_ENTERCHAT" );
    // DEBUG_Print( packet );

    // 2 bytes					-> Header
    // 2 bytes					-> Length
    // null terminated string	-> UniqueName

    if (!ValidateLength(packet) || packet.size() < 5) {
      return EnterChatResultView();
    }
    string_view uniqueName = ExtractUTF8View(packet, 4, 0);
    if (uniqueName.empty() || HasUnsafeUTF8CodePoints(uniqueName)) {
      return EnterChatResultView();
    }
    return EnterChatResultView(true, uniqueName);
  }

  IncomingChatResultView RECEIVE_SID_CHATEVENT(const string_view packet)
  {
    // DEBUG_Print( "RECEIVED SID_CHATEVENT" );
    // DEBUG_Print( packet );

    // 2 bytes					-> Header
    // 2 bytes					-> Length
    // 4 bytes					-> EventID
    // 4 bytes					-> ???
    // 4 bytes					-> Ping
    // 12 bytes					-> ???
    // null terminated string	-> User
    // null terminated string	-> Message

    if (!ValidateLength(packet) || packet.size() < 29) {
      return IncomingChatResultView();
    }
    const uint32_t eventID = ByteArrayToUInt32LE(packet, 4);
    string_view userName = ExtractUTF8View(packet, 28, MAX_PLAYER_NAME_SIZE);
    if (userName.empty() || HasUnsafeUTF8CodePoints(userName)) {
      return IncomingChatResultView();
    }
    if (packet.size() <= 29 + userName.size()) {
      return IncomingChatResultView();
    }
    string_view message = ExtractUTF8View(packet, 29 + userName.size(), 256);
    if (message.empty() || HasUnsafeUTF8CodePoints(message)) {
      return IncomingChatResultView();
    }
    return IncomingChatResultView(true, eventID, userName, message);
  }

  bool RECEIVE_SID_CHECKAD(const string_view packet)
  {
    // DEBUG_Print( "RECEIVED SID_CHECKAD" );
    // DEBUG_Print( packet );

    // 2 bytes					-> Header
    // 2 bytes					-> Length

    return ValidateLength(packet);
  }

  bool RECEIVE_SID_STARTADVEX3(const string_view packet)
  {
    // DEBUG_Print( "RECEIVED SID_STARTADVEX3" );
    // DEBUG_Print( packet );

    // 2 bytes					-> Header
    // 2 bytes					-> Length
    // 4 bytes					-> Status

    if (ValidateLength(packet) && packet.size() >= 8)
    {
      if (ByteArrayToUInt32LE(packet, 4) == 0)
        return true;
    }

    return false;
  }

  uint32_t RECEIVE_SID_PING(const string_view packet)
  {
    // DEBUG_Print( "RECEIVED SID_PING" );
    // DEBUG_Print( packet );

    // 2 bytes					-> Header
    // 2 bytes					-> Length
    // 4 bytes					-> Ping

    if (ValidateLength(packet) && packet.size() >= 8) {
      return ByteArrayToUInt32LE(packet, 4);
    }
    return 0;
  }

  AuthInfoResult RECEIVE_SID_AUTH_INFO(const string_view packet)
  {
    // DEBUG_Print( "RECEIVED SID_AUTH_INFO" );
    // DEBUG_Print( packet );

    // 2 bytes				    -> Header
    // 2 bytes				    -> Length
    // 4 bytes				    -> LogonType
    // 4 bytes				    -> ServerToken
    // 4 bytes				    -> ???
    // 8 bytes				    -> MPQFileTime
    // null terminated string	    -> IX86VerFileName
    // null terminated string	    -> ValueStringFormula

    if (!ValidateLength(packet) || packet.size() < 25) {
      return AuthInfoResult();
    }
    string_view::size_type fileNameEndPos = packet.find('\x00', 24);
    if (fileNameEndPos == string_view::npos || fileNameEndPos > 0xFFFF) {
      return AuthInfoResult();
    }
    string_view::size_type valueStringFormulaStartPos = fileNameEndPos + 1;
    if (valueStringFormulaStartPos >= packet.size()) {
      return AuthInfoResult();
    }
    string_view::size_type valueStringFormulaEndPos = packet.find('\x00', valueStringFormulaStartPos);
    if (valueStringFormulaEndPos == string_view::npos) {
      return AuthInfoResult();
    }
    return AuthInfoResult(
      true,
      ByteArrayToUInt32LE(packet, 4),
      ByteArrayToUInt32LE(packet, 8),
      ByteArrayToUInt64LE(packet, 16),
      packet.substr(24, fileNameEndPos - 24),
      packet.substr(valueStringFormulaStartPos, valueStringFormulaEndPos - valueStringFormulaStartPos)
    );
  }

  AuthCheckResultView RECEIVE_SID_AUTH_CHECK(const string_view packet)
  {
    // DEBUG_Print( "RECEIVED SID_AUTH_CHECK" );
    // DEBUG_Print( packet );

    // 2 bytes					-> Header
    // 2 bytes					-> Length
    // 4 bytes					-> KeyState
    // null terminated string	    -> KeyStateDescription

    if (!ValidateLength(packet) || packet.size() < 9) {
      return AuthCheckResultView();
    }
    string_view description = ExtractUTF8View(packet, 8, 0);
    if (HasUnsafeUTF8CodePoints(description)) {
      description.remove_prefix(description.size());
    }
    return AuthCheckResultView(ByteArrayToUInt32LE(packet, 4), description);
  }

  AuthLoginResultView RECEIVE_SID_AUTH_ACCOUNTLOGON(const string_view packet)
  {
    // DEBUG_Print( "RECEIVED SID_AUTH_ACCOUNTLOGON" );
    // DEBUG_Print( packet );

    // 2 bytes					-> Header
    // 2 bytes					-> Length
    // 4 bytes					-> Status
    // if( Status == 0 )
    //		32 bytes			-> Salt
    //		32 bytes			-> ServerPublicKey

    if (ValidateLength(packet) && packet.size() >= 8) {
      if (ByteArrayToUInt32LE(packet, 4) == 0 && packet.size() >= 72) {
        return AuthLoginResultView(true, packet.substr(8, 32), packet.substr(40, 32));
      }
    }

    return AuthLoginResultView();
  }

  bool RECEIVE_SID_AUTH_ACCOUNTLOGONPROOF(const string_view packet)
  {
    // DEBUG_Print( "RECEIVED SID_AUTH_ACCOUNTLOGONPROOF" );
    // DEBUG_Print( packet );

    // 2 bytes					-> Header
    // 2 bytes					-> Length
    // 4 bytes					-> Status

    if (ValidateLength(packet) && packet.size() >= 8)
    {
      uint32_t status = ByteArrayToUInt32LE(packet, 4);

      if (status == 0 || status == 0xE) {
        // OK: 0x0, EMAIL: 0xE
        return true;
      }
    }

    return false;
  }

  bool RECEIVE_SID_AUTH_ACCOUNTSIGNUP(const string_view packet)
  {
    // DEBUG_Print( "RECEIVED SID_AUTH_ACCOUNTSIGNUP" );
    // DEBUG_Print( packet );

    // 2 bytes					-> Header
    // 2 bytes					-> Length
    // 4 bytes					-> Status

    if (ValidateLength(packet) && packet.size() >= 8)
    {
      uint32_t status = ByteArrayToUInt32LE(packet, 4);

      if (status == 0x1) {
        return true;
      }
    }

    return false;
  }

  vector<string> RECEIVE_SID_FRIENDLIST(const string_view packet)
  {
    // DEBUG_Print( "RECEIVED SID_FRIENDSLIST" );
    // DEBUG_Print( packet );

    // 2 bytes					-> Header
    // 2 bytes					-> Length
    // 1 byte					    -> Total
    // for( 1 .. Total )
    //		null term string	-> Account
    //		1 byte				-> Status
    //		1 byte				-> Area
    //		4 bytes				-> ???
    //		null term string	-> Location

    vector<string> friends;

    if (ValidateLength(packet) && packet.size() >= 5)
    {
      size_t   i     = 5;
      uint8_t  Total = GetByteAt(packet, 4);

      while (Total > 0)
      {
        --Total;

        if (packet.size() < i + 1)
          break;

        string_view account = ExtractUTF8View(packet, i, MAX_PLAYER_NAME_SIZE);
        if (account.empty() || HasUnsafeUTF8CodePoints(account))
          break;

        i += account.size() + 1;

        if (packet.size() < i + 7)
          break;

        i += 6;

        string_view::size_type nullIndex = packet.find('\x00', i);
        if (nullIndex == string_view::npos) {
          return vector<string>();
        }

        i = nullIndex + 1;

        friends.emplace_back(account);
      }
    }

    return friends;
  }

  vector<string> RECEIVE_SID_CLANMEMBERLIST(const string_view packet)
  {
    // DEBUG_Print( "RECEIVED SID_CLANMEMBERLIST" );
    // DEBUG_Print( packet );

    // 2 bytes					-> Header
    // 2 bytes					-> Length
    // 4 bytes					-> ???
    // 1 byte					    -> Total
    // for( 1 .. Total )
    //		null term string	-> Name
    //		1 byte				-> Rank
    //		1 byte				-> Status
    //		null term string	-> Location

    vector<string> clanList;

    if (ValidateLength(packet) && packet.size() >= 9)
    {
      size_t   i     = 9;
      uint8_t  Total = GetByteAt(packet, 8);

      while (Total > 0)
      {
        --Total;

        if (packet.size() < i + 1)
          break;

        string_view name = ExtractUTF8View(packet, i, MAX_PLAYER_NAME_SIZE);
        if (name.empty() || HasUnsafeUTF8CodePoints(name))
          break;

        i += name.size() + 1;

        if (packet.size() < i + 3)
          break;

        i += 2;

        // in the original VB source the location string is read but discarded, so that's what I do here
        string_view::size_type nullIndex = packet.find('\x00', i);
        if (nullIndex == string_view::npos) {
          return vector<string>();
        }

        i = nullIndex + 1;
        clanList.emplace_back(name);
      }
    }

    return clanList;
  }

  optional<CConfig> RECEIVE_HOSTED_GAME_CONFIG(const string_view packet)
  {
    optional<CConfig> gameConfig;
    if (packet.size() < 64 || !ValidateLength(packet)) {
      Print("[BNETPROTO] RECEIVE_HOSTED_GAME_CONFIG bad packet size");
      return gameConfig;
    }
    const uint16_t slotInfoSize = ByteArrayToUInt16LE(packet, 44);
    if (static_cast<uint16_t>(packet.size()) < 44u + slotInfoSize) {
      Print("[BNETPROTO] RECEIVE_HOSTED_GAME_CONFIG bad slot packet size");
      return gameConfig;
    }
    const uint8_t maxSlots = GetByteAt(packet, 46);
    if (slotInfoSize != static_cast<uint16_t>(maxSlots) * 9 + 7) {
      Print("[BNETPROTO] RECEIVE_HOSTED_GAME_CONFIG bad slot count");
      return gameConfig;
    }
    gameConfig.emplace();
    gameConfig->SetUint32("rehost.unknown_1", ByteArrayToUInt32LE(packet, 4));
    gameConfig->SetUint32("rehost.unknown_2", ByteArrayToUInt32LE(packet, 8));

    vector<uint8_t> mapSize = vector<uint8_t>(4, 0);
    vector<uint8_t> mapCRC32 = vector<uint8_t>(4, 0);
    vector<uint8_t> mapWeakHash = vector<uint8_t>(4, 0);
    vector<uint8_t> rehostSeed = vector<uint8_t>(4, 0);
    vector<uint8_t> mapSHA1 = vector<uint8_t>(20, 0);

    copy_n(packet.data() + 12, 4, mapSize.begin());
    gameConfig->SetUint8Vector("map.size", mapSize);

    copy_n(packet.data() + 16, 4, mapCRC32.begin());
    gameConfig->SetUint8Vector("map.file_hash.crc32", mapCRC32);

    copy_n(packet.data() + 20, 4, mapWeakHash.begin());
    gameConfig->SetUint8Vector("map.scripts_hash.blizz", mapWeakHash);

    copy_n(packet.data() + 24, 20, mapSHA1.begin());
    gameConfig->SetUint8Vector("map.scripts_hash.sha1", mapSHA1);

    size_t cursor = 44u;

    // skip 3 bytes - slot info size, max slots
    cursor += 3u;

    uint8_t slotIndex = 0;
    while (slotIndex < maxSlots) {
      vector<uint8_t> slotInfo = vector<uint8_t>(9, 0);
      copy_n(packet.data() + cursor, 9, slotInfo.begin());
      gameConfig->SetUint8Vector("map.slot_" + ToDecString(slotIndex + 1), slotInfo);
      ++slotIndex;
      cursor += 9;
    }

    copy_n(packet.data() + cursor, 4, rehostSeed.begin());
    cursor += 4;
    gameConfig->SetUint8Vector("rehost.game.seed", rehostSeed);

    const uint8_t mapLayout = GetByteAt(packet, cursor);
    if (mapLayout > 3) {
      // It seems like this is sometimes 4?
      Print("[BNETPROTO] Map layout is unexpectedly " + ToDecString(mapLayout));
    }
    gameConfig->SetUint8("rehost.game.layout", mapLayout);
    cursor++;

    const uint8_t mapNumPlayers = GetByteAt(packet, cursor);
    gameConfig->SetUint8("map.num_players", mapNumPlayers);
    cursor++;

    string_view mapClientPath = ExtractUTF8View(packet, cursor, 255);
    if (mapClientPath.empty() || HasUnsafeUTF8CodePoints(mapClientPath)) {
      return gameConfig;
    }

    gameConfig->SetString("map.path", mapClientPath);

    cursor += mapClientPath.size() + 1u;
    if (cursor >= packet.size()) {
      Print("[BNETPROTO] Missing hosted game name, etc.");
      return gameConfig;
    }

    gameConfig->SetBool("rehost.game.private", GetByteAt(packet, cursor));
    cursor += 1;

    if (cursor < packet.size()) {
      string_view gameName = ExtractUTF8View(packet, cursor, MAX_GAME_NAME_SIZE);
      if (gameName.empty() || HasUnsafeUTF8CodePoints(gameName)) {
        Print("[BNETPROTO] Game name is not valid UTF8.");
        return gameConfig;
      }
      gameConfig->SetString("rehost.game.name", gameName);
    }

    // TODO: RECEIVE_HOSTED_GAME_CONFIG game flags
    // Game flags are the first 4 bytes of the decoded stat string
    // In a PvPGN server, the stat string is stored (encoded) at t_game.info + 9
    // See src/bnetd/game_conv.cpp for decoding routines.
    //
    // Meanwhile, this should be enough as a PoC.
    if (maxSlots > mapNumPlayers) {
      gameConfig->SetString("map.observers", "referees");
    } else {
      gameConfig->SetString("map.observers", "none");
    }

    // In a PVPGN server, stored at t_game.mapsize_x
    // Meanwhile, use GProxy values as placeholder
    vector<uint8_t> placeholderSize = {192, 7};
    gameConfig->SetUint8Vector("map.width", placeholderSize);

    // In a PVPGN server, stored at t_game.mapsize_y
    // Meanwhile, use GProxy values as placeholder
    gameConfig->SetUint8Vector("map.height", placeholderSize);
    return gameConfig;
  }

  optional<BNETProtocol::WhoisInfo> PARSE_WHOIS_INFO(string_view message, const PvPGNLocale realmLocale)
  {
    optional<BNETProtocol::WhoisInfo> result;
    const string::size_type spIndex = message.find(' ');
    const string::size_type msgSize = message.size();
    if (spIndex == string::npos) {
      return result;
    }
    switch (realmLocale) {
      case PvPGNLocale::kENUS: {
        constexpr static string::size_type SIZE_CLIENT_WC3 = GetStringLength(BNETProtocol::WhoisTexts::enUS::CLIENT_WC3);
        constexpr static string::size_type SIZE_CLIENT_TFT = GetStringLength(BNETProtocol::WhoisTexts::enUS::CLIENT_TFT);
        constexpr static string::size_type SIZE_LOCATION_PREFIX = GetStringLength(BNETProtocol::WhoisTexts::enUS::LOCATION_PREFIX);
        constexpr static string::size_type SIZE_LOCATION_SUFFIX = GetStringLength(BNETProtocol::WhoisTexts::enUS::LOCATION_SUFFIX);
        constexpr static string::size_type SIZE_LOCATION_PRIVATE_GAME = GetStringLength(BNETProtocol::WhoisTexts::enUS::LOCATION_PRIVATE_GAME);
        constexpr static string::size_type SIZE_LOCATION_PUBLIC_GAME = GetStringLength(BNETProtocol::WhoisTexts::enUS::LOCATION_PUBLIC_GAME);
        constexpr static string::size_type SIZE_LOCATION_CHAT = GetStringLength(BNETProtocol::WhoisTexts::enUS::LOCATION_CHAT);
        constexpr static string::size_type SIZE_LAST_SEEN_PREFIX = GetStringLength(BNETProtocol::WhoisTexts::enUS::LAST_SEEN_PREFIX);

        if (msgSize > spIndex + SIZE_CLIENT_WC3 &&
            message.substr(spIndex + 1, SIZE_CLIENT_WC3) == BNETProtocol::WhoisTexts::enUS::CLIENT_WC3) {
          string::size_type tagEndPosNext = spIndex + 1 + SIZE_CLIENT_WC3;
          uint8_t clientTag = BNETProtocol::WhoisInfoClientTag::ROC;
          if ((msgSize > spIndex + SIZE_CLIENT_WC3 + SIZE_CLIENT_TFT) &&
              (message.substr(tagEndPosNext, SIZE_CLIENT_TFT) == BNETProtocol::WhoisTexts::enUS::CLIENT_TFT))
          {
            clientTag = BNETProtocol::WhoisInfoClientTag::TFT;
            tagEndPosNext += SIZE_CLIENT_TFT;
          }
          if (msgSize == tagEndPosNext + 1 && message[tagEndPosNext] == '.') {
            // {} is using Warcraft III.
            // {} is using Warcraft III Frozen Throne.
            result.emplace(BNETProtocol::WhoisInfoStatus::ONLINE, BNETProtocol::WhoisInfoClientTag::TFT);
          } else if (
              (msgSize >= tagEndPosNext + SIZE_LOCATION_PREFIX) &&
              (message.substr(
                tagEndPosNext,
                SIZE_LOCATION_PREFIX
              ) == BNETProtocol::WhoisTexts::enUS::LOCATION_PREFIX) &&
              (message.substr(msgSize - 2) == BNETProtocol::WhoisTexts::enUS::LOCATION_SUFFIX)
          ) {
            if (
              msgSize >= tagEndPosNext + (
                SIZE_LOCATION_PREFIX +
                SIZE_LOCATION_PRIVATE_GAME +
                SIZE_LOCATION_SUFFIX
              ) && (
                message.substr(
                  tagEndPosNext + SIZE_LOCATION_PREFIX,
                  SIZE_LOCATION_PRIVATE_GAME
                ) == BNETProtocol::WhoisTexts::enUS::LOCATION_PRIVATE_GAME
              )
            ) {
              string_view gameName = message.substr(
                tagEndPosNext + (
                  SIZE_LOCATION_PREFIX +
                  SIZE_LOCATION_PRIVATE_GAME
                ), message.size() - (
                  tagEndPosNext + (
                    SIZE_LOCATION_SUFFIX +
                    SIZE_LOCATION_PREFIX +
                    SIZE_LOCATION_PRIVATE_GAME
                  )
                )
              );
              result.emplace(BNETProtocol::WhoisInfoStatus::ONLINE_IN_GAME_PRIVATE, clientTag, message.substr(0, spIndex), gameName);
            } else if (
              msgSize >= tagEndPosNext + (
                SIZE_LOCATION_PREFIX +
                SIZE_LOCATION_PUBLIC_GAME +
                SIZE_LOCATION_SUFFIX
              ) &&
              message.substr(
                tagEndPosNext + SIZE_LOCATION_PREFIX,
                SIZE_LOCATION_PUBLIC_GAME
              ) == BNETProtocol::WhoisTexts::enUS::LOCATION_PUBLIC_GAME
            ) {
              string_view gameName = message.substr(
                tagEndPosNext + (
                  SIZE_LOCATION_PREFIX +
                  SIZE_LOCATION_PUBLIC_GAME
                ), message.size() - (
                  tagEndPosNext + (
                    SIZE_LOCATION_SUFFIX +
                    SIZE_LOCATION_PREFIX +
                    SIZE_LOCATION_PUBLIC_GAME
                  )
                )
              );
              result.emplace(BNETProtocol::WhoisInfoStatus::ONLINE_IN_GAME_PUBLIC, clientTag, message.substr(0, spIndex), gameName);
            } else if (
              msgSize >= tagEndPosNext + (
                SIZE_LOCATION_PREFIX +
                SIZE_LOCATION_CHAT +
                SIZE_LOCATION_SUFFIX
              ) &&
              message.substr(
                tagEndPosNext + SIZE_LOCATION_PREFIX,
                SIZE_LOCATION_CHAT
              ) == BNETProtocol::WhoisTexts::enUS::LOCATION_CHAT
            ) {
              string_view gameName = message.substr(
                tagEndPosNext + (
                  SIZE_LOCATION_PREFIX +
                  SIZE_LOCATION_CHAT
                ), message.size() - (
                  tagEndPosNext + (
                    SIZE_LOCATION_SUFFIX +
                    SIZE_LOCATION_PREFIX +
                    SIZE_LOCATION_CHAT
                  )
                )
              );
              result.emplace(BNETProtocol::WhoisInfoStatus::ONLINE_IN_CHAT, clientTag, message.substr(0, spIndex), gameName);
            }
          }
        } else if (
          msgSize >= SIZE_LAST_SEEN_PREFIX &&
          message.substr(
            0,
            SIZE_LAST_SEEN_PREFIX
          ) == BNETProtocol::WhoisTexts::enUS::LAST_SEEN_PREFIX
        ) {
          result.emplace(BNETProtocol::WhoisInfoStatus::OFFLINE_LAST_SEEN);
        } else if (message == BNETProtocol::WhoisTexts::enUS::USER_UNKNOWN) {
          result.emplace(BNETProtocol::WhoisInfoStatus::UNKNOWN);
        } else if (message == BNETProtocol::WhoisTexts::enUS::USER_OFFLINE) {
          result.emplace(BNETProtocol::WhoisInfoStatus::OFFLINE_GENERIC);
        }
        break;
      }
      case PvPGNLocale::kESES: {
        constexpr static string::size_type SIZE_CLIENT_WC3 = GetStringLength(BNETProtocol::WhoisTexts::esES::CLIENT_WC3);
        constexpr static string::size_type SIZE_CLIENT_TFT = GetStringLength(BNETProtocol::WhoisTexts::esES::CLIENT_TFT);
        constexpr static string::size_type SIZE_LOCATION_PREFIX = GetStringLength(BNETProtocol::WhoisTexts::esES::LOCATION_PREFIX);
        constexpr static string::size_type SIZE_LOCATION_SUFFIX = GetStringLength(BNETProtocol::WhoisTexts::esES::LOCATION_SUFFIX);
        constexpr static string::size_type SIZE_LOCATION_PRIVATE_GAME = GetStringLength(BNETProtocol::WhoisTexts::esES::LOCATION_PRIVATE_GAME);
        constexpr static string::size_type SIZE_LOCATION_PUBLIC_GAME = GetStringLength(BNETProtocol::WhoisTexts::esES::LOCATION_PUBLIC_GAME);
        constexpr static string::size_type SIZE_LOCATION_CHAT = GetStringLength(BNETProtocol::WhoisTexts::esES::LOCATION_CHAT);
        constexpr static string::size_type SIZE_LAST_SEEN_PREFIX = GetStringLength(BNETProtocol::WhoisTexts::esES::LAST_SEEN_PREFIX);

        if (msgSize > spIndex + SIZE_CLIENT_WC3 &&
            message.substr(spIndex + 1, SIZE_CLIENT_WC3) == BNETProtocol::WhoisTexts::esES::CLIENT_WC3) {
          string::size_type tagEndPosNext = spIndex + 1 + SIZE_CLIENT_WC3;
          uint8_t clientTag = BNETProtocol::WhoisInfoClientTag::ROC;
          if ((msgSize >= spIndex + SIZE_CLIENT_WC3 + SIZE_CLIENT_TFT) &&
              (message.substr(tagEndPosNext, SIZE_CLIENT_TFT) == BNETProtocol::WhoisTexts::esES::CLIENT_TFT))
          {
            clientTag = BNETProtocol::WhoisInfoClientTag::TFT;
            tagEndPosNext += SIZE_CLIENT_TFT;
          }
          if (msgSize == tagEndPosNext + 1 && message[tagEndPosNext] == '.') {
            // {} is using Warcraft III.
            // {} is using Warcraft III Frozen Throne.
            result.emplace(BNETProtocol::WhoisInfoStatus::ONLINE, BNETProtocol::WhoisInfoClientTag::TFT);
          } else if (
              (msgSize >= tagEndPosNext + SIZE_LOCATION_PREFIX) &&
              (message.substr(
                tagEndPosNext,
                SIZE_LOCATION_PREFIX
              ) == BNETProtocol::WhoisTexts::esES::LOCATION_PREFIX) &&
              (message.substr(msgSize - 2) == BNETProtocol::WhoisTexts::esES::LOCATION_SUFFIX)
          ) {
            if (
              msgSize >= tagEndPosNext + (
                SIZE_LOCATION_PREFIX +
                SIZE_LOCATION_PRIVATE_GAME +
                SIZE_LOCATION_SUFFIX
              ) && (
                message.substr(
                  tagEndPosNext + SIZE_LOCATION_PREFIX,
                  SIZE_LOCATION_PRIVATE_GAME
                ) == BNETProtocol::WhoisTexts::esES::LOCATION_PRIVATE_GAME
              )
            ) {
              string_view gameName = message.substr(
                tagEndPosNext + (
                  SIZE_LOCATION_PREFIX +
                  SIZE_LOCATION_PRIVATE_GAME
                ), message.size() - (
                  tagEndPosNext + (
                    SIZE_LOCATION_SUFFIX +
                    SIZE_LOCATION_PREFIX +
                    SIZE_LOCATION_PRIVATE_GAME
                  )
                )
              );
              result.emplace(BNETProtocol::WhoisInfoStatus::ONLINE_IN_GAME_PRIVATE, clientTag, message.substr(0, spIndex), gameName);
            } else if (
              msgSize >= tagEndPosNext + (
                SIZE_LOCATION_PREFIX +
                SIZE_LOCATION_PUBLIC_GAME +
                SIZE_LOCATION_SUFFIX
              ) &&
              message.substr(
                tagEndPosNext + SIZE_LOCATION_PREFIX,
                SIZE_LOCATION_PUBLIC_GAME
              ) == BNETProtocol::WhoisTexts::esES::LOCATION_PUBLIC_GAME
            ) {
              string_view gameName = message.substr(
                tagEndPosNext + (
                  SIZE_LOCATION_PREFIX +
                  SIZE_LOCATION_PUBLIC_GAME
                ), message.size() - (
                  tagEndPosNext + (
                    SIZE_LOCATION_SUFFIX +
                    SIZE_LOCATION_PREFIX +
                    SIZE_LOCATION_PUBLIC_GAME
                  )
                )
              );
              result.emplace(BNETProtocol::WhoisInfoStatus::ONLINE_IN_GAME_PUBLIC, clientTag, message.substr(0, spIndex), gameName);
            } else if (
              msgSize >= tagEndPosNext + (
                SIZE_LOCATION_PREFIX +
                SIZE_LOCATION_CHAT +
                SIZE_LOCATION_SUFFIX
              ) &&
              message.substr(
                tagEndPosNext + SIZE_LOCATION_PREFIX,
                SIZE_LOCATION_CHAT
              ) == BNETProtocol::WhoisTexts::esES::LOCATION_CHAT
            ) {
              string_view gameName = message.substr(
                tagEndPosNext + (
                  SIZE_LOCATION_PREFIX +
                  SIZE_LOCATION_CHAT
                ), message.size() - (
                  tagEndPosNext + (
                    SIZE_LOCATION_SUFFIX +
                    SIZE_LOCATION_PREFIX +
                    SIZE_LOCATION_CHAT
                  )
                )
              );
              result.emplace(BNETProtocol::WhoisInfoStatus::ONLINE_IN_CHAT, clientTag, message.substr(0, spIndex), gameName);
            }
          }
        } else if (
          msgSize >= SIZE_LAST_SEEN_PREFIX &&
          message.substr(
            0,
            SIZE_LAST_SEEN_PREFIX
          ) == BNETProtocol::WhoisTexts::esES::LAST_SEEN_PREFIX
        ) {
          result.emplace(BNETProtocol::WhoisInfoStatus::OFFLINE_LAST_SEEN);
        } else if (message == BNETProtocol::WhoisTexts::esES::USER_UNKNOWN) {
          result.emplace(BNETProtocol::WhoisInfoStatus::UNKNOWN);
        } else if (message == BNETProtocol::WhoisTexts::esES::USER_OFFLINE) {
          result.emplace(BNETProtocol::WhoisInfoStatus::OFFLINE_GENERIC);
        }
        break;
      }
      case PvPGNLocale::kDEDE: {
        constexpr static string::size_type SIZE_CLIENT_WC3 = GetStringLength(BNETProtocol::WhoisTexts::deDE::CLIENT_WC3);
        constexpr static string::size_type SIZE_CLIENT_TFT = GetStringLength(BNETProtocol::WhoisTexts::deDE::CLIENT_TFT);
        constexpr static string::size_type SIZE_LOCATION_PREFIX = GetStringLength(BNETProtocol::WhoisTexts::deDE::LOCATION_PREFIX);
        constexpr static string::size_type SIZE_LOCATION_SUFFIX = GetStringLength(BNETProtocol::WhoisTexts::deDE::LOCATION_SUFFIX);
        constexpr static string::size_type SIZE_LOCATION_PRIVATE_GAME = GetStringLength(BNETProtocol::WhoisTexts::deDE::LOCATION_PRIVATE_GAME);
        constexpr static string::size_type SIZE_LOCATION_PUBLIC_GAME = GetStringLength(BNETProtocol::WhoisTexts::deDE::LOCATION_PUBLIC_GAME);
        constexpr static string::size_type SIZE_LOCATION_CHAT = GetStringLength(BNETProtocol::WhoisTexts::deDE::LOCATION_CHAT);
        constexpr static string::size_type SIZE_LAST_SEEN_PREFIX = GetStringLength(BNETProtocol::WhoisTexts::deDE::LAST_SEEN_PREFIX);

        if (msgSize > spIndex + SIZE_CLIENT_WC3 &&
            message.substr(spIndex + 1, SIZE_CLIENT_WC3) == BNETProtocol::WhoisTexts::deDE::CLIENT_WC3) {
          string::size_type tagEndPosNext = spIndex + 1 + SIZE_CLIENT_WC3;
          uint8_t clientTag = BNETProtocol::WhoisInfoClientTag::ROC;
          if ((msgSize >= spIndex + SIZE_CLIENT_WC3 + SIZE_CLIENT_TFT) &&
              (message.substr(tagEndPosNext, SIZE_CLIENT_TFT) == BNETProtocol::WhoisTexts::deDE::CLIENT_TFT))
          {
            clientTag = BNETProtocol::WhoisInfoClientTag::TFT;
            tagEndPosNext += SIZE_CLIENT_TFT;
          }
          if (msgSize == tagEndPosNext + 1 && message[tagEndPosNext] == '.') {
            // {} is using Warcraft III.
            // {} is using Warcraft III Frozen Throne.
            result.emplace(BNETProtocol::WhoisInfoStatus::ONLINE, BNETProtocol::WhoisInfoClientTag::TFT);
          } else if (
              (msgSize >= tagEndPosNext + SIZE_LOCATION_PREFIX) &&
              (message.substr(
                tagEndPosNext,
                SIZE_LOCATION_PREFIX
              ) == BNETProtocol::WhoisTexts::deDE::LOCATION_PREFIX) &&
              (message.substr(msgSize - 2) == BNETProtocol::WhoisTexts::deDE::LOCATION_SUFFIX)
          ) {
            if (
              msgSize >= tagEndPosNext + (
                SIZE_LOCATION_PREFIX +
                SIZE_LOCATION_PRIVATE_GAME +
                SIZE_LOCATION_SUFFIX
              ) && (
                message.substr(
                  tagEndPosNext + SIZE_LOCATION_PREFIX,
                  SIZE_LOCATION_PRIVATE_GAME
                ) == BNETProtocol::WhoisTexts::deDE::LOCATION_PRIVATE_GAME
              )
            ) {
              string_view gameName = message.substr(
                tagEndPosNext + (
                  SIZE_LOCATION_PREFIX +
                  SIZE_LOCATION_PRIVATE_GAME
                ), message.size() - (
                  tagEndPosNext + (
                    SIZE_LOCATION_SUFFIX +
                    SIZE_LOCATION_PREFIX +
                    SIZE_LOCATION_PRIVATE_GAME
                  )
                )
              );
              result.emplace(BNETProtocol::WhoisInfoStatus::ONLINE_IN_GAME_PRIVATE, clientTag, message.substr(0, spIndex), gameName);
            } else if (
              msgSize >= tagEndPosNext + (
                SIZE_LOCATION_PREFIX +
                SIZE_LOCATION_PUBLIC_GAME +
                SIZE_LOCATION_SUFFIX
              ) &&
              message.substr(
                tagEndPosNext + SIZE_LOCATION_PREFIX,
                SIZE_LOCATION_PUBLIC_GAME
              ) == BNETProtocol::WhoisTexts::deDE::LOCATION_PUBLIC_GAME
            ) {
              string_view gameName = message.substr(
                tagEndPosNext + (
                  SIZE_LOCATION_PREFIX +
                  SIZE_LOCATION_PUBLIC_GAME
                ), message.size() - (
                  tagEndPosNext + (
                    SIZE_LOCATION_SUFFIX +
                    SIZE_LOCATION_PREFIX +
                    SIZE_LOCATION_PUBLIC_GAME
                  )
                )
              );
              result.emplace(BNETProtocol::WhoisInfoStatus::ONLINE_IN_GAME_PUBLIC, clientTag, message.substr(0, spIndex), gameName);
            } else if (
              msgSize >= tagEndPosNext + (
                SIZE_LOCATION_PREFIX +
                SIZE_LOCATION_CHAT +
                SIZE_LOCATION_SUFFIX
              ) &&
              message.substr(
                tagEndPosNext + SIZE_LOCATION_PREFIX,
                SIZE_LOCATION_CHAT
              ) == BNETProtocol::WhoisTexts::deDE::LOCATION_CHAT
            ) {
              string_view gameName = message.substr(
                tagEndPosNext + (
                  SIZE_LOCATION_PREFIX +
                  SIZE_LOCATION_CHAT
                ), message.size() - (
                  tagEndPosNext + (
                    SIZE_LOCATION_SUFFIX +
                    SIZE_LOCATION_PREFIX +
                    SIZE_LOCATION_CHAT
                  )
                )
              );
              result.emplace(BNETProtocol::WhoisInfoStatus::ONLINE_IN_CHAT, clientTag, message.substr(0, spIndex), gameName);
            }
          }
        } else if (
          msgSize >= SIZE_LAST_SEEN_PREFIX &&
          message.substr(
            0,
            SIZE_LAST_SEEN_PREFIX
          ) == BNETProtocol::WhoisTexts::deDE::LAST_SEEN_PREFIX
        ) {
          result.emplace(BNETProtocol::WhoisInfoStatus::OFFLINE_LAST_SEEN);
        } else if (message == BNETProtocol::WhoisTexts::deDE::USER_UNKNOWN) {
          result.emplace(BNETProtocol::WhoisInfoStatus::UNKNOWN);
        } else if (message == BNETProtocol::WhoisTexts::deDE::USER_OFFLINE) {
          result.emplace(BNETProtocol::WhoisInfoStatus::OFFLINE_GENERIC);
        }
        break;
      }

      IGNORE_ENUM_LAST(PvPGNLocale)

      default:
        // FIXME: default case suppresses warnings for unhandled cases in BNETProtocol::PARSE_WHOIS_INFO
        break;
    }

    return result;
  }

  ////////////////////
  // SEND FUNCTIONS //
  ////////////////////

  vector<uint8_t> SEND_PROTOCOL_INITIALIZE_SELECTOR()
  {
    return vector<uint8_t>{1};
  }

  vector<uint8_t> SEND_SID_ZERO()
  {
    return vector<uint8_t>{BNETProtocol::Magic::BNET_HEADER, BNETProtocol::Magic::ZERO, 4, 0};
  }

  vector<uint8_t> SEND_SID_STOPADV()
  {
    return vector<uint8_t>{BNETProtocol::Magic::BNET_HEADER, BNETProtocol::Magic::STOPADV, 4, 0};
  }

  vector<uint8_t> SEND_SID_GETADVLISTEX()
  {
    vector<uint8_t> packet = {BNETProtocol::Magic::BNET_HEADER, BNETProtocol::Magic::GETADVLISTEX, 0, 0, /* short */ 0, 0, /* short */ 0, 0, /* unknown */ 0, 0, 0, 0, /* unknown */  0, 0, 0, 0};
    const uint8_t MaxGames[] = {255, 255, 255, 255};
    //const uint8_t GameName[] = {};
    //const uint8_t GamePassword[] = {};
    //const uint8_t GameStats[] = {};
    AppendBytes(packet, MaxGames, 4);
    //AppendBytes(packet, GameName, 0);     // Game Name
    packet.push_back(0);                        // Null terminator
    //AppendBytes(packet, GamePassword, 0); // Game Password
    packet.push_back(0);                        // Null terminator
    //AppendBytes(packet, GameStats, 0);    // Game Stats (unsupported by PvPGN)
    packet.push_back(0);                        // Null terminator
    AssignLength(packet);
    return packet;
  }

  vector<uint8_t> SEND_SID_GETADVLISTEX(string_view gameName, string_view gamePassword)
  {
    vector<uint8_t> packet = {BNETProtocol::Magic::BNET_HEADER, BNETProtocol::Magic::GETADVLISTEX, 0, 0, /* short */ 0, 0, /* short */ 0, 0, /* unknown */ 0, 0, 0, 0, /* unknown */  0, 0, 0, 0};
    const uint8_t MaxGames[] = {255, 255, 255, 255};
    //const uint8_t GameStats[] = {};
    AppendBytes(packet, MaxGames, 4);
    AppendByteArrayString(packet, gameName, true);         // Game Name
    AppendByteArrayString(packet, gamePassword, true);     // Game Password
    //AppendBytes(packet, GameStats, 0);               // Game Stats (unsupported by PvPGN)
    packet.push_back(0);                                   // Null terminator
    AssignLength(packet);
    return packet;
  }

  vector<uint8_t> SEND_SID_ENTERCHAT()
  {
    return vector<uint8_t>{BNETProtocol::Magic::BNET_HEADER, BNETProtocol::Magic::ENTERCHAT, 6, 0, 0, 0};
  }

  vector<uint8_t> SEND_SID_JOINCHANNEL(string_view channel)
  {
    vector<uint8_t> packet = {BNETProtocol::Magic::BNET_HEADER, BNETProtocol::Magic::JOINCHANNEL, 0, 0};

    if (channel.size() > 0)
    {
      const uint8_t NoCreateJoin[] = {2, 0, 0, 0};
      AppendBytes(packet, NoCreateJoin, 4); // flags for no create join
    }
    else
    {
      const uint8_t FirstJoin[] = {1, 0, 0, 0};
      AppendBytes(packet, FirstJoin, 4); // flags for first join
    }

    AppendByteArrayString(packet, channel, true);
    AssignLength(packet);

    return packet;
  }

  vector<uint8_t> SEND_SID_CHAT_PUBLIC(string_view message)
  {
    vector<uint8_t> packet = {BNETProtocol::Magic::BNET_HEADER, BNETProtocol::Magic::CHATMESSAGE, 0, 0};
    AppendByteArrayString(packet, message, true); // null-terminator
    AssignLength(packet);
    return packet;
  }

  vector<uint8_t> SEND_SID_CHAT_PUBLIC(const vector<uint8_t>& message)
  {
    vector<uint8_t> packet = {BNETProtocol::Magic::BNET_HEADER, BNETProtocol::Magic::CHATMESSAGE, 0, 0};
    AppendContainer(packet, message);
    packet.push_back(0);
    AssignLength(packet);
    return packet;
  }

  vector<uint8_t> SEND_SID_CHAT_WHISPER(string_view message, string_view user)
  {
    // /w USER MESSAGE
    vector<uint8_t> packet = {BNETProtocol::Magic::BNET_HEADER, BNETProtocol::Magic::CHATMESSAGE, 0, 0, 0x2f, 0x77, 0x20};
    AppendByteArrayString(packet, user, false);
    packet.push_back(0x20);
    AppendByteArrayString(packet, message, true); // With null terminator
    AssignLength(packet);
    return packet;
  }

  vector<uint8_t> SEND_SID_CHAT_WHISPER(const vector<uint8_t>& message, const vector<uint8_t>& user)
  {
    // /w USER MESSAGE
    vector<uint8_t> packet = {BNETProtocol::Magic::BNET_HEADER, BNETProtocol::Magic::CHATMESSAGE, 0, 0, 0x2f, 0x77, 0x20};
    AppendContainer(packet, user);
    packet.push_back(0x20);
    AppendContainer(packet, message);
    packet.push_back(0);
    AssignLength(packet);
    return packet;
  }

  vector<uint8_t> SEND_SID_CHECKAD()
  {
    return vector<uint8_t>{BNETProtocol::Magic::BNET_HEADER, BNETProtocol::Magic::CHECKAD, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  }

  vector<uint8_t> SEND_SID_PUBLICHOST(const array<uint8_t, 4> address, uint16_t port)
  {
    vector<uint8_t> packet;

    const uint8_t Zeros[] = {0, 0, 0, 0};

    packet.push_back(BNETProtocol::Magic::BNET_HEADER);     // BNET header constant
    packet.push_back(BNETProtocol::Magic::PUBLICHOST);      // SID_PUBLICHOST
    packet.push_back(0);                                    //
    packet.push_back(0);                                    //
    packet.push_back(2);                                    // AF_INET
    packet.push_back(0);                                    // AF_INET continued
    AppendNumberBE(packet, port);                           // Custom port
    AppendContainer(packet, address);                       // Custom IP
    AppendBytes(packet, Zeros, 4);                          //
    AppendBytes(packet, Zeros, 4);                          //
    AssignLength(packet);

    return packet;
  }

  vector<uint8_t> SEND_SID_STARTADVEX3(uint8_t state, const uint32_t mapGameType, const uint32_t gameFlags, const array<uint8_t, 2>& mapWidth, const array<uint8_t, 2>& mapHeight, string_view gameName, string_view hostName, uint32_t upTime, string_view mapPath, const array<uint8_t, 4>& mapBlizzHash, const optional<array<uint8_t, 20>>& maybeSHA1, uint32_t hostCounter, uint8_t maxSupportedSlots)
  {
    string hostCounterString = ToHexString(hostCounter);
    if (hostCounterString.size() < 8) {
      hostCounterString.insert(static_cast<size_t>(0), static_cast<size_t>(8) - hostCounterString.size(), '0');
    }

    hostCounterString = string(hostCounterString.rbegin(), hostCounterString.rend());
    assert(hostCounterString.size() == 8 && "hostCounterString should be 8 ASCII characters long");

    GameStat gameStat(gameFlags, ByteArrayToUInt16LE(mapWidth), ByteArrayToUInt16LE(mapHeight), mapPath, hostName, mapBlizzHash, maybeSHA1);
    vector<uint8_t> statString = gameStat.Encode();
    vector<uint8_t> packet;

    //Slots Free (ascii 98/110 = char b/n = 11/23 slots free) - note: do not reduce this as this is the # of UID's Warcraft III will allocate
    uint8_t encodedMaxSlots = (uint8_t)86u + maxSupportedSlots;

    if (!gameName.empty() && !hostName.empty() && !mapPath.empty())
    {
      // make the rest of the packet

      const uint8_t Unknown[]    = {255, 3, 0, 0};
      const uint8_t CustomGame[] = {0, 0, 0, 0};

      packet.push_back(BNETProtocol::Magic::BNET_HEADER);    // BNET header constant
      packet.push_back(BNETProtocol::Magic::STARTADVEX3);    // SID_STARTADVEX3
      packet.push_back(0);                                   // packet length will be assigned later
      packet.push_back(0);                                   // packet length will be assigned later
      packet.push_back(state);                               // State (16 = public, 17 = private, 18 = close)
      packet.push_back(0);                                   // State continued...
      packet.push_back(0);                                   // State continued...
      packet.push_back(0);                                   // State continued...
      AppendNumberLE(packet, upTime);                // time since creation
      AppendNumberLE(packet, mapGameType);           // Game Type (public? saved?)
      AppendBytes(packet, Unknown, 4);                   // ???
      AppendBytes(packet, CustomGame, 4);                // Custom Game
      AppendByteArrayString(packet, gameName, true);         // Game Name
      packet.push_back(0);                                   // Game Password is empty
      packet.push_back(encodedMaxSlots);                     // Slots Free
      AppendByteArrayString(packet, hostCounterString, false); // Host Counter - exclude null terminator
      AppendContainer(packet, statString);               // Stat String
      packet.push_back(0);                                   // Stat String null terminator (the stat string is encoded to remove all even numbers i.e. zeros)
      AssignLength(packet);
    } else {
      Print("[BNETPROTO] invalid parameters passed to SEND_SID_STARTADVEX3");
    }

    return packet;
  }

  vector<uint8_t> SEND_SID_NOTIFYJOIN(string_view gameName)
  {
    vector<uint8_t> packet = {BNETProtocol::Magic::BNET_HEADER, BNETProtocol::Magic::NOTIFYJOIN, 0, 0, 0, 0, 0, 0, 14, 0, 0, 0};
    AppendByteArrayString(packet, gameName, true);            // Game Name
    packet.push_back(0);                                      // Game Password is NULL
    AssignLength(packet);

    return packet;
  }

  vector<uint8_t> SEND_SID_PING(const uint32_t pingValue)
  {
    vector<uint8_t> packet = {BNETProtocol::Magic::BNET_HEADER, BNETProtocol::Magic::PING, 0, 0};
    AppendNumberLE(packet, pingValue); // Ping Value
    AssignLength(packet);
    return packet;
  }

  vector<uint8_t> SEND_SID_LOGONRESPONSE(const vector<uint8_t>& clientToken, const vector<uint8_t>& serverToken, const vector<uint8_t>& passwordHash, string_view accountName)
  {
    vector<uint8_t> packet;
    packet.push_back(BNETProtocol::Magic::BNET_HEADER);                       // BNET header constant
    packet.push_back(BNETProtocol::Magic::LOGONRESPONSE);    // SID_LOGONRESPONSE
    packet.push_back(0);                                          // packet length will be assigned later
    packet.push_back(0);                                          // packet length will be assigned later
    AppendContainer(packet, clientToken);                     // Client Token
    AppendContainer(packet, serverToken);                     // Server Token
    AppendContainer(packet, passwordHash);                    // Password Hash
    AppendByteArrayString(packet, accountName, true);             // Account Name
    AssignLength(packet);

    return packet;
  }

  vector<uint8_t> SEND_SID_NETGAMEPORT(uint16_t serverPort)
  {
    vector<uint8_t> packet;
    packet.push_back(BNETProtocol::Magic::BNET_HEADER);                       // BNET header constant
    packet.push_back(BNETProtocol::Magic::NETGAMEPORT);      // SID_NETGAMEPORT
    packet.push_back(0);                                          // packet length will be assigned later
    packet.push_back(0);                                          // packet length will be assigned later
    AppendNumberLE(packet, serverPort);                   // local game server port
    AssignLength(packet);

    return packet;
  }

  vector<uint8_t> SEND_SID_AUTH_INFO(const bool isExpansion, const Version& war3Version, uint32_t localeID, uint32_t languageID, const array<uint8_t, 4>& localeShort, string_view countryShort, string_view country)
  {
    const uint8_t ProtocolID[]    = {0, 0, 0, 0};
    const uint8_t PlatformID[]    = {54, 56, 88, 73};              // "IX86"
    const uint8_t version4[]      = {war3Version.second, 0, 0, 0};
    const uint8_t LocalIP[]       = {127, 0, 0, 1};
    const uint8_t TimeZoneBias[]  = {60, 0, 0, 0};                 // 60 minutes (GMT +0100) but this is probably -0100

    vector<uint8_t> packet;
    packet.push_back(BNETProtocol::Magic::BNET_HEADER);             // BNET header constant
    packet.push_back(BNETProtocol::Magic::AUTH_INFO);              // SID_AUTH_INFO
    packet.push_back(0);                                           // packet length will be assigned later
    packet.push_back(0);                                           // packet length will be assigned later
    AppendBytes(packet, ProtocolID, 4);                        // Protocol ID
    AppendBytes(packet, PlatformID, 4);                        // Platform ID
    if (isExpansion) {
      AppendNumberLE(packet, ProductID_TFT_LE);                     // Product ID (TFT)
    } else {
      AppendNumberLE(packet, ProductID_ROC_LE);                     // Product ID (ROC)
    }
    AppendBytes(packet, version4, 4);                           // Version
    AppendContainer(packet, localeShort);                      // Reverse language (ISO 639-1 concatenated with ISO 3166 alpha-2, and reversed)
    AppendBytes(packet, LocalIP, 4);                           // Local IP for NAT compatibility
    AppendBytes(packet, TimeZoneBias, 4);                      // Time Zone Bias
    AppendNumberLE(packet, localeID);                      // Locale ID
    AppendNumberLE(packet, languageID);                    // Language ID
    AppendByteArrayString(packet, countryShort, true);             // Country Abbreviation - PvPGN accepts up to 64 characters, including null terminator
    AppendByteArrayString(packet, country, true);                  // Country - PvPGN accepts up to 128 characters, including null terminator
    AssignLength(packet);

    return packet;
  }

  vector<uint8_t> SEND_SID_AUTH_CHECK(const uint32_t clientToken, const bool isExpansion, const array<uint8_t, 4>& exeVersion, const array<uint8_t, 4>& exeVersionHash, const vector<uint8_t>& keyInfoROC, const vector<uint8_t>& keyInfoTFT, string_view exeInfo, string_view keyOwnerName)
  {
    vector<uint8_t> packet;
    uint32_t numKeys = 1;
    if (isExpansion) ++numKeys;
    packet.push_back(BNETProtocol::Magic::BNET_HEADER);             // BNET header constant
    packet.push_back(BNETProtocol::Magic::AUTH_CHECK);              // SID_AUTH_CHECK
    packet.push_back(0);                                            // packet length will be assigned later
    packet.push_back(0);                                            // packet length will be assigned later
    AppendNumberLE(packet, clientToken);                    // Client Token
    AppendContainer(packet, exeVersion);                        // EXE Version
    AppendContainer(packet, exeVersionHash);                    // EXE Version Hash
    AppendNumberLE(packet, numKeys);                        // number of keys in this packet
    AppendNumberLE(packet, static_cast<uint32_t>(0));       // boolean Using Spawn (32 bit)
    AppendContainer(packet, keyInfoROC);                        // ROC Key Info
    if (isExpansion) AppendContainer(packet, keyInfoTFT);       // TFT Key Info
    AppendByteArrayString(packet, exeInfo, true);                   // EXE Info
    AppendByteArrayString(packet, keyOwnerName, true);              // CD Key Owner Name
    AssignLength(packet);
    return packet;
  }

  vector<uint8_t> SEND_SID_AUTH_ACCOUNTLOGON(const array<uint8_t, 32>& clientPublicKey, string_view accountName)
  {
    vector<uint8_t> packet;
    packet.push_back(BNETProtocol::Magic::BNET_HEADER);              // BNET header constant
    packet.push_back(BNETProtocol::Magic::AUTH_ACCOUNTLOGON);        // SID_AUTH_ACCOUNTLOGON
    packet.push_back(0);                                             // packet length will be assigned later
    packet.push_back(0);                                             // packet length will be assigned later
    AppendContainer(packet, clientPublicKey);                    // Client Key
    AppendByteArrayString(packet, accountName, true);                // Account Name
    AssignLength(packet);
    return packet;
  }

  vector<uint8_t> SEND_SID_AUTH_ACCOUNTLOGONPROOF(const array<uint8_t, 20>& clientPasswordProof)
  {
    vector<uint8_t> packet;
    packet.push_back(BNETProtocol::Magic::BNET_HEADER);                              // BNET header constant
    packet.push_back(BNETProtocol::Magic::AUTH_ACCOUNTLOGONPROOF);  // SID_AUTH_ACCOUNTLOGONPROOF
    packet.push_back(0);                                                 // packet length will be assigned later
    packet.push_back(0);                                                 // packet length will be assigned later
    AppendContainer(packet, clientPasswordProof);                    // Client Password Proof
    AssignLength(packet);
    return packet;
  }

  vector<uint8_t> SEND_SID_AUTH_ACCOUNTSIGNUP(string_view userName, const array<uint8_t, 20>& clientPasswordProof)
  {
    vector<uint8_t> packet;
    packet.push_back(BNETProtocol::Magic::BNET_HEADER);                              // BNET header constant
    packet.push_back(BNETProtocol::Magic::AUTH_ACCOUNTSIGNUP);      // SID_AUTH_ACCOUNTSIGNUP
    packet.push_back(0);                                                 // packet length will be assigned later
    packet.push_back(0);                                                 // packet length will be assigned later
    AppendContainer(packet, clientPasswordProof);                    // Client Password Proof
    AppendByteArrayString(packet, userName, true);
    AssignLength(packet);
    return packet;
  }

  vector<uint8_t> SEND_SID_FRIENDLIST()
  {
    return vector<uint8_t>{BNETProtocol::Magic::BNET_HEADER, BNETProtocol::Magic::FRIENDLIST, 4, 0};
  }

  vector<uint8_t> SEND_SID_CLANMEMBERLIST()
  {
    return vector<uint8_t>{BNETProtocol::Magic::BNET_HEADER, BNETProtocol::Magic::CLANMEMBERLIST, 8, 0, 0, 0, 0, 0};
  }
};
