
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

#include "realm.h"

#include "auradb.h"
#include "bncsutil_interface.h"
#include "protocol/bnet_protocol.h"
#include "command.h"
#include "config/config.h"
#include "config/config_realm.h"
#include "file_util.h"
#include "game.h"
#include "game_host.h"
#include "protocol/game_protocol.h"
#include "game_user.h"
#include "protocol/gps_protocol.h"
#include "hash.h"
#include "map.h"
#include "realm_chat.h"
#include "realm_games.h"
#include "socket.h"
#include "util.h"

#include "aura.h"

using namespace std;

// {220, 1, 203, 7}
constexpr array<uint8_t, 4> BNET_INFO_CLIENT_TOKEN_BYTES = {220, 1, 203, 7};
constexpr uint32_t BNET_INFO_CLIENT_TOKEN = ByteArrayToUInt32LE(BNET_INFO_CLIENT_TOKEN_BYTES);

//
// CRealm
//

CRealm::CRealm(CAura* nAura, CRealmConfig* nRealmConfig)
  : m_Aura(nAura),
    m_Config(*nRealmConfig),
    m_Socket(nullptr),
    m_BNCSUtil(new CBNCSUtilInterface(nRealmConfig->m_UserName, nRealmConfig->m_PassWord)),

    m_GameBroadcastInFlight(false),
    m_GameBroadcastWantsRename(false),
    m_GameIsExpansion(false),
    m_GameVersion(GAMEVER(0u, 0u)),
    m_AuthGameVersion(GAMEVER(0u, 0u)),

    m_LastGamePort(6112),
    m_LastGameHostCounter(0),

    m_InternalServerID(nAura->NextServerID()),
    m_ServerIndex(nRealmConfig->m_ServerIndex),
    m_PublicServerID(14 + 2 * nRealmConfig->m_ServerIndex), // First is 16
    m_LastDisconnectedTime(0),
    m_LastConnectionAttemptTime(0),
    m_LastGameRefreshTime(APP_MIN_TICKS),
    m_LastGameListTime(APP_MIN_TICKS),
    m_MinReconnectDelay(5),
    m_SessionID(0),
    m_NullPacketsSent(0),
    m_FirstConnect(true),
    m_ReconnectNextTick(true),
    m_WaitingToConnect(true),
    m_LoggedIn(false),
    m_FailedLogin(false),
    m_FailedSignup(false),
    m_EnteringChat(false),
    m_HadChatActivity(false),
    m_AnyWhisperRejected(false),
    m_ChatQueuedGameAnnouncement(false),

    m_LoginSalt({}),
    m_LoginServerPublicKey({}),
    m_InfoClientToken(BNET_INFO_CLIENT_TOKEN),
    m_InfoLogonType(0),
    m_InfoServerToken(0),
    m_InfoMPQFileTime(0),

    m_HostName(nRealmConfig->m_HostName),

    m_ChatQueueJoinCallback(nullptr),
    m_ChatQueueGameHostWhois(nullptr),
    m_GameSearchQuery(nullptr)
{
}

CRealm::~CRealm()
{
  StopConnection(false);

  delete m_Socket;
  delete m_BNCSUtil;
}

uint32_t CRealm::SetFD(fd_set* fd, fd_set* send_fd, int32_t* nfds) const
{
  if (m_Socket && !m_Socket->HasError() && !m_Socket->HasFin() && m_Socket->GetConnected())
  {
    m_Socket->SetFD(fd, send_fd, nfds);
    return 1;
  }

  return 0;
}

void CRealm::EventConnectionTimeOut()
{
  // the connection attempt timed out (10 seconds)
  NetworkHost host = NetworkHost(m_Config.m_HostName, m_Config.m_ServerPort);
  m_Aura->m_Net.OnThrottledConnectionError(host);
  PRINT_IF(LogLevel::kWarning, GetLogPrefix() + "failed to connect to [" + m_HostName + ":" + to_string(m_Config.m_ServerPort) + "]");
  PRINT_IF(LogLevel::kInfo, GetLogPrefix() + "waiting " + to_string(m_Aura->m_Net.GetThrottleTime(host, m_MinReconnectDelay)) + " seconds to retry");
  m_Socket->Reset();
  //m_Socket->SetKeepAlive(true, REALM_TCP_KEEPALIVE_IDLE_TIME);
  m_LastDisconnectedTime = m_Aura->GetLoopTime();
  m_WaitingToConnect     = true;
}

void CRealm::EventConnected(fd_set* /*fd*/, fd_set* send_fd)
{
  // the connection attempt completed
  m_Aura->m_Net.OnThrottledConnectionSuccess(NetworkHost(m_Config.m_HostName, m_Config.m_ServerPort));
  ++m_SessionID;
  m_Socket->SetKeepAlive(true, REALM_TCP_KEEPALIVE_IDLE_TIME);

  PRINT_IF(LogLevel::kDebug, GetLogPrefix() + "connected to [" + m_HostName + "]");
  if (!ResolveGameVersion()) {
    PRINT_IF(LogLevel::kWarning, GetLogPrefix() + "config error - misconfigured <realm_" + to_string(m_ServerIndex) + ".game_version>");
    Disable();
    m_Socket->Disconnect();
    return;
  }
  SendAuth(BNETProtocol::SEND_PROTOCOL_INITIALIZE_SELECTOR());
  SendAuth(BNETProtocol::SEND_SID_AUTH_INFO(m_GameIsExpansion, m_AuthGameVersion, m_Config.m_Win32LocaleID, m_Config.m_Win32LanguageID, m_Config.m_LocaleShort, m_Config.m_CountryShort, m_Config.m_Country));
  m_Socket->DoSend(send_fd);
  m_LastGameListTime = m_Aura->GetLoopTime();
}

