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

#include "aura.h"
#include "util.h"
#include "file_util.h"
#include "os_util.h"
#include "bncsutil_interface.h"
#include <crc32/crc32.h>
#include <sha1/sha1.h>
#include "auradb.h"
#include "mdns.h"
#include <csvparser/csvparser.h>
#include "config/config.h"
#include "config/config_bot.h"
#include "config/config_db.h"
#include "config/config_realm.h"
#include "config/config_game.h"
#include "config/config_irc.h"
#include "socket.h"
#include "connection.h"
#include "realm.h"
#include "map.h"
#include "game_seeker.h"
#include "game_user.h"
#include "pjass.h"
#include "protocol/game_protocol.h"
#include "protocol/gps_protocol.h"
#include "game.h"
#include "cli.h"
#include "integration/irc.h"
#include "protocol/vlan_protocol.h"
#include "test/runner.h"
#include <utf8/utf8.h>

#include <csignal>
#include <cstdlib>
#include <thread>
#include <fstream>
#include <string>
#include <bitset>
#include <iterator>
#include <exception>
#include <system_error>
#include <thread>
#include <locale>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <ws2def.h>
#include <iphlpapi.h>
#include <mstcpip.h>
#include <process.h>
#include <timeapi.h>
#endif

using namespace std;

#undef FD_SETSIZE
#define FD_SETSIZE 512

bool                  gRestart     = false;
volatile sig_atomic_t gGracefulExit = 0;

constexpr int APP_METRICS_LOBBY_SAMPLE_RATE = 10;
constexpr size_t APP_METRICS_LOBBY_CAPACITY = 100;
constexpr int APP_METRICS_GAME_SAMPLE_RATE = 10;
constexpr size_t APP_METRICS_GAME_CAPACITY = 100;
constexpr int APP_METRICS_FRAME_SAMPLE_RATE = 10;
constexpr size_t APP_METRICS_FRAME_CAPACITY = 100;

inline unsigned int GetThreadPoolSize()
{
  unsigned int n = thread::hardware_concurrency();
  return (n > 1) ? (n - 1) : 1;
}

inline void GetAuraHome(const CCLI& cliApp, filesystem::path& homeDir)
{
  if (cliApp.m_HomePath.has_value()) {
    homeDir = cliApp.m_HomePath.value();
    return;
  }
  PLATFORM_STRING_TYPE homeDirString = GetEnvironmentVariableTrimmed(PLATFORM_STRING("AURA_HOME"));
  if (!homeDirString.empty()) {
    homeDir = filesystem::path(homeDirString);
    NormalizeDirectory(homeDir);
    return;
  }
  if (cliApp.m_CFGPath.has_value()) {
    homeDir = cliApp.m_CFGPath.value().parent_path();
    NormalizeDirectory(homeDir);
    return;
  }

  homeDir = GetExeDirectory();
}

inline filesystem::path GetConfigPath(const CCLI& cliApp, const filesystem::path& homeDir)
{
  if (!cliApp.m_CFGPath.has_value()) return homeDir / "config.ini";
  if (!cliApp.m_UseStandardPaths && (cliApp.m_CFGPath.value() == cliApp.m_CFGPath.value().filename())) {
    return homeDir / cliApp.m_CFGPath.value();
  } else {
    return cliApp.m_CFGPath.value();
  }
}

inline filesystem::path GetConfigAdapterPath(const CCLI& cliApp, const filesystem::path& homeDir)
{
  if (!cliApp.m_CFGAdapterPath.has_value()) return homeDir / "legacy-config-adapter.ini";
  if (!cliApp.m_UseStandardPaths && (cliApp.m_CFGAdapterPath.value() == cliApp.m_CFGAdapterPath.value().filename())) {
    return homeDir / cliApp.m_CFGAdapterPath.value();
  } else {
    return cliApp.m_CFGAdapterPath.value();
  }
}

inline bool LoadConfig(CConfig& CFG, CCLI& cliApp, const filesystem::path& homeDir)
{
  const filesystem::path configPath = GetConfigPath(cliApp, homeDir);
  const bool isCustomConfigFile = cliApp.m_CFGPath.has_value();
  bool isDirectSuccess = false;

  // Config adapter is a back-compat mechanism
  if (cliApp.m_CFGAdapterPath.has_value()) {
    CConfig configAdapter;
    const filesystem::path configAdapterPath = GetConfigAdapterPath(cliApp, homeDir);
    const bool adapterSuccess = configAdapter.Read(configAdapterPath);
    if (!adapterSuccess) {
      filesystem::path cwd;
      try {
        cwd = filesystem::current_path();
      } catch (...) {}
      NormalizeDirectory(cwd);

      Print("[AURA] required config adapter file not found [" + PathToString(configAdapterPath) + "]");
      if (!cliApp.m_UseStandardPaths && configAdapterPath.parent_path() == homeDir.parent_path() && (cwd.empty() || homeDir.parent_path() != cwd.parent_path())) {
        Print("[HINT] --config-adapter was resolved relative to [" + PathToAbsoluteString(homeDir) + "]");
        Print("[HINT] use --stdpaths to read [" + PathToString(cwd / configAdapterPath.filename()) + "]");
      }
      return false;
    }
    isDirectSuccess = CFG.Read(configPath, &configAdapter);
  } else {
    isDirectSuccess = CFG.Read(configPath, nullptr);
  }

  if (!isDirectSuccess && isCustomConfigFile) {
    filesystem::path cwd;
    try {
      cwd = filesystem::current_path();
    } catch (...) {}
    NormalizeDirectory(cwd);
    Print("[AURA] required config file not found [" + PathToString(configPath) + "]");
    if (!cliApp.m_UseStandardPaths && configPath.parent_path() == homeDir.parent_path() && (cwd.empty() || homeDir.parent_path() != cwd.parent_path())) {
      Print("[HINT] --config was resolved relative to [" + PathToAbsoluteString(homeDir) + "]");
      Print("[HINT] use --stdpaths to read [" + PathToString(cwd / configPath.filename()) + "]");
    }
#ifdef _WIN32
    Print("[HINT] using --config=<FILE> is not recommended, prefer --homedir=<DIR>, or setting %AURA_HOME% instead");
#else
    Print("[HINT] using --config=<FILE> is not recommended, prefer --homedir=<DIR>, or setting $AURA_HOME instead");
#endif
    Print("[HINT] both alternatives auto-initialize \"config.ini\" from \"config-example.ini\" in the same folder");
    return false;
  }
  const bool homePathMatchRequired = CFG.GetBool("bot.home_path.allow_mismatch", false);
  if (isCustomConfigFile) {
    bool pathsMatch = configPath.parent_path() == homeDir.parent_path();
    if (!pathsMatch) {
      try {
        pathsMatch = filesystem::absolute(configPath.parent_path()) == filesystem::absolute(homeDir.parent_path());
      } catch (...) {}
    }
    if (homePathMatchRequired && !pathsMatch) {
      Print("[AURA] error - config file is not located within home dir [" + PathToString(homeDir) + "] - this is not recommended");
      Print("[HINT] to skip this check and execute Aura nevertheless, set <bot.home_path.allow_mismatch = yes> in your config file");
      Print("[HINT] paths in your config file [" + PathToString(configPath) + "] will be resolved relative to the home dir");
      return false;
    } else if (cliApp.m_HomePath.has_value()) {
      Print("[AURA] using --homedir=" + PathToString(homeDir));
    } else {
#ifdef _WIN32
      Print("[AURA] using %AURA_HOME%=" + PathToString(homeDir));
#else
      Print("[AURA] using $AURA_HOME=" + PathToString(homeDir));
#endif
    }
  }

  if (isDirectSuccess) {
    CFG.SetHomeDir(move(homeDir));

    if (cliApp.m_CFGAdapterPath.has_value()) {
      const string baseMigratedFileName = "config-migrated";
      const vector<uint8_t> migratedBytes = CFG.Export();
      filesystem::path migratedPath;
      uint32_t loopCounter = 0;
      do {
        if ((0 < loopCounter) && (loopCounter % 100 == 0)) {
          // Just so it doesn't look like Aura hung up.
          Print("[AURA] destination file [" + PathToString(migratedPath) + "] already exists");
        }
        string migratedFileName;
        if (loopCounter > 0) {
          migratedFileName = migratedFileName + "." + to_string(loopCounter) + ".ini";
        } else {
          migratedFileName = baseMigratedFileName + ".ini";
        }
        migratedPath = configPath.parent_path() / filesystem::path(migratedFileName);
        ++loopCounter;
      } while (migratedPath == configPath || FileExists(migratedPath));

      Print("[AURA] exporting updated configuration to [" + PathToString(migratedPath) + "]...");
      if (!FileWrite(migratedPath, migratedBytes.data(), migratedBytes.size())) {
        Print("[AURA] error exporting configuration file");
        return false;
      }
      Print("[AURA] configuration exported OK");
      Print("[AURA] before starting Aura again, please check the contents of the exported file, and rename it");
      Print("[AURA] see the CONFIG.md file for up-to-date documentation on supported config keys, and their accepted values");
      return true;
    }

    return true;
  }

  const filesystem::path configExamplePath = homeDir / filesystem::path("config-example.ini");

  vector<uint8_t> exampleContents;
  if (!FileRead(configExamplePath, exampleContents, MAX_READ_FILE_SIZE) || exampleContents.empty()) {
    // But Aura can actually work without a config file ;)
    Print("[AURA] config.ini, config-example.ini not found within home dir [" + PathToString(homeDir) + "].");
    Print("[AURA] using automatic configuration");
  } else {
    Print("[AURA] copying config-example.ini to config.ini...");
    FileWrite(configPath, reinterpret_cast<const uint8_t*>(exampleContents.data()), exampleContents.size());
    if (!CFG.Read(configPath)) {
      Print("[AURA] error initializing config.ini");
      return false;
    }
  }

  CFG.SetHomeDir(move(homeDir));
  return true;
}

inline PLATFORM_STRING_TYPE GetAuraTitle(shared_ptr<CGame> detailsGame, size_t lobbyCount, size_t gameCount, bool hasRehost, optional<size_t> fps)
{
  const static PLATFORM_STRING_TYPE HyphenConnector = PLATFORM_STRING(" - ");
  const static PLATFORM_STRING_TYPE DetailsLobbyPrefix = PLATFORM_STRING(" - Lobby: ");
  const static PLATFORM_STRING_TYPE DetailsGamePrefix = PLATFORM_STRING(" - Playing: ");
  const static PLATFORM_STRING_TYPE SingleLobbySuffix = PLATFORM_STRING(" hosted lobby");
  const static PLATFORM_STRING_TYPE PluralLobbySuffix = PLATFORM_STRING(" hosted lobbies");
  const static PLATFORM_STRING_TYPE SingleGameSuffix = PLATFORM_STRING(" hosted game");
  const static PLATFORM_STRING_TYPE PluralGameSuffix = PLATFORM_STRING(" hosted games");
  const static PLATFORM_STRING_TYPE IdleSuffix = PLATFORM_STRING(" - Idle");
  const static PLATFORM_STRING_TYPE ColumnSeparator = PLATFORM_STRING(" | ");
  const static PLATFORM_STRING_TYPE RehostingFragment = PLATFORM_STRING("Auto-rehosting");
  const static PLATFORM_STRING_TYPE EmptyString = PLATFORM_STRING("");
  const static PLATFORM_STRING_TYPE FPSUnits = PLATFORM_STRING(" FPS");
  const bool showDetails = detailsGame != nullptr;

  PLATFORM_STRING_TYPE titleText = PLATFORM_STRING(AURA_APP_NAME);

  if (showDetails) {
    string detailsText = detailsGame->GetStatusDescription();
#ifdef _WIN32
    PLATFORM_STRING_TYPE detailsTextPlatform;
    utf8::utf8to16(detailsText.begin(), detailsText.end(), back_inserter(detailsTextPlatform));
#else
    PLATFORM_STRING_TYPE& detailsTextPlatform = detailsText;
#endif
    titleText += (lobbyCount == 1 ? DetailsLobbyPrefix : DetailsGamePrefix) + detailsTextPlatform;
  } else if (lobbyCount == 0 && gameCount == 0) {
    titleText += IdleSuffix;
  } else if (lobbyCount > 0 && gameCount > 0) {
    titleText += (
      HyphenConnector +
      ToDecStringCPlatform(lobbyCount) + (lobbyCount > 1 ? PluralLobbySuffix : SingleLobbySuffix) +
      HyphenConnector +
      ToDecStringCPlatform(gameCount) + (gameCount > 1 ? PluralGameSuffix : SingleGameSuffix)
    );
  } else if (lobbyCount > 0) {
    titleText += HyphenConnector + ToDecStringCPlatform(lobbyCount) + (lobbyCount > 1 ? PluralLobbySuffix : SingleLobbySuffix);
  } else {
    titleText += HyphenConnector + ToDecStringCPlatform(gameCount) + (gameCount > 1 ? PluralGameSuffix : SingleGameSuffix);
  }

  if (fps.has_value()) {
    titleText += ColumnSeparator + ToDecStringCPlatform(fps.value()) + FPSUnits;
  }

  if (hasRehost) {
    titleText += ColumnSeparator + RehostingFragment;
  }

  return titleText;
}

