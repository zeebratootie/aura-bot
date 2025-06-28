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

#include "config_bot.h"
#include "../util.h"
#include "../net.h"
#include "../file_util.h"
#include "../map.h"
#include "../pjass.h"
#include "../command.h"

#include <utility>
#include <fstream>

using namespace std;

//
// CBotConfig
//

CBotConfig::CBotConfig(CConfig& CFG)
{
  m_Enabled                      = CFG.GetBool("hosting.enabled", true);
  m_ExtractJASS                  = CFG.GetBool("game.extract_jass.enabled", true);
  m_Warcraft3Path                = CFG.GetMaybeDirectory("game.install_path");
  m_Warcraft3DataVersion         = CFG.GetMaybeVersion("game.install_version");
  CFG.FailIfErrorLast();
  m_MapPath                      = CFG.GetDirectory("bot.maps_path", CFG.GetHomeDir() / filesystem::path("maps"));
  m_MapCFGPath                   = CFG.GetDirectory("bot.map_configs_path", CFG.GetHomeDir() / filesystem::path("mapcfgs"));
  m_MapCachePath                 = CFG.GetDirectory("bot.map_cache_path", CFG.GetHomeDir() / filesystem::path("mapcache"));
  m_JASSPath                     = CFG.GetDirectory("bot.jass_path", CFG.GetHomeDir() / filesystem::path("jass"));
  m_GameSavePath                 = CFG.GetDirectory("bot.save_path", CFG.GetHomeDir() / filesystem::path("saves"));

  // Non-configurable
  m_AliasesPath                  = CFG.GetHomeDir() / filesystem::path("aliases.ini");

  m_MainLogPath                  = CFG.GetPath("bot.log_path", CFG.GetHomeDir() / filesystem::path("aura.log"));
  m_RemoteLogPath                = CFG.GetPath("hosting.log_remote.file", CFG.GetHomeDir() / filesystem::path("remote.log"));

  set<string> supportedGameVersionStrings = CFG.GetSetSensitive("hosting.game_versions.supported", ',', true, false, {});
  set<Version> supportedGameVersions;
  for (const auto& versionString : supportedGameVersionStrings) {
    optional<Version> maybeVersion = ParseGameVersion(versionString);
    if (!maybeVersion.has_value()) {
      Print("[CONFIG] <hosting.game_versions.supported> invalid entry <<" + versionString + ">>.");
      CFG.SetFailed();
    } else {
      supportedGameVersions.insert(maybeVersion.value());
    }
  }
  m_SupportedGameVersions        = vector<Version>(supportedGameVersions.begin(), supportedGameVersions.end());
  stable_sort(m_SupportedGameVersions.begin(), m_SupportedGameVersions.end());

  m_TargetCommunity              = CFG.GetBool("hosting.game_versions.community", false);

  m_GameLocaleModDefault         = CFG.GetEnumSensitive<W3ModLocale>("hosting.game_locale.mod.default", TO_ARRAY("enUS", "deDE", "esES", "esMX", "frFR", "itIT", "koKR", "plPL", "ptBR", "ruRU", "zhCN", "zhTW"), W3ModLocale::kENUS);
  m_GameLocaleLangID             = CFG.GetUint16("hosting.game_locale.lang_id.default", 0);

  m_AllowJASS                    = CFG.GetBool("maps.jass.enabled", true);
  m_AllowLua                     = CFG.GetStringIndex("maps.lua.enabled", {"never", "always", "auto"}, MAP_ALLOW_LUA_AUTO);

#ifdef DISABLE_PJASS
  m_ValidateJASS                 = CFG.GetBool("maps.validators.jass.enabled", false);
  if (m_ValidateJASS) {
    Print("[CONFIG] warning - <maps.validators.jass.enabled = yes> unsupported in this Aura distribution");
    Print("[CONFIG] warning - <maps.validators.jass.enabled = yes> requires compilation without #define DISABLE_PJASS");
    m_ValidateJASS = false;
  }
#else
  m_ValidateJASS                 = CFG.GetBool("maps.validators.jass.enabled", true);
#endif

  if (!CFG.GetBool("maps.validators.jass.syntax.enabled", true)) {
    m_ValidateJASSFlags.set(PJASS_OPTIONS_NOSYNTAXERROR);
  }
  if (!CFG.GetBool("maps.validators.jass.semantics.enabled", true)) {
    m_ValidateJASSFlags.set(PJASS_OPTIONS_NOSEMANTICERROR);
  }
  if (!CFG.GetBool("maps.validators.jass.runtime.enabled", true)) {
    m_ValidateJASSFlags.set(PJASS_OPTIONS_NORUNTIMEERROR);
  }

  if (CFG.GetStringIndex("maps.validators.jass.filters_signatures", {"permissive", "strict"}, PJASS_PERMISSIVE) == PJASS_STRICT) {
    m_ValidateJASSFlags.set(PJASS_OPTIONS_FILTER);
  }
  if (CFG.GetStringIndex("maps.validators.jass.globals_initialization", {"permissive", "strict"}, PJASS_PERMISSIVE) == PJASS_STRICT) {
    m_ValidateJASSFlags.set(PJASS_OPTIONS_CHECKGLOBALSINIT);
  }
  if (CFG.GetStringIndex("maps.validators.jass.string_hashes", {"permissive", "strict"}, PJASS_PERMISSIVE) == PJASS_STRICT) {
    m_ValidateJASSFlags.set(PJASS_OPTIONS_CHECKSTRINGHASH);
  }
  if (CFG.GetStringIndex("maps.validators.jass.overflows", {"permissive", "strict"}, PJASS_PERMISSIVE) == PJASS_STRICT) {
    m_ValidateJASSFlags.set(PJASS_OPTIONS_CHECKNUMBERLITERALS);
  }

  m_LogRemoteMode                = CFG.GetStringIndex("hosting.log_remote.mode", {"none", "file", "network", "mixed"}, LOG_REMOTE_MODE_NETWORK);
  m_LogGameChat                  = CFG.GetStringIndex("hosting.log_chat", {"never", "allowed", "always"}, LOG_GAME_CHAT_NEVER);
  m_MinHostCounter               = CFG.GetUint32("hosting.namepace.first_game_id", 100) & 0x00FFFFFF;

  m_MaxLobbies                   = CFG.GetUint16("hosting.games_quota.max_lobbies", 1);
  m_MaxStartedGames              = CFG.GetUint16("hosting.games_quota.max_started", 20);
  m_MaxJoinInProgressGames       = CFG.GetUint16("hosting.games_quota.max_join_in_progress", 0);
  m_MaxTotalGames                = CFG.GetUint32("hosting.games_quota.max_total", 20);
  m_AutoRehostQuotaConservative  = CFG.GetBool("hosting.games_quota.auto_rehost.conservative", false);

  m_AutomaticallySetGameOwner    = CFG.GetBool("hosting.game_owner.from_creator", true);
  m_EnableEndGame                = CFG.GetBool("hosting.early_end.enabled", true);

  m_EnableDeleteOversizedMaps    = CFG.GetBool("maps.storage.delete_huge.enabled", false);
  m_MaxSavedMapSize              = CFG.GetUint32("maps.storage.delete_huge.size", 0x6400); // 25 MiB

  optional<filesystem::path> maybeGreeting = CFG.GetMaybePath("bot.greeting_path");
  if (maybeGreeting.has_value() && !maybeGreeting.value().empty()) {
    m_Greeting = ReadChatTemplate(maybeGreeting.value());
  }

  m_StrictSearch                 = CFG.GetBool("bot.load_maps.strict_search", false);
  m_MapSearchShowSuggestions     = CFG.GetBool("bot.load_maps.show_suggestions", true);
  m_EnableCFGCache               = CFG.GetBool("bot.load_maps.cache.enabled", true);
  m_CFGCacheRevalidateAlgorithm  = CFG.GetEnum<CacheRevalidationMethod>("bot.load_maps.cache.revalidation.algorithm", TO_ARRAY("never", "always", "modified"), CacheRevalidationMethod::kModified);
  m_LANReHostCounterTemplate     = CFG.GetGameNameTemplate("lan_realm.rehost.name_template", "-{COUNT}");
  m_LANLobbyNameTemplate         = CFG.GetGameNameTemplate("lan_realm.lobby.name_template", "{NAME}{COUNTER}");
  m_LANWatchableNameTemplate     = CFG.GetGameNameTemplate("lan_realm.watchable.name_template", "{NAME}{COUNTER}");

  m_MaxGameNameFixedCharsSize    = CountTemplateFixedChars(m_LANReHostCounterTemplate).value_or(MAX_GAME_NAME_SIZE) + max(
    CountTemplateFixedChars(m_LANLobbyNameTemplate).value_or(MAX_GAME_NAME_SIZE),
    CountTemplateFixedChars(m_LANWatchableNameTemplate).value_or(MAX_GAME_NAME_SIZE)
  );
  if (m_MaxGameNameFixedCharsSize >= MAX_GAME_NAME_SIZE) {
    Print("[CONFIG] Game name templates are invalid or too long");
    CFG.SetFailed();
  }

  vector<string> commandPermissions = {"disabled", "sudo", "sudo_unsafe", "rootadmin", "admin", "verified_owner", "owner", "verified", "auto", "potential_owner", "unverified"};

  m_LANCommandCFG = new CCommandConfig(
    CFG, "lan_realm.", false, false,
    CFG.GetStringIndex("lan_realm.commands.common.permissions", commandPermissions, COMMAND_PERMISSIONS_AUTO),
    CFG.GetStringIndex("lan_realm.commands.hosting.permissions", commandPermissions, COMMAND_PERMISSIONS_AUTO),
    CFG.GetStringIndex("lan_realm.commands.moderator.permissions", commandPermissions, COMMAND_PERMISSIONS_AUTO),
    CFG.GetStringIndex("lan_realm.commands.admin.permissions", commandPermissions, COMMAND_PERMISSIONS_AUTO),
    CFG.GetStringIndex("lan_realm.commands.bot_owner.permissions", commandPermissions, COMMAND_PERMISSIONS_AUTO)
  );

#ifdef DEBUG
  m_LogLevel                     = CFG.GetEnum<LogLevel>("bot.log_level", TO_ARRAY("emergency", "alert", "critical", "error", "warning", "notice", "info", "debug", "trace", "trace2", "trace3"), LogLevel::kInfo);
#else
  m_LogLevel                     = CFG.GetEnum<LogLevel>("bot.log_level", TO_ARRAY("emergency", "alert", "critical", "error", "warning", "notice", "info", "debug"), LogLevel::kInfo);
#endif
  m_ExitOnStandby                = CFG.GetBool("bot.exit_on_standby", false);

  // Master switch mainly intended for CLI. CFG key provided for completeness.
  m_EnableBNET                   = CFG.GetMaybeBool("bot.toggle_every_realm");

  m_SudoKeyWord                  = CFG.GetString("bot.keywords.sudo", "sudo");

  CFG.Accept("bot.home_path.allow_mismatch");
}

void CBotConfig::Reset()
{
  m_LANCommandCFG = nullptr;
}

CBotConfig::~CBotConfig()
{
  delete m_LANCommandCFG;
}