void CRealm::UpdateConnected(fd_set* fd, fd_set* send_fd)
{
  // the socket is connected and everything appears to be working properly
  if (m_Socket->DoRecv(fd)) {
    string_view data = m_Socket->GetRecvBufferView();

    // extract as many packets as possible from the socket's receive buffer and process them
    bool Abort = false;

    // a packet is at least 4 bytes so loop as long as the buffer contains 4 bytes

    while (data.size() >= 4) {
      // bytes 2 and 3 contain the length of the packet
      const uint16_t packetSize = ByteArrayToUInt16LE(data, 2);
      if (packetSize < 4) {
        Abort = true;
        break;
      }
      if (data.size() < packetSize) {
        // we don't have the complete packet yet
        break;
      }

      string_view packet = data.substr(0, packetSize);
      const uint8_t packetFamily = GetByteAt(packet, 0);
      const uint8_t packetType = GetByteAt(packet, 1);

      // byte 0 is always 255
      if (packetFamily == BNETProtocol::Magic::BNET_HEADER)
      {
        // Any BNET packet is fine to reset app-level inactivity timeout.
        m_NullPacketsSent = 0;

        switch (packetType)
        {
          case BNETProtocol::Magic::ZERO:
            // warning: we do not respond to NULL packets with a NULL packet of our own
            // this is because PVPGN servers are programmed to respond to NULL packets so it will create a vicious cycle of useless traffic
            // official battle.net servers do not respond to NULL packets
            break;

          case BNETProtocol::Magic::GETADVLISTEX: {
            vector<uint8_t> sanityBuffer = vector<uint8_t>(packet.begin(), packet.end()); // TODO: Convert RECEIVE_SID_GETADVLISTEX to std::string_view
            vector<NetworkGameInfo> thirdPartyHostedGames = BNETProtocol::RECEIVE_SID_GETADVLISTEX(GetGameVersion(), sanityBuffer);
            if (!thirdPartyHostedGames.empty() && m_Aura->m_Net.m_Config.m_UDPForwardGameLists) {
              std::vector<uint8_t> relayPacket = {GameProtocol::Magic::W3FW_HEADER, 0, 0, 0};
              std::string ipString = m_Socket->GetIPString();
              AppendByteArrayString(relayPacket, ipString, true);
              AppendNumberBE(relayPacket, static_cast<uint16_t>(6112u));
              AppendNumberLE(relayPacket, (uint32_t)m_AuthGameVersion.second);
              AppendByteArrayString(relayPacket, packet, false);
              AssignLength(relayPacket);
              DPRINT_IF(LogLevel::kTrace2, GetLogPrefix() + "sending game list to " + AddressToString(m_Aura->m_Net.m_Config.m_UDPForwardAddress) + " (" + to_string(relayPacket.size()) + " bytes)");
              m_Aura->m_Net.Send(&(m_Aura->m_Net.m_Config.m_UDPForwardAddress), relayPacket);
            }

            if (m_GameSearchQuery) {
              DPRINT_IF(LogLevel::kTrace, GetLogPrefix() + "game list received (" + to_string(thirdPartyHostedGames.size()) + " games) - searching for query <" + string(m_GameSearchQuery->m_GameName) + ">...");
              bool keepSearching = true;
              for (const auto& gameInfo : thirdPartyHostedGames) {
                if (m_GameSearchQuery->GetIsMatch(GetGameVersion(), gameInfo)) {
                  PRINT_IF(LogLevel::kDebug, GetLogPrefix() + "[" + gameInfo.GetMapClientFileName() + "] matching search query found: " + gameInfo.GetHostDetails());
                  keepSearching = m_GameSearchQuery->EventMatch(gameInfo);
                  if (!keepSearching) {
                    break;
                  }
                } else if (!gameInfo.GetIsValid()) {
                  PRINT_IF(LogLevel::kDebug, GetLogPrefix() + "received invalid game");
                } else {
                  DPRINT_IF(LogLevel::kTrace, GetLogPrefix() + "[" + gameInfo.GetMapClientFileName() + "] NOT matching search query: " + gameInfo.GetHostDetails());
                }
              }
              if (!keepSearching)  {
                PRINT_IF(LogLevel::kDebug, GetLogPrefix() + "game search stopped");
                m_GameSearchQuery.reset();
              }
            }

            break;
          }

          case BNETProtocol::Magic::ENTERCHAT: {
            BNETProtocol::EnterChatResult enterChatResult = BNETProtocol::RECEIVE_SID_ENTERCHAT(packet);
            if (enterChatResult.success) {
              PRINT_IF(LogLevel::kDebug, GetLogPrefix() + "entered chat");
              m_ChatNickName = string(enterChatResult.uniqueName);
              ResetGameBroadcastData(); // m_EnteringChat guards against ENTERCHAT network loop
              AutoJoinChat();
            }
            m_EnteringChat = false;
            break;
          }

          case BNETProtocol::Magic::CHATEVENT: {
            BNETProtocol::IncomingChatResult chatEventResult = BNETProtocol::RECEIVE_SID_CHATEVENT(packet);
            if (chatEventResult.success) {
              ProcessChatEvent(chatEventResult.type, chatEventResult.userName, chatEventResult.message);
            }
            break;
          }

          case BNETProtocol::Magic::CHECKAD:
            //BNETProtocol::RECEIVE_SID_CHECKAD(packet);
            break;

          case BNETProtocol::Magic::STARTADVEX3:
            if (!GetIsGameBroadcastInFlight()) {
              Print(GetLogPrefix() + "got STARTADVEX3 but there was no broadcast in flight");
              break;
            }
            if (!m_GameBroadcast.expired()) {
              if (BNETProtocol::RECEIVE_SID_STARTADVEX3(packet)) {
                DPRINT_IF(LogLevel::kTrace2, GetLogPrefix() + "Game published OK <<" + GetGameBroadcastName() + ">>");
                m_Aura->EventBNETGameRefreshSuccess(shared_from_this());
              } else {
                PRINT_IF(LogLevel::kNotice, GetLogPrefix() + "Failed to publish game <<" + GetGameBroadcastName() + ">> Try another name");
                m_Aura->EventBNETGameRefreshError(shared_from_this());
              }
            }
            ResetGameBroadcastInFlight();
            break;

          case BNETProtocol::Magic::PING:
            SendAuth(BNETProtocol::SEND_SID_PING(BNETProtocol::RECEIVE_SID_PING(packet)));
            break;

          case BNETProtocol::Magic::AUTH_INFO: {
            {
              BNETProtocol::AuthInfoResult infoResult = BNETProtocol::RECEIVE_SID_AUTH_INFO(packet);
              if (!infoResult.success)
                break;

              m_InfoLogonType = infoResult.logonType;
              m_InfoServerToken = infoResult.serverToken;
              m_InfoMPQFileTime = infoResult.mpqFileTime;
              m_InfoIX86VerFileName.swap(infoResult.verFileName);
              m_InfoValueStringFormula.swap(infoResult.valueStringFormula);
            }

            bool versionSuccess = m_BNCSUtil->HELP_SID_AUTH_CHECK(m_Aura->m_GameInstallPath, m_Aura->m_GameDataVersion, m_GameIsExpansion, m_AuthGameVersion, &m_Config, GetValueStringFormula(), GetIX86VerFileName(), GetInfoClientToken(), GetInfoServerToken());
            if (versionSuccess) {
              const array<uint8_t, 4>& exeVersion = m_BNCSUtil->GetEXEVersion();
              const array<uint8_t, 4>& exeVersionHash = m_BNCSUtil->GetEXEVersionHash();
              const string& exeInfo = m_BNCSUtil->GetEXEInfo();
              string expansionSuffix = "TFT";
              if (!m_GameIsExpansion) expansionSuffix = "ROC";

              PRINT_IF(LogLevel::kDebug,
                GetLogPrefix() + "attempting to auth as WC3: " + expansionSuffix + " v" +
                to_string(exeVersion[3]) + "." + to_string(exeVersion[2]) + std::string(1, char(97 + exeVersion[1])) +
                " (Build " + to_string(exeVersion[0]) + ") - " +
                "version hash <" + ByteArrayToDecString(exeVersionHash) + ">"
              );

              SendAuth(BNETProtocol::SEND_SID_AUTH_CHECK(GetInfoClientToken(), m_GameIsExpansion, exeVersion, exeVersionHash, m_BNCSUtil->GetKeyInfoROC(), m_BNCSUtil->GetKeyInfoTFT(), exeInfo, m_Config.m_LicenseeName));
              SendAuth(BNETProtocol::SEND_SID_ZERO());
              SendNetworkConfig();
            } else {
              if (m_Aura->MatchLogLevel(LogLevel::kError)) {
                if (m_Config.m_LoginHashType.value() == REALM_AUTH_PVPGN) {
                  Print(GetLogPrefix() + "config error - misconfigured <game.install_path>");
                } else {
                  Print(GetLogPrefix() + "config error - misconfigured <game.install_path>, or <realm_" + to_string(m_ServerIndex) + ".cd_key.roc>, or <realm_" + to_string(m_ServerIndex) + ".cd_key.tft>");
                }
                Print(GetLogPrefix() + "bncsutil key hash failed, disconnecting...");
              }
              Disable();
              m_Socket->Disconnect();
            }

            break;
          }

          case BNETProtocol::Magic::AUTH_CHECK: {
            BNETProtocol::AuthCheckResult checkResult = BNETProtocol::RECEIVE_SID_AUTH_CHECK(packet);
            if (m_Config.m_ExeAuthIgnoreVersionError || checkResult.state == BNETProtocol::KeyResult::GOOD)
            {
              // cd keys accepted
              DPRINT_IF(LogLevel::kTrace, GetLogPrefix() + "version OK");
              m_BNCSUtil->HELP_SID_AUTH_ACCOUNTLOGON();
              SendAuth(BNETProtocol::SEND_SID_AUTH_ACCOUNTLOGON(m_BNCSUtil->GetClientKey(), m_Config.m_UserName));
            }
            else
            {
              // cd keys not accepted
              switch (checkResult.state)
              {
                case BNETProtocol::KeyResult::ROC_KEY_IN_USE:
                  PRINT_IF(LogLevel::kError, GetLogPrefix() + "logon failed - ROC CD key in use by user [" + string(checkResult.description) + "], disconnecting...");
                  break;

                case BNETProtocol::KeyResult::TFT_KEY_IN_USE:
                  PRINT_IF(LogLevel::kError, GetLogPrefix() + "logon failed - TFT CD key in use by user [" + string(checkResult.description) + "], disconnecting...");
                  break;

                case BNETProtocol::KeyResult::OLD_GAME_VERSION:
                case BNETProtocol::KeyResult::INVALID_VERSION:
                    PRINT_IF(LogLevel::kError, GetLogPrefix() + "config error - rejected <realm_" + to_string(m_ServerIndex) + ".auth_exe_version = " + ByteArrayToDecString(m_BNCSUtil->GetEXEVersion()) + ">");
                    PRINT_IF(LogLevel::kError, GetLogPrefix() + "config error - rejected <realm_" + to_string(m_ServerIndex) + ".auth_exe_version_hash = " + ByteArrayToDecString(m_BNCSUtil->GetEXEVersionHash()) + ">");
                    PRINT_IF(LogLevel::kError, GetLogPrefix() + "logon failed - version not supported, or version hash invalid, disconnecting...");
                  break;

                default:
                  PRINT_IF(LogLevel::kError, GetLogPrefix() + "logon failed - cd keys not accepted, disconnecting...");
                  break;
              }

              Disable();
              m_Socket->Disconnect();
            }

            break;
          }

          case BNETProtocol::Magic::AUTH_ACCOUNTLOGON: {
            BNETProtocol::AuthLoginResult loginResult = BNETProtocol::RECEIVE_SID_AUTH_ACCOUNTLOGON(packet);
            if (loginResult.success) {
              copy_n(loginResult.salt.data(), 32, m_LoginSalt.begin());
              copy_n(loginResult.serverPublicKey.data(), 32, m_LoginServerPublicKey.begin());
              DPRINT_IF(LogLevel::kTrace, GetLogPrefix() + "username [" + m_Config.m_UserName + "] OK");
              Login();
            } else {
              DPRINT_IF(LogLevel::kTrace, GetLogPrefix() + "username [" + m_Config.m_UserName + "] invalid");
              if (!TrySignup()) {
                Disable();
                PRINT_IF(LogLevel::kWarning, GetLogPrefix() + "logon failed - invalid username, disconnecting");
                m_Socket->Disconnect();
              }
            }

            break;
          }

          case BNETProtocol::Magic::AUTH_ACCOUNTLOGONPROOF:
            if (BNETProtocol::RECEIVE_SID_AUTH_ACCOUNTLOGONPROOF(packet)) {
              OnLoginOkay();
            } else {
              m_FailedLogin = true;
              Disable();
              PRINT_IF(LogLevel::kError, GetLogPrefix() + "logon failed - invalid password, disconnecting");
              m_Socket->Disconnect();
            }
            break;

          case BNETProtocol::Magic::AUTH_ACCOUNTSIGNUP:
            if (BNETProtocol::RECEIVE_SID_AUTH_ACCOUNTSIGNUP(packet)) {
              OnSignupOkay();
            } else {
              m_FailedSignup = true;
              Disable();
              PRINT_IF(LogLevel::kWarning, GetLogPrefix() + "sign up failed, disconnecting");
              m_Socket->Disconnect();
            }
            break;

          case BNETProtocol::Magic::FRIENDLIST:
            m_Friends = BNETProtocol::RECEIVE_SID_FRIENDLIST(packet);
            break;

          case BNETProtocol::Magic::CLANMEMBERLIST:
            m_Clan = BNETProtocol::RECEIVE_SID_CLANMEMBERLIST(packet);
            break;

          case BNETProtocol::Magic::GETGAMEINFO:
            PRINT_IF(LogLevel::kWarning, GetLogPrefix() + "got SID_GETGAMEINFO: " + GetStringBytesHex(packet));
            break;

          case BNETProtocol::Magic::HOSTGAME: {
            if (!GetIsReHoster()) {
              break;
            }

            optional<CConfig> hostedGameConfig = BNETProtocol::RECEIVE_HOSTED_GAME_CONFIG(packet);
            if (!hostedGameConfig.has_value()) {
              PRINT_IF(LogLevel::kWarning, GetLogPrefix() + "got invalid SID_HOSTGAME message");
              break;
            }
            shared_ptr<CCommandContext> ctx = nullptr;
            shared_ptr<CGameSetup> gameSetup = nullptr;
            try {
              ctx = make_shared<CCommandContext>(ServiceType::kRealm, m_Aura, string(), false, &cout);
              gameSetup = make_shared<CGameSetup>(m_Aura, ctx, &(hostedGameConfig.value()));
            } catch (...) {
              PRINT_IF(LogLevel::kWarning, GetLogPrefix() + "hostgame memory allocation failure");
              break;
            }
            if (!gameSetup->GetHasGameVersion()) {
              PRINT_IF(LogLevel::kWarning, GetLogPrefix() + "game version missing - cannot host");
              break;
            }
            if (!gameSetup->GetMapLoaded()) {
              PRINT_IF(LogLevel::kWarning, GetLogPrefix() + "map is invalid");
              break;
            }
            gameSetup->SetDisplayMode(hostedGameConfig->GetBool("rehost.game.private", false) ? GAME_DISPLAY_PRIVATE : GAME_DISPLAY_PUBLIC);
            gameSetup->SetMapReadyCallback(MAP_ONREADY_HOST, hostedGameConfig->GetString("rehost.game.name", 1, 31, "Rehosted Game"));
            gameSetup->SetActive();
            gameSetup->LoadMap();
            break;
          }
        }
      }

      data.remove_prefix(packetSize);
    }

    if (Abort) {
      data.remove_prefix(data.size());
    }

    if (data.size() != m_Socket->GetRecvBufferSize()) {
      m_Socket->UpdateRecvBuffer(data);
    }
  }

  if (m_Socket->HasError() || m_Socket->HasFin()) {
    return;
  }

  if (GetIsPvPGN() && m_Aura->GetTicksIsAfterDelay(m_Socket->GetLastRecv(), REALM_APP_KEEPALIVE_IDLE_TICKS)) {
    // Many PvPGN servers do not implement TCP Keep Alive. However, all PvPGN servers reply to BNET protocol null packets.
    int64_t expectedNullsSent = ((m_Aura->GetLoopTicks() - m_Socket->GetLastRecv() - REALM_APP_KEEPALIVE_IDLE_TICKS) / REALM_APP_KEEPALIVE_INTERVAL) + 1;
    if (expectedNullsSent > REALM_APP_KEEPALIVE_MAX_MISSED) {
      PRINT_IF(LogLevel::kWarning, GetLogPrefix() + "socket inactivity timeout");
      ResetConnection(false);
      return;
    }
    if (m_NullPacketsSent < expectedNullsSent) {
      SendAuth(BNETProtocol::SEND_SID_ZERO());
      ++m_NullPacketsSent;
      m_Socket->Flush();
    }
  }

  TrySendPendingChats();
  CheckPendingGameBroadcast();

  if (m_Aura->GetTimeIsAfterDelay(m_LastGameRefreshTime, 3)) {
    if (auto game = GetGameBroadcast()) {
      TrySendGameRefresh(game);
    }
    m_LastGameRefreshTime = m_Aura->GetLoopTime();
  }

  if (m_Aura->GetTimeIsAfterDelay(m_LastGameListTime, m_GameBroadcast.expired() ? 90 : 20)) {
    TrySendGetGamesList();
    m_LastGameListTime = m_Aura->GetLoopTime();
  }

  m_Socket->DoSend(send_fd);
}