//
// main
//

int main(const int argc, char** argv)
{
  int exitCode = 0;

  // seed the PRNG
  srand(static_cast<uint32_t>(time(nullptr)));

  // disable sync since we don't use cstdio anyway
  ios_base::sync_with_stdio(false);

#ifdef _WIN32
  // print UTF-8 to the console
  SetConsoleOutputCP(CP_UTF8);
#endif

  signal(SIGINT, [](int32_t) -> void {
    if (gGracefulExit == 1) {
      Print("[!!!] caught signal SIGINT, exiting NOW");
      exit(1);
    } else {
      Print("[!!!] caught signal SIGINT, exiting gracefully...");
      gGracefulExit = 1;
    }
  });

#ifndef _WIN32
  // disable SIGPIPE since some systems like OS X don't define MSG_NOSIGNAL

  signal(SIGPIPE, SIG_IGN);
#endif

#ifdef _WIN32
  unsigned int timerResolution = 0;
  for (unsigned int i = 1; i <= 5; ++i) {
    // increase timer resolution from ~15.6 ms default
    if (timeBeginPeriod(i) == TIMERR_NOERROR) {
      timerResolution = i;
      break;
    }
  }
#endif

#ifdef _WIN32
  // initialize winsock

  WSADATA wsadata;

  if (WSAStartup(MAKEWORD(2, 2), &wsadata) != 0) {
    Print("[AURA] error starting winsock");
    return 1;
  }

  // increase process priority
  SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
#endif

  if (GetIsHostBigEndian()) {
    Print("[AURA] warning - big endian system support is experimental");
  }

  // extra scopes for tracking lifetimes
  {
    optional<CAura> gAura;
    {
      CCLI cliApp;
      CLIResult cliResult = cliApp.Parse(argc, argv);
      switch (cliResult) {
        case CLIResult::kInfoAndQuit:
          cliApp.RunInfoActions();
          exitCode = 0;
          break;
        case CLIResult::kError:
          Print("[AURA] invalid CLI usage - please see CLI.md");
          exitCode = 1;
          break;
        case CLIResult::kTest:
        case CLIResult::kOk:
        case CLIResult::kConfigAndQuit: {
          CConfig CFG;
          filesystem::path homeDir;
          GetAuraHome(cliApp, homeDir);
          if (!LoadConfig(CFG, cliApp, homeDir)) {
            Print("[AURA] error loading configuration");
            exitCode = 1;
            break;
          }
          if (cliResult == CLIResult::kConfigAndQuit) {
            exitCode = 0;
            break;
          }
          // initialize aura
          gAura.emplace(CFG, cliApp);
          if (!gAura->GetReady()) {
            exitCode = 1;
            Print("[AURA] initialization failure");
          }
        }
      }
    }

    if (gAura.has_value() && gAura->GetReady()) {
      // loop start

      gAura->UpdateClock();
      gAura->m_LoopTicks = gAura->m_ClockTicks;

      while (!gAura->Update())
        ;

      gAura->AwaitSettled();

      // loop end - shut down
      Print("[AURA] shutting down");
    }
  }


#ifdef _WIN32
  // shutdown winsock

  WSACleanup();

  // shutdown timer

  // Hack to disable timer APIs in ReleaseLite distributions
  if (timerResolution > 0) {
    timeEndPeriod(timerResolution);
  }
#endif

  // restart the program

  if (gRestart)
  {
#ifdef _WIN32
    _spawnl(_P_OVERLAY, argv[0], argv[0], nullptr);
#else
    execl(argv[0], argv[0], nullptr);
#endif
  }

  return exitCode;
}

//
// CAura
//

CAura::CAura(CConfig& CFG, const CCLI& nCLI)
  : m_ScriptsExtracted(false),
    m_Exiting(false),
    m_ExitingSoon(false),
    m_Ready(true),
    m_AutoReHosted(false),

    m_LogLevel(LogLevel::kDebug),
    m_LoopTicks(APP_MIN_TICKS),
    m_ClockTicks(APP_MIN_TICKS),
    m_ClockTime(APP_MIN_TICKS),
    m_LastPerformanceWarningTicks(APP_MIN_TICKS),
    m_SupportsModernSlots(false),
    m_MDNSDependency(OptionalDependencyMode::kUnknown),
    m_DPPDependency(OptionalDependencyMode::kUnknown),
    m_FoundDeps(APP_FOUND_DEPS_NONE),

    m_LastServerID(0xFu),
    m_HostCounter(0u),
    m_ReplacingLobbiesCounter(0u),
    m_HistoryGameID(0u),
    m_MaxGameNameSize(MAX_GAME_NAME_SIZE),

    m_ThreadPool(BS::light_thread_pool(GetThreadPoolSize())),
    m_DataBaseConfig(nullptr),
    m_GameDefaultConfig(nullptr),
    m_RealmDefaultConfig(nullptr),
    m_CommandDefaultConfig(new CCommandConfig()),

    m_DB(nullptr),
    //m_GameSetup(nullptr),
    //m_AutoRehostGameSetup(nullptr),

    m_ReloadContext(nullptr),
    m_SudoContext(nullptr),

    m_Version(AURA_VERSION),
    m_RepositoryURL(AURA_REPOSITORY_URL),
    m_IssuesURL(AURA_ISSUES_URL),

    m_Discord(CDiscord(CFG)),
    m_IRC(CIRC(CFG)),
    m_Net(CNet(CFG)),
    m_Config(CBotConfig(CFG)),
    m_ConfigPath(CFG.GetFile()),
    m_PerfMetrics(
      APP_METRICS_LOBBY_SAMPLE_RATE, APP_METRICS_LOBBY_CAPACITY,
      APP_METRICS_GAME_SAMPLE_RATE, APP_METRICS_GAME_CAPACITY,
      APP_METRICS_FRAME_SAMPLE_RATE, APP_METRICS_FRAME_CAPACITY
    )
{
  m_Discord.m_Aura = this;
  m_IRC.m_Aura = this;
  m_Net.m_Aura = this;

  if constexpr (sizeof(void*) == 4) {
    Print("[AURA] Aura version " + m_Version + " - 32 bits");
  } else {
    Print("[AURA] Aura version " + m_Version + " - 64 bits");
  }

  if (!CFG.GetSuccess() || !LoadDefaultConfigs(CFG, &m_Net.m_Config) || !LoadDataBaseConfig(CFG)) {
    Print("[CONFIG] Error: Critical errors found in " + PathToString(m_ConfigPath.filename()));
    m_Ready = false;
    return;
  }

  nCLI.OverrideConfig(this);
  OnLoadConfigs();
  
  m_DB = new CAuraDB(this, m_DataBaseConfig);
  if (m_DB->HasError()) {
    Print("[CONFIG] Error: Critical errors found in [" + PathToString(m_DB->GetFile()) + "]: " + m_DB->GetError());
    m_Ready = false;
    return;
  }
  m_HistoryGameID = m_DB->GetLatestHistoryGameId();

  // Eagerly install as shell extension.
  if (m_DB->GetIsFirstRun()) {
    LoadMapAliases();
    LoadIPToCountryData(CFG);
    if (nCLI.GetInitSystem().value_or(true)) {
      InitSystem();
    }
  } else if (nCLI.GetInitSystem().value_or(false)) {
    InitSystem();
  }

  if (!m_Net.Init()) {
    Print("[AURA] error - close active instances of Warcraft, and/or pause LANViewer to initialize Aura.");
    m_Ready = false;
    return;
  }

  if (m_Net.m_Config.m_UDPEnableCustomPortTCP4) {
    Print("[AURA] broadcasting games port " + to_string(m_Net.m_Config.m_UDPCustomPortTCP4) + " over LAN");
  }

  m_RealmsIdentifiers.resize(16);
  if (m_Config.m_EnableBNET.has_value()) {
    if (m_Config.m_EnableBNET.value()) {
      Print("[AURA] all realms forcibly set to ENABLED <bot.toggle_every_realm = on>");
    } else {
      Print("[AURA] all realms forcibly set to DISABLED <bot.toggle_every_realm = off>");
    }
  }
  bitset<120> definedRealms;
  if (m_Config.m_EnableBNET.value_or(true)) {
    LoadBNETs(CFG, definedRealms, false);
  }

  try {
    filesystem::create_directory(m_Config.m_MapPath);
  } catch (...) {
    Print("[AURA] warning - <bot.maps_path> is not a valid directory");
  }

  try {
    filesystem::create_directory(m_Config.m_MapCFGPath);
  } catch (...) {
    Print("[AURA] warning - <bot.map.configs_path> is not a valid directory");
  }

  try {
    filesystem::create_directory(m_Config.m_MapCachePath);
  } catch (...) {
    Print("[AURA] warning - <bot.map.cache_path> is not a valid directory");
  }

  try {
    filesystem::create_directory(m_Config.m_JASSPath);
  } catch (...) {
    Print("[AURA] warning - <bot.jass_path> is not a valid directory");
  }

  if (m_Config.m_ExtractJASS) {
    // extract common.j and blizzard.j from War3Patch.mpq or War3.mpq (depending on version) if we can
    // these two files are necessary for calculating <map.scripts_hash.blizz>, and <map.scripts_hash.sha1> when loading maps so we make sure they are available
    // see CMap :: Load for more information
    m_ScriptsExtracted = ExtractScripts() == 2;
    if (!CopyScripts()) {
      m_Ready = false;
      return;
    }
  } else {
    CheckScripts();
  }

  if (m_Config.m_EnableCFGCache) {
    UpdateCFGCacheEntries();
  }

#ifndef DISABLE_TESTS
  if (nCLI.m_RunTests) {
    uint16_t result = TestRunner::Run();
    if (result == 0) {
      Print("[TEST] Tests OK");
    } else {
      Print("[TEST] Tests failed with code " + to_string(result));
      m_Ready = false;
    }
    return;
  }
#endif

  if (!nCLI.QueueActions(this)) {
    m_Ready = false;
    return;
  }

  vector<string> invalidKeys = CFG.GetInvalidKeys(definedRealms);
  if (!invalidKeys.empty()) {
    Print("[CONFIG] warning - some keys are misnamed: " + JoinStrings(invalidKeys));
  }

  if (m_Realms.empty() && m_Config.m_EnableBNET.value_or(true))
    Print("[AURA] notice - no enabled battle.net connections configured");
  if (!m_IRC.GetIsEnabled())
    Print("[AURA] notice - no irc connection configured");
#ifndef DISABLE_DPP
  if (!m_Discord.GetIsEnabled())
    Print("[AURA] notice - no discord connection configured");
#endif

  if (m_Realms.empty() && !m_IRC.GetIsEnabled() && !m_Discord.GetIsEnabled() && m_PendingActions.empty()) {
    Print("[AURA] error - no inputs connected");
    m_Ready = false;
    return;
  }

  UpdateMetaData();
}

