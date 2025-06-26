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

#include "config_realm.h"
#include "config_bot.h"
#include "../command.h"
#include "../util.h"
#include "../protocol/bnet_protocol.h"
#include "../optional.h"

#include <utility>

using namespace std;

//
// CRealmConfig
//

CRealmConfig::CRealmConfig(CConfig& CFG, CNetConfig* NetConfig)
  : // Not meant to be inherited
    m_ServerIndex(0), // m_ServerIndex is one-based
    m_CFGKeyPrefix("global_realm."),
    m_CommandCFG(nullptr),

    // Inheritable
    m_Enabled(true),
    m_Valid(true),
    m_LocaleShort({83, 69, 115, 101}), // esES, reversed
    m_Locale(PvPGNLocale::kESES),

    m_PublicHostPort(0),

    m_AutoRegister(false),
    m_UserNameCaseSensitive(false),
    m_PassWordCaseSensitive(false),
    m_LicenseeName("Aura"),

    m_SudoUsers({}),
    m_Admins({}),
    m_CryptoHosts({}),
    m_MaxGameNameFixedCharsSize(0),
    m_MaxUploadSize(NetConfig->m_MaxUploadSize), // The setting in AuraCFG applies to LAN always.
    m_LobbyDisplayPriority(RealmBroadcastDisplayPriority::kHigh),
    m_WatchableDisplayPriority(RealmBroadcastDisplayPriority::kNone),
    m_FloodImmune(false),

    m_QueryGameLists(false)
{
  m_CountryShort           = CFG.GetString(m_CFGKeyPrefix + "country_short", "PER");
  m_Country                = CFG.GetString(m_CFGKeyPrefix + "country", "Peru");
  m_Win32Locale            = CFG.GetString(m_CFGKeyPrefix + "win32.lcid", "system");

  string localeShort = CFG.GetString(m_CFGKeyPrefix + "locale_short", 4, 4, "esES");
  copy_n(localeShort.rbegin(), 4, m_LocaleShort.begin());

  {
    bool localeError = CFG.GetErrorLast();
    if (!localeError) {
      if (
        m_LocaleShort[0] < 0x41 || m_LocaleShort[1] < 0x41 || m_LocaleShort[2] < 0x61 || m_LocaleShort[3] < 0x61 ||
        m_LocaleShort[0] > 0x5a || m_LocaleShort[1] > 0x5a || m_LocaleShort[2] > 0x7a || m_LocaleShort[3] > 0x7a
      )
      {
        localeError = true;
      }
    }
    if (localeError) {
      Print("[CONFIG] Error - invalid value provided for <" + m_CFGKeyPrefix + "locale_short> - must provide a valid pair of ISO 639-1, ISO 3166 alpha-2 identifiers");
      CFG.SetFailed();
    }
  }

  m_Locale = CFG.GetEnumSensitive<PvPGNLocale>(m_CFGKeyPrefix + "locale_short", TO_ARRAY("enUS", "csCZ", "deDE", "esES", "frFR", "itIT", "jaJA", "koKR", "plPL", "ruRU", "zhCN", "zhTW"), PvPGNLocale::kESES);

  if (m_Win32Locale == "system") {
    m_Win32LocaleID = 10250;
  } else {
    try {
      m_Win32LocaleID  = stoul(m_Win32Locale);
    } catch (...) {
      m_Win32Locale = "system";
      m_Win32LocaleID = 10250;
    }
  }

  m_Win32LanguageID        = CFG.GetUint32(m_CFGKeyPrefix + "win32.langid", m_Win32LocaleID);

  m_PrivateCmdToken        = CFG.GetString(m_CFGKeyPrefix + "commands.trigger", "!");
  if (!m_PrivateCmdToken.empty() && m_PrivateCmdToken[0] == '/') {
    Print("[CONFIG] Error - invalid value provided for <" + m_CFGKeyPrefix + "commands.trigger> - slash (/) is reserved by Battle.net");
    CFG.SetFailed();
  }
  m_BroadcastCmdToken      = CFG.GetString(m_CFGKeyPrefix + "commands.broadcast.trigger");
  if (!m_BroadcastCmdToken.empty() && m_BroadcastCmdToken[0] == '/') {
    Print("[CONFIG] Error - invalid value provided for <" + m_CFGKeyPrefix + "commands.broadcast.trigger> - slash (/) is reserved by Battle.net");
    CFG.SetFailed();
  }
  m_EnableBroadcast        = CFG.GetBool(m_CFGKeyPrefix + "commands.broadcast.enabled", false);

  m_AnnounceHostToChat     = CFG.GetBool(m_CFGKeyPrefix + "announce_chat", true);
  m_IsMain                 = CFG.GetBool(m_CFGKeyPrefix + "main", false);
  m_IsReHoster             = CFG.GetBool(m_CFGKeyPrefix + "rehoster", false);
  m_IsMirror               = CFG.GetBool(m_CFGKeyPrefix + "mirror", false);
  m_IsVPN                  = CFG.GetBool(m_CFGKeyPrefix + "vpn", false);

  m_IsHostOften            = !CFG.GetBool(m_CFGKeyPrefix + "game_host.throttle", true);
  m_IsHostMulti            = !CFG.GetBool(m_CFGKeyPrefix + "game_host.unique", true);

  m_EnableCustomAddress    = CFG.GetBool(m_CFGKeyPrefix + "custom_ip_address.enabled", false);

  memset(&m_PublicHostAddress, 0, sizeof(sockaddr_storage));
  optional<sockaddr_storage> maybeCustomAddress = CFG.GetMaybeAddressIPv4(m_CFGKeyPrefix + "custom_ip_address.value");
  if (m_EnableCustomAddress)
    CFG.FailIfErrorLast();
  ReadOpt(maybeCustomAddress) >> m_PublicHostAddress;

  m_EnableCustomPort       = CFG.GetBool(m_CFGKeyPrefix + "custom_port.enabled", false);

  optional<uint16_t> maybePublicHostPort = CFG.GetMaybeNonZeroPort(m_CFGKeyPrefix + "custom_port.value");
  if (m_EnableCustomPort)
    CFG.FailIfErrorLast();
  ReadOpt(maybePublicHostPort) >> m_PublicHostPort;

  m_HostName               = CFG.GetString(m_CFGKeyPrefix + "host_name");
  m_ServerPort             = CFG.GetUint16(m_CFGKeyPrefix + "server_port", 6112);
  m_Type                   = CFG.GetStringIndex(m_CFGKeyPrefix + "type", {"pvpgn", "classic.battle.net"}, REALM_TYPE_PVPGN);

  m_AutoRegister           = CFG.GetBool(m_CFGKeyPrefix + "auto_register", m_AutoRegister);
  m_UserNameCaseSensitive  = CFG.GetBool(m_CFGKeyPrefix + "username.case_sensitive", false);
  m_PassWordCaseSensitive  = CFG.GetBool(m_CFGKeyPrefix + "password.case_sensitive", false);

  m_UserName               = CFG.GetString(m_CFGKeyPrefix + "username", m_UserName);
  m_PassWord               = CFG.GetString(m_CFGKeyPrefix + "password", m_PassWord);
  m_LicenseeName           = CFG.GetString(m_CFGKeyPrefix + "licensee", m_LicenseeName);

  m_ExeAuthUseCustomVersionData   = CFG.GetBool(m_CFGKeyPrefix + "exe_auth.custom", false);
  m_ExeAuthIgnoreVersionError     = CFG.GetBool(m_CFGKeyPrefix + "exe_auth.ignore_version_error", false);

  if (CFG.Exists(m_CFGKeyPrefix + "login.hash_type")) {
    m_LoginHashType        = CFG.GetStringIndex(m_CFGKeyPrefix + "login.hash_type", {"pvpgn", "classic.battle.net"}, REALM_TYPE_PVPGN);
    CFG.FailIfErrorLast();
  }

  if (CFG.Exists(m_CFGKeyPrefix + "expansion")) {
    m_GameIsExpansion   = static_cast<bool>(CFG.GetStringIndex(m_CFGKeyPrefix + "expansion", {"roc", "tft"}, SELECT_EXPANSION_TFT));
    CFG.FailIfErrorLast();
  }
  

  m_GameVersion        = CFG.GetMaybeVersion(m_CFGKeyPrefix + "game_version");
  m_ExeAuthVersion     = CFG.GetMaybeVersion(m_CFGKeyPrefix + "exe_auth.version");
  m_ExeAuthVersionDetails         = CFG.GetMaybeUint8Vector(m_CFGKeyPrefix + "exe_auth.version_details", 4);
  if (m_ExeAuthUseCustomVersionData) CFG.FailIfErrorLast();
  m_ExeAuthVersionHash     = CFG.GetMaybeUint8Vector(m_CFGKeyPrefix + "exe_auth.version_hash", 4);
  if (m_ExeAuthUseCustomVersionData) CFG.FailIfErrorLast();
  m_ExeAuthInfo            = CFG.GetString(m_CFGKeyPrefix + "exe_auth.info");

  m_FirstChannel           = CFG.GetString(m_CFGKeyPrefix + "first_channel", "The Void");
  m_SudoUsers              = CFG.GetSet(m_CFGKeyPrefix + "sudo_users", ',', true, false, m_SudoUsers);
  m_Admins                 = CFG.GetSet(m_CFGKeyPrefix + "admins", ',', true, false, m_Admins);
  m_CryptoHosts            = CFG.GetSet(m_CFGKeyPrefix + "crypto_hosts", ',', true, false, m_CryptoHosts);

  m_ReHostCounterTemplate  = CFG.GetGameNameTemplate(m_CFGKeyPrefix + "game_list.rehost.name_template", "-{COUNT}");
  m_LobbyNameTemplate      = CFG.GetGameNameTemplate(m_CFGKeyPrefix + "game_list.lobby.name_template", "{NAME}{COUNTER}");
  m_WatchableNameTemplate  = CFG.GetGameNameTemplate(m_CFGKeyPrefix + "game_list.watchable.name_template", "{NAME}{COUNTER}");

  m_MaxUploadSize          = CFG.GetUint32(m_CFGKeyPrefix + "map_transfers.max_size", m_MaxUploadSize);
  m_LobbyDisplayPriority       = CFG.GetEnum<RealmBroadcastDisplayPriority>(m_CFGKeyPrefix + "game_list.lobby.display.priority", TO_ARRAY("none", "low", "high"), m_LobbyDisplayPriority);
  m_WatchableDisplayPriority   = CFG.GetEnum<RealmBroadcastDisplayPriority>(m_CFGKeyPrefix + "game_list.watchable.display.priority", TO_ARRAY("none", "low", "high"), m_WatchableDisplayPriority);

  m_ConsoleLogChat         = CFG.GetBool(m_CFGKeyPrefix + "logs.console.chat", true);
  m_FloodQuotaLines        = CFG.GetUint8(m_CFGKeyPrefix + "flood.lines", 5) - 1;
  m_FloodQuotaTime         = CFG.GetUint8(m_CFGKeyPrefix + "flood.time", 5);
  m_VirtualLineLength      = CFG.GetUint16(m_CFGKeyPrefix + "flood.wrap", 40);
  m_MaxLineLength          = CFG.GetUint16(m_CFGKeyPrefix + "flood.max_size", 160);
  m_FloodImmune            = CFG.GetBool(m_CFGKeyPrefix + "flood.immune", m_FloodImmune);

  if (0 == m_FloodQuotaLines) {
    m_FloodQuotaLines = 1;
    Print("[CONFIG] Error - Invalid value provided for <" + m_CFGKeyPrefix + "flood.lines>.");
  }
  if (100 < m_FloodQuotaLines) {
    m_FloodQuotaLines = 100;
    Print("[CONFIG] Error - Invalid value provided for <" + m_CFGKeyPrefix + "flood.lines>.");
  }
  if (60 < m_FloodQuotaTime) {
    m_FloodQuotaTime = 60;
    Print("[CONFIG] Error - Invalid value provided for <" + m_CFGKeyPrefix + "flood.time>.");
  }
  if (0 == m_VirtualLineLength || 256 < m_VirtualLineLength) {
    m_VirtualLineLength = 256;
    Print("[CONFIG] Error - Invalid value provided for <" + m_CFGKeyPrefix + "flood.wrap>.");
  }
  if (m_MaxLineLength < 6 || 256 < m_MaxLineLength) {
    m_MaxLineLength = 256;
    Print("[CONFIG] Error - Invalid value provided for <" + m_CFGKeyPrefix + "flood.max_size>.");
  }
  const uint32_t maxDeductedLineLength = static_cast<uint32_t>(m_VirtualLineLength) * static_cast<uint32_t>(m_FloodQuotaLines);
  if (static_cast<uint32_t>(m_MaxLineLength) > maxDeductedLineLength) {
    m_MaxLineLength = maxDeductedLineLength;
    Print("[CONFIG] Error - Invalid value provided for <" + m_CFGKeyPrefix + "flood.max_size>. It cannot exceed " + to_string(maxDeductedLineLength) + " characters because of flood quota.");
  }

  m_WhisperErrorReply      = CFG.GetString(m_CFGKeyPrefix + "protocol.whisper.error_reply", string());
  m_QueryGameLists         = CFG.GetBool(m_CFGKeyPrefix + "queries.games_list.enabled", m_QueryGameLists);

  m_Enabled                = CFG.GetBool(m_CFGKeyPrefix + "enabled", true);
  m_BindAddress            = CFG.GetMaybeAddress(m_CFGKeyPrefix + "bind_address");

  vector<string> commandPermissions = {"disabled", "sudo", "sudo_unsafe", "rootadmin", "admin", "verified_owner", "owner", "verified", "auto", "potential_owner", "unverified"};

  m_InheritOnlyCommandCommonBasePermissions = CFG.GetStringIndex(m_CFGKeyPrefix + "commands.common.permissions", commandPermissions, COMMAND_PERMISSIONS_AUTO);
  m_InheritOnlyCommandHostingBasePermissions = CFG.GetStringIndex(m_CFGKeyPrefix + "commands.hosting.permissions", commandPermissions, COMMAND_PERMISSIONS_AUTO);
  m_InheritOnlyCommandModeratorBasePermissions = CFG.GetStringIndex(m_CFGKeyPrefix + "commands.moderator.permissions", commandPermissions, COMMAND_PERMISSIONS_AUTO);
  m_InheritOnlyCommandAdminBasePermissions = CFG.GetStringIndex(m_CFGKeyPrefix + "commands.admin.permissions", commandPermissions, COMMAND_PERMISSIONS_AUTO);
  m_InheritOnlyCommandBotOwnerBasePermissions = CFG.GetStringIndex(m_CFGKeyPrefix + "commands.bot_owner.permissions", commandPermissions, COMMAND_PERMISSIONS_AUTO);

  m_UnverifiedRejectCommands      = CFG.GetBool(m_CFGKeyPrefix + "unverified_users.reject_commands", false);
  m_UnverifiedCannotStartGame     = CFG.GetBool(m_CFGKeyPrefix + "unverified_users.reject_start", false);
  m_UnverifiedAutoKickedFromLobby = CFG.GetBool(m_CFGKeyPrefix + "unverified_users.auto_kick", false);
  m_AlwaysSpoofCheckPlayers       = CFG.GetBool(m_CFGKeyPrefix + "unverified_users.always_verify", false);
}