void CRealm::Update(fd_set* fd, fd_set* send_fd)
{
  // we return at the end of each if statement so we don't have to deal with errors related to the order of the if statements
  // that means it might take a few ms longer to complete a task involving multiple steps (in this case, reconnecting) due to blocking or sleeping
  // but it's not a big deal at all, maybe 100ms in the worst possible case (based on a 50ms blocking time)

  if (!m_Config.m_Enabled) {
    if (m_Socket && m_Socket->GetConnected()) {
      StopConnection(false);
    }
    return;
  }

  if (!m_Socket) {
    m_Socket = new CTCPClient(AF_INET, m_Config.m_HostName);
    //m_Socket->SetKeepAlive(true, REALM_TCP_KEEPALIVE_IDLE_TIME);
  }

  if (m_Socket->HasError() || m_Socket->HasFin()) {
    // the socket has an error, or the server terminated the connection
    NetworkHost host = NetworkHost(m_Config.m_HostName, m_Config.m_ServerPort);
    if (m_Socket->GetConnecting()) {
      m_Aura->m_Net.OnThrottledConnectionError(host);
    }
    ResetConnection(true);
    PRINT_IF(LogLevel::kInfo, GetLogPrefix() + "waiting " + to_string(m_Aura->m_Net.GetThrottleTime(host, m_MinReconnectDelay)) + " seconds to reconnect");
    return;
  }

  if (m_Socket->GetConnected()) {
    UpdateConnected(fd, send_fd);
    return;
  }

  if (!m_Socket->GetConnected() && !m_Socket->GetConnecting() && !m_WaitingToConnect) {
    // the socket was disconnected
    ResetConnection(false);
    return;
  }

  if (!m_Socket->GetConnecting() && !m_Socket->GetConnected() && GetIsDueReconnect()) {
    // attempt to connect to battle.net

    if (!m_FirstConnect) {
      PRINT_IF(LogLevel::kNotice, GetLogPrefix() + "reconnecting to [" + m_HostName + ":" + to_string(m_Config.m_ServerPort) + "]...");
    } else {
      if (m_Config.m_BindAddress.has_value()) {
        PRINT_IF(LogLevel::kInfo, GetLogPrefix() + "connecting with local address [" + AddressToString(m_Config.m_BindAddress.value()) + "]...");
      } else {
        DPRINT_IF(LogLevel::kTrace, GetLogPrefix() + "connecting...");
      }
    }
    m_FirstConnect = false;
    m_ReconnectNextTick = false;

    sockaddr_storage resolvedAddress;
    if (m_Aura->m_Net.ResolveHostName(resolvedAddress, ACCEPT_ANY, m_Config.m_HostName, m_Config.m_ServerPort)) {
      m_Aura->m_Net.OnThrottledConnectionStart(NetworkHost(m_Config.m_HostName, m_Config.m_ServerPort));
      m_Socket->Connect(m_Config.m_BindAddress, resolvedAddress);
    } else {
      m_Socket->m_HasError = true;
    }
    m_WaitingToConnect          = false;
    m_LastConnectionAttemptTime = m_Aura->GetLoopTime();
  }

  if (m_Socket->GetConnecting()) {
    // we are currently attempting to connect to battle.net

    if (m_Socket->CheckConnect()) {
      EventConnected(fd, send_fd);
    } else if (m_Aura->GetTimeIsAfterDelay(m_LastConnectionAttemptTime, 10)) {
      EventConnectionTimeOut();
    }
  }
}