bool CAura::LoadBNETs(CConfig& CFG, bitset<120>& definedRealms, bool strict)
{
  // load the battle.net connections
  // we're just loading the config data and creating the CRealm classes here,
  // the connections are established later (in the CRealm::Update function)

  bool isInvalidConfig = false;
  map<string, uint8_t> uniqueInputIds;
  map<string, uint8_t> uniqueNames;
  vector<CRealmConfig*> realmConfigs(120, nullptr);
  const bool hasGlobalHostName = CFG.Exists("realm_global.host_name");
  for (uint8_t i = 1; i <= 120; ++i) {
    if (!hasGlobalHostName && !CFG.Exists("realm_" + to_string(i) + ".host_name")) {
      continue;
    }
    CRealmConfig* thisConfig = new CRealmConfig(CFG, m_RealmDefaultConfig, i);
    if (m_Config.m_EnableBNET.has_value()) {
      thisConfig->m_Enabled = m_Config.m_EnableBNET.value();
    }
    if (thisConfig->m_UserName.empty() || thisConfig->m_PassWord.empty()) {
      thisConfig->m_Enabled = false;
    }
    if (!thisConfig->m_Enabled) {
      delete thisConfig;
    } else if (!thisConfig->m_Valid) {
      Print("[CONFIG] <realm_" + ToDecString(i) + "> - critical errors found");
      isInvalidConfig = true;
      delete thisConfig;
    } else if (uniqueNames.find(thisConfig->m_UniqueName) != uniqueNames.end()) {
      Print("[CONFIG] <realm_" + to_string(uniqueNames.at(thisConfig->m_UniqueName) + 1) + ".unique_name> must be different from <realm_" + ToDecString(i) + ".unique_name>");
      isInvalidConfig = true;
      delete thisConfig;
    } else if (uniqueInputIds.find(thisConfig->m_InputID) != uniqueInputIds.end()) {
      Print("[CONFIG] <realm_" + to_string(uniqueNames.at(thisConfig->m_UniqueName) + 1) + ".input_id> must be different from <realm_" + ToDecString(i) + ".input_id>");
      isInvalidConfig = true;
      delete thisConfig;
    } else {
      uniqueNames[thisConfig->m_UniqueName] = i - 1;
      uniqueInputIds[thisConfig->m_InputID] = i - 1;
      realmConfigs[i - 1] = thisConfig;
      definedRealms.set(i - 1);
    }
  }

  if (strict && isInvalidConfig) {
    for (auto& realmConfig : realmConfigs) {
      delete realmConfig;
    }
    return false;
  }

  m_RealmsByHostCounter.clear();
  size_t i = m_Realms.size();
  while (i--) {
    string inputID = m_Realms[i]->GetInputID();
    if (uniqueInputIds.find(inputID) == uniqueInputIds.end()) {
      EventRealmDeleted(m_Realms[i]);
      m_Realms[i].reset();
      m_RealmsByInputID.erase(inputID);
      m_Realms.erase(m_Realms.begin() + signed_cast<ptrdiff_t>(i));
    }
  }

  size_t longestGameParticlesSize = 0;
  for (const auto& entry : uniqueInputIds) {
    shared_ptr<CRealm> matchingRealm = GetRealmByInputId(entry.first);
    CRealmConfig* realmConfig = realmConfigs[entry.second];
    if (matchingRealm == nullptr) {
      matchingRealm = make_shared<CRealm>(this, realmConfig);
      m_Realms.push_back(matchingRealm);
      m_RealmsByInputID[entry.first] = matchingRealm;
      m_RealmsIdentifiers.push_back(entry.first);
      // m_RealmsIdentifiers[matchingRealm->GetInternalID()] == matchingRealm->GetInputID();
      if (MatchLogLevel(LogLevel::kDebug)) {
        Print("[AURA] server found: " + matchingRealm->GetUniqueDisplayName());
      }
    } else {
      matchingRealm->ReloadConfig(realmConfig);
    }

    if (realmConfig->m_MaxGameNameFixedCharsSize > longestGameParticlesSize)
      longestGameParticlesSize = realmConfig->m_MaxGameNameFixedCharsSize;

    m_RealmsByHostCounter[matchingRealm->GetHostCounterID()] = matchingRealm;
    realmConfig->Reset();
    delete realmConfig;
  }

  if (MAX_GAME_NAME_SIZE - longestGameParticlesSize < m_MaxGameNameSize) {
    m_MaxGameNameSize = MAX_GAME_NAME_SIZE - longestGameParticlesSize;
  }
  return true;
}

bool CAura::CopyScripts()
{
  set<filesystem::path> checkedPaths;
  for (const auto& version : m_Config.m_SupportedGameVersions) {
    // Try to use manually extracted files already available in <bot.map.configs_path>
    filesystem::path autoExtractedCommonPath = m_Config.m_JASSPath / filesystem::path("common-" + GetScriptsVersionRangeHeadString(version) + ".j");
    filesystem::path autoExtractedBlizzardPath = m_Config.m_JASSPath / filesystem::path("blizzard-" + GetScriptsVersionRangeHeadString(version) + ".j");
    if (checkedPaths.find(autoExtractedCommonPath) != checkedPaths.end() || checkedPaths.find(autoExtractedBlizzardPath) != checkedPaths.end()) {
      continue;
    }
    bool commonExists = FileExists(autoExtractedCommonPath);
    bool blizzardExists = FileExists(autoExtractedBlizzardPath);
    if (commonExists && blizzardExists) {
      checkedPaths.insert(autoExtractedCommonPath);
      checkedPaths.insert(autoExtractedBlizzardPath);
      continue;
    }

    if (!commonExists) {
      filesystem::path manuallyExtractedCommonPath = m_Config.m_JASSPath / filesystem::path("common.j");
      try {
        filesystem::copy_file(manuallyExtractedCommonPath, autoExtractedCommonPath, filesystem::copy_options::skip_existing);
      } catch (const exception& e) {
        Print("[AURA] " + string(e.what()));
        return false;
      }
      Print("[AURA] file copied [" + PathToString(manuallyExtractedCommonPath) + "] -> [" + PathToString(autoExtractedCommonPath) + "]");
    }
    if (!blizzardExists) {
      filesystem::path manuallyExtractedBlizzardPath = m_Config.m_JASSPath / filesystem::path("blizzard.j");
      try {
        filesystem::copy_file(manuallyExtractedBlizzardPath, autoExtractedBlizzardPath, filesystem::copy_options::skip_existing);
      } catch (const exception& e) {
        Print("[AURA] " + string(e.what()));
        return false;
      }
      Print("[AURA] file copied [" + PathToString(manuallyExtractedBlizzardPath) + "] -> [" + PathToString(autoExtractedBlizzardPath) + "]");
    }
    checkedPaths.insert(autoExtractedCommonPath);
    checkedPaths.insert(autoExtractedBlizzardPath);
  }
  return true;
}

void CAura::CheckScripts()
{
  set<filesystem::path> checkedPaths;
  for (const auto& version : m_Config.m_SupportedGameVersions) {
    filesystem::path autoExtractedCommonPath = m_Config.m_JASSPath / filesystem::path("common-" + GetScriptsVersionRangeHeadString(version) + ".j");
    filesystem::path autoExtractedBlizzardPath = m_Config.m_JASSPath / filesystem::path("blizzard-" + GetScriptsVersionRangeHeadString(version) + ".j");
    if (checkedPaths.find(autoExtractedCommonPath) != checkedPaths.end() || checkedPaths.find(autoExtractedBlizzardPath) != checkedPaths.end()) {
      continue;
    }
    bool commonExists = FileExists(autoExtractedCommonPath);
    bool blizzardExists = FileExists(autoExtractedBlizzardPath);
    if (!commonExists && MatchLogLevel(LogLevel::kWarning)) {
      Print("[AURA] Support for v" + ToVersionString(version) + " requires missing file [" + PathToString(autoExtractedCommonPath) + "]");
    }
    if (!blizzardExists && MatchLogLevel(LogLevel::kWarning)) {
      Print("[AURA] Support for v" + ToVersionString(version) + " requires missing file [" + PathToString(autoExtractedBlizzardPath) + "]");
    }
    checkedPaths.insert(autoExtractedCommonPath);
    checkedPaths.insert(autoExtractedBlizzardPath);
  }
}

void CAura::ClearAutoRehost()
{
  if (!m_AutoRehostGameSetup) {
    return;
  }
  m_AutoRehostGameSetup.reset();
}

CAura::~CAura()
{
  m_SudoContext.reset();
  m_ReloadContext.reset();

  delete m_DataBaseConfig;
  delete m_GameDefaultConfig;
  delete m_RealmDefaultConfig;
  delete m_CommandDefaultConfig;

  ClearAutoRehost();

  if (m_GameSetup) {
    m_GameSetup->m_ExitingSoon = true;
  }

  while (!m_Realms.empty()) {
    EventRealmDeleted(m_Realms.back());
    m_Realms.erase(m_Realms.end() - 1);
  }

  m_JoinInProgressGames.clear();

#define CLEAR_GAMES(vec)\
  while (!vec.empty()) {\
    EventGameDeleted(vec.back());\
    vec.erase(vec.end() - 1);\
  }

  CLEAR_GAMES(m_StartedGames)
  CLEAR_GAMES(m_Lobbies)
  CLEAR_GAMES(m_LobbiesPending)

#undef CLEAR_GAMES

  delete m_DB;
}

optional<size_t> CAura::GetFPS() const
{
  auto timeStamps = m_PerfMetrics.globalFrames.GetData();
  if (timeStamps.size() < 2) return nullopt;
  int64_t deltaTicks = timeStamps.back() - timeStamps.front();
  if (deltaTicks < 0) return nullopt;
  size_t fps = static_cast<size_t>(round((double)((int64_t)(timeStamps.size() - 1) * (int64_t)(m_PerfMetrics.globalFrames.GetRate()) * (int64_t)1000) / (double)(deltaTicks)));
  return {fps};
}

vector<Version> CAura::GetSupportedVersionsCrossPlayRangeHeads() const
{
  set<Version> versionHeads;
  for (const auto& version : m_Config.m_SupportedGameVersions) {
    versionHeads.insert(GetScriptsVersionRangeHead(version));
  }
  return vector<Version>(versionHeads.begin(), versionHeads.end());
}

shared_ptr<CGame> CAura::GetMostRecentLobby(bool allowPending) const
{
  if (allowPending && !m_LobbiesPending.empty()) return m_LobbiesPending.back();
  if (m_Lobbies.empty()) return nullptr;
  return m_Lobbies.back();
}

shared_ptr<CGame> CAura::GetMostRecentLobbyFromCreator(const string& fromName) const
{
  for (auto it = rbegin(m_Lobbies); it != rend(m_Lobbies); ++it) {
    if ((*it)->GetCreatorName() == fromName) {
      return (*it);
    }
  }
  return nullptr;
}

shared_ptr<CGame> CAura::GetLobbyByHostCounterExact(uint32_t hostCounter) const
{
  for (const auto& lobby : m_Lobbies) {
    if (lobby->GetHostCounter() == hostCounter) {
      return lobby;
    }
  }
  return nullptr;
}

shared_ptr<CGame> CAura::GetLobbyOrObservableByHostCounterExact(uint32_t hostCounter) const
{
  for (const auto& game : GetJoinableGames()) {
    if (game->GetHostCounter() == hostCounter) {
      return game;
    }
  }
  return nullptr;
}

shared_ptr<CGame> CAura::GetLobbyByHostCounter(uint32_t hostCounter) const
{
  uint32_t baseHostCounter = hostCounter & 0x00FFFFFF;
  for (const auto& lobby : m_Lobbies) {
    uint32_t thisHostCounter = lobby->GetHostCounter();
    if (thisHostCounter == hostCounter || thisHostCounter == baseHostCounter) {
      return lobby;
    }
  }
  return nullptr;
}

shared_ptr<CGame> CAura::GetLobbyOrObservableByHostCounter(uint32_t hostCounter) const
{
  uint32_t baseHostCounter = hostCounter & 0x00FFFFFF;
  for (const auto& game : GetJoinableGames()) {
    uint32_t thisHostCounter = game->GetHostCounter();
    if (thisHostCounter == hostCounter || thisHostCounter == baseHostCounter) {
      return game;
    }
  }
  return nullptr;
}

shared_ptr<CGame> CAura::GetGameByIdentifier(const uint64_t gameIdentifier) const
{
  for (const auto& game : GetAllGames()) {
    if (game->GetGameID() == gameIdentifier) {
      return game;
    }
  }
  return nullptr;
}

shared_ptr<CGame> CAura::GetGameByString(const string& rawInput) const
{
  // See also util.h:CheckTargetGameSyntax
  if (rawInput.empty()) {
    return nullptr;
  }
  string inputGame = ToLowerCase(rawInput);
  if (inputGame == "lobby" || inputGame == "game#lobby") {
    return GetMostRecentLobby();
  }
  if (inputGame == "oldest" || inputGame == "game#oldest") {
    if (m_StartedGames.empty()) return nullptr;
    return m_StartedGames[0];
  }
  if (inputGame == "newest" || inputGame == "latest" || inputGame == "game#newest" || inputGame == "game#latest") {
    if (m_StartedGames.empty()) return nullptr;
    return m_StartedGames[m_StartedGames.size() - 1];
  }
  if (inputGame == "lobby#oldest") {
    if (m_Lobbies.empty()) return nullptr;
    return m_Lobbies[0];
  }
  if (inputGame == "lobby#newest") {
    return GetMostRecentLobby();
  }
  if (inputGame.substr(0, 5) == "game#") {
    inputGame = inputGame.substr(5);
  } else if (inputGame.substr(0, 6) == "lobby#") {
    inputGame = inputGame.substr(6);
  }

  uint64_t gameID = 0;
  try {
    long long value = stoll(inputGame);
    if (value < 0) return nullptr;
    gameID = signed_cast<uint64_t>(value);
  } catch (...) {
    return nullptr;
  }

  return GetGameByIdentifier(gameID);
}