CRealmConfig::CRealmConfig(CConfig& CFG, CRealmConfig* nRootConfig, uint8_t nServerIndex)
  : m_ServerIndex(nServerIndex),
    m_CFGKeyPrefix("realm_" + to_string(nServerIndex) + "."),

    m_InheritOnlyCommandCommonBasePermissions(nRootConfig->m_InheritOnlyCommandCommonBasePermissions),
    m_InheritOnlyCommandHostingBasePermissions(nRootConfig->m_InheritOnlyCommandHostingBasePermissions),
    m_InheritOnlyCommandModeratorBasePermissions(nRootConfig->m_InheritOnlyCommandModeratorBasePermissions),
    m_InheritOnlyCommandAdminBasePermissions(nRootConfig->m_InheritOnlyCommandAdminBasePermissions),
    m_InheritOnlyCommandBotOwnerBasePermissions(nRootConfig->m_InheritOnlyCommandBotOwnerBasePermissions),

    m_UnverifiedRejectCommands(nRootConfig->m_UnverifiedRejectCommands),
    m_UnverifiedCannotStartGame(nRootConfig->m_UnverifiedCannotStartGame),
    m_UnverifiedAutoKickedFromLobby(nRootConfig->m_UnverifiedAutoKickedFromLobby),
    m_AlwaysSpoofCheckPlayers(nRootConfig->m_AlwaysSpoofCheckPlayers),

    m_CommandCFG(nullptr),

    m_Enabled(nRootConfig->m_Enabled),
    m_BindAddress(nRootConfig->m_BindAddress),
    m_Valid(nRootConfig->m_Valid),

    m_CountryShort(nRootConfig->m_CountryShort),
    m_Country(nRootConfig->m_Country),
    m_Win32Locale(nRootConfig->m_Win32Locale),
    m_Win32LocaleID(nRootConfig->m_Win32LocaleID),
    m_Win32LanguageID(nRootConfig->m_Win32LanguageID),
    m_LocaleShort(nRootConfig->m_LocaleShort),
    m_Locale(nRootConfig->m_Locale),

    m_PrivateCmdToken(nRootConfig->m_PrivateCmdToken),
    m_BroadcastCmdToken(nRootConfig->m_BroadcastCmdToken),
    m_AnnounceHostToChat(nRootConfig->m_AnnounceHostToChat),
    m_IsMain(nRootConfig->m_IsMain),
    m_IsReHoster(nRootConfig->m_IsReHoster),
    m_IsMirror(nRootConfig->m_IsMirror),
    m_IsVPN(nRootConfig->m_IsVPN),

    m_IsHostOften(nRootConfig->m_IsHostOften),
    m_IsHostMulti(nRootConfig->m_IsHostMulti),

    m_EnableCustomAddress(nRootConfig->m_EnableCustomAddress),
    m_PublicHostAddress(nRootConfig->m_PublicHostAddress),

    m_EnableCustomPort(nRootConfig->m_EnableCustomPort),
    m_PublicHostPort(nRootConfig->m_PublicHostPort),

    m_HostName(nRootConfig->m_HostName),
    m_ServerPort(nRootConfig->m_ServerPort),
    m_Type(nRootConfig->m_Type.value_or(REALM_TYPE_PVPGN)),

    m_AutoRegister(nRootConfig->m_AutoRegister),
    m_UserNameCaseSensitive(nRootConfig->m_UserNameCaseSensitive),
    m_PassWordCaseSensitive(nRootConfig->m_PassWordCaseSensitive),
    m_UserName(nRootConfig->m_UserName),
    m_PassWord(nRootConfig->m_PassWord),
    m_LicenseeName(nRootConfig->m_LicenseeName),

    m_ExeAuthUseCustomVersionData(nRootConfig->m_ExeAuthUseCustomVersionData),
    m_ExeAuthIgnoreVersionError(nRootConfig->m_ExeAuthIgnoreVersionError),
    m_LoginHashType(nRootConfig->m_LoginHashType),

    m_GameIsExpansion(nRootConfig->m_GameIsExpansion),
    m_GameVersion(nRootConfig->m_GameVersion),
    m_ExeAuthVersion(nRootConfig->m_ExeAuthVersion),
    m_ExeAuthVersionDetails(nRootConfig->m_ExeAuthVersionDetails),
    m_ExeAuthVersionHash(nRootConfig->m_ExeAuthVersionHash),
    m_ExeAuthInfo(nRootConfig->m_ExeAuthInfo),

    m_FirstChannel(nRootConfig->m_FirstChannel),
    m_SudoUsers(nRootConfig->m_SudoUsers),
    m_Admins(nRootConfig->m_Admins),
    m_CryptoHosts(nRootConfig->m_CryptoHosts),
    m_ReHostCounterTemplate(nRootConfig->m_ReHostCounterTemplate),
    m_LobbyNameTemplate(nRootConfig->m_LobbyNameTemplate),
    m_WatchableNameTemplate(nRootConfig->m_WatchableNameTemplate),
    m_MaxUploadSize(nRootConfig->m_MaxUploadSize),
    m_LobbyDisplayPriority(nRootConfig->m_LobbyDisplayPriority),
    m_WatchableDisplayPriority(nRootConfig->m_WatchableDisplayPriority),

    m_ConsoleLogChat(nRootConfig->m_ConsoleLogChat),
    m_FloodQuotaLines(nRootConfig->m_FloodQuotaLines),
    m_FloodQuotaTime(nRootConfig->m_FloodQuotaTime),
    m_VirtualLineLength(nRootConfig->m_VirtualLineLength),
    m_MaxLineLength(nRootConfig->m_MaxLineLength),
    m_FloodImmune(nRootConfig->m_FloodImmune),

    m_WhisperErrorReply(nRootConfig->m_WhisperErrorReply),
    m_QueryGameLists(nRootConfig->m_QueryGameLists)
{
  m_HostName               = ToLowerCase(CFG.GetString(m_CFGKeyPrefix + "host_name", m_HostName));
  m_ServerPort             = CFG.GetUint16(m_CFGKeyPrefix + "server_port", m_ServerPort);

  if (CFG.Exists(m_CFGKeyPrefix + "type")) {
    m_Type = CFG.GetStringIndex(m_CFGKeyPrefix + "type", {"pvpgn", "classic.battle.net"}, REALM_TYPE_PVPGN);
    CFG.FailIfErrorLast(&m_Valid);
  }
  
  m_UniqueName             = CFG.GetString(m_CFGKeyPrefix + "unique_name", m_HostName);
  m_CanonicalName          = CFG.GetString(m_CFGKeyPrefix + "canonical_name", m_UniqueName); // may be shared by several servers
  m_InputID                = CFG.GetString(m_CFGKeyPrefix + "input_id", m_UniqueName); // expected unique
  transform(begin(m_InputID), end(m_InputID), begin(m_InputID), [](char c) { return static_cast<char>(std::tolower(c)); });
  m_DataBaseID             = CFG.GetString(m_CFGKeyPrefix + "db_id", m_HostName); // may be shared by several servers
  m_CDKeyROC               = CFG.GetString(m_CFGKeyPrefix + "cd_key.roc", 26, 26, "FFFFFFFFFFFFFFFFFFFFFFFFFF");
  m_CDKeyTFT               = CFG.GetString(m_CFGKeyPrefix + "cd_key.tft", 26, 26, "FFFFFFFFFFFFFFFFFFFFFFFFFF");

  // remove dashes and spaces from CD keys and convert to uppercase
  m_CDKeyROC.erase(remove(begin(m_CDKeyROC), end(m_CDKeyROC), '-'), end(m_CDKeyROC));
  m_CDKeyTFT.erase(remove(begin(m_CDKeyTFT), end(m_CDKeyTFT), '-'), end(m_CDKeyTFT));
  m_CDKeyROC.erase(remove(begin(m_CDKeyROC), end(m_CDKeyROC), ' '), end(m_CDKeyROC));
  m_CDKeyTFT.erase(remove(begin(m_CDKeyTFT), end(m_CDKeyTFT), ' '), end(m_CDKeyTFT));
  transform(begin(m_CDKeyROC), end(m_CDKeyROC), begin(m_CDKeyROC), [](char c) { return static_cast<char>(std::toupper(c)); });
  transform(begin(m_CDKeyTFT), end(m_CDKeyTFT), begin(m_CDKeyTFT), [](char c) { return static_cast<char>(std::toupper(c)); });

  m_CountryShort           = CFG.GetString(m_CFGKeyPrefix + "country_short", m_CountryShort);
  m_Country                = CFG.GetString(m_CFGKeyPrefix + "country", m_Country);
  m_Win32Locale            = CFG.GetString(m_CFGKeyPrefix + "win32.lcid", m_Win32Locale);

  // These are the main supported locales, but we still accept other values
  if (CFG.Exists(m_CFGKeyPrefix + "locale_short")) {
    string localeShort = CFG.GetString(m_CFGKeyPrefix + "locale_short", 4, 4, "esES");
    bool localeError = CFG.GetErrorLast();
    if (!localeError) {
      copy_n(localeShort.rbegin(), 4, m_LocaleShort.begin());
      if (
        m_LocaleShort[0] < 0x41 || m_LocaleShort[1] < 0x41 || m_LocaleShort[2] < 0x61 || m_LocaleShort[3] < 0x61 ||
        m_LocaleShort[0] > 0x5a || m_LocaleShort[1] > 0x5a || m_LocaleShort[2] > 0x7a || m_LocaleShort[3] > 0x7a
      )
      {
        localeError = true;
      }
    }
    if (localeError) {
      Print("[CONFIG] Error - invalid value provided for <" + m_CFGKeyPrefix + "locale_short> - must provide a valid pair of ISO 639-1, ISO 3166 alpha-2 identifiers");
      m_Valid = false;
    }
  }

  m_Locale = CFG.GetEnumSensitive<PvPGNLocale>(m_CFGKeyPrefix + "locale_short", TO_ARRAY("enUS", "csCZ", "deDE", "esES", "frFR", "itIT", "jaJA", "koKR", "plPL", "ruRU", "zhCN", "zhTW"), m_Locale);

  if (m_Win32Locale == "system") {
    m_Win32LocaleID = 10250;
  } else {
    try {
      m_Win32LocaleID  = stoul(m_Win32Locale);
    } catch (...) {
      m_Win32Locale = nRootConfig->m_Win32Locale;
    }
  }

  m_Win32LanguageID        = CFG.GetUint32(m_CFGKeyPrefix + "win32.langid", m_Win32LanguageID);

  m_PrivateCmdToken        = CFG.GetString(m_CFGKeyPrefix + "commands.trigger", m_PrivateCmdToken);
  if (!m_PrivateCmdToken.empty() && m_PrivateCmdToken[0] == '/') {
    Print("[CONFIG] Error - invalid value provided for <" + m_CFGKeyPrefix + "commands.trigger - slash (/) is not allowed");
    m_Valid = false;
  }
  m_BroadcastCmdToken      = CFG.GetString(m_CFGKeyPrefix + "commands.broadcast.trigger", m_BroadcastCmdToken);
  if (!m_BroadcastCmdToken.empty() && m_BroadcastCmdToken[0] == '/') {
    Print("[CONFIG] Error - invalid value provided for <" + m_CFGKeyPrefix + "commands.broadcast.trigger - slash (/) is not allowed");
    m_Valid = false;
  }
  m_EnableBroadcast        = CFG.GetBool(m_CFGKeyPrefix + "commands.broadcast.enabled", m_EnableBroadcast);

  if (!m_EnableBroadcast)
    m_BroadcastCmdToken.clear();

  m_AnnounceHostToChat     = CFG.GetBool(m_CFGKeyPrefix + "announce_chat", m_AnnounceHostToChat);
  m_IsMain                 = CFG.GetBool(m_CFGKeyPrefix + "main", m_IsMain);
  m_IsReHoster             = CFG.GetBool(m_CFGKeyPrefix + "rehoster", m_IsReHoster);
  m_IsMirror               = CFG.GetBool(m_CFGKeyPrefix + "mirror", m_IsMirror);
  m_IsVPN                  = CFG.GetBool(m_CFGKeyPrefix + "vpn", m_IsVPN);

  m_IsHostOften            = !CFG.GetBool(m_CFGKeyPrefix + "game_host.throttle", !m_IsHostOften);
  m_IsHostMulti            = !CFG.GetBool(m_CFGKeyPrefix + "game_host.unique", !m_IsHostMulti);

  m_EnableCustomAddress    = CFG.GetBool(m_CFGKeyPrefix + "custom_ip_address.enabled", m_EnableCustomAddress);
  optional<sockaddr_storage> maybeAddress = CFG.GetMaybeAddressIPv4(m_CFGKeyPrefix + "custom_ip_address.value");
  if (m_EnableCustomAddress)
    CFG.FailIfErrorLast(&m_Valid);
  ReadOpt(maybeAddress) >> m_PublicHostAddress;
  if (m_PublicHostAddress.ss_family != AF_INET) {
    m_EnableCustomAddress = false;
  }

  m_EnableCustomPort       = CFG.GetBool(m_CFGKeyPrefix + "custom_port.enabled", m_EnableCustomPort);

  optional<uint16_t> maybePublicHostPort = CFG.GetMaybeNonZeroPort(m_CFGKeyPrefix + "custom_port.value");
  if (m_EnableCustomPort)
    CFG.FailIfErrorLast(&m_Valid);

  ReadOpt(maybePublicHostPort) >> m_PublicHostPort;
  if (m_PublicHostPort == 0) { // default inherited from root CRealmConfig
    m_EnableCustomPort = false;
  }

  m_AutoRegister           = CFG.GetBool(m_CFGKeyPrefix + "auto_register", m_AutoRegister);
  m_UserNameCaseSensitive  = CFG.GetBool(m_CFGKeyPrefix + "username.case_sensitive", m_UserNameCaseSensitive);
  m_PassWordCaseSensitive  = CFG.GetBool(m_CFGKeyPrefix + "password.case_sensitive", m_PassWordCaseSensitive);

  m_UserName               = CFG.GetString(m_CFGKeyPrefix + "username", m_UserName);
  m_PassWord               = CFG.GetString(m_CFGKeyPrefix + "password", m_PassWord);
  if (!m_UserNameCaseSensitive) m_UserName = ToLowerCase(m_UserName);
  if (!m_PassWordCaseSensitive) m_PassWord = ToLowerCase(m_PassWord);

  m_LicenseeName           = CFG.GetString(m_CFGKeyPrefix + "licensee", m_LicenseeName);

  m_ExeAuthUseCustomVersionData   = CFG.GetBool(m_CFGKeyPrefix + "exe_auth.custom", m_ExeAuthUseCustomVersionData);
  m_ExeAuthIgnoreVersionError = CFG.GetBool(m_CFGKeyPrefix + "exe_auth.ignore_version_error", m_ExeAuthIgnoreVersionError);

  if (CFG.Exists(m_CFGKeyPrefix + "login.hash_type")) {
    m_LoginHashType = CFG.GetStringIndex(m_CFGKeyPrefix + "login.hash_type", {"pvpgn", "classic.battle.net"}, REALM_AUTH_PVPGN);
    CFG.FailIfErrorLast(&m_Valid);
  } else if (!m_LoginHashType.has_value()) {
    static_assert(REALM_TYPE_PVPGN == REALM_AUTH_PVPGN);
    static_assert(REALM_TYPE_BATTLENET_CLASSIC == REALM_AUTH_BATTLENET);
    m_LoginHashType = m_Type;
  }

  if (CFG.Exists(m_CFGKeyPrefix + "expansion")) {
    m_GameIsExpansion   = static_cast<bool>(CFG.GetStringIndex(m_CFGKeyPrefix + "expansion", {"roc", "tft"}, SELECT_EXPANSION_TFT));
    CFG.FailIfErrorLast(&m_Valid);
  }

  optional<Version> war3Version = CFG.GetMaybeVersion(m_CFGKeyPrefix + "game_version");
  ReadOpt(war3Version) >> m_GameVersion;

  optional<Version> authWar3Version = CFG.GetMaybeVersion(m_CFGKeyPrefix + "exe_auth.version");
  if (authWar3Version.has_value()) m_ExeAuthVersion = authWar3Version.value();
  else if (m_GameVersion.has_value()) m_ExeAuthVersion = m_GameVersion.value();

  // These are optional, since they can be figured out with bncsutil.
  optional<vector<uint8_t>> authExeVersion = CFG.GetMaybeUint8Vector(m_CFGKeyPrefix + "exe_auth.version_details", 4);
  if (m_ExeAuthUseCustomVersionData) CFG.FailIfErrorLast(&m_Valid);
  optional<vector<uint8_t>> authExeVersionHash = CFG.GetMaybeUint8Vector(m_CFGKeyPrefix + "exe_auth.version_hash", 4);
  if (m_ExeAuthUseCustomVersionData) CFG.FailIfErrorLast(&m_Valid);
  string authExeInfo = CFG.GetString(m_CFGKeyPrefix + "exe_auth.info");

  if (m_ExeAuthUseCustomVersionData) {
    ReadOpt(authExeVersion) >> m_ExeAuthVersionDetails;
    ReadOpt(authExeVersionHash) >> m_ExeAuthVersionHash;
    if (!authExeInfo.empty()) m_ExeAuthInfo = authExeInfo;
  } else {
    m_ExeAuthVersionDetails.reset();
    m_ExeAuthVersionHash.reset();
    m_ExeAuthInfo.clear();
  }

  if (m_ExeAuthVersionDetails.has_value() && m_ExeAuthVersion.has_value()) {
    if (m_ExeAuthVersion->first != m_ExeAuthVersionDetails.value()[3] || m_ExeAuthVersion->second != m_ExeAuthVersionDetails.value()[2]) {
      Print("[CONFIG] Error - mismatch between <" + m_CFGKeyPrefix + "exe_auth.version> and <" + m_CFGKeyPrefix + "exe_auth.version_details>");
      m_Valid = false;
    }
  }

  if (m_GameVersion.has_value() && m_ExeAuthVersion.has_value() && m_GameVersion.value() != m_ExeAuthVersion.value()) {
    Print("[CONFIG] Experimental Warning - mismatch between <" + m_CFGKeyPrefix + "game_version> and <" + m_CFGKeyPrefix + "exe_auth.version>");
  }

  m_FirstChannel           = CFG.GetString(m_CFGKeyPrefix + "first_channel", m_FirstChannel);
  m_SudoUsers              = CFG.GetSet(m_CFGKeyPrefix + "sudo_users", ',', true, false, m_SudoUsers);
  m_Admins                 = CFG.GetSet(m_CFGKeyPrefix + "admins", ',', true, false, m_Admins);
  m_CryptoHosts            = CFG.GetSet(m_CFGKeyPrefix + "crypto_hosts", ',', true, false, m_CryptoHosts);

  m_ReHostCounterTemplate  = CFG.GetGameNameTemplate(m_CFGKeyPrefix + "game_list.rehost.name_template", m_ReHostCounterTemplate);
  m_LobbyNameTemplate      = CFG.GetGameNameTemplate(m_CFGKeyPrefix + "game_list.lobby.name_template", m_LobbyNameTemplate);
  m_WatchableNameTemplate  = CFG.GetGameNameTemplate(m_CFGKeyPrefix + "game_list.watchable.name_template", m_WatchableNameTemplate);

  m_LobbyDisplayPriority       = CFG.GetEnum<RealmBroadcastDisplayPriority>(m_CFGKeyPrefix + "game_list.lobby.display.priority", TO_ARRAY("none", "low", "high"), m_LobbyDisplayPriority);
  m_WatchableDisplayPriority   = CFG.GetEnum<RealmBroadcastDisplayPriority>(m_CFGKeyPrefix + "game_list.watchable.display.priority", TO_ARRAY("none", "low", "high"), m_WatchableDisplayPriority);

  m_MaxGameNameFixedCharsSize = CountTemplateFixedChars(m_ReHostCounterTemplate).value_or(MAX_GAME_NAME_SIZE) + max(
    CountTemplateFixedChars(m_LobbyNameTemplate).value_or(MAX_GAME_NAME_SIZE),
    CountTemplateFixedChars(m_WatchableNameTemplate).value_or(MAX_GAME_NAME_SIZE)
  );
  if (m_MaxGameNameFixedCharsSize >= MAX_GAME_NAME_SIZE) {
    Print("[CONFIG] Game name templates are invalid or too long");
    m_Valid = false;
  }

  m_MaxUploadSize          = CFG.GetUint32(m_CFGKeyPrefix + "map_transfers.max_size", m_MaxUploadSize);

  m_ConsoleLogChat         = CFG.GetBool(m_CFGKeyPrefix + "logs.console.chat", m_ConsoleLogChat);
  m_FloodQuotaLines        = CFG.GetUint8(m_CFGKeyPrefix + "flood.lines", m_FloodQuotaLines + 1) - 1;
  m_FloodQuotaTime         = CFG.GetUint8(m_CFGKeyPrefix + "flood.time", m_FloodQuotaTime);
  m_VirtualLineLength      = CFG.GetUint16(m_CFGKeyPrefix + "flood.wrap", m_VirtualLineLength);
  m_MaxLineLength          = CFG.GetUint16(m_CFGKeyPrefix + "flood.max_size", m_MaxLineLength);
  m_FloodImmune            = CFG.GetBool(m_CFGKeyPrefix + "flood.immune", m_FloodImmune);

  if (0 == m_FloodQuotaLines) {
    m_FloodQuotaLines = 1;
    Print("[CONFIG] Error - Invalid value provided for <" + m_CFGKeyPrefix + "flood.lines>.");
  }
  if (100 < m_FloodQuotaLines) {
    m_FloodQuotaLines = 100;
    Print("[CONFIG] Error - Invalid value provided for <" + m_CFGKeyPrefix + "flood.lines>.");
  }
  if (60 < m_FloodQuotaTime) {
    m_FloodQuotaTime = 60;
    Print("[CONFIG] Error - Invalid value provided for <" + m_CFGKeyPrefix + "flood.time>.");
  }
  if (0 == m_VirtualLineLength || 256 < m_VirtualLineLength) {
    m_VirtualLineLength = 256;
    Print("[CONFIG] Error - Invalid value provided for <" + m_CFGKeyPrefix + "flood.wrap>.");
  }
  if (m_MaxLineLength < 6 || 256 < m_MaxLineLength) {
    m_MaxLineLength = 256;
    Print("[CONFIG] Error - Invalid value provided for <" + m_CFGKeyPrefix + "flood.max_size>.");
  }
  const uint32_t maxDeductedLineLength = static_cast<uint32_t>(m_VirtualLineLength) * static_cast<uint32_t>(m_FloodQuotaLines);
  if (static_cast<uint32_t>(m_MaxLineLength) > maxDeductedLineLength) {
    m_MaxLineLength = maxDeductedLineLength;
    // PvPGN defaults make no sense: 40x5=200 seems logical, but in fact the 5th line is not allowed.
    Print("[CONFIG] using <" + m_CFGKeyPrefix + "flood.max_size = " + to_string(m_MaxLineLength) + ">");
  }

  m_WhisperErrorReply      = CFG.GetString(m_CFGKeyPrefix + "protocol.whisper.error_reply", m_WhisperErrorReply);

  if (m_WhisperErrorReply.empty()) {
    switch (m_Locale) {
      case PvPGNLocale::kDEDE:
        m_WhisperErrorReply = "Dieser Nutzer ist nicht eingeloggt.";
        break;
      case PvPGNLocale::kESES:
        m_WhisperErrorReply = "Ese usuario no está conectado.";
        break;
      case PvPGNLocale::kENUS:
      default:
        m_WhisperErrorReply = "That user is not logged on.";
        break;
    }
  }

  m_QueryGameLists         = CFG.GetBool(m_CFGKeyPrefix + "queries.games_list.enabled", m_QueryGameLists);

  m_UnverifiedRejectCommands      = CFG.GetBool(m_CFGKeyPrefix + "unverified_users.reject_commands", m_UnverifiedRejectCommands);
  m_UnverifiedCannotStartGame     = CFG.GetBool(m_CFGKeyPrefix + "unverified_users.reject_start", m_UnverifiedCannotStartGame);
  m_UnverifiedAutoKickedFromLobby = CFG.GetBool(m_CFGKeyPrefix + "unverified_users.auto_kick", m_UnverifiedAutoKickedFromLobby);
  m_AlwaysSpoofCheckPlayers       = CFG.GetBool(m_CFGKeyPrefix + "unverified_users.always_verify", m_AlwaysSpoofCheckPlayers);

  vector<string> commandPermissions = {"disabled", "sudo", "sudo_unsafe", "rootadmin", "admin", "verified_owner", "owner", "verified", "auto", "potential_owner", "unverified"};

  m_CommandCFG             = new CCommandConfig(
    CFG, m_CFGKeyPrefix, false, m_UnverifiedRejectCommands,
    CFG.GetStringIndex(m_CFGKeyPrefix + "commands.common.permissions", commandPermissions, m_InheritOnlyCommandCommonBasePermissions),
    CFG.GetStringIndex(m_CFGKeyPrefix + "commands.hosting.permissions", commandPermissions, m_InheritOnlyCommandHostingBasePermissions),
    CFG.GetStringIndex(m_CFGKeyPrefix + "commands.moderator.permissions", commandPermissions, m_InheritOnlyCommandModeratorBasePermissions),
    CFG.GetStringIndex(m_CFGKeyPrefix + "commands.admin.permissions", commandPermissions, m_InheritOnlyCommandAdminBasePermissions),
    CFG.GetStringIndex(m_CFGKeyPrefix + "commands.bot_owner.permissions", commandPermissions, m_InheritOnlyCommandBotOwnerBasePermissions)
  );

  m_Enabled                       = CFG.GetBool(m_CFGKeyPrefix + "enabled", m_Enabled);

  optional<sockaddr_storage> customBindAddress = CFG.GetMaybeAddress(m_CFGKeyPrefix + "bind_address");
  ReadOpt(customBindAddress) >> m_BindAddress;
}

void CRealmConfig::Reset()
{
  m_CommandCFG = nullptr;
}

CRealmConfig::~CRealmConfig()
{
  delete m_CommandCFG;
}
