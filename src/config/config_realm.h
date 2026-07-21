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

#ifndef AURA_CONFIG_REALM_H_
#define AURA_CONFIG_REALM_H_

#include "../includes.h"
#include "config.h"
#include "config_net.h"
#include "config_commands.h"
#include "../socket.h"

//
// CRealmConfig
//

struct CRealmConfig
{
  // Automatically-assigned values
  uint8_t m_ServerIndex;                         // unique server ID to identify players' realms through host counters (may be recycled on !reload)
  std::string m_CFGKeyPrefix;                    // 

  // Inheritable
  uint8_t m_InheritOnlyCommandCommonBasePermissions;
  uint8_t m_InheritOnlyCommandHostingBasePermissions;
  uint8_t m_InheritOnlyCommandModeratorBasePermissions;
  uint8_t m_InheritOnlyCommandAdminBasePermissions;
  uint8_t m_InheritOnlyCommandBotOwnerBasePermissions;

  // Inheritable
  bool m_UnverifiedRejectCommands;
  bool m_UnverifiedCannotStartGame;
  bool m_UnverifiedAutoKickedFromLobby;
  bool m_AlwaysSpoofCheckPlayers;
  bool m_TrustAdmins;

  CCommandConfig* m_CommandCFG;

  // Inheritable
  bool        m_Enabled;
  std::optional<sockaddr_storage> m_BindAddress; // the local address from which we connect

  // Not inheritable

  bool m_Valid;
  std::string m_InputID;                         // for IRC commands
  std::string m_UniqueName;                      // displayed on the console
  std::string m_CanonicalName;                   // displayed on game rooms
  std::string m_DataBaseID;                      // server ID for database queries
  std::string m_CDKeyROC;
  std::string m_CDKeyTFT;

  // Inheritable

  std::string m_CountryShort;                    // ISO 3166 alpha-3 country code
  std::string m_Country;                         // country name - this is actually useless
  std::string m_Win32Locale;                     // locale used: numeric or "system" - this is nominal, used to figure out m_Win32LocaleID - but also actually useless
  uint32_t m_Win32LocaleID;                      // see: http://msdn.microsoft.com/en-us/library/0h88fahh%28VS.85%29.aspx - this is actually useless
  uint32_t m_Win32LanguageID;
  std::array<uint8_t, 4> m_LocaleShort;          // language (ISO 639-1 concatenated with ISO 3166 alpha-2) - reversed internally
  PvPGNLocale m_Locale;                          // PvPGN supports a limited set of languages, so use 1 byte to identify them

  std::string m_PrivateCmdToken;                 // a symbol prefix to identify commands and send a private reply
  std::string m_BroadcastCmdToken;               // a symbol prefix to identify commands and send the reply to everyone
  bool m_EnableBroadcast;
  bool m_AnnounceHostToChat;
  bool m_IsMain;
  bool m_IsReHoster;
  bool m_IsMirror;
  bool m_IsVPN;

  bool m_IsHostOften;
  bool m_IsHostMulti;

  bool m_EnableCustomAddress;                    // enable to make peers from pvpgn servers connect to m_PublicHostAddress
  sockaddr_storage m_PublicHostAddress;          // the address to broadcast in pvpgn servers

  bool m_EnableCustomPort;                       // enable to make peers from pvpgn servers connect to m_PublicHostPort
  uint16_t m_PublicHostPort;                     // the port to broadcast in pvpgn servers

  std::string m_HostName;                        // server address to connect to
  uint16_t m_ServerPort;
  std::optional<uint8_t> m_Type;                 // pvpgn or classic.battle.net - optional for global_realm, but required at the end of the chain

  bool m_AutoRegister;
  bool m_UserNameCaseSensitive;
  bool m_PassWordCaseSensitive;
  std::string m_UserName;                        //
  std::string m_PassWord;                        //
  std::string m_LicenseeName;

  bool m_ExeAuthUseCustomVersionData;
  bool m_ExeAuthIgnoreVersionError;
  std::optional<uint8_t> m_LoginHashType;        // pvpgn or classic.battle.net - optional for global_realm, but required at the end of the chain

  std::optional<bool> m_GameIsExpansion;
  std::optional<Version> m_GameVersion;                         // WC3 major+minor version
  std::optional<Version> m_ExeAuthVersion;
  std::optional<std::vector<uint8_t>> m_ExeAuthVersionDetails;  // 4 bytes: WC3 version as {build, patch, minor, major}
  std::optional<std::vector<uint8_t>> m_ExeAuthVersionHash;     // 4 bytes
  std::string m_ExeAuthInfo;                                    // filename.exe dd/MM/yy hh::mm:ss size

  std::string m_FirstChannel;                    //
  std::set<std::string> m_SudoUsers;             //
  std::set<std::string> m_Admins;                //
  std::set<std::string> m_CryptoHosts;
  std::string m_ReHostCounterTemplate;           // string in the form PREFIX {COUNT} SUFFIX
  std::string m_LobbyNameTemplate;               // string in the form PREFIX {NAME} {MODE} {COUNTER} SUFFIX - if {COUNTER} is not provided for an autorehostable game, it gets appended
  std::string m_WatchableNameTemplate;           // string in the form PREFIX {NAME} {MODE} {COUNTER} SUFFIX - if {COUNTER} is not provided for an autorehostable game, it gets appended
  size_t m_MaxGameNameFixedCharsSize;
  uint32_t m_MaxUploadSize;                      // in KB

  RealmBroadcastDisplayPriority m_LobbyDisplayPriority;        // none, low, high
  RealmBroadcastDisplayPriority m_WatchableDisplayPriority;       // none, low, high

  bool m_ConsoleLogChat;                         // whether we should print public chat messages
  uint8_t m_FloodQuotaLines;                     // - PvPGN: corresponds to bnetd.conf: quota_lines (default 5)
  uint8_t m_FloodQuotaTime;                      // - PvPGN: corresponds to bnetd.conf: quota_time (default 5)
  uint16_t m_VirtualLineLength;                  // - PvPGN: corresponds to bnetd.conf: quota_wrapline (default 40)
  uint16_t m_MaxLineLength;                      // - PvPGN: corresponds to bnetd.conf: quota_maxline (default 200)
  bool m_FloodImmune;                            // whether we are allowed to send unlimited commands to the server - PvPGN: corresponds to lua/confg.lua: flood_immunity, or ghost_bots

  std::string m_WhisperErrorReply;

  bool m_QueryGameLists;                         // whether we should periodically request a list of hosted games

  CRealmConfig(CConfig& CFG, CNetConfig* nNetConfig);
  CRealmConfig(CConfig& CFG, CRealmConfig* nRootConfig, uint8_t nServerIndex);
  ~CRealmConfig();

  void Reset();
};

#endif