shared_ptr<CRealm> CAura::GetRealmByInputId(const string& inputId) const
{
  auto it = m_RealmsByInputID.find(inputId);
  if (it == m_RealmsByInputID.end()) return nullptr;
  return it->second.lock();
}

shared_ptr<CRealm> CAura::GetRealmByHostCounter(const uint8_t hostCounter) const
{
  auto it = m_RealmsByHostCounter.find(hostCounter);
  if (it == m_RealmsByHostCounter.end()) return nullptr;
  return it->second.lock();
}

shared_ptr<CRealm> CAura::GetRealmByHostName(const string& hostName) const
{
  for (const auto& realm : m_Realms) {
    if (!realm->GetLoggedIn()) continue;
    if (realm->GetIsMirror()) continue;
    if (realm->GetServer() == hostName) return realm;
  }
  return nullptr;
}

ServiceType CAura::FindServiceFromHostName(const string& hostName, void*& location) const
{
  if (m_IRC.MatchHostName(hostName)) {
    return ServiceType::kIRC;
  }
  if (m_Discord.MatchHostName(hostName)) {
    return ServiceType::kDiscord;
  }
  for (const auto& realm : m_Realms) {
    if (realm->GetServer() == hostName) {
      location = realm.get();
      return ServiceType::kRealm;
    }
  }
  return ServiceType::kNone;
}

vector<shared_ptr<CGame>> CAura::GetAllGames() const
{
  vector<shared_ptr<CGame>> joinables;
  joinables.reserve(m_Lobbies.size() + m_StartedGames.size() + m_LobbiesPending.size());
  joinables.insert(joinables.end(), m_Lobbies.begin(), m_Lobbies.end());
  joinables.insert(joinables.end(), m_StartedGames.begin(), m_StartedGames.end());
  joinables.insert(joinables.end(), m_LobbiesPending.begin(), m_LobbiesPending.end());
  return joinables;
}

vector<shared_ptr<CGame>> CAura::GetJoinableGames() const
{
  vector<shared_ptr<CGame>> joinables;
  joinables.reserve(m_Lobbies.size() + m_JoinInProgressGames.size());
  joinables.insert(joinables.end(), m_Lobbies.begin(), m_Lobbies.end());

  for (auto ptr : m_JoinInProgressGames) {
    if (auto game = ptr.lock()) {
      joinables.push_back(game);
    }
  }

  return joinables;
}

AppActionStatus CAura::HandleAction(const AppAction& action)
{
  switch (action.type) {
#ifndef DISABLE_MINIUPNP
    case AppActionType::kUPnP: {
      uint16_t externalPort = integer_cast_lossy<uint16_t>(action.value_1);
      uint16_t internalPort = integer_cast_lossy<uint16_t>(action.value_2);
      switch (action.mode) {
        case AppActionMode::kTCP:
          m_Net.RequestUPnP(NetProtocol::kTCP, externalPort, internalPort, LogLevel::kDebug);
          break;
        case AppActionMode::kUDP:
          m_Net.RequestUPnP(NetProtocol::kUDP, externalPort, internalPort, LogLevel::kDebug);
          break;
        case AppActionMode::kNone:
          UNREACHABLE();
          break;
      }
      return AppActionStatus::kDone;
    }
#endif
    case AppActionType::kHostActiveMirror: {
      bool success = m_GameSetup->RunHost();
      if (!success) {
        // Delete all other pending actions
        return AppActionStatus::kError;
      }
      if (MergePendingLobbies()) {
        m_LastMetaDataUpdateTime.reset();
      }
      return AppActionStatus::kDone;
    }
    case AppActionType::kCommand:
      UNREACHABLE();
      break;
  }

  return AppActionStatus::kError;
}

AppActionStatus CAura::HandleDeferredCommandContext(const LazyCommandContext& lazyCtx)
{
  return CCommandContext::TryDeferred(this, lazyCtx);
}

AppActionStatus CAura::HandleGenericAction(const GenericAppAction& genAction)
{
  switch (genAction.index()) {
    case 0: { // AppAction
      const AppAction& action = std::get<AppAction>(genAction);
      AppActionStatus result = HandleAction(action);
      if (result == AppActionStatus::kWait && action.queuedTicks + 20000 < m_ClockTicks) {
        result = AppActionStatus::kTimeOut;
      }
      return result;
    }
    case 1: { // LazyCommandContext
      const LazyCommandContext& lazyCtx = std::get<LazyCommandContext>(genAction);
      AppActionStatus result = HandleDeferredCommandContext(lazyCtx);
      if (result == AppActionStatus::kWait && lazyCtx.queuedTicks + 20000 < m_ClockTicks) {
        result = AppActionStatus::kTimeOut;
      }
      return result;
    }
  }

  return AppActionStatus::kError;
}

int64_t CAura::GetSelectBlockTime() const
{
  // before we call select we need to determine how long to block for
  // 50 ms is the hard maximum (idle: ~20 FPS)

  int64_t usecBlock = 50000;

  for (const auto& game : m_StartedGames) {
    game->UpdateSelectBlockTime<1000>(usecBlock);
  }
  m_Net.UpdateSelectBlockTime(usecBlock);

  return usecBlock;
}

int64_t CAura::GetSelectBlockTimeRefreshed()
{
  UpdateClock();
  return GetSelectBlockTime();
}

void CAura::UpdateClock()
{
  m_ClockTicks = GetTicks();
  m_ClockTime = GetTime();
}

bool CAura::Update()
{
  m_PerfMetrics.globalFrames.TrySample(m_LoopTicks);

  if (gGracefulExit == 1 || m_ExitingSoon) {
    // Intentionally execute on every loop turn after graceful exit is flagged.
    GracefulExit();
  }

  // 1. pending actions
  bool skipActions = false;
  while (!m_PendingActions.empty()) {
    if (skipActions) {
      m_PendingActions.pop();
      continue;
    }
    AppActionStatus actionResult = HandleGenericAction(m_PendingActions.front());
    if (actionResult == AppActionStatus::kWait) {
      break;
    }
    if (actionResult == AppActionStatus::kError) {
      Print("[AURA] Queued action errored. Pending actions aborted.");
      skipActions = true;
    } else if (actionResult == AppActionStatus::kTimeOut) {
      Print("[AURA] Queued action timed out. Pending actions aborted.");
      skipActions = true;
    }
    m_PendingActions.pop();
  }

  if (m_ReloadContext) {
    TryReloadConfigs();
    assert(m_ReloadContext == nullptr && "m_ReloadContext should be reset");
  }

  if (m_AutoRehostGameSetup && !m_AutoReHosted) {
    if (!(m_GameSetup && (m_GameSetup->GetIsDownloading() || m_GameSetup->GetMirror().GetIsSearching())) &&
      (GetNewGameIsInQuotaAutoReHost() && !GetIsAutoHostThrottled())
    ) {
      m_AutoRehostGameSetup->SetActive();
      AppAction rehostAction = AppAction(AppActionType::kHostActiveMirror);
      m_PendingActions.push(rehostAction);
    }
  }

  bool isStandby = (
    m_Lobbies.empty() && m_StartedGames.empty() &&
    m_Net.GetIsStandby() &&
    !(m_GameSetup && (m_GameSetup->GetIsDownloading() || m_GameSetup->GetMirror().GetIsSearching())) &&
    m_PendingActions.empty() &&
    !m_AutoRehostGameSetup
  );

  if (isStandby && (m_Config.m_ExitOnStandby || (m_ExitingSoon && CheckGracefulExit()))) {
    return true;
  }

  uint32_t numFDs = 0;

  // take every socket we own and throw it in one giant select statement so we can block on all sockets

  int32_t nfds = 0;
  FD_ZERO(&m_ReadFDs);
  FD_ZERO(&m_SendFDs);

  // the current lobby's player sockets

  for (const auto& lobby : m_Lobbies) {
    numFDs += lobby->SetFD(&m_ReadFDs, &m_SendFDs, &nfds);
  }

  // all running games' player sockets

  for (const auto& game : m_StartedGames) {
    numFDs += game->SetFD(&m_ReadFDs, &m_SendFDs, &nfds);
  }

  // all battle.net sockets

  for (const auto& realm : m_Realms) {
    numFDs += realm->SetFD(&m_ReadFDs, &m_SendFDs, &nfds);
  }

  // irc socket
  if (m_IRC.GetIsEnabled()) {
    numFDs += m_IRC.SetFD(&m_ReadFDs, &m_SendFDs, &nfds);
  }

  // UDP sockets, outgoing test connections, observers
  numFDs += m_Net.SetFD(&m_ReadFDs, &m_SendFDs, &nfds);

  struct timeval tv;
  tv.tv_sec  = 0;
  tv.tv_usec = static_cast<long int>(GetSelectBlockTimeRefreshed());

  struct timeval send_tv;
  send_tv.tv_sec  = 0;
  send_tv.tv_usec = 0;

#ifdef _WIN32
  select(1, &m_ReadFDs, nullptr, nullptr, &tv);
  select(1, nullptr, &m_SendFDs, nullptr, &send_tv);
#else
  select(nfds + 1, &m_ReadFDs, nullptr, nullptr, &tv);
  select(nfds + 1, nullptr, &m_SendFDs, nullptr, &send_tv);
#endif

  UpdateClock();

  // update map downloads
  if (m_GameSetup) {
    if (m_GameSetup->Update()) {
      m_GameSetup.reset();
    }
  }

  m_Net.UpdateBeforeGames(&m_ReadFDs, &m_SendFDs);

  // update games, starting from lobbies

  for (auto it = begin(m_Lobbies); it != end(m_Lobbies);) {
#ifdef PROFILING
    auto t = m_PerfMetrics.lobbies.TryStart();
#endif
    if ((*it)->Update(&m_ReadFDs, &m_SendFDs)) {
      if ((*it)->GetExiting()) {
        EventGameDeleted(*it);
        it->reset();
      } else {
        EventGameStarted(*it);
      }
      it = m_Lobbies.erase(it);
      m_LastMetaDataUpdateTime.reset();
    } else {
      (*it)->UpdatePost(&m_SendFDs);
      ++it;
    }
#ifdef PROFILING
    int64_t dt = t->TryEndNano();
    if (dt > 2e6 && MatchLogLevel(LogLevel::kWarning)) {
      Print(Concat("Game update (lobby) took " + to_string(dt / 1e6) + " ms"));
    }
#endif
  }

  for (auto it = begin(m_StartedGames); it != end(m_StartedGames);) {
#ifdef PROFILING
    auto t = m_PerfMetrics.games.TryStart();
#endif
    if ((*it)->Update(&m_ReadFDs, &m_SendFDs)) {
      (*it)->FlushLogs();
      if ((*it)->GetExiting()) {
        EventGameDeleted(*it);
        it->reset();
      } else {
        EventGameRemake(*it);
      }
      it = m_StartedGames.erase(it);
      m_LastMetaDataUpdateTime.reset();
    } else {
      (*it)->UpdatePost(&m_SendFDs);
      ++it;
    }
#ifdef PROFILING
    int64_t dt = t->TryEndNano();
    if (dt > 2e6 && MatchLogLevel(LogLevel::kWarning)) {
      Print(Concat("Game update (started) took " + to_string(dt / 1e6) + " ms"));
    }
#endif
  }

  for (const auto& realm : m_Realms) {
    realm->Update(&m_ReadFDs, &m_SendFDs);
  }

  m_IRC.Update(&m_ReadFDs, &m_SendFDs);
  m_Discord.Update();

  // UDP sockets, outgoing test connections
  m_Net.UpdateAfterGames(&m_ReadFDs, &m_SendFDs);

  // move stuff from pending vectors to their intended places
  m_Net.MergeDownGradedConnections();
  if (MergePendingLobbies()) {
    m_LastMetaDataUpdateTime.reset();
  }

  if (GetTimeIsFirstOrAfterDelay(m_LastMetaDataUpdateTime, 3)) {
    UpdateMetaData();
#ifndef DISABLE_DPP
    if (m_Discord.GetIsEnabled()) {
      shared_ptr<CGame> game = GetMostRecentLobby();
      if (game) {
        m_Discord.SetStatusHostingLobby(game->GetMap()->GetMapTitle(), game->GetCreationTime());
      } else if (!m_StartedGames.empty()) {
        game = m_StartedGames.back();
        m_Discord.SetStatusHostingGame(game->GetMap()->GetMapTitle(), ToNameListSentence(game->GetPlayers()), game->GetCreationTime());
      } else {
        m_Discord.SetStatusIdle();
      }
    }
#endif
  }

  // house-keeping
  ClearStaleContexts();

  UpdateClock();

  const int64_t deltaLoopTicks = m_ClockTicks - m_LoopTicks;
  m_LoopTicks = m_ClockTicks;

  if (numFDs == 0) {
    // we don't have any sockets - Aura is idle, so we should just sleep for a while
    // this results in ~5 FPS

    this_thread::sleep_for(chrono::milliseconds(200));
  } else if (deltaLoopTicks < APP_MIN_FRAME_PERIOD) {
    // prevent incoming traffic from causing CPU starvation,
    // but ensure we respect each game's individual frame rate
    // this effectively tries to cap frame rate to ~300 FPS

    int64_t blockTicks = APP_MIN_FRAME_PERIOD - deltaLoopTicks;
    for (const auto& game : m_StartedGames) {
      game->UpdateSelectBlockTime<1>(blockTicks);
    }
    this_thread::sleep_for(chrono::milliseconds(blockTicks));
  }

  return m_Exiting;
}