void CRealm::ProcessChatEvent(const uint32_t eventType, string_view fromUser, string_view message)
{
  bool isWhisper = (eventType == BNETProtocol::IncomingChatEvent::WHISPER);

  if (!m_Socket->GetConnected()) {
    PRINT_IF(LogLevel::kDebug, Concat(GetLogPrefix(), "not connected - message from [", fromUser, "] rejected: [", message, "]"));
    return;
  }

  // handle spoof checking for current game
  // this case covers whispers - we assume that anyone who sends a whisper to the bot with message "spoofcheck" should be considered spoof checked
  // note that this means you can whisper "spoofcheck" even in a public game to manually spoofcheck if the /whois fails

  if (eventType == BNETProtocol::IncomingChatEvent::WHISPER && (message == "s" || message == "sc" || message == "spoofcheck")) {
    shared_ptr<CGame> gameBroadcast = GetGameBroadcast();
    if (gameBroadcast && !gameBroadcast->GetIsMirror()) {
      GameUser::CGameUser* matchUser = gameBroadcast->GetUserFromName<CaseSensitive::kStrict>(fromUser);
      if (matchUser) gameBroadcast->AddToRealmVerified(m_Config.m_HostName, matchUser, true);
      return;
    }
  }

  if (eventType == BNETProtocol::IncomingChatEvent::WHISPER || eventType == BNETProtocol::IncomingChatEvent::TALK) {
    m_HadChatActivity = true;
  }

  if (eventType == BNETProtocol::IncomingChatEvent::WHISPER || eventType == BNETProtocol::IncomingChatEvent::TALK) {
    if (fromUser == GetLoginName()) {
      return;
    }
    // FIXME: Chat logging kinda sucks
    if (isWhisper) {
      PRINT_IF(LogLevel::kNotice, Concat("[WHISPER: ", m_Config.m_UniqueName, "] [", fromUser, "] ", message));
    } else if (GetShouldLogChatToConsole()) {
      Print(Concat("[CHAT: ", m_Config.m_UniqueName, "] [", fromUser, "] ", message));
    }

    // handle bot commands

    if (eventType == BNETProtocol::IncomingChatEvent::TALK && m_Config.m_IsMirror) {
      // Let bots on servers with <realm_x.mirror = yes> ignore commands at channels
      // (but still accept commands through whispers).
      return;
    }

    if (message.empty()) {
      return;
    }

    CommandTokensView commandTokens;
    ExtractMessageTokensAny(message, m_Config.m_PrivateCmdToken, m_Config.m_BroadcastCmdToken, commandTokens);
    if (commandTokens.matchType == CommandTokensMatchType::kNone) {
      if (isWhisper && fromUser != "PvPGN Realm") {
        string_view tokenName = GetTokenName(m_Config.m_PrivateCmdToken);
        string_view example = m_Aura->m_Net.m_Config.m_AllowDownloads ? "host wc3maps-8" : "host castle";
        QueueWhisper(Concat("Hi, ", fromUser, ". Use ", m_Config.m_PrivateCmdToken, tokenName, " for commands. Example: ", m_Config.m_PrivateCmdToken, example), fromUser);
      }
      return;
    } else {
      string cmdToken(commandTokens.token);
      string command = ToLowerCase(commandTokens.cmd);
      string target(commandTokens.target);
      shared_ptr<CCommandContext> ctx = nullptr;
      try {
        ctx = make_shared<CCommandContext>(
          ServiceType::kRealm, m_Aura, m_Config.m_CommandCFG,
          shared_from_this(), fromUser, isWhisper,
          (!isWhisper && commandTokens.matchType == CommandTokensMatchType::kBroadcast), &std::cout
        );
      } catch (...) {
      }
      if (ctx) {
        ctx->Run(cmdToken, command, target);
      }
    }
  }
  else if (eventType == BNETProtocol::IncomingChatEvent::CHANNEL)
  {
    PRINT_IF(LogLevel::kInfo, Concat(GetLogPrefix(), "joined channel [", message, "]"));
    m_CurrentChannel = message;
  } else if (eventType == BNETProtocol::IncomingChatEvent::WHISPERSENT) {
    PRINT_IF(LogLevel::kDebug, Concat(GetLogPrefix(), "whisper sent OK [", message, "]"));
    if (!m_ChatSentWhispers.empty()) {
      CQueuedChatMessage* oldestWhisper = m_ChatSentWhispers.front();
      if (oldestWhisper->IsProxySent()) {
        shared_ptr<CCommandContext> fromCtx = oldestWhisper->GetProxyCtx();
        if (fromCtx->CheckActionMessage(message) && !fromCtx->GetPartiallyDestroyed()) {
          fromCtx->SendReply(Concat("message sent to ", oldestWhisper->GetReceiver(), "."));
        }
        fromCtx->ClearActionMessage();
      }
      delete oldestWhisper;
      m_ChatSentWhispers.pop();
    }
  } else if (eventType == BNETProtocol::IncomingChatEvent::INFO) {
    bool LogInfo = m_HadChatActivity;

    // extract the first word which we hope is the username
    // this is not necessarily true though since info messages also include channel MOTD's and such

    shared_ptr<CGame> gameBroadcast = GetGameBroadcast();
    if (gameBroadcast && gameBroadcast->GetIsLobbyStrict()) {
      // note: if the game is rehosted, bnet will not be aware of the game being renamed
      optional<BNETProtocol::WhoisInfo> whoisInfo = ParseWhoisInfo(message);
      if (whoisInfo.has_value() && !whoisInfo->name.empty()) {
        GameUser::CGameUser* aboutPlayer = gameBroadcast->GetUserFromName(whoisInfo->name, true);
        if (aboutPlayer && aboutPlayer->GetRealmInternalID() == m_InternalServerID && !aboutPlayer->GetIsRealmVerified()) {
          // handle spoof checking for current game
          // this case covers whois results which are used when hosting a public game (we send out a "/whois [player]" for each player)
          // at all times you can still /w the bot with "spoofcheck" to manually spoof check
          if (whoisInfo->GetIsInGame() && whoisInfo->location == aboutPlayer->GetGameName()) {
            gameBroadcast->AddToRealmVerified(m_HostName, aboutPlayer, true);
          } else {
            gameBroadcast->ReportSpoofed(m_HostName, aboutPlayer);
          }
        }
      }
    }
    if (LogInfo) {
      PRINT_IF(LogLevel::kInfo, Concat("[INFO: ", m_Config.m_UniqueName, "] ", message));
    }
  } else if (eventType == BNETProtocol::IncomingChatEvent::NOTICE) {
    // Note that the default English error message <<That user is not logged on.>> is also received in other two circumstances:
    // - When sending /netinfo <USER>, if the bot has admin permissions in this realm.
    // - When sending /ban <USER>, if the bot has admin permissions in this realm.
    //
    // Therefore, abuse of the !SAY command will temporarily break feedback for !TELL/INVITE, until m_ChatSentWhispers is emptied.
    // This is (yet another reason) why !SAY /COMMAND must always be gated behind sudo.
    if (!m_ChatSentWhispers.empty() && message.length() == m_Config.m_WhisperErrorReply.length() && message == m_Config.m_WhisperErrorReply) {
      m_AnyWhisperRejected = true;
      CQueuedChatMessage* oldestWhisper = m_ChatSentWhispers.front();
      if (oldestWhisper->IsProxySent()) {
        shared_ptr<CCommandContext> fromCtx = oldestWhisper->GetProxyCtx();
        if (!fromCtx->GetPartiallyDestroyed()) {
          fromCtx->SendReply(Concat(oldestWhisper->GetReceiver(), " is offline."));
        }
        fromCtx->ClearActionMessage();
      }
      delete oldestWhisper;
      m_ChatSentWhispers.pop();
    }
    PRINT_IF(LogLevel::kNotice, Concat("[NOTE: ", m_Config.m_UniqueName, "] ", message));
  }
}

uint8_t CRealm::CountChatQuota()
{
  if (m_ChatQuotaInUse.empty()) return 0;
  int64_t minTicks = m_Aura->GetLoopTicks() - static_cast<int64_t>(m_Config.m_FloodQuotaTime) * 1000 - 300; // 300 ms hardcoded latency
  uint16_t spentQuota = 0;
  for (auto it = begin(m_ChatQuotaInUse); it != end(m_ChatQuotaInUse);) {
    if ((*it).first < minTicks) {
      it = m_ChatQuotaInUse.erase(it);
    } else {
      spentQuota += (*it).second;
      if (0xFF <= spentQuota) break;
      ++it;
    }
  }
  if (0xFF < spentQuota) return 0xFF;
  return static_cast<uint8_t>(spentQuota);
}

bool CRealm::CheckWithinChatQuota(CQueuedChatMessage* message)
{
  if (m_Config.m_FloodImmune) return true;
  uint16_t spentQuota = CountChatQuota();
  if (m_Config.m_FloodQuotaLines <= spentQuota) {
    message->SetWasThrottled(true);
    return false;
  }
  uint16_t size = message->SelectSize(m_Config.m_VirtualLineLength, m_CurrentChannel);
  const bool success = size + spentQuota <= m_Config.m_FloodQuotaLines;
  if (!success) message->SetWasThrottled(true);
  return success;
}

bool CRealm::SendQueuedMessage(CQueuedChatMessage* message)
{
  bool deleteMessage = true;
  uint8_t selectType;
  Send(message->SelectBytes(m_CurrentChannel, selectType));
  if (message->GetSendsEarlyFeedback()) {
    message->SendEarlyFeedback();
  }

  if (m_Aura->MatchLogLevel(LogLevel::kInfo)) {
    string_view modeFragment = "sent message <<";
    if (message->GetWasThrottled()) {
      if (selectType == CHAT_RECV_SELECTED_WHISPER) {
        modeFragment = "sent whisper (throttled) <<";
      } else {
        modeFragment = "sent message (throttled) <<";
      }
    } else if (selectType == CHAT_RECV_SELECTED_WHISPER) {
      modeFragment = "sent whisper <<";
    }
    PRINT_IF(LogLevel::kInfo, Concat(GetLogPrefix(), modeFragment, message->GetInnerMessage(), ">>"));
  }
  if (selectType == CHAT_RECV_SELECTED_WHISPER) {
    m_ChatSentWhispers.push(message);
    if (m_ChatSentWhispers.size() > 25 && !m_AnyWhisperRejected && m_Aura->MatchLogLevel(LogLevel::kWarning)) {
      Print(Concat(GetLogPrefix(), "warning - ", to_string(m_ChatSentWhispers.size()), " sent whispers have not been confirmed by the server"));
      Print(Concat(GetLogPrefix(), "warning - <", m_Config.m_CFGKeyPrefix + "protocol.whisper.error_reply = ", m_Config.m_WhisperErrorReply, "> may not match the language of this realm's system messages."));
    }
    // Caller must not delete the message.
    deleteMessage = false;
  }
  if (!m_Config.m_FloodImmune) {
    uint8_t extraQuota = message->GetVirtualSize(m_Config.m_VirtualLineLength, selectType);
    m_ChatQuotaInUse.push_back(make_pair(m_Aura->GetLoopTicks(), extraQuota));
  }

  switch (message->GetCallback()) {
    case CHAT_CALLBACK_REFRESH_GAME: {
      RunMessageCallbackRefreshGame(message);
      break;
    }

    case CHAT_CALLBACK_RESET: {
      ResetConnection(false);
      break;
    }

    default:
      // Do nothing
      break;
  }

  return deleteMessage;
}

optional<BNETProtocol::WhoisInfo> CRealm::ParseWhoisInfo(string_view message) const
{
  const string tmp(message);
  return BNETProtocol::PARSE_WHOIS_INFO(tmp, m_Config.m_Locale);
}

bool CRealm::GetConnected() const
{
  return m_Socket != nullptr && m_Socket->GetConnected();
}

bool CRealm::GetEnabled() const
{
  return m_Config.m_Enabled;
}

bool CRealm::GetIsPvPGN() const
{
  return m_Config.m_Type == REALM_TYPE_PVPGN;
}