void CAura::AwaitSettled()
{
  if (m_GameSetup) {
    m_GameSetup->AwaitSettled();
  }
  if (m_AutoRehostGameSetup) {
    m_AutoRehostGameSetup->AwaitSettled();
  }
  m_ThreadPool.purge();
  m_ThreadPool.wait();
  m_Discord.AwaitSettled();
}

void CAura::EventBNETGameRefreshSuccess(shared_ptr<CRealm> successRealm)
{
  successRealm->ResolveGameBroadcastStatus(true);
}

void CAura::EventBNETGameRefreshError(shared_ptr<CRealm> errorRealm)
{
  if (errorRealm->GetIsGameBroadcastErrored()) {
    return;
  }

  errorRealm->ResolveGameBroadcastStatus(false); 

  // If the game has someone in it, advertise the fail only in the lobby (as it is probably a rehost).
  // Otherwise whisper the game creator that the (re)host failed.

  shared_ptr<CGame> game = errorRealm->GetGameBroadcast();

  if (game->GetHasAnyUser()) {
    game->SendAllChat("Cannot publish game on server [" + errorRealm->GetServer() + "]. Try another name");
  } else {
    switch (game->GetCreatedFromType()) {
      case ServiceType::kRealm:
        if (!game->GetCreatedFromIsExpired()) {
          game->GetCreatedFrom<CRealm>()->QueueWhisper("Cannot publish game on on server [" + errorRealm->GetServer() + "]. Try another name", game->GetCreatorName());
        }
        break;
      case ServiceType::kIRC:
        m_IRC.SendUser("Cannot publish game on server [" + errorRealm->GetServer() + "]. Try another name", game->GetCreatorName());
        break;
      /*
      // FIXME: CAura::EventBNETGameRefreshError SendUser() - Discord case
      case ServiceType::kDiscord:
        m_Discord.SendUser("Unable to create game on server [" + errorRealm->GetServer() + "]. Try another name", game->GetCreatorName());
        break;*/
      default:
        break;
    }
  }

  Print(game->GetLogPrefix() + "cannot publish game on server [" + errorRealm->GetServer() + "]. Try another game name");

  bool earlyExit = false;
  switch (game->m_Config.m_BroadcastErrorHandler) {
    case OnRealmBroadcastErrorHandler::kExitOnMainError:
      if (!errorRealm->GetIsMain()) break;
      // fall through
    case OnRealmBroadcastErrorHandler::kExitOnAnyError:
      earlyExit = true;
      break;
    case OnRealmBroadcastErrorHandler::kExitOnMainErrorIfEmpty:
      if (!errorRealm->GetIsMain()) break;
      // fall through
    case OnRealmBroadcastErrorHandler::kExitOnAnyErrorIfEmpty:
      if (!game->GetHasAnyUser()) {
        // we only close the game if it has no players since we support game rehosting (via !priv and !pub in the lobby)
        earlyExit = true;
      }
      break;
    default:
      break;
  }
  if (earlyExit) {
    game->StopPlayers("failed to publish game");
    game->SetExiting(true);
    return;
  }

  if (game->m_Config.m_BroadcastErrorHandler == OnRealmBroadcastErrorHandler::kExitOnMaxErrors) {
    for (auto& realm : m_Realms) {
      if (!realm->GetEnabled()) {
        continue;
      }
      if (game->GetIsMirror() && realm->GetIsMirror()) {
      // A mirror realm is a realm whose purpose is to mirror games actually hosted by Aura.
      // Do not display external games in those realms.
        continue;
      }
      if (!realm->GetIsGameVersionCompatible(game).value_or(true)) {
        continue;
      }
      if (game->GetIsRealmExcluded(realm->GetServer())) {
        continue;
      }
      if (!realm->GetIsGameBroadcastErrored()) {
        return;
      }
    }

    game->StopPlayers("failed to publish game");
    game->SetExiting(true);
    return;
  }
}

void CAura::EventGameReset(shared_ptr<CGame> game)
{
  for (const auto& ptr : m_ActiveContexts) {
    auto ctx = ptr.lock();
    if (!ctx) continue;
    if (ctx->GetSourceGame() == game) {
      ctx->SetPartiallyDestroyed();
      ctx->ExpireGameSource();
    }
    if (ctx->GetTargetGame() == game) {
      ctx->SetPartiallyDestroyed();
      ctx->ResetTargetGame();
    }
  }

  m_Net.EventGameReset(game);
  UntrackGameJoinInProgress(game);
}

void CAura::EventGameDeleted(shared_ptr<CGame> game)
{
  if (game->GetFromAutoReHost()) {
    m_AutoReHosted = false;
  }

  if (game->GetIsGameDiscoveryActive()) {
    game->SendGameDiscoveryDecreate();
    game->SetGameDiscoveryActive(false);
  }
  for (auto& realm : m_Realms) {
    if (realm->GetGameBroadcast() == game) {
      realm->ResetGameBroadcastData();
      realm->TrySendEnterChat();
    }
    if (realm->GetGameBroadcastPending() == game) {
      realm->ResetGameBroadcastPending();
    }
  }

  if (game->GetIsLobbyOrMirror()) {
    Print("[AURA] deleting lobby [" + game->GetGameName() + "]");
  } else {
    Print("[AURA] deleting game [" + game->GetGameName() + "]");
    if ((game->GetEffectiveTicks() / 1000) < 180) {
      // Do not announce game ended if game lasted less than 3 minutes.
      return;
    }
    if (game->GetGameLoaded()) {
      game->RunGameResults();
    }
    for (auto& realm : m_Realms) {
      if (!realm->GetAnnounceHostToChat()) continue;
      if (game->GetGameLoaded()) {
        realm->QueueChatChannel("Game ended: " + game->GetEndDescription(realm));
        if (game->MatchesCreatedFromRealm(realm)) {
          realm->QueueWhisper("Game ended: " + game->GetEndDescription(realm), game->GetCreatorName());
        }
      }
    }
  }

  EventGameReset(game);
  // CGame::Reset() is the first thing done by CGame::~CGame()
}

void CAura::EventGameRemake(shared_ptr<CGame> game)
{
  // Only called from CGame::Update() while iterating m_StartedGames
  Print("[AURA] remaking game [" + game->GetGameName() + "]");
  m_LobbiesPending.push_back(game);

  /*
  if (game->GetFromAutoReHost()) {
    m_AutoReHosted = true;
  }
  */

  for (auto& realm : m_Realms) {
    realm->TrySetGameBroadcastPending(game);
    if (realm->GetAnnounceHostToChat()) {
      realm->QueueChatChannel(Concat("Game remake: ", SanitizeUTF8(game->GetMap()->GetServerFileName())));
      if (game->MatchesCreatedFromRealm(realm)) {
        realm->QueueWhisper(Concat("Game remake: ", SanitizeUTF8(game->GetMap()->GetServerFileName())), game->GetCreatorName());
      }
    }
  }
}

void CAura::EventGameStarted(shared_ptr<CGame> game)
{
  // Only called from CGame::Update() while iterating m_Lobbies
  Print("[AURA] started game [" + game->GetGameName() + "]");
  m_StartedGames.push_back(game);

  if (game->GetFromAutoReHost()) {
    m_AutoReHosted = false;
  }

  /*
  for (auto& realm : m_Realms) {
    if (!realm->GetAnnounceHostToChat()) continue;
    realm->QueueChatChannel(Concat("Game started: ", SanitizeUTF8(game->GetMap()->GetServerFileName())));
    if (game->MatchesCreatedFromRealm(realm)) {
      realm->QueueWhisper(Concat("Game started: ", SanitizeUTF8(game->GetMap()->GetServerFileName())), game->GetCreatorName());
    }
  }
  */
}

void CAura::EventRealmDeleted(shared_ptr<CRealm> realm)
{
  for (const auto& ptr : m_ActiveContexts) {
    auto ctx = ptr.lock();
    if (!ctx) continue;
    if (ctx->GetSourceRealm() == realm) {
      ctx->SetPartiallyDestroyed();
      ctx->ResetServiceSource();
    }
    if (ctx->GetTargetRealm() == realm) {
      ctx->SetPartiallyDestroyed();
      ctx->m_TargetRealm.reset();
    }
  }

  for (auto& game : GetAllGames()) {
    if (game->MatchesCreatedFromRealm(realm)) {
      game->RemoveCreator();
    }
  }

  if (m_GameSetup && m_GameSetup->MatchesCreatedFromRealm(realm)) {
    m_GameSetup->RemoveCreator();
  }

  m_Net.EventRealmDeleted(realm);
}

bool CAura::ReloadConfigs()
{
  bool success = true;
  optional<Version> WasDataVersion = m_Config.m_Warcraft3DataVersion;
  set<Version> WasVersions = set<Version>(m_Config.m_SupportedGameVersions.begin(), m_Config.m_SupportedGameVersions.end());
  bool WasCacheEnabled = m_Config.m_EnableCFGCache;
  filesystem::path WasMapPath = m_Config.m_MapPath;
  filesystem::path WasCFGPath = m_Config.m_MapCFGPath;
  filesystem::path WasCachePath = m_Config.m_MapCachePath;
  filesystem::path WasJASSPath = m_Config.m_JASSPath;
  CConfig CFG;
  if (!CFG.Read(m_ConfigPath)) {
    Print("[CONFIG] warning - failed to read config file");
  } else if (!LoadAllConfigs(CFG)) {
    Print("[CONFIG] error - bot configuration invalid: not reloaded");
    success = false;
  }
  OnLoadConfigs();
  bitset<120> definedRealms;
  if (!LoadBNETs(CFG, definedRealms, true)) {
    Print("[CONFIG] error - realms misconfigured: not reloaded");
    success = false;
  }
  CDataBaseConfig{CFG}; // suppress warnings for invalid DB keys
  vector<string> invalidKeys = CFG.GetInvalidKeys(definedRealms);
  if (!invalidKeys.empty()) {
    Print("[CONFIG] warning - the following keys are invalid/misnamed: " + JoinStrings(invalidKeys));
  }

  optional<Version> NowDataVersion = m_Config.m_Warcraft3DataVersion;
  set<Version> NowVersions = set<Version>(m_Config.m_SupportedGameVersions.begin(), m_Config.m_SupportedGameVersions.end());
  if (m_Config.m_ExtractJASS) {
    if (!m_ScriptsExtracted || WasDataVersion != NowDataVersion) {
      m_ScriptsExtracted = ExtractScripts() == 2;
    }
    if (NowVersions != WasVersions) {
      CopyScripts();
    }
  } else {
    CheckScripts();
  }

  bool reCachePresets = WasCacheEnabled != m_Config.m_EnableCFGCache;
  if (WasMapPath != m_Config.m_MapPath) {
    try {
      filesystem::create_directory(m_Config.m_MapPath);
    } catch (...) {
      Print("[AURA] warning - <bot.maps_path> is not a valid directory");
    }
    reCachePresets = true;
  }
  if (WasCachePath != m_Config.m_MapCachePath) {
    try {
      filesystem::create_directory(m_Config.m_MapCachePath);
    } catch (...) {
      Print("[AURA] warning - <bot.map.cache_path> is not a valid directory");
    }
    reCachePresets = true;
  }
  if (WasCFGPath != m_Config.m_MapCFGPath) {
    try {
      filesystem::create_directory(m_Config.m_MapCFGPath);
    } catch (...) {
      Print("[AURA] warning - <bot.map.configs_path> is not a valid directory");
    }
  }
  if (WasJASSPath != m_Config.m_JASSPath) {
    try {
      filesystem::create_directory(m_Config.m_JASSPath);
    } catch (...) {
      Print("[AURA] warning - <bot.jass_path> is not a valid directory");
    }
  }

  if (!m_Config.m_EnableCFGCache) {
    m_CFGCacheNamesByMapNames.clear();
  } else if (reCachePresets) {
    UpdateCFGCacheEntries();
  }
  m_Net.OnConfigReload();

  return success;
}