string CRealm::GetServer() const
{
  return m_Config.m_HostName;
}

uint16_t CRealm::GetServerPort() const
{
  return m_Config.m_ServerPort;
}

string CRealm::GetInputID() const
{
  return m_Config.m_InputID;
}

string CRealm::GetUniqueDisplayName() const
{
  return m_Config.m_UniqueName;
}

string CRealm::GetCanonicalDisplayName() const
{
  return m_Config.m_CanonicalName;
}

string CRealm::GetDataBaseID() const
{
  return m_Config.m_DataBaseID;
}

string CRealm::GetLogPrefix() const
{
  return "[BNET: " + m_Config.m_UniqueName + "] ";
}

optional<Version> CRealm::GetExpectedGameVersion() const
{
  optional<Version> result;
  if (m_GameVersion >= GAMEVER(1u, 0u)) {
    result = m_GameVersion;
  } else if (m_Config.m_GameVersion.has_value()) {
    result = m_Config.m_GameVersion.value();
  }
  return result;
}

bool CRealm::ResolveGameVersion()
{
  if (m_Config.m_GameVersion.has_value()) {
    m_GameVersion = m_Config.m_GameVersion.value();
  } else {
    shared_ptr<const CGame> lobby = m_Aura->GetMostRecentLobby();
    if (lobby) {
      m_GameVersion = lobby->m_Config.m_GameVersion.value();
    } else if (m_Aura->m_GameSetup && m_Aura->m_GameSetup->m_GameVersion.has_value()) {
      m_GameVersion = m_Aura->m_GameSetup->m_GameVersion.value();
    } else if (m_Aura->m_GameDefaultConfig->m_GameVersion.has_value()) {
      m_GameVersion = m_Aura->m_GameDefaultConfig->m_GameVersion.value();
    } else if (m_Aura->m_GameDataVersion.has_value()) {
      m_GameVersion = m_Aura->m_GameDataVersion.value();
    } else {
      return false;
    }
  }

  if (m_Config.m_GameIsExpansion.has_value()) {
    m_GameIsExpansion = m_Config.m_GameIsExpansion.value();
  } else {
    shared_ptr<const CGame> lobby = m_Aura->GetMostRecentLobby();
    if (lobby) {
      m_GameIsExpansion = lobby->m_Config.m_GameIsExpansion;
    } else if (m_Aura->m_GameSetup && m_Aura->m_GameSetup->m_GameIsExpansion.has_value()) {
      m_GameIsExpansion = m_Aura->m_GameSetup->m_GameIsExpansion.value();
    } else {
      m_GameIsExpansion = m_Aura->m_GameDefaultConfig->m_GameIsExpansion;
    }
  }

  if (m_Config.m_ExeAuthVersion.has_value()) {
    m_AuthGameVersion = m_Config.m_ExeAuthVersion.value();
  } else {
    m_AuthGameVersion = m_GameVersion;
  }

  return true;
}

optional<bool> CRealm::GetIsGameVersionCompatible(shared_ptr<const CGame> game) const
{
  Version realmGameVersion = GetGameVersion();
  if (realmGameVersion.first == 0) return nullopt;
  if (m_GameIsExpansion != game->GetIsExpansion()) return false;
  return game->GetIsSupportedGameVersion(realmGameVersion);
}

bool CRealm::GetShouldLogChatToConsole() const
{
  return m_Config.m_ConsoleLogChat;
}

string CRealm::GetLoginName() const
{
  return m_Config.m_UserName;
}

bool CRealm::GetIsMain() const
{
  return m_Config.m_IsMain;
}

bool CRealm::GetIsReHoster() const
{
  return m_Config.m_IsReHoster;
}

bool CRealm::GetIsMirror() const
{
  return m_Config.m_IsMirror;
}

bool CRealm::GetIsVPN() const
{
  return m_Config.m_IsVPN;
}

bool CRealm::GetUsesCustomIPAddress() const
{
  return m_Config.m_EnableCustomAddress;
}

bool CRealm::GetUsesCustomPort() const
{
  return m_Config.m_EnableCustomPort;
}

const sockaddr_storage* CRealm::GetPublicHostAddress() const
{
  return &(m_Config.m_PublicHostAddress);
}

uint16_t CRealm::GetPublicHostPort() const
{
  return m_Config.m_PublicHostPort;
}

uint32_t CRealm::GetMaxUploadSize() const
{
  return m_Config.m_MaxUploadSize;
}

bool CRealm::GetIsFloodImmune() const
{
  return m_Config.m_FloodImmune;
}

bool CRealm::GetIsDueReconnect() const
{
  if (m_ReconnectNextTick) {
    return true;
  }
  if (!m_Aura->GetTimeIsAfterDelay(m_LastDisconnectedTime, m_MinReconnectDelay)) {
    return false;
  }
  return !m_Aura->m_Net.GetIsOutgoingThrottled(NetworkHost(m_Config.m_HostName, m_Config.m_ServerPort));
}

bool CRealm::GetAnnounceHostToChat() const
{
  return m_Config.m_AnnounceHostToChat;
}

bool CRealm::GetHasEnhancedAntiSpoof() const
{
  return (
    (m_Config.m_CommandCFG->m_Enabled && m_Config.m_UnverifiedRejectCommands) ||
    m_Config.m_UnverifiedCannotStartGame || m_Config.m_UnverifiedAutoKickedFromLobby ||
    m_Config.m_AlwaysSpoofCheckPlayers
  );
}

bool CRealm::GetUnverifiedCannotStartGame() const
{
  return m_Config.m_UnverifiedCannotStartGame;
}

bool CRealm::GetUnverifiedAutoKickedFromLobby() const
{
  return m_Config.m_UnverifiedAutoKickedFromLobby;
}

RealmBroadcastDisplayPriority CRealm::GetLobbyDisplayPriority() const
{
  return m_Config.m_LobbyDisplayPriority;
}

RealmBroadcastDisplayPriority CRealm::GetWatchableGamesDisplayPriority() const
{
  return m_Config.m_WatchableDisplayPriority;
}

CCommandConfig* CRealm::GetCommandConfig() const
{
  return m_Config.m_CommandCFG;
}

string CRealm::GetCommandToken() const
{
  return m_Config.m_PrivateCmdToken;
}

void CRealm::SendGetFriendsList()
{
  Send(BNETProtocol::SEND_SID_FRIENDLIST());
}

void CRealm::SendGetClanList()
{
  Send(BNETProtocol::SEND_SID_CLANMEMBERLIST());
}

void CRealm::SendGetGamesList()
{
  if (m_GameSearchQuery) {
    Send(BNETProtocol::SEND_SID_GETADVLISTEX(m_GameSearchQuery->GetGameName(), string()));
  } else {
    Send(BNETProtocol::SEND_SID_GETADVLISTEX());
  }
}

void CRealm::TrySendGetGamesList()
{
  if (m_Config.m_QueryGameLists || m_GameSearchQuery) {
    SendGetGamesList();
  }
}

void CRealm::SendNetworkConfig()
{
  shared_ptr<CGame> lobbyPendingForBroadcast = GetGameBroadcastPending();
  if (lobbyPendingForBroadcast && lobbyPendingForBroadcast->GetPublicHostOverride()) {
    m_PublicHostAddress = lobbyPendingForBroadcast->GetPublicHostAddress();
    PRINT_IF(LogLevel::kDebug, GetLogPrefix() + "mirroring public game host " + IPv4ToString(*m_PublicHostAddress) + ":" + to_string(lobbyPendingForBroadcast->GetPublicHostPort()));
    SendAuth(BNETProtocol::SEND_SID_PUBLICHOST(*m_PublicHostAddress, lobbyPendingForBroadcast->GetPublicHostPort()));
    m_LastGamePort = lobbyPendingForBroadcast->GetPublicHostPort();
  } else if (m_Config.m_EnableCustomAddress) {
    m_PublicHostAddress = AddressToIPv4Array(&(m_Config.m_PublicHostAddress));
    uint16_t port = 6112;
    if (m_Config.m_EnableCustomPort) {
      port = m_Config.m_PublicHostPort;
    } else if (lobbyPendingForBroadcast && lobbyPendingForBroadcast->GetIsLobbyStrict()) {
      port = lobbyPendingForBroadcast->GetHostPort();
    }
    PRINT_IF(LogLevel::kDebug, GetLogPrefix() + "using public game host " + IPv4ToString(*m_PublicHostAddress) + ":" + to_string(port));
    SendAuth(BNETProtocol::SEND_SID_PUBLICHOST(*m_PublicHostAddress, port));
    m_LastGamePort = port;
  } else if (m_Config.m_EnableCustomPort) {
    PRINT_IF(LogLevel::kDebug, GetLogPrefix() + "using public game port " + to_string(m_Config.m_PublicHostPort));
    SendAuth(BNETProtocol::SEND_SID_NETGAMEPORT(m_Config.m_PublicHostPort));
    m_LastGamePort = m_Config.m_PublicHostPort;
  }
}

void CRealm::AutoJoinChat()
{
  const string& targetChannel = m_AnchorChannel.empty() ? m_Config.m_FirstChannel : m_AnchorChannel;
  if (targetChannel.empty()) return;
  Send(BNETProtocol::SEND_SID_JOINCHANNEL(targetChannel));
  PRINT_IF(LogLevel::kDebug, GetLogPrefix() + "joining channel [" + targetChannel + "]");
}

void CRealm::SendEnterChat()
{
  DPRINT_IF(LogLevel::kTrace, GetLogPrefix() + "entering chat...");
  Send(BNETProtocol::SEND_SID_ENTERCHAT());
  m_EnteringChat = true;
}

void CRealm::TrySendEnterChat()
{
  if (m_EnteringChat) {
    return;
  }
  if (!m_AnchorChannel.empty() || !m_Config.m_FirstChannel.empty()) {
    SendEnterChat();
  }
}

void CRealm::Send(const vector<uint8_t>& packet)
{
  if (m_LoggedIn) {
    SendAuth(packet);
  } else {
    DPRINT_IF(LogLevel::kTrace2, GetLogPrefix() + "packet not sent (not logged in)");
  }
}

void CRealm::SendAuth(const vector<uint8_t>& packet)
{
  // This function is public only for the login phase.
  // Though it's also privately used by CRealm::Send.
  DPRINT_IF(LogLevel::kTrace2, GetLogPrefix() + "sending packet - " + ByteArrayToHexString(packet));
  m_Socket->PutBytes(packet);
}

bool CRealm::TrySignup()
{
  if (m_FailedSignup || !m_Config.m_AutoRegister) {
    return false;
  }
  if (m_Config.m_LoginHashType.value() != REALM_AUTH_PVPGN) {
    return false;
  }
  Signup();
  return true;
}

void CRealm::Signup()
{
  //if (m_Config.m_LoginHashType.value() == REALM_AUTH_PVPGN) {
  // exclusive to pvpgn logon
  PRINT_IF(LogLevel::kNotice, GetLogPrefix() + "registering new account in PvPGN realm");
  m_BNCSUtil->HELP_PvPGNPasswordHash(m_Config.m_PassWord);
  SendAuth(BNETProtocol::SEND_SID_AUTH_ACCOUNTSIGNUP(m_Config.m_UserName, m_BNCSUtil->GetPvPGNPasswordHash()));
  //}
}

bool CRealm::Login()
{
  if (m_Config.m_LoginHashType.value() == REALM_AUTH_PVPGN) {
    // pvpgn logon
    DPRINT_IF(LogLevel::kTrace, GetLogPrefix() + "using pvpgn logon type");
    m_BNCSUtil->HELP_PvPGNPasswordHash(m_Config.m_PassWord);
    SendAuth(BNETProtocol::SEND_SID_AUTH_ACCOUNTLOGONPROOF(m_BNCSUtil->GetPvPGNPasswordHash()));
  } else {
    // battle.net logon
    DPRINT_IF(LogLevel::kTrace, GetLogPrefix() + "using battle.net logon type");
    m_BNCSUtil->HELP_SID_AUTH_ACCOUNTLOGONPROOF(GetLoginSalt(), GetLoginServerPublicKey());
    SendAuth(BNETProtocol::SEND_SID_AUTH_ACCOUNTLOGONPROOF(m_BNCSUtil->GetM1()));
  }
  return true;
}

void CRealm::OnLoginOkay()
{
  m_LoggedIn = true;
  PRINT_IF(LogLevel::kNotice, GetLogPrefix() + "logged in as [" + m_Config.m_UserName + "]");

  TrySendGetGamesList();
  SendGetFriendsList();
  SendGetClanList();
  TrySendEnterChat();
}

void CRealm::OnSignupOkay()
{
  PRINT_IF(LogLevel::kNotice, GetLogPrefix() + "signed up as [" + m_Config.m_UserName + "]");
  m_BNCSUtil->HELP_SID_AUTH_ACCOUNTLOGON();
  SendAuth(BNETProtocol::SEND_SID_AUTH_ACCOUNTLOGON(m_BNCSUtil->GetClientKey(), m_Config.m_UserName));
  //Login();
}

CQueuedChatMessage* CRealm::QueueCommand(const string& message, shared_ptr<CCommandContext> fromCtx, const bool isProxy)
{
  if (message.empty() || !m_LoggedIn)
    return nullptr;

  if (!m_Config.m_FloodImmune && message.length() > m_Config.m_MaxLineLength) {
    return nullptr;
  }

  CQueuedChatMessage* entry = new CQueuedChatMessage(shared_from_this(), fromCtx, isProxy);
  entry->SetMessage(message);
  entry->SetReceiver(RECV_SELECTOR_SYSTEM);
  m_ChatQueueMain.push(entry);
  m_HadChatActivity = true;

  DPRINT_IF(LogLevel::kTrace, GetLogPrefix() + "queued command \"" + entry->GetInnerMessage() + "\"");
  return entry;
}

CQueuedChatMessage* CRealm::QueuePriorityWhois(const string& message)
{
  if (message.empty() || !m_LoggedIn)
    return nullptr;

  if (!m_Config.m_FloodImmune && message.length() > m_Config.m_MaxLineLength) {
    return nullptr;
  }

  if (m_ChatQueueGameHostWhois) {
    delete m_ChatQueueGameHostWhois;
  }
  m_ChatQueueGameHostWhois = new CQueuedChatMessage(shared_from_this(), nullptr, false);
  m_ChatQueueGameHostWhois->SetMessage(message);
  m_ChatQueueGameHostWhois->SetReceiver(RECV_SELECTOR_SYSTEM);
  m_HadChatActivity = true;

  DPRINT_IF(LogLevel::kTrace, GetLogPrefix() + "queued fast spoofcheck \"" + m_ChatQueueGameHostWhois->GetInnerMessage() + "\"");
  return m_ChatQueueGameHostWhois;
}

CQueuedChatMessage* CRealm::QueueChatChannel(const string& message, shared_ptr<CCommandContext> fromCtx, const bool isProxy)
{
  if (message.empty() || !m_LoggedIn)
    return nullptr;

  CQueuedChatMessage* entry = new CQueuedChatMessage(shared_from_this(), fromCtx, isProxy);
  if (!m_Config.m_FloodImmune && m_Config.m_MaxLineLength < message.length()) {
    entry->SetMessage(message.substr(0, m_Config.m_MaxLineLength));
  } else {
    entry->SetMessage(message);
  }
  entry->SetReceiver(RECV_SELECTOR_ONLY_PUBLIC);
  m_ChatQueueMain.push(entry);
  m_HadChatActivity = true;

  DPRINT_IF(LogLevel::kTrace, GetLogPrefix() + "queued chat message \"" + entry->GetInnerMessage() + "\"");
  return entry;
}

CQueuedChatMessage* CRealm::QueueChatReply(const uint8_t messageValue, const string& message, const string& user, const uint8_t selector, shared_ptr<CCommandContext> fromCtx, const bool isProxy)
{
  if (message.empty() || !m_LoggedIn)
    return nullptr;

  CQueuedChatMessage* entry = new CQueuedChatMessage(shared_from_this(), fromCtx, isProxy);
  entry->SetMessage(messageValue, message);
  entry->SetReceiver(selector, user);
  m_ChatQueueMain.push(entry);
  m_HadChatActivity = true;

  DPRINT_IF(LogLevel::kTrace, GetLogPrefix() + "queued reply to [" + user + "] - \"" + entry->GetInnerMessage() + "\"");
  return entry;
}

CQueuedChatMessage* CRealm::QueueWhisper(const string& message, string_view user, shared_ptr<CCommandContext> fromCtx, const bool isProxy)
{
  if (message.empty() || !m_LoggedIn)
    return nullptr;

  CQueuedChatMessage* entry = new CQueuedChatMessage(shared_from_this(), fromCtx, isProxy);
  if (!m_Config.m_FloodImmune && (m_Config.m_MaxLineLength - 20u) < message.length()) {
    entry->SetMessage(message.substr(0, m_Config.m_MaxLineLength - 20u));
  } else {
    entry->SetMessage(message);
  }
  entry->SetReceiver(RECV_SELECTOR_ONLY_WHISPER, user);
  m_ChatQueueMain.push(entry);
  m_HadChatActivity = true;

  DPRINT_IF(LogLevel::kTrace, GetLogPrefix() + "queued whisper to [" + string(user) + "] - \"" + entry->GetInnerMessage() + "\"");
  return entry;
}

CQueuedChatMessage* CRealm::QueueGameChatAnnouncement(shared_ptr<const CGame> game, shared_ptr<CCommandContext> fromCtx, const bool isProxy)
{
  if (!m_LoggedIn)
    return nullptr;

  m_ChatQueuedGameAnnouncement = true;

  m_ChatQueueJoinCallback = new CQueuedChatMessage(shared_from_this(), fromCtx, isProxy);
  m_ChatQueueJoinCallback->SetMessage(game->GetAnnounceText(shared_from_this()));
  m_ChatQueueJoinCallback->SetReceiver(RECV_SELECTOR_ONLY_PUBLIC);
  m_ChatQueueJoinCallback->SetCallback(CHAT_CALLBACK_REFRESH_GAME, game->GetHostCounter());
  if (!game->GetIsMirror()) {
    m_ChatQueueJoinCallback->SetValidator(CHAT_VALIDATOR_LOBBY_JOINABLE, game->GetHostCounter());
  }
  m_HadChatActivity = true;

  DPRINT_IF(LogLevel::kTrace, GetLogPrefix() + "queued game announcement");
  return m_ChatQueueJoinCallback;
}

void CRealm::TryQueueChatReply(const string& message, const string& user, bool isPrivate, shared_ptr<CCommandContext> /*fromCtx*/, const uint8_t /*ctxFlags*/)
{
  // don't respond to non admins if there are more than 25 messages already in the queue
  // this prevents malicious users from filling up the bot's chat queue and crippling the bot
  // in some cases the queue may be full of legitimate messages but we don't really care
  // if the bot does not reply to a command once in awhile
  // e.g. when several users join a game at the same time and cause multiple /whois messages to be queued at once

  if (m_ChatQueueMain.size() >= 25 && !(GetIsFloodImmune() || GetIsModerator(user) || GetIsAdmin(user) || GetIsSudoer(user))) {
    if (m_Aura->MatchLogLevel(LogLevel::kWarning)) {
      Print(GetLogPrefix() + "warning - " + to_string(m_ChatQueueMain.size()) + " queued messages");
      Print(GetLogPrefix() + message);
      Print("[AURA] Quota exceeded (reply dropped.)");
    }
    return;
  }

  if (isPrivate) {
    QueueWhisper(message, user);
  } else {
    QueueChatChannel(message);
  }
}