void CAura::TryReloadConfigs()
{
  const bool success = ReloadConfigs();
  if (!m_ReloadContext->GetPartiallyDestroyed()) {
    if (success) {
      m_ReloadContext->SendReply("Reloaded successfully.");
    } else {
      m_ReloadContext->ErrorReply("Reload failed. See the console output.");
    }
  }
  m_ReloadContext.reset();
}

bool CAura::LoadDataBaseConfig(CConfig& CFG)
{
  m_DataBaseConfig = new CDataBaseConfig(CFG);
  return CFG.GetSuccess();
}

bool CAura::LoadDefaultConfigs(CConfig& CFG, CNetConfig* netConfig)
{
  CRealmConfig* RealmDefaultConfig = new CRealmConfig(CFG, netConfig);
  CGameConfig* GameDefaultConfig = new CGameConfig(CFG);

  if (!CFG.GetSuccess()) {
    delete RealmDefaultConfig;
    delete GameDefaultConfig;
    return false;
  }
  
  delete m_RealmDefaultConfig;
  delete m_GameDefaultConfig;

  m_RealmDefaultConfig = RealmDefaultConfig;
  m_GameDefaultConfig = GameDefaultConfig;

  return true;
}

bool CAura::LoadAllConfigs(CConfig& CFG)
{
  CBotConfig BotConfig = CBotConfig(CFG);
  CNetConfig NetConfig = CNetConfig(CFG);
  CIRCConfig IRCConfig = CIRCConfig(CFG);
  CDiscordConfig DiscordConfig = CDiscordConfig(CFG);
  // CDataBaseConfig excluded because it's not actually reloaded

  if (!CFG.GetSuccess()) {
    return false;
  }

  if (!LoadDefaultConfigs(CFG, &NetConfig)) {
    return false;
  }

  // Copy, but prevent double free of CCommandConfig* members
  m_Config = BotConfig;
  m_IRC.m_Config = IRCConfig;
  m_Discord.m_Config = DiscordConfig;
  m_Net.m_Config = NetConfig;

  BotConfig.Reset();
  IRCConfig.Reset();
  DiscordConfig.Reset();
  return true;
}

void CAura::OnLoadConfigs()
{
  m_LogLevel = m_Config.m_LogLevel;

  if (m_Config.m_Warcraft3Path.has_value()) {
    m_GameInstallPath = m_Config.m_Warcraft3Path.value();
  } else if (m_GameInstallPath.empty()) {
    PLATFORM_STRING_TYPE war3Path = GetEnvironmentVariableTrimmed(PLATFORM_STRING("WAR3_HOME"));
    if (!war3Path.empty()) {
      m_GameInstallPath = filesystem::path(war3Path);
    } else {
#ifdef _WIN32
      optional<filesystem::path> maybeInstallPath = MaybeReadRegistryPath(L"SOFTWARE\\Blizzard Entertainment\\Warcraft III", L"InstallPath");
      if (maybeInstallPath.has_value()) {
        m_GameInstallPath = maybeInstallPath.value();
      } else {
        vector<const wchar_t*> tryPaths = {
          L"C:\\Program Files (x86)\\Warcraft III\\",
          L"C:\\Program Files\\Warcraft III\\",
          L"C:\\Games\\Warcraft III\\",
          L"C:\\Warcraft III\\",
          L"D:\\Games\\Warcraft III\\",
          L"D:\\Warcraft III\\"
        };
        error_code ec;
        for (const auto& opt : tryPaths) {
          filesystem::path testPath = opt;
          if (filesystem::is_directory(testPath, ec)) {
            m_GameInstallPath = testPath;
          }
        }
      }
#endif
    }
    if (m_GameInstallPath.empty()) {
#ifdef _WIN32
      // Make sure this error message can be looked up.
      Print("[AURA] Registry error loading key 'Warcraft III\\InstallPath'");
#endif
    } else {
      NormalizeDirectory(m_GameInstallPath);
      Print("[AURA] Using <game.install_path = " + PathToString(m_GameInstallPath) + ">");
    }
  }

  if (m_Config.m_Warcraft3DataVersion.has_value()) {
    m_GameDataVersion = m_Config.m_Warcraft3DataVersion.value();
  } else if (!m_GameDataVersion.has_value() && !m_GameInstallPath.empty() && htons(0xe017) == 0x17e0) {
    optional<Version> AutoVersion = CBNCSUtilInterface::GetGameVersion(m_GameInstallPath);
    if (AutoVersion.has_value()) {
      m_GameDataVersion = AutoVersion.value();
    }
  }

  m_MaxGameNameSize = MAX_GAME_NAME_SIZE - m_Config.m_MaxGameNameFixedCharsSize;

  // Hosting basics: autocomplete <hosting.game_versions.main>, and <hosting.game_versions.supported>
  if (!m_GameDefaultConfig->m_GameVersion.has_value()) {
    if (m_Config.m_SupportedGameVersions.size() == 1) {
      m_GameDefaultConfig->m_GameVersion = m_Config.m_SupportedGameVersions[0];
    } else if (m_Config.m_SupportedGameVersions.empty() && m_GameDataVersion.has_value()) {
      m_Config.m_SupportedGameVersions.push_back(m_GameDataVersion.value());
      m_GameDefaultConfig->m_GameVersion = m_GameDataVersion.value();
    }
  } else if (!GetIsSupportedGameVersion(m_GameDefaultConfig->m_GameVersion.value())) {
    m_Config.m_SupportedGameVersions.push_back(m_GameDefaultConfig->m_GameVersion.value());
    stable_sort(m_Config.m_SupportedGameVersions.begin(), m_Config.m_SupportedGameVersions.end());
  }

  m_SupportsModernSlots = false;

  for (const auto& version : m_Config.m_SupportedGameVersions) {
    if (version >= GAMEVER(1u, 30u) && m_MDNSDependency != OptionalDependencyMode::kRequired) {
      m_MDNSDependency = OptionalDependencyMode::kOptEnhancement;
    }
    if (GetIs24PlayersGameVersion(version)) {
      m_SupportsModernSlots = true;
    }
  }

  if (m_MDNSDependency == OptionalDependencyMode::kUnknown) {
    m_MDNSDependency = OptionalDependencyMode::kNotUseful;
  }
  m_DPPDependency = m_Discord.m_Config.m_Enabled ? OptionalDependencyMode::kOptEnhancement : OptionalDependencyMode::kNotUseful;

  if (!CheckDependencies()) {
#ifdef _WIN32
    Print("[AURA] error - some features are disabled because one or more DLL files are missing.");
#else
    Print("[AURA] error - some features are disabled because one or more shared library files are missing.");
#endif
  }

  m_Lobbies.reserve(m_Config.m_MaxLobbies);
  m_StartedGames.reserve(m_Config.m_MaxStartedGames);
}

uint8_t CAura::ExtractScripts()
{
  if (m_GameInstallPath.empty() || !m_GameDataVersion.has_value()) {
    return 0;
  }

  uint8_t FilesExtracted = 0;
  const filesystem::path MPQFilePath = [&]() {
    if (m_GameDataVersion.value() >= GAMEVER(1u, 28u))
      return m_GameInstallPath / filesystem::path("War3.mpq");
    else
      return m_GameInstallPath / filesystem::path("War3Patch.mpq");
  }();

  void* MPQ = nullptr;
  if (OpenMPQArchive(&MPQ, MPQFilePath)) {
    FilesExtracted += ExtractMPQFile(MPQ, R"(Scripts\common.j)", m_Config.m_JASSPath / filesystem::path("common-" + GetScriptsVersionRangeHeadString(m_GameDataVersion.value()) + ".j"));
    FilesExtracted += ExtractMPQFile(MPQ, R"(Scripts\blizzard.j)", m_Config.m_JASSPath / filesystem::path("blizzard-" + GetScriptsVersionRangeHeadString(m_GameDataVersion.value()) + ".j"));
    CloseMPQArchive(MPQ);
  } else {
#ifdef _WIN32
    uint32_t errorCode = (uint32_t)GetLastOSError();
    string errorCodeString = (
      errorCode == 2 ? "Config error: <game.install_path> is not the WC3 directory" : (
      errorCode == 11 ? "File is corrupted." : (
      (errorCode == 3 || errorCode == 15) ? "Config error: <game.install_path> is not a valid directory" : (
      (errorCode == 32 || errorCode == 33) ? "File is currently opened by another process." : (
      "Error code " + to_string(errorCode)
      ))))
    );
#else
    string errorCodeString = "Error code " + to_string(errno);
#endif
    Print("[AURA] warning - unable to load MPQ archive [" + PathToString(MPQFilePath) + "] - " + errorCodeString);
  }

  return FilesExtracted;
}

bool CAura::LoadMapAliases()
{
  CConfig aliases;
  if (!aliases.Read(m_Config.m_AliasesPath)) {
    return false;
  }

  if (!m_DB->Begin()) {
    Print("[AURA] internal database error - map aliases will not be available");
    return false;
  }

  for (const auto& entry : aliases.GetEntries()) {
    string normalizedAlias = GetNormalizedAlias(entry.first);
    if (normalizedAlias.empty()) continue;
    static_cast<void>(m_DB->AliasAdd(normalizedAlias, entry.second));
  }

  if (!m_DB->Commit()) {
    Print("[AURA] internal database error - map aliases will not be available");
    return false;
  }
  return true;
}

void CAura::LoadIPToCountryData(const CConfig& CFG)
{
  ifstream in;
  filesystem::path GeoFilePath = CFG.GetHomeDir() / filesystem::path("ip-to-country.csv");
  in.open(GeoFilePath.native().c_str(), ios::in);

  if (in.fail()) {
    Print("[AURA] warning - unable to read file [ip-to-country.csv], geolocalization data not loaded");
    return;
  }
  // the begin and commit statements are optimizations
  // we're about to insert ~4 MB of data into the database so if we allow the database to treat each insert as a transaction it will take a LONG time

  if (!m_DB->Begin()) {
    Print("[AURA] internal database error - geolocalization will not be available");
    in.close();
    return;
  }

  string Line, Skip, IP1, IP2, Country;
  CSVParser parser;

  in.seekg(0, ios::end);
  in.seekg(0, ios::beg);

  try {
    while (!in.eof()) {
      getline(in, Line);

      if (Line.empty())
        continue;

      parser << Line;
      parser >> Skip;
      parser >> Skip;
      parser >> IP1;
      parser >> IP2;
      parser >> Country;

      uint32_t IPValue1 = integer_cast_lossy<uint32_t>(stoul(IP1));
      uint32_t IPValue2 = integer_cast_lossy<uint32_t>(stoul(IP2));
      static_cast<void>(m_DB->FromAdd(IPValue1, IPValue2, Country));
    }
  } catch (const exception& e) {
    Print("[AURA] warning - unable to parse file [ip-to-country.csv] (" + string(e.what()) + "), geolocalization data not loaded");
  }

  if (!m_DB->Commit()) {
    Print("[AURA] internal database error - geolocalization will not be available");
  }

  in.close();
}

void CAura::InitContextMenu()
{
#ifdef _WIN32
  DeleteUserRegistryKey(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.w3m");
  DeleteUserRegistryKey(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.w3x");

  wstring scenario = L"WorldEdit.Scenario";
  wstring scenarioEx = L"WorldEdit.ScenarioEx";

  wstring openBaseWithAuraCommand = L"\"";
  openBaseWithAuraCommand += GetExePath().wstring();
  openBaseWithAuraCommand += L"\" \"%1\" --stdpaths";

  wstring openROCWithAuraCommand = openBaseWithAuraCommand + L" --roc";

  wstring openBaseWithAuraPoECommand = L"cmd.exe /c \"" + openBaseWithAuraCommand + L" || pause\"";
  wstring openROCWithAuraPoECommand = L"cmd.exe /c \"" + openROCWithAuraCommand + L" || pause\"";

  SetUserRegistryKey(L"Software\\Classes\\.w3m", L"", scenario.c_str());
  SetUserRegistryKey(L"Software\\Classes\\.w3x", L"", scenarioEx.c_str());
  SetUserRegistryKey(L"Software\\Classes\\WorldEdit.Scenario\\shell\\Host with Aura\\command", L"", openBaseWithAuraPoECommand.c_str());
  SetUserRegistryKey(L"Software\\Classes\\WorldEdit.Scenario\\shell\\Host with Aura - RoC\\command", L"", openROCWithAuraPoECommand.c_str());
  SetUserRegistryKey(L"Software\\Classes\\WorldEdit.ScenarioEx\\shell\\Host with Aura\\command", L"", openBaseWithAuraPoECommand.c_str());
  Print("[AURA] Installed to context menu.");
#endif
}

void CAura::InitPathVariable()
{
  filesystem::path exeDirectory = GetExeDirectory();
  try {
    filesystem::path exeDirectoryAbsolute = filesystem::absolute(exeDirectory);
    EnsureDirectoryInUserPath(exeDirectoryAbsolute);
  } catch (...) {
  }
}

void CAura::InitSystem()
{
  InitContextMenu();
  InitPathVariable();
}

void CAura::UpdateWindowTitle()
{
  shared_ptr<CGame> detailsGame = nullptr;
  if (m_Lobbies.size() == 1) {
    if (m_StartedGames.size() == 0) detailsGame = m_Lobbies.back();
  } else if (m_Lobbies.empty() && m_StartedGames.size() == 1) {
    detailsGame = m_StartedGames.back();
  }
  PLATFORM_STRING_TYPE windowTitle = GetAuraTitle(detailsGame, m_Lobbies.size(), m_StartedGames.size(), m_AutoRehostGameSetup != nullptr, GetFPS());
  SetWindowTitle(windowTitle);
}

void CAura::UpdateMetaData()
{
  UpdateWindowTitle();
  m_LastMetaDataUpdateTime = GetClockTime();
}

void CAura::UpdateCFGCacheEntries()
{
  m_CFGCacheNamesByMapNames.clear();

  // Preload map.Localpath -> mapcache entries
  const vector<filesystem::path> cacheFiles = FilesMatch(m_Config.m_MapCachePath, FILE_EXTENSIONS_CONFIG);
  for (const auto& cfgName : cacheFiles) {
    string localPathString = CConfig::ReadString(m_Config.m_MapCachePath / cfgName, "map.local_path");
    filesystem::path localPath = localPathString;
    localPath = localPath.lexically_normal();
    try {
      if (localPath == localPath.filename() || filesystem::absolute(localPath.parent_path()) == filesystem::absolute(m_Config.m_MapPath.parent_path())) {
        string mapString = PathToString(localPath.filename());
        string cfgString = PathToString(cfgName);
        if (mapString.empty() || cfgString.empty()) continue;
        m_CFGCacheNamesByMapNames[localPath.filename()] = cfgString;
      }
    } catch (...) {
      // filesystem::absolute may throw errors
    }
  }
}

void CAura::ClearStaleContexts()
{
  auto it = m_ActiveContexts.rbegin();
  auto itEnd = m_ActiveContexts.rend();
  while (it != itEnd) {
    if (it->expired()) {
      it = vector<weak_ptr<CCommandContext>>::reverse_iterator(m_ActiveContexts.erase((++it).base()));
    } else {
      ++it;
    }
  }

  if (m_ActiveContexts.size() > 5) {
    Print("[DEBUG] weak_ptr<CCommandContext> leak detected (m_ActiveContexts size is " + to_string(m_ActiveContexts.size()) + ")");
  }
}

void CAura::ClearStaleFileChunks()
{
  vector<filesystem::path> staleCacheKeys;
  for (const auto& cacheEntries : m_CachedFileContents) {
    if (cacheEntries.second.bytes.expired()) {
      staleCacheKeys.push_back(cacheEntries.first);
    }
  }
  for (const auto& staleCacheKey : staleCacheKeys) {
    m_CachedFileContents.erase(staleCacheKey);
  }
}

void CAura::LogPersistent(const string& logText)
{
  ofstream writeStream;
  writeStream.open(m_Config.m_MainLogPath.native().c_str(), ios::binary | ios::app);

  if (writeStream.fail()) {
    return;
  }

  LogStream(writeStream, logText, true);
  writeStream.close();
}

void CAura::LogRemoteFile(const string& logText)
{
  ofstream writeStream;
  writeStream.open(m_Config.m_RemoteLogPath.native().c_str(), ios::binary | ios::app);

  if (writeStream.fail()) {
    return;
  }

  LogStream(writeStream, logText, true);
  writeStream.close();
}

void CAura::LogPerformanceWarning(const TaskType taskType, const void* taskPtr, const int64_t frameDrift, const int64_t oldInterval, const int64_t newInterval)
{
  // something is going terribly wrong - Aura is probably starved of resources
  // print a message because even though this will take more resources it should provide some information to the administrator for future reference
  // other solutions - dynamically modify the latency, request higher priority, terminate other games, ???
  if (!MatchLogLevel(LogLevel::kWarning)) {
    return;
  }
  int64_t Ticks = m_ClockTicks;
  if (Ticks < m_LastPerformanceWarningTicks + 5000) {
    return;
  }

  string prefix;
  if (taskType == TaskType::kGameFrame) {
    prefix = static_cast<const CGame*>(taskPtr)->GetLogPrefix();
  }
  char buffer[256];
#ifdef _MSC_VER
  int length = _snprintf_s(
    buffer, 256, 255,
#else
  int length = snprintf(
    buffer, 256,
#endif
    "%swarning - frame drift of %lldms (expected %lldms interval, rescheduling to %lldms)",
   prefix.c_str(), (long long int)frameDrift, (long long int)oldInterval, (long long int)newInterval
  );
  buffer[length] = '\x00';
  Print(buffer);

  m_LastPerformanceWarningTicks = Ticks;
}

void CAura::GracefulExit()
{
  m_ExitingSoon = true;
  m_Config.m_Enabled = false;

  ClearAutoRehost();

  if (m_GameSetup) {
    m_GameSetup->m_ExitingSoon = true;
  }
  m_Discord.m_ExitingSoon = true;

  for (auto& game : GetJoinableGames()) {
    game->AnnounceDecreateToRealms();
  }

  for (auto& game : m_StartedGames) {
    game->SendEveryoneElseLeftAndDisconnect("shutdown");
  }

  for (auto& lobby : m_Lobbies) {
    lobby->StopPlayers("shutdown");
    lobby->SetExiting(true);
  }

  m_Net.GracefulExit();

  for (auto& realm : m_Realms) {
    realm->Disable();
  }

  m_IRC.Disable();
  m_Discord.Disable();
}

bool CAura::CheckDependencies()
{
  bool success = true;
  if (m_MDNSDependency != OptionalDependencyMode::kNotUseful && !(m_FoundDeps & APP_FOUND_DEPS_MDNS)) {
#ifndef DISABLE_MDNS
    bool foundMDNS = CMDNS::CheckLibrary();
    if (foundMDNS) {
      SET_TINY(m_FoundDeps, APP_FOUND_DEPS_MDNS);
    } else {
      success = false;
      UNSET_TINY(m_FoundDeps, APP_FOUND_DEPS_MDNS);
#ifdef _WIN32
      Print("[AURA] warning - MDNS not found. Install MDNS to enable LAN support for v1.30 onwards: https://support.apple.com/en-us/106380");
#else
      Print("[AURA] warning - MDNS not found. Install MDNS to enable LAN support for v1.30 onwards.");
#endif
    }
#else
    Print("[AURA] warning - LAN for v1.30 onwards is not supported in this Aura distribution.");
#endif
  }
  if (m_DPPDependency != OptionalDependencyMode::kNotUseful && !(m_FoundDeps & APP_FOUND_DEPS_DPP)) {
    bool foundDPP = CDiscord::CheckLibraries();
    if (foundDPP) {
      SET_TINY(m_FoundDeps, APP_FOUND_DEPS_DPP);
    } else {
      success = false;
      UNSET_TINY(m_FoundDeps, APP_FOUND_DEPS_DPP);
      m_Discord.m_Config.m_Enabled = false;
      Print("[AURA] error - Discord service disabled because some required files are missing.");
    }
  }
  return success;
}

bool CAura::CheckGracefulExit()
{
  /* Already checked:
    (m_Lobbies.empty() && m_StartedGames.empty() &&
    !m_Net.m_HealthCheckInProgress &&
    !(m_GameSetup && (m_GameSetup->GetIsDownloading() || m_GameSetup->GetMirror().GetIsSearching())) &&
    m_PendingActions.empty())
  */

  if (m_IRC.GetIsEnabled() && m_IRC.GetSocket()->GetConnected()) {
    return false;
  }
  for (auto& realm : m_Realms) {
    if (realm->GetSocket()->GetConnected()) {
      return false;
    }
  }
  if (!m_Net.CheckGracefulExit()) {
    return false;
  }
  return true;
}

bool CAura::GetIsSupportedGameVersion(const Version& version) const
{
  return find(
    m_Config.m_SupportedGameVersions.begin(),
    m_Config.m_SupportedGameVersions.end(),
    version
  ) != m_Config.m_SupportedGameVersions.end();
}

bool CAura::GetNewGameIsInQuota() const
{
  if (m_Lobbies.size() + m_JoinInProgressGames.size() - m_ReplacingLobbiesCounter >= m_Config.m_MaxLobbies) return false;
  if (m_Lobbies.size() + m_StartedGames.size() >= m_Config.m_MaxTotalGames) return false;
  return true;
}

bool CAura::GetNewGameIsInQuotaReplace() const
{
  if (m_Lobbies.size() + m_JoinInProgressGames.size() - m_ReplacingLobbiesCounter > m_Config.m_MaxLobbies) return false;
  if (m_Lobbies.size() + m_StartedGames.size() >= m_Config.m_MaxTotalGames) return false;
  return true;
}

bool CAura::GetNewGameIsInQuotaConservative() const
{
  if (m_Lobbies.size() + m_JoinInProgressGames.size() - m_ReplacingLobbiesCounter >= m_Config.m_MaxLobbies) return false;
  if (m_StartedGames.size() >= m_Config.m_MaxStartedGames) return false;
  if (m_Lobbies.size() + m_StartedGames.size() >= m_Config.m_MaxTotalGames) return false;
  return true;
}

bool CAura::GetNewGameIsInQuotaAutoReHost() const
{
  if (m_Config.m_AutoRehostQuotaConservative) {
    return GetNewGameIsInQuotaConservative();
  } else {
    return GetNewGameIsInQuota();
  }
}

bool CAura::GetIsAutoHostThrottled() const
{
  if (m_Realms.empty()) return false;
  return m_LastGameAutoHostedTicks.has_value() && m_LastGameAutoHostedTicks.value() + AUTO_REHOST_COOLDOWN_TICKS >= m_ClockTicks;
}

bool CAura::CreateGame(shared_ptr<CGameSetup> gameSetup)
{
  if (!m_Config.m_Enabled) {
    gameSetup->m_Ctx->ErrorReply("The bot is disabled", CHAT_SEND_SOURCE_ALL | CHAT_LOG_INCIDENT);
    return false;
  }

  if (gameSetup->m_Name.size() > m_MaxGameNameSize) {
    gameSetup->m_Ctx->ErrorReply("The game name is too long (max " + to_string(m_MaxGameNameSize) + " characters)", CHAT_SEND_SOURCE_ALL | CHAT_LOG_INCIDENT);
    return false;
  }

  if (!gameSetup->m_Map) {
    gameSetup->m_Ctx->ErrorReply("The currently loaded game setup is invalid", CHAT_SEND_SOURCE_ALL | CHAT_LOG_INCIDENT);
    return false;
  }
  if (!gameSetup->m_Map->GetValid()) {
    gameSetup->m_Ctx->ErrorReply("The currently loaded map config file is invalid", CHAT_SEND_SOURCE_ALL | CHAT_LOG_INCIDENT);
    return false;
  }

  if (!gameSetup->m_Map->GetMapHasTargetGameVersion()) {
    gameSetup->m_Ctx->ErrorReply("The game version has not been specified", CHAT_SEND_SOURCE_ALL | CHAT_LOG_INCIDENT);
    if (MatchLogLevel(LogLevel::kWarning)) {
      Print("[CONFIG] Game cannot be hosted because <hosting.game_versions.main> is missing.");
    }
    return false;
  }
  if (gameSetup->GetMap()->GetMapTargetGameVersion() < gameSetup->GetMap()->GetMapMinGameVersion()) {
    gameSetup->m_Ctx->ErrorReply("map requires v" + ToVersionString(gameSetup->GetMap()->GetMapMinGameVersion()), CHAT_SEND_SOURCE_ALL | CHAT_LOG_INCIDENT);
    return false;
  }
  if (!gameSetup->GetMap()->GetMapTargetGameIsExpansion() && gameSetup->GetMap()->GetMapRequiresExpansion()) {
    gameSetup->m_Ctx->ErrorReply("map requires The Frozen Throne", CHAT_SEND_SOURCE_ALL | CHAT_LOG_INCIDENT);
    return false;
  }

  if (!GetNewGameIsInQuota()) {
    if (m_Lobbies.size() == 1) {
      gameSetup->m_Ctx->ErrorReply("Another game lobby [" + GetMostRecentLobby()->GetStatusDescription() + "] is currently hosted.", CHAT_SEND_SOURCE_ALL | CHAT_LOG_INCIDENT);
    } else {
      gameSetup->m_Ctx->ErrorReply("Too many lobbies (" + to_string(m_Lobbies.size()) + ") are currently hosted.", CHAT_SEND_SOURCE_ALL | CHAT_LOG_INCIDENT);
    }
    return false;
  }

  if (gameSetup->GetIsMirror()) {
    Print("[AURA] mirroring game [" + gameSetup->m_Name + "]");
  } else if (gameSetup->m_RestoredGame) {
    Print("[AURA] creating loaded game [" + gameSetup->m_Name + "]");
  } else {
    Print("[AURA] creating game [" + gameSetup->m_Name + "]");
  }

  shared_ptr<CGame> createdLobby = make_shared<CGame>(this, gameSetup);

  m_LastGameHostedTicks = m_ClockTicks;
  if (createdLobby->GetFromAutoReHost()) {
    m_AutoRehostGameSetup = gameSetup;
    m_LastGameAutoHostedTicks = m_LastGameHostedTicks;
    m_AutoReHosted = true;
  }

  if (createdLobby->GetExiting()) {
    createdLobby.reset();
    gameSetup->m_Ctx->ErrorReply("Cannot assign a TCP/IP port to game [" + gameSetup->m_Name + "].", CHAT_SEND_SOURCE_ALL | CHAT_LOG_INCIDENT);
    return false;
  }

  m_LobbiesPending.push_back(createdLobby);
  gameSetup->OnGameCreate();

  UpdateMetaData();

#ifndef DISABLE_MINIUPNP
  if (m_Net.m_Config.m_EnableUPnP && createdLobby->GetIsLobbyStrict() && m_StartedGames.empty()) {
    // FIXME? This is a long synchronous network call.
    m_Net.RequestUPnP(NetProtocol::kTCP, createdLobby->GetHostPortFromType(GAME_DISCOVERY_INTERFACE_IPV4), createdLobby->GetHostPort(), LogLevel::kInfo);
  }
#endif

  if (createdLobby->GetIsCheckJoinable() && !m_Net.GetIsFetchingIPAddresses()) {
    uint32_t checkMode = (uint32_t)HEALTH_CHECK_ALL;
    if (!m_Net.m_SupportTCPOverIPv6) {
      checkMode &= ~(uint32_t)HEALTH_CHECK_PUBLIC_IPV6;
      checkMode &= ~(uint32_t)HEALTH_CHECK_LOOPBACK_IPV6;
    }
    if (createdLobby->GetIsVerbose()) {
      checkMode |= (uint32_t)HEALTH_CHECK_VERBOSE;
    }
    m_Net.QueryHealthCheck(gameSetup->m_Ctx, checkMode, nullptr, createdLobby);
    createdLobby->SetIsCheckJoinable(false);
  }

  if (createdLobby->GetUDPEnabled()) {
    createdLobby->SendGameDiscoveryCreate();
    createdLobby->SetGameDiscoveryActive(true);
  }

  for (auto& realm : m_Realms) {
    if (!createdLobby->GetIsMirror() && !createdLobby->GetIsRestored()) {
      realm->HoldFriends(createdLobby);
      realm->HoldClan(createdLobby);
    }

    realm->TrySetGameBroadcastPending(createdLobby);
  }

  if (createdLobby->GetDisplayMode() != GAME_DISPLAY_PUBLIC ||
    gameSetup->GetCreatedFromType() != ServiceType::kRealm ||
    gameSetup->m_Ctx->GetIsWhisper()) {
    gameSetup->m_Ctx->SendPrivateReply(createdLobby->GetAnnounceText());
  }

  if (createdLobby->GetDisplayMode() == GAME_DISPLAY_PUBLIC) {
    if (m_IRC.GetIsEnabled() && m_IRC.GetIsAnnounceGames()) {
     m_IRC.SendAllChannels(createdLobby->GetAnnounceText());
    }
#ifndef DISABLE_DPP
    if (m_Discord.GetIsEnabled() && m_Discord.GetIsAnnounceGames()) {
      m_Discord.SendAllChannels(createdLobby->GetAnnounceText());
    }
#endif
  }

  Version gameVersion = createdLobby->m_Config.m_GameVersion.value();
  const uint32_t mapSize = createdLobby->GetMap()->GetMapSize();
  if (mapSize > MAX_MAP_SIZE_RF) {
    // Reforged
    Print(Concat("[AURA] warning - hosting game beyond 512 MB map size limit: ", SanitizeWrapUTF8(createdLobby->GetMap()->GetServerFileName())));
  } else if (gameVersion <= GAMEVER(1u, 28u) && mapSize > MAX_MAP_SIZE_1_28) {
    Print(Concat("[AURA] warning - hosting game beyond 128 MB map size limit: ", SanitizeWrapUTF8(createdLobby->GetMap()->GetServerFileName())));
  } else if (gameVersion <= GAMEVER(1u, 26u) && mapSize > MAX_MAP_SIZE_1_26) {
    Print(Concat("[AURA] warning - hosting game beyond 8 MB map size limit: ", SanitizeWrapUTF8(createdLobby->GetMap()->GetServerFileName())));
  } else if (gameVersion <= GAMEVER(1u, 23u) && mapSize > MAX_MAP_SIZE_1_23) {
    Print(Concat("[AURA] warning - hosting game beyond 4 MB map size limit: ", SanitizeWrapUTF8(createdLobby->GetMap()->GetServerFileName())));
  }
  if (gameVersion < createdLobby->GetMap()->GetMapMinSuggestedGameVersion()) {
    Print(Concat("[AURA] warning - hosting game that MAY require version ", ToVersionString(createdLobby->GetMap()->GetMapMinSuggestedGameVersion())));
  }

  for (const auto& entry : createdLobby->GetMap()->GetInitCommands()) {
    string cmdToken;
    shared_ptr<CCommandContext> ctx = nullptr;
    try {
      ctx = make_shared<CCommandContext>(
        ServiceType::kCLI, this, m_Config.m_LANCommandCFG, createdLobby, createdLobby->GetLobbyVirtualHostName(), false, &std::cout
      );
      ctx->SetPermissions(USER_PERMISSIONS_GAME_OWNER | USER_PERMISSIONS_GAME_PLAYER);
    } catch (...) {}
    if (ctx) ctx->Run(cmdToken, entry.first, entry.second);
  }
  return true;
}

bool CAura::MergePendingLobbies()
{
  if (m_LobbiesPending.empty()) return false;
  m_Lobbies.reserve(m_Lobbies.size() + m_LobbiesPending.size());
  m_Lobbies.insert(m_Lobbies.end(), m_LobbiesPending.begin(), m_LobbiesPending.end());
  m_LobbiesPending.clear();
  return true;
}

void CAura::TrackGameJoinInProgress(shared_ptr<CGame> game)
{
  m_JoinInProgressGames.emplace_back(game);
}

void CAura::UntrackGameJoinInProgress(shared_ptr<CGame> game)
{
  for (auto it = begin(m_JoinInProgressGames); it != end(m_JoinInProgressGames);) {
    if ((*it).lock() == game) {
      it = m_JoinInProgressGames.erase(it);
      break;
    } else {
      ++it;
    }
  }
}

bool CAura::QueueConfigReload(shared_ptr<CCommandContext> nCtx)
{
  if (m_ReloadContext) return false;
  m_ReloadContext = nCtx;
  return true;
}

uint32_t CAura::NextHostCounter()
{
  m_HostCounter = (m_HostCounter + 1) & 0x00FFFFFF;
  if (m_HostCounter < m_Config.m_MinHostCounter) {
    m_HostCounter = m_Config.m_MinHostCounter;
  }
  return m_HostCounter;
}

uint64_t CAura::NextHistoryGameID()
{
  m_HistoryGameID = (m_HistoryGameID + 1);
  return m_HistoryGameID;
}

uint32_t CAura::NextServerID()
{
  ++m_LastServerID;
  if (m_LastServerID < 0x10) {
    // Ran out of server IDs.
    m_LastServerID = 0;
  }
  return m_LastServerID;
}

FileChunkTransient CAura::ReadFileChunkCacheable(const std::filesystem::path& filePath, const size_t start, const size_t end)
{
  auto it = m_CachedFileContents.find(filePath);
  if (it != m_CachedFileContents.end()) {
    const FileChunkCached& chunk = it->second;
    if (chunk.start <= start && start < chunk.end) {
      WeakByteArray maybeCachedPtr = chunk.bytes;
      if (!maybeCachedPtr.expired()) {
        //Print("[DEBUG] Reusing cached map data for [" + PathToString(filePath) + ":" + to_string(chunk.start) + "] (" + to_string((chunk.end - chunk.start) / 1024) + " / " + to_string(chunk.fileSize / 1024) + " KB)");
        return FileChunkTransient(chunk);
      }
    }
  }

  SharedByteArray fileContentsPtr = make_shared<vector<uint8_t>>();
  size_t fileSize = 0;
  size_t actualReadSize = 0;
  if (!FileReadPartial(filePath, *(fileContentsPtr.get()), start, end - start, &fileSize, &actualReadSize) || fileContentsPtr->empty()) {
    m_CachedFileContents.erase(filePath);
    fileContentsPtr.reset();
    return FileChunkTransient();
  }

#ifdef DEBUG
  if (GetIsLoggingTrace()) {
    Print("[AURA] Cached map file contents in-memory for [" + PathToString(filePath) + ":" + to_string(start) + "] ( " + to_string(actualReadSize / 1024) + " / " + to_string(fileSize / 1024) + " KB)");
  }
#endif
  m_CachedFileContents[filePath] = FileChunkCached(fileSize, start, start + actualReadSize, fileContentsPtr);

  // Try to dedupe across maps with different names but same content.
  for (const auto& cacheEntries : m_CachedFileContents) {
    if (cacheEntries.first == filePath) continue;
    const FileChunkCached& otherFileChunk = cacheEntries.second;
    WeakByteArray maybeCachedPtr = otherFileChunk.bytes;
    if (maybeCachedPtr.expired()) {
      continue;
    }
    SharedByteArray otherContentsPtr = maybeCachedPtr.lock();
    if (otherContentsPtr->size() != fileContentsPtr->size()) {
      continue;
    }
    if (memcmp(otherContentsPtr->data(), fileContentsPtr->data(), fileContentsPtr->size()) == 0) {
      //Print("[DEBUG] Reusing cached " + to_string((otherFileChunk.end - otherFileChunk.start) / 1024) + " KB from [" + PathToString(cacheEntries.first) + "] for [" + PathToString(filePath) + "]");
      //fileContentsPtr = otherContentsPtr;
      m_CachedFileContents[filePath] = FileChunkCached(otherFileChunk.fileSize, otherFileChunk.start, otherFileChunk.end, otherContentsPtr);
      // Iterator is invalid now
      break;
    }
  }

  // Prevent the cache from being filled with stale chunk data
  ClearStaleFileChunks();

  return FileChunkTransient(m_CachedFileContents[filePath]);
}

SharedByteArray CAura::ReadFile(const std::filesystem::path& filePath, const size_t maxSize)
{
  SharedByteArray fileContentsPtr = make_shared<vector<uint8_t>>();
  size_t fileSize = 0;
  size_t actualReadSize = 0;
  if (!FileReadPartial(filePath, *(fileContentsPtr.get()), 0, maxSize, &fileSize, &actualReadSize) || fileContentsPtr->empty() || actualReadSize < fileSize) {
    fileContentsPtr.reset();
  }
  return fileContentsPtr;
}

string CAura::GetSudoAuthTarget(const string& target)
{
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, 15);

  // Generate random hex digits
  string result;
  result.reserve(21 + target.length());

  for (size_t i = 0; i < 20; ++i) {
      const int randomDigit = dis(gen);
      result += (randomDigit < 10) ? (char)('0' + randomDigit) : (char)('a' + (randomDigit - 10));
  }

  result += " " + target;
  m_SudoAuthTarget = result;
  return result;
}