void CRealm::TrySendPendingChats()
{
  bool waitForPriority = false;
  if (m_LoggedIn && m_ChatQueueJoinCallback && GetInChat()) {
    if (m_ChatQueueJoinCallback->GetIsStale()) {
      delete m_ChatQueueJoinCallback;
      m_ChatQueueJoinCallback = nullptr;
    } else {
      if (CheckWithinChatQuota(m_ChatQueueJoinCallback)) {
        if (SendQueuedMessage(m_ChatQueueJoinCallback)) {
          delete m_ChatQueueJoinCallback;
        }
        m_ChatQueueJoinCallback = nullptr;
      } else {
        waitForPriority = true;
      }
    }
  }
  if (m_LoggedIn && m_ChatQueueGameHostWhois) {
    if (m_ChatQueueGameHostWhois->GetIsStale()) {
      delete m_ChatQueueGameHostWhois;
      m_ChatQueueGameHostWhois = nullptr;
    } else {
      if (CheckWithinChatQuota(m_ChatQueueGameHostWhois)) {
        if (SendQueuedMessage(m_ChatQueueGameHostWhois)) {
          delete m_ChatQueueGameHostWhois;
        }
        m_ChatQueueGameHostWhois = nullptr;
      } else {
        waitForPriority = true;
      }
    }
  }

  if (!waitForPriority) {
    while (m_LoggedIn && !m_ChatQueueMain.empty()) {
      CQueuedChatMessage* nextMessage = m_ChatQueueMain.front();
      if (nextMessage->GetIsStale()) {
        delete nextMessage;
        m_ChatQueueMain.pop();
      } else {
        if (CheckWithinChatQuota(nextMessage)) {
          if (SendQueuedMessage(nextMessage)) {
            delete nextMessage;
          }
          m_ChatQueueMain.pop();
        } else {
          break;
        }
      }
    }
  }
}

void CRealm::QueueGameUncreate()
{
  ResetGameChatAnnouncement();
  Send(BNETProtocol::SEND_SID_STOPADV());
}

void CRealm::ResetGameBroadcastData()
{
  DPRINT_IF(LogLevel::kTrace, GetLogPrefix() + "game broadcast cleared");
  m_GameBroadcastName.clear();
  m_GameBroadcast.reset();
  ResetGameBroadcastStatus();
  QueueGameUncreate();
}

bool CRealm::GetCanSetGameBroadcastPending(shared_ptr<CGame> game) const
{
  if (game->GetDisplayMode() == GAME_DISPLAY_NONE) {
    DPRINT_IF(LogLevel::kTrace2, GetLogPrefix() + "Not setting pending because display mode is none");
    return false;
  }
  if (game->GetIsMirror() && GetIsMirror()) {
    // A mirror realm is a realm whose purpose is to mirror games actually hosted by Aura.
    // Do not display external games in those realms.
    DPRINT_IF(LogLevel::kTrace2, GetLogPrefix() + "Not setting pending mirror game because realm is mirror");
    return false;
  }
  if (m_GameVersion >= GAMEVER(1u, 0u)) {
    if (!game->GetIsSupportedGameVersion(GetGameVersion())) {
      DPRINT_IF(LogLevel::kTrace2, GetLogPrefix() + "Not setting pending game because version is unsupported");
      return false;
    }
    if (game->GetIsExpansion() != GetGameIsExpansion()) {
      DPRINT_IF(LogLevel::kTrace2, GetLogPrefix() + "Not setting pending game because expansion does not match");
      return false;
    }
  }
  if (game->GetIsRealmExcluded(GetServer())) {
    DPRINT_IF(LogLevel::kTrace2, GetLogPrefix() + "Not setting pending game because realm is excluded");
    return false;
  }

  RealmBroadcastDisplayPriority targetPriority = game->GetCanJoinInProgress() ? GetWatchableGamesDisplayPriority() : GetLobbyDisplayPriority();
  if (targetPriority == RealmBroadcastDisplayPriority::kNone) {
    DPRINT_IF(LogLevel::kTrace2, GetLogPrefix() + "Not setting pending game because priority is none");
    return false;
  }
  if (targetPriority == RealmBroadcastDisplayPriority::kLow) {
    auto currentGame = GetGameBroadcast();
    if (currentGame) {
      RealmBroadcastDisplayPriority activePriority = currentGame->GetCanJoinInProgress() ? GetWatchableGamesDisplayPriority() : GetLobbyDisplayPriority();
      if (activePriority > targetPriority) {
        DPRINT_IF(LogLevel::kTrace2, GetLogPrefix() + "Not setting pending game because priority is too low");
        return false;
      }
    }
  }
  return true;
}

bool CRealm::TrySetGameBroadcastPending(shared_ptr<CGame> game)
{
  if (!GetCanSetGameBroadcastPending(game)) {
    return false;
  }
  SetGameBroadcastPending(game);
  DPRINT_IF(LogLevel::kTrace2, GetLogPrefix() + "Game [" + game->GetGameName() + "] is now pending broadcast");
  return true;
}

void CRealm::CheckPendingGameBroadcast()
{
  if (!m_LoggedIn || !GetIsGameBroadcastPending() || GetIsGameBroadcastInFlight() || (GetIsPvPGN() && m_EnteringChat)) {
    return;
  }

  shared_ptr<CGame> pendingGame = GetGameBroadcastPending();

  if (pendingGame->GetPublicHostOverride() &&
    !(m_PublicHostAddress.has_value() && pendingGame->GetPublicHostAddress() == m_PublicHostAddress.value())
  ) {
    PRINT_IF(LogLevel::kInfo, GetLogPrefix() + "resetting session to setup mirroring to " + IPv4ToString(pendingGame->GetPublicHostAddress()) + "...");
    ResetConnection(false);
    return;
  }

  optional<bool> pendingChat = m_GameBroadcastPendingChat;
  ResetGameBroadcastPending(); // early reset to unlock CRealm::SendGameRefresh
  ResetGameBroadcastPendingChat();

  if (
    (pendingGame->GetIsExpansion() != GetGameIsExpansion()) ||
    !(pendingGame->GetIsSupportedGameVersion(GetGameVersion()))
  ) {
    // GetCanSetGameBroadcastPending() cannot do a proper version check, so we must do it now.
    return;
  }

  ResetGameBroadcastData();

  if (pendingGame->GetDisplayMode() == GAME_DISPLAY_PUBLIC && pendingChat.value_or(GetAnnounceHostToChat())) {
    TrySendEnterChat();
    QueueGameChatAnnouncement(pendingGame);
  } else {
    // Send STARTADVEX3
    m_GameBroadcast = pendingGame;
    TrySendGameRefresh(pendingGame);
  }
}

void CRealm::RunMessageCallbackRefreshGame(CQueuedChatMessage* message)
{
  m_ChatQueuedGameAnnouncement = false;
  shared_ptr<CGame> matchLobby = m_Aura->GetLobbyOrObservableByHostCounterExact(message->GetCallbackData());
  if (!matchLobby) {
    Print(GetLogPrefix() + " !! lobby not found !! host counter 0x" + ToHexString(message->GetCallbackData()));
    if (message->GetIsStale()) {
      Print(GetLogPrefix() + " !! lobby is stale !!");
    } else {
      Print(GetLogPrefix() + " !! lobby is not stale !!");
    }
  } else if (matchLobby->GetIsSupportedGameVersion(GetGameVersion()) && matchLobby->GetIsExpansion() == GetGameIsExpansion()) {
    // Queue the game as pending again. But this time use an override to prevent sending another chat announcement.
    // Note that SetGameBroadcastPendingChat(false) is NOT the same as ResetGameBroadcastPendingChat()
    SetGameBroadcastPending(matchLobby);
    SetGameBroadcastPendingChat(false);
  }
}

bool CRealm::TrySendGameRefresh(shared_ptr<CGame> game)
{
  if (!game->HasSlotsOpen() && !game->GetCanJoinInProgress()) {
    return false;
  }
  if (!GetLoggedIn() || GetIsGameBroadcastErrored() || GetIsGameBroadcastInFlight() || GetIsGameBroadcastPending()) {
    return false;
  }
  if (GetIsChatQueuedGameAnnouncement()) {
    // Wait til we have sent a chat message first.
    return false;
  }

  /*
  if (game->GetDisplayMode == GAME_DISPLAY_NONE) {
    return false;
  }
  if (game->GetIsMirror() && GetIsMirror()) {
  // A mirror realm is a realm whose purpose is to mirror games actually hosted by Aura.
  // Do not display external games in those realms.
    return false;
  }
  if (!game->GetIsSupportedGameVersion(GetGameVersion())) {
    return false;
  }
  if (game->GetIsExpansion() != GetGameIsExpansion()) {
    return false;
  }
  if (game->GetIsRealmExcluded(GetServer())) {
    return false;
  }
  if (game->GetCanJoinInProgress() && GetWatchableGamesDisplayMode() != REALM_OBSERVER_DISPLAY_ALWAYS) {
    return false;
  }
  */
  return SendGameRefresh(game);
}

bool CRealm::SendGameRefresh(shared_ptr<CGame> game)
{
  const uint16_t connectPort = (
    game->GetIsMirror() ? game->GetPublicHostPort() :
    (GetUsesCustomPort() ? GetPublicHostPort() :
    game->GetHostPort())
  );

  // construct a fixed host counter which will be used to identify players from this realm
  // the fixed host counter's highest-order byte will contain a 7 bit ID (0-127), plus a trailing bit to discriminate join from info requests
  // the rest of the fixed host counter will contain the 24 least significant bits of the actual host counter
  // since we're destroying 8 bits of information here the actual host counter should not be greater than 2^24 which is a reasonable assumption
  // when a user joins a game we can obtain the ID from the received host counter
  // note: LAN broadcasts use an ID of 0, IDs 1 to 15 are reserved
  // battle.net refreshes use IDs of 16-255
  const uint32_t hostCounter = game->GetHostCounter() | (game->GetIsMirror() ? 0 : (static_cast<uint32_t>(m_PublicServerID) << 24));

  bool changedAny = (
    m_LastGamePort != connectPort ||
    m_LastGameHostCounter != hostCounter
  );

  if (m_GameBroadcastName.empty() || m_GameBroadcastWantsRename) {
    string broadcastName = game->GetCustomGameName(shared_from_this());
    // Never advertise games with names beyond 31 characters long.
    // This could happen after a config reload. Ignore the reloaded name template in that case.
    if (broadcastName.size() > MAX_GAME_NAME_SIZE) {
      broadcastName = broadcastName.substr(0, MAX_GAME_NAME_SIZE);
    }
    if (m_GameBroadcastName != broadcastName) {
      m_GameBroadcastName = broadcastName;
      changedAny = true;
    }
    m_GameBroadcastWantsRename = false;
  }

  if (m_LastGamePort != connectPort) {
    DPRINT_IF(LogLevel::kTrace, GetLogPrefix() + "updating net game port to " + to_string(connectPort));
    // Some PvPGN servers will disconnect if this message is sent while logged in
    Send(BNETProtocol::SEND_SID_NETGAMEPORT(connectPort));
    m_LastGamePort = connectPort;
  }

  if (!changedAny) {
    DPRINT_IF(LogLevel::kTrace2, GetLogPrefix() + "game refreshed");
  } else {
    if (!m_Config.m_IsHostOften && !m_Aura->GetTicksIsFirstOrAfterDelay(m_GameBroadcastStartTicks, static_cast<int64_t>(REALM_HOST_COOLDOWN_TICKS))) {
      // Still in cooldown
      DPRINT_IF(LogLevel::kTrace, GetLogPrefix() + "not registering game... still in cooldown");
      return false;
    }
    PRINT_IF(LogLevel::kDebug, GetLogPrefix() + "registering game...");
    m_LastGameHostCounter = hostCounter;
    m_GameBroadcastStartTicks = m_Aura->GetLoopTicks();
  }

  Version version = GetGameVersion();
  optional<array<uint8_t, 20>> maybeSHA1;
  if (version >= GAMEVER(1u, 23u)) {
    maybeSHA1 = game->GetMapSHA1(version);
  }
  string hostName = m_Config.m_UserName;
  Send(BNETProtocol::SEND_SID_STARTADVEX3(
    game->GetDisplayMode(),
    game->GetGameType(),
    game->GetGameFlags(),
    game->GetAnnounceWidth(),
    game->GetAnnounceHeight(),
    m_GameBroadcastName,
    hostName,
    game->GetUptime(),
    game->GetSourceFilePath(),
    game->GetSourceFileHashBlizz(version),
    maybeSHA1,
    hostCounter,
    game->GetMap()->GetVersionMaxSlots()
  ));
  SetGameBroadcastInFlight();

  if (!m_CurrentChannel.empty()) {
    m_AnchorChannel = m_CurrentChannel;
    m_CurrentChannel.clear();
  }

  return true;
}

void CRealm::StopConnection(bool hadError)
{
  m_LastDisconnectedTime = m_Aura->GetLoopTime();
  m_BNCSUtil->Reset(m_Config.m_UserName, m_Config.m_PassWord);

  if (m_Socket) {
    if (m_Socket->GetConnected()) {
      if (m_Aura->MatchLogLevel(hadError ? LogLevel::kWarning : LogLevel::kNotice)) {
        if (m_Socket->HasFin()) {
          Print(GetLogPrefix() + "remote terminated the connection");
        } else if (hadError) {
          Print(GetLogPrefix() + "disconnected - " + m_Socket->GetErrorString());
        } else {
          Print(GetLogPrefix() + "disconnected");
        }
      }
      m_Socket->Close();
    }
  }

  m_LoggedIn = false;
  m_EnteringChat = false;
  m_CurrentChannel.clear();
  m_AnchorChannel.clear();
  m_WaitingToConnect = true;
  m_PublicHostAddress.reset();

  auto gameBroadcast = GetGameBroadcast();
  if (gameBroadcast) {
    SetGameBroadcastPending(gameBroadcast);
  }
  ResetGameBroadcastInFlight();
  ResetGameBroadcastStatus();

  m_ChatQuotaInUse.clear();
  if (m_ChatQueueJoinCallback) {
    delete m_ChatQueueJoinCallback;
    m_ChatQueueJoinCallback = nullptr;
  }
  if (m_ChatQueueGameHostWhois) {
    delete m_ChatQueueGameHostWhois;
    m_ChatQueueGameHostWhois = nullptr;
  }
  while (!m_ChatQueueMain.empty()) {
    delete m_ChatQueueMain.front();
    m_ChatQueueMain.pop();
  }
  while (!m_ChatSentWhispers.empty()) {
    delete m_ChatSentWhispers.front();
    m_ChatSentWhispers.pop();
  }
}

void CRealm::ResetConnection(bool hadError)
{
  StopConnection(hadError);

  if (m_Socket) {
    m_Socket->Reset();
    //m_Socket->SetKeepAlive(true, REALM_TCP_KEEPALIVE_IDLE_TIME);
  }
}

bool CRealm::GetIsModerator(string name) const
{
  transform(begin(name), end(name), begin(name), [](char c) { return static_cast<char>(std::tolower(c)); });

  if (m_Aura->m_DB->ModeratorCheck(m_Config.m_DataBaseID, name))
    return true;

  return false;
}

bool CRealm::GetIsAdmin(string name) const
{
  transform(begin(name), end(name), begin(name), [](char c) { return static_cast<char>(std::tolower(c)); });
  return m_Config.m_Admins.find(name) != m_Config.m_Admins.end();
}

bool CRealm::GetIsSudoer(string name) const
{
  transform(begin(name), end(name), begin(name), [](char c) { return static_cast<char>(std::tolower(c)); });
  return m_Config.m_SudoUsers.find(name) != m_Config.m_SudoUsers.end();
}

bool CRealm::GetIsCryptoHost(string name) const
{
  transform(begin(name), end(name), begin(name), [](char c) { return static_cast<char>(std::tolower(c)); });
  return m_Config.m_CryptoHosts.find(name) != m_Config.m_CryptoHosts.end();
}

bool CRealm::IsBannedPlayer(string name, string hostName) const
{
  return m_Aura->m_DB->GetIsUserBanned(name, hostName, m_Config.m_DataBaseID);
}

bool CRealm::IsBannedIP(string ip) const
{
  return m_Aura->m_DB->GetIsIPBanned(ip, m_Config.m_DataBaseID);
}

void CRealm::HoldFriends(shared_ptr<CGame> game)
{
  for (auto& friend_ : m_Friends)
    game->AddToReserved(friend_);
}

void CRealm::HoldClan(shared_ptr<CGame> game)
{
  for (auto& clanmate : m_Clan)
    game->AddToReserved(clanmate);
}

void CRealm::Disable()
{
  m_Config.m_Enabled = false;
}

void CRealm::ResetLogin()
{
  m_FailedLogin = false;
  m_FailedSignup = false;
}

void CRealm::ReloadConfig(CRealmConfig* realmConfig)
{
  const bool needsResetConnection = (
    GetServer() != realmConfig->m_HostName ||
    GetServerPort() != realmConfig->m_ServerPort ||
    GetLoginName() != realmConfig->m_UserName ||
    (GetEnabled() && !realmConfig->m_Enabled) ||
    !GetLoggedIn() ||
    (realmConfig->m_ExeAuthVersion.has_value() && realmConfig->m_ExeAuthVersion.value() != GetAuthGameVersion()) ||
    (!realmConfig->m_ExeAuthVersion.has_value() && realmConfig->m_GameVersion.has_value() && realmConfig->m_GameVersion.value() != GetAuthGameVersion()) ||
    (realmConfig->m_GameIsExpansion.has_value() && realmConfig->m_GameIsExpansion.value() != GetGameIsExpansion())
  );

  m_Config = *realmConfig;

  SetHostCounter(m_Config.m_ServerIndex + 15);
  ResetLogin();

  if (auto game = GetGameBroadcast()) {
    if (!GetCanSetGameBroadcastPending(game)) {
      ResetGameBroadcastData();
      //TrySendEnterChat();
    }
  }
  SetGameBroadcastWantsRename();

  if (needsResetConnection) ResetConnection(false);

  if (m_Aura->MatchLogLevel(LogLevel::kDebug)) {
    Print(GetLogPrefix() + "config reloaded");
  }
}

string CRealm::GetReHostCounterTemplate() const
{
  return m_Config.m_ReHostCounterTemplate;
}

const string& CRealm::GetLobbyNameTemplate() const
{
  return m_Config.m_LobbyNameTemplate;
}

const string& CRealm::GetWatchableNameTemplate() const
{
  return m_Config.m_WatchableNameTemplate;
}

void CRealm::QuerySearch(const string& gameName, shared_ptr<CGameSetup> gameSetup)
{
  m_GameSearchQuery = make_shared<GameSearchQuery>(gameName, string(), gameSetup->GetMap());
  m_GameSearchQuery->SetCallback(GameSearchQueryCallback::kHostActiveMirror, gameSetup);
  PRINT_IF(LogLevel::kDebug, GetLogPrefix() + "searching game [" + gameName + "]...");
}
