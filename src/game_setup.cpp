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

#include "game_setup.h"

#include "auradb.h"
#include "command.h"
#include "file_util.h"
#include "game.h"
#include "game_user.h"
#include "protocol/game_protocol.h"
#include "hash.h"
#include "integration/irc.h"
#include "map.h"
#include "optional.h"
#include "realm.h"
#include "save_game.h"

#include "aura.h"

#define SEARCH_RESULT(a, b) (make_pair<uint8_t, filesystem::path>((uint8_t)a, (filesystem::path)(b)))

using namespace std;

constexpr uint8_t MAP_MATCH_TYPE_MAP = 0x80;
constexpr uint8_t NOT_MAP_MATCH_TYPE_MAP = NOT_TINY(MAP_MATCH_TYPE_MAP);

// CGameExtraOptions

CGameExtraOptions::CGameExtraOptions()
{
}

bool CGameExtraOptions::ParseMapObservers(const string& s) {
  string lower = ToLowerCase(s);
  if (lower == "no" || lower == "none" || lower == "no observers" || lower == "no obs" || lower == "sin obs" || lower == "sin observador" || lower == "sin observadores") {
    m_Observers = GameObserversMode::kNone;
  } else if (lower == "referee" || lower == "referees" || lower == "arbiter" || lower == "arbitro" || lower == "arbitros" || lower == "árbitros") {
    m_Observers = GameObserversMode::kReferees;
  } else if (lower == "observadores derrotados" || lower == "derrotados" || lower == "obs derrotados" || lower == "obs on defeat" || lower == "observers on defeat" || lower == "on defeat" || lower == "defeat" || lower == "ondefeat") {
    m_Observers = GameObserversMode::kOnDefeat;
  } else if (lower == "full observers" || lower == "solo observadores" || lower == "full") {
    m_Observers = GameObserversMode::kStartOrOnDefeat;
  } else {
    return false;
  }
  return true;
}

bool CGameExtraOptions::ParseMapVisibility(const string& s) {
  string lower = ToLowerCase(s);
  if (lower == "no" || lower == "default" || lower == "predeterminado" || lower == "fog" || lower == "fog of war" || lower == "niebla" || lower == "niebla de guerra" || lower == "fow") {
    m_Visibility = GameVisibilityMode::kDefault;
  } else if (lower == "hide terrain" || lower == "hide" || lower == "ocultar terreno" || lower == "ocultar" || lower == "hidden") {
    m_Visibility = GameVisibilityMode::kHideTerrain;
  } else if (lower == "explored map" || lower == "map explored" || lower == "explored" || lower == "mapa explorado" || lower == "explorado") {
    m_Visibility = GameVisibilityMode::kExplored;
  } else if (lower == "always visible" || lower == "always" || lower == "visible" || lower == "todo visible" || lower == "todo" || lower == "revelar" || lower == "todo revelado") {
    m_Visibility = GameVisibilityMode::kAlwaysVisible;
  } else {
    return false;
  }
  return true;
}

bool CGameExtraOptions::ParseMapSpeed(const string& s) {
  string lower = ToLowerCase(s);
  if (lower == "slow") {
    m_Speed = GameSpeed::kSlow;
  } else if (lower == "normal") {
    m_Speed = GameSpeed::kNormal;
  } else if (lower == "fast") {
    m_Speed = GameSpeed::kFast;
  } else {
    return false;
  }
  return true;
}

bool CGameExtraOptions::ParseMapRandomRaces(const string& s) {
  string lower = ToLowerCase(s);
  if (lower == "random race" || lower == "rr" || lower == "yes" || lower == "random" || lower == "random races") {
    m_RandomRaces = true;
  } else if (lower == "default" || lower == "no" || lower == "predeterminado") {
    m_RandomRaces = false;
  } else {
    return false;
  }
  return true;
}

bool CGameExtraOptions::ParseMapRandomHeroes(const string& s) {
  string lower = ToLowerCase(s);
  if (lower == "random hero" || lower == "rh" || lower == "yes" || lower == "random" || lower == "random heroes") {
    m_RandomHeroes = true;
  } else if (lower == "default" || lower == "no" || lower == "predeterminado") {
    m_RandomHeroes = false;
  } else {
    return false;
  }
  return true;
}

void CGameExtraOptions::AcquireCLI(const CCLI* nCLI) {
  ReadOpt(nCLI->m_GameObservers) >> m_Observers;
  ReadOpt(nCLI->m_GameVisibility) >> m_Visibility;
  ReadOpt(nCLI->m_GameSpeed) >> m_Speed;
  ReadOpt(nCLI->m_GameTeamsLocked) >> m_TeamsLocked;
  ReadOpt(nCLI->m_GameTeamsTogether) >> m_TeamsTogether;
  ReadOpt(nCLI->m_GameAdvancedSharedUnitControl) >> m_AdvancedSharedUnitControl;
  ReadOpt(nCLI->m_GameRandomRaces) >> m_RandomRaces;
  ReadOpt(nCLI->m_GameRandomHeroes) >> m_RandomHeroes;
}

CGameExtraOptions::~CGameExtraOptions() = default;

//
// GameMirrorSource
//


bool GameMirrorSetup::SetRawSource(const GameHost& gameHost)
{
  Enable();
  m_Source = gameHost;
  return true;
}

bool GameMirrorSetup::SetRawSource(const sockaddr_storage& sourceAddress, const uint32_t gameIdentifier, const uint32_t entryKey)
{
  return SetRawSource(GameHost(sourceAddress, gameIdentifier, entryKey));
}

bool GameMirrorSetup::SetRawSource(const string& input)
{
  return SetRawSource(GameHost(input));
}

bool GameMirrorSetup::SetRegistrySource(const StringPair& registry)
{
  Enable();
  m_Source = registry;
  return true;
}

bool GameMirrorSetup::SetRegistrySource(const string& gameName, const string& registryName)
{
  return SetRegistrySource(StringPair(gameName, registryName));
}

const GameHost* GameMirrorSetup::GetRawSource() const
{
  if (!GetIsEnabled() || m_Source.index() != 2) {
    return nullptr;
  }
  return get_if<GameHost>(&m_Source);
}

//
// CGameSetup
//

CGameSetup::CGameSetup(CAura* nAura, shared_ptr<CCommandContext> nCtx, CConfig* nMapCFG)
  : m_Aura(nAura),
    //m_Map(nullptr),
    m_Ctx(nCtx),

    m_Attribution(nCtx->GetUserAttribution()),
    //m_SearchRawTarget(string()),
    m_SearchType(SEARCH_TYPE_ANY),

    m_AllowPaths(false),
    m_StandardPaths(false),
    m_LuckyMode(false),

    m_FoundSuggestions(false),
    m_IsDownloadable(false),
    m_IsStepDownloading(false),
    m_IsStepDownloaded(false),
    m_MapDownloadSize(0),
    m_DownloadFileStream(nullptr),
    m_DownloadTimeout(m_Aura->m_Net.m_Config.m_DownloadTimeout),
    m_SuggestionsTimeout(SUGGESTIONS_TIMEOUT),
    m_AsyncStep(GAMESETUP_STEP_MAIN),

    m_IsMapDownloaded(false),

    m_OwnerLess(false),
    m_RealmsDisplayMode(GAME_DISPLAY_PUBLIC),
    m_LobbyReplaceable(false),
    m_LobbyAutoRehosted(false),

    m_CreationCounter(0),
    m_Creator(ServiceUser()),

    m_MapExtraOptions(nullptr),
    m_MapReadyCallbackAction(MAP_ONREADY_SET_ACTIVE),

    m_ExitingSoon(false),
    m_DeleteMe(false)
{
  if (auto gameUser = nCtx->GetGameUser()) {
    m_GameVersion = gameUser->GetGameVersion();
  } else if (auto sourceRealm = nCtx->GetSourceRealm()) {
    m_GameVersion = sourceRealm->GetGameVersion();
  }
  if ((!m_GameVersion.has_value() || m_GameVersion->first == 0) && m_Aura->m_GameDefaultConfig->m_GameVersion.has_value()) {
    m_GameVersion = m_Aura->m_GameDefaultConfig->m_GameVersion.value();
  }
  m_Map = GetBaseMapFromConfig(nMapCFG, false);
}

CGameSetup::CGameSetup(CAura* nAura, shared_ptr<CCommandContext> nCtx, const string nSearchRawTarget, const uint8_t nSearchType, const bool nAllowPaths, const bool nUseStandardPaths, const bool nUseLuckyMode)
  : m_Aura(nAura),
    //m_Map(nullptr),
    m_Ctx(nCtx),

    m_Attribution(nCtx->GetUserAttribution()),
    m_SearchRawTarget(move(nSearchRawTarget)),
    m_SearchType(nSearchType),

    m_AllowPaths(nAllowPaths),
    m_StandardPaths(nUseStandardPaths),
    m_LuckyMode(nUseLuckyMode),

    m_FoundSuggestions(false),
    m_IsDownloadable(false),
    m_IsStepDownloading(false),
    m_IsStepDownloaded(false),
    m_MapDownloadSize(0),
    m_DownloadFileStream(nullptr),
    m_DownloadTimeout(m_Aura->m_Net.m_Config.m_DownloadTimeout),
    m_SuggestionsTimeout(SUGGESTIONS_TIMEOUT),
    m_AsyncStep(GAMESETUP_STEP_MAIN),

    m_IsMapDownloaded(false),

    m_OwnerLess(false),
    m_RealmsDisplayMode(GAME_DISPLAY_PUBLIC),
    m_LobbyReplaceable(false),
    m_LobbyAutoRehosted(false),

    m_CreationCounter(0),
    m_Creator(ServiceUser()),

    m_MapExtraOptions(nullptr),
    m_MapReadyCallbackAction(MAP_ONREADY_SET_ACTIVE),

    m_ExitingSoon(false),
    m_DeleteMe(false)
    
{
  if (auto gameUser = nCtx->GetGameUser()) {
    m_GameVersion = gameUser->GetGameVersion();
  } else if (auto sourceRealm = nCtx->GetSourceRealm()) {
    m_GameVersion = sourceRealm->GetGameVersion();
  }
  if ((!m_GameVersion.has_value() || m_GameVersion->first == 0) && m_Aura->m_GameDefaultConfig->m_GameVersion.has_value()) {
    m_GameVersion = m_Aura->m_GameDefaultConfig->m_GameVersion.value();
  }
}

std::string CGameSetup::GetInspectName() const
{
  return m_Map->GetConfigName();
}

bool CGameSetup::GetIsStale() const
{
  if (m_ActiveTicks.has_value()) return false;
  return m_Aura->GetTicksIsAfterDelay(*m_ActiveTicks, GAMESETUP_STALE_TICKS);
}

void CGameSetup::ParseInputLocal()
{
  m_SearchTarget = make_pair("local", m_SearchRawTarget);
}

void CGameSetup::ParseInput()
{
  if (m_StandardPaths) {
    ParseInputLocal();
    return;
  }

  if (m_SearchType != SEARCH_TYPE_ANY) {
    ParseInputLocal();
    return;
  }

  string lower = m_SearchRawTarget;
  transform(begin(lower), end(lower), begin(lower), [](char c) { return static_cast<char>(std::tolower(c)); });

  string aliasSource = GetNormalizedAlias(lower);
  string aliasTarget;
  if (!aliasSource.empty()) aliasTarget = m_Aura->m_DB->AliasCheck(aliasSource);
  if (!aliasTarget.empty()) {
    m_SearchRawTarget = aliasTarget;
    lower = m_SearchRawTarget;
    transform(begin(lower), end(lower), begin(lower), [](char c) { return static_cast<char>(std::tolower(c)); });
  } else {
    auto it = m_Aura->m_LastMapIdentifiersFromSuggestions.find(lower);
    if (it != m_Aura->m_LastMapIdentifiersFromSuggestions.end()) {
      m_SearchRawTarget = it->second;
      lower = ToLowerCase(m_SearchRawTarget);
    }
  }

  if (lower.length() >= 6 && (lower.substr(0, 6) == "local-" || lower.substr(0, 6) == "local:")) {
    ParseInputLocal();
    return;
  }

  // Custom namespace/protocol
  if (lower.length() >= 8 && (lower.substr(0, 8) == "epicwar-" || lower.substr(0, 8) == "epicwar:")) {
    m_SearchTarget = make_pair("epicwar", MaybeBase10(lower.substr(8)));
    m_IsDownloadable = true;
    return;
  }

  if (lower.length() >= 8 && (lower.substr(0, 8) == "wc3maps-" || lower.substr(0, 8) == "wc3maps:")) {
    m_SearchTarget = make_pair("wc3maps", MaybeBase10(lower.substr(8)));
    m_IsDownloadable = true;
    return;
  }
 

  bool isUri = false;
  if (lower.length() >= 7 && lower.substr(0, 7) == "http://") {
    isUri = true;
    lower = lower.substr(7);
  } else if (lower.length() >= 8 && lower.substr(0, 8) == "https://") {
    isUri = true;
    lower = lower.substr(8);
  }

  if (lower.length() >= 17 && lower.substr(0, 12) == "epicwar.com/") {
    string mapCode = lower.substr(17);
    m_SearchTarget = make_pair("epicwar", MaybeBase10(TrimTrailingSlash(mapCode)));
    m_IsDownloadable = true;
    return;
  }
  if (lower.length() >= 21 && lower.substr(0, 16) == "www.epicwar.com/") {
    string mapCode = lower.substr(21);
    m_SearchTarget = make_pair("epicwar", MaybeBase10(TrimTrailingSlash(mapCode)));
    m_IsDownloadable = true;
    return;
  }
  if (lower.length() >= 16 && lower.substr(0, 12) == "wc3maps.com/") {
    string::size_type mapCodeEnd = lower.find_first_of('/', 16);
    string mapCode = mapCodeEnd == string::npos ? lower.substr(16) : lower.substr(16, mapCodeEnd - 16);
    m_SearchTarget = make_pair("wc3maps", MaybeBase10(TrimTrailingSlash(mapCode)));
    m_IsDownloadable = true;
    return;
  }
  if (lower.length() >= 20 && lower.substr(0, 16) == "www.wc3maps.com/") {
    string::size_type mapCodeEnd = lower.find_first_of('/', 20);
    string mapCode = mapCodeEnd == string::npos ? lower.substr(20) : lower.substr(20, mapCodeEnd - 20);
    m_SearchTarget = make_pair("wc3maps", MaybeBase10(TrimTrailingSlash(mapCode)));
    m_IsDownloadable = true;
    return;
  }
  if (isUri) {
    m_SearchTarget = make_pair("remote", string());
  } else {
    ParseInputLocal();
  }
}


pair<uint8_t, filesystem::path> CGameSetup::SearchInputStandard()
{
  // Error handling: SearchInputStandard() reports the full file path
  // This can be absolute. That's not a problem.
  filesystem::path targetPath = m_SearchTarget.second;
  if (!FileExists(targetPath)) {
    Print("[CLI] File not found: " + PathToString(targetPath));
    if (!targetPath.is_absolute()) {
      try {
        Print("[CLI] (File resolved to: " + PathToString(filesystem::absolute(targetPath)) + ")");
      } catch (const exception& e) {
        Print("[CLI] (File absolute path cannot be resolved: " + string(e.what()));
      }
    }
    return SEARCH_RESULT(MATCH_TYPE_NONE, targetPath);
  }
  if (m_SearchType == SEARCH_TYPE_ONLY_MAP) {
    return SEARCH_RESULT(MATCH_TYPE_MAP, targetPath);
  }
  if (m_SearchType == SEARCH_TYPE_ONLY_CONFIG) {
    return SEARCH_RESULT(MATCH_TYPE_CONFIG, targetPath);
  }
  if (m_SearchTarget.second.length() < 5) {
    return SEARCH_RESULT(MATCH_TYPE_INVALID, targetPath);
  }

  string targetExt = ParseFileExtension(PathToString(targetPath.filename()));
  if (targetExt == ".w3m" || targetExt == ".w3x") {
    return SEARCH_RESULT(MATCH_TYPE_MAP, targetPath);
  }
  if (targetExt == ".ini") {
    return SEARCH_RESULT(MATCH_TYPE_CONFIG, targetPath);
  }
  return SEARCH_RESULT(MATCH_TYPE_INVALID, targetPath);
}

pair<uint8_t, filesystem::path> CGameSetup::SearchInputLocalExact()
{
  string fileExtension = ParseFileExtension(m_SearchTarget.second);
  if (m_SearchType == SEARCH_TYPE_ONLY_MAP || m_SearchType == SEARCH_TYPE_ONLY_FILE || m_SearchType == SEARCH_TYPE_ANY) {
    filesystem::path testPath = (m_Aura->m_Config.m_MapPath / filesystem::path(m_SearchTarget.second)).lexically_normal();
    if (testPath.parent_path() != m_Aura->m_Config.m_MapPath.parent_path()) {
      return SEARCH_RESULT(MATCH_TYPE_FORBIDDEN, filesystem::path());
    }
    if ((fileExtension == ".w3m" || fileExtension == ".w3x") && FileExists(testPath)) {
      return SEARCH_RESULT(MATCH_TYPE_MAP, testPath);
    }
  }
  if (m_SearchType == SEARCH_TYPE_ONLY_CONFIG || m_SearchType == SEARCH_TYPE_ONLY_FILE || m_SearchType == SEARCH_TYPE_ANY) {
    filesystem::path testPath = (m_Aura->m_Config.m_MapCFGPath / filesystem::path(m_SearchTarget.second)).lexically_normal();
    if (testPath.parent_path() != m_Aura->m_Config.m_MapCFGPath.parent_path()) {
      return SEARCH_RESULT(MATCH_TYPE_FORBIDDEN, filesystem::path());
    }
    if ((fileExtension == ".ini") && FileExists(testPath)) {
      return SEARCH_RESULT(MATCH_TYPE_CONFIG, testPath);
    }
  }
  return SEARCH_RESULT(MATCH_TYPE_NONE, filesystem::path());
}

pair<uint8_t, filesystem::path> CGameSetup::SearchInputLocalTryExtensions()
{
  string fileExtension = ParseFileExtension(m_SearchTarget.second);
  bool hasExtension = fileExtension == ".w3x" || fileExtension == ".w3m" || fileExtension == ".ini";
  if (hasExtension) {
    return SEARCH_RESULT(MATCH_TYPE_NONE, filesystem::path());
  }
  if (m_SearchType == SEARCH_TYPE_ONLY_MAP || m_SearchType == SEARCH_TYPE_ONLY_FILE || m_SearchType == SEARCH_TYPE_ANY) {
    filesystem::path testPath = (m_Aura->m_Config.m_MapPath / filesystem::path(m_SearchTarget.second + ".w3x")).lexically_normal();
    if (testPath.parent_path() != m_Aura->m_Config.m_MapPath.parent_path()) {
      return SEARCH_RESULT(MATCH_TYPE_FORBIDDEN, filesystem::path());
    }
    if (FileExists(testPath)) {
      return SEARCH_RESULT(MATCH_TYPE_MAP, testPath);
    }
    testPath = (m_Aura->m_Config.m_MapPath / filesystem::path(m_SearchTarget.second + ".w3m")).lexically_normal();
    if (FileExists(testPath)) {
      return SEARCH_RESULT(MATCH_TYPE_MAP, testPath);
    }
  }
  if (m_SearchType == SEARCH_TYPE_ONLY_CONFIG || m_SearchType == SEARCH_TYPE_ONLY_FILE || m_SearchType == SEARCH_TYPE_ANY) {
    filesystem::path testPath = (m_Aura->m_Config.m_MapCFGPath / filesystem::path(m_SearchTarget.second + ".ini")).lexically_normal();
    if (testPath.parent_path() != m_Aura->m_Config.m_MapCFGPath.parent_path()) {
      return SEARCH_RESULT(MATCH_TYPE_FORBIDDEN, filesystem::path());
    }
    if (FileExists(testPath)) {
      return SEARCH_RESULT(MATCH_TYPE_CONFIG, testPath);
    }
  }
  return SEARCH_RESULT(MATCH_TYPE_NONE, filesystem::path());
}

pair<uint8_t, filesystem::path> CGameSetup::SearchInputLocalFuzzy(vector<string>& fuzzyMatches)
{
  vector<pair<string, int>> allResults;
  if (m_SearchType == SEARCH_TYPE_ONLY_MAP || m_SearchType == SEARCH_TYPE_ONLY_FILE || m_SearchType == SEARCH_TYPE_ANY) {
    vector<pair<string, int>> mapResults = FuzzySearchFiles(m_Aura->m_Config.m_MapPath, FILE_EXTENSIONS_MAP, m_SearchTarget.second);
    for (const auto& result : mapResults) {
      // Whether 0x80 is set flags the type of result: If it is there, it's a map
      allResults.push_back(make_pair(result.first, result.second | MAP_MATCH_TYPE_MAP));
    }
  }
  if (m_SearchType == SEARCH_TYPE_ONLY_CONFIG || m_SearchType == SEARCH_TYPE_ONLY_FILE || m_SearchType == SEARCH_TYPE_ANY) {
    vector<pair<string, int>> cfgResults = FuzzySearchFiles(m_Aura->m_Config.m_MapCFGPath, FILE_EXTENSIONS_CONFIG, m_SearchTarget.second);
    allResults.insert(allResults.end(), cfgResults.begin(), cfgResults.end());
  }
  if (allResults.empty()) {
    return SEARCH_RESULT(MATCH_TYPE_NONE, filesystem::path());
  }

  size_t resultCount = min(FILE_SEARCH_FUZZY_MAX_RESULTS, allResults.size());
  partial_sort(
    allResults.begin(),
    allResults.begin() + resultCount,
    allResults.end(),
    [](const pair<string, int>& a, const pair<string, int>& b) {
        return (a.second & NOT_MAP_MATCH_TYPE_MAP) < (b.second & NOT_MAP_MATCH_TYPE_MAP);
    }
  );

  if (m_LuckyMode || allResults.size() == 1) {
    if (allResults[0].second & MAP_MATCH_TYPE_MAP) {
      return SEARCH_RESULT(MATCH_TYPE_MAP, m_Aura->m_Config.m_MapPath / filesystem::path(allResults[0].first));
    } else {
      return SEARCH_RESULT(MATCH_TYPE_CONFIG, m_Aura->m_Config.m_MapCFGPath / filesystem::path(allResults[0].first));
    }
  }

  // Return suggestions through passed argument.
  for (uint8_t i = 0; i < resultCount; i++) {
    fuzzyMatches.push_back(allResults[i].first);
  }

  return SEARCH_RESULT(MATCH_TYPE_NONE, filesystem::path());
}

#ifndef DISABLE_CPR
void CGameSetup::SearchInputRemoteFuzzy(vector<string>& fuzzyMatches) {
  if (fuzzyMatches.size() >= 5) return;
  vector<pair<string, string>> remoteSuggestions = GetMapRepositorySuggestions(m_SearchTarget.second, 5 - integer_cast_lossy<uint8_t>(fuzzyMatches.size()));
  if (remoteSuggestions.empty()) {
    return;
  }
  // Return suggestions through passed argument.
  m_FoundSuggestions = true;
  m_Aura->m_LastMapIdentifiersFromSuggestions.clear();
  for (const auto& suggestion : remoteSuggestions) {
    string lowerName = ToLowerCase(suggestion.first);
    m_Aura->m_LastMapIdentifiersFromSuggestions[lowerName] = suggestion.second;
    fuzzyMatches.push_back(suggestion.first + " (" + suggestion.second + ")");
  }
}
#endif

pair<uint8_t, filesystem::path> CGameSetup::SearchInputLocal(vector<string>& fuzzyMatches)
{
  // 1. Try exact match
  pair<uint8_t, filesystem::path> exactResult = SearchInputLocalExact();
  if (exactResult.first == MATCH_TYPE_MAP || exactResult.first == MATCH_TYPE_CONFIG) {
    return exactResult;
  }
  // 2. If no extension, try adding extensions: w3x, w3m, ini
  pair<uint8_t, filesystem::path> plusExtensionResult = SearchInputLocalTryExtensions();
  if (plusExtensionResult.first == MATCH_TYPE_MAP || plusExtensionResult.first == MATCH_TYPE_CONFIG) {
    return plusExtensionResult;
  }
  // 3. Fuzzy search
  if (!m_Aura->m_Config.m_StrictSearch) {
    pair<uint8_t, filesystem::path> fuzzyResult = SearchInputLocalFuzzy(fuzzyMatches);
    if (fuzzyResult.first == MATCH_TYPE_MAP || fuzzyResult.first == MATCH_TYPE_CONFIG) {
      return fuzzyResult;
    }
  }
  return SEARCH_RESULT(MATCH_TYPE_NONE, filesystem::path());
}

pair<uint8_t, filesystem::path> CGameSetup::SearchInput()
{
  if (m_StandardPaths) {
    return SearchInputStandard();
  }

  if (m_SearchTarget.first == "remote") {
    // "remote" means unsupported URL.
    // Supported URLs specify the domain in m_SearchTarget.first, such as "epicwar".
    return SEARCH_RESULT(MATCH_TYPE_FORBIDDEN, filesystem::path());
  }

  if (m_SearchTarget.first == "local") {
    filesystem::path testSearchPath = filesystem::path(m_SearchTarget.second);
    if (testSearchPath != testSearchPath.filename()) {
      if (m_AllowPaths) {
        // Search target has slashes. Treat as standard path.
        return SearchInputStandard();
      } else {
        // Search target has slashes. Protect against arbitrary directory traversal.
        return SEARCH_RESULT(MATCH_TYPE_FORBIDDEN, filesystem::path());        
      }
    }

    // Find config or map path
    vector<string> fuzzyMatches;
    pair<uint8_t, filesystem::path> result = SearchInputLocal(fuzzyMatches);
    if (result.first == MATCH_TYPE_MAP || result.first == MATCH_TYPE_CONFIG) {
      return result;
    }

    if (m_Aura->m_Config.m_MapSearchShowSuggestions) {
      if (m_Aura->m_StartedGames.empty()) {
        // Synchronous download, only if there are no ongoing games.
#ifndef DISABLE_CPR
        SearchInputRemoteFuzzy(fuzzyMatches);
#endif
      }
      if (!fuzzyMatches.empty()) {
        m_Ctx->ErrorReply("Suggestions: " + JoinStrings(fuzzyMatches), CHAT_SEND_SOURCE_ALL);
      }
    }

    return SEARCH_RESULT(MATCH_TYPE_NONE, filesystem::path());
  }

  // Input corresponds to a namespace, such as epicwar.
  // Gotta find a matching config file.
  string resolvedCFGName = m_SearchTarget.first + "-" + m_SearchTarget.second + ".ini";
  filesystem::path resolvedCFGPath = (m_Aura->m_Config.m_MapCFGPath / filesystem::path(resolvedCFGName)).lexically_normal();
  if (PathHasNullBytes(resolvedCFGPath) || resolvedCFGPath.parent_path() != m_Aura->m_Config.m_MapCFGPath.parent_path()) {
    return SEARCH_RESULT(MATCH_TYPE_FORBIDDEN, filesystem::path());
  }
  if (FileExists(resolvedCFGPath)) {
    return SEARCH_RESULT(MATCH_TYPE_CONFIG, resolvedCFGPath);
  }
  return SEARCH_RESULT(MATCH_TYPE_NONE, resolvedCFGPath);
}

shared_ptr<CMap> CGameSetup::GetBaseMapFromConfig(CConfig* mapCFG, const bool silent)
{
  ExportTemporaryToMap(mapCFG);

  shared_ptr<CMap> map = nullptr;
  try {
    map = make_shared<CMap>(m_Aura, mapCFG);
  } catch (const exception& e) {
    Print("[MAP] Failed to load map : " + string(e.what()));
    return map;
  }
  /*
  if (!map) {
    if (!silent) m_Ctx->ErrorReply("Failed to load map config", CHAT_SEND_SOURCE_ALL);
    return nullptr;
  }
  */
  string errorMessage = map->CheckProblems();
  if (!errorMessage.empty()) {
    if (!silent) m_Ctx->ErrorReply("Failed to load map config: " + errorMessage, CHAT_SEND_SOURCE_ALL);
    return map;
  }
  return map;
}

shared_ptr<CMap> CGameSetup::GetBaseMapFromConfigFile(const filesystem::path& filePath, const bool isCache, const bool silent)
{
  CConfig MapCFG;
  if (!MapCFG.Read(filePath)) {
    if (!silent) m_Ctx->ErrorReply("Map config file [" + PathToString(filePath.filename()) + "] not found.", CHAT_SEND_SOURCE_ALL);
    return nullptr;
  }
  shared_ptr<CMap> map = GetBaseMapFromConfig(&MapCFG, silent);
  if (!map) return nullptr;
  if (isCache) {
    if (MapCFG.GetIsModified()) {
      vector<uint8_t> bytes = MapCFG.Export();
      FileWrite(filePath, bytes.data(), bytes.size());
      Print("[AURA] Updated map cache for [" + PathToString(filePath.filename()) + "] as [" + PathToString(filePath) + "]");
    }
  }
  return map;
}

shared_ptr<CMap> CGameSetup::GetBaseMapFromMapFile(const filesystem::path& filePath, const bool silent)
{
  bool isInMapsFolder = filePath.parent_path() == m_Aura->m_Config.m_MapPath.parent_path();
  string fileName = PathToString(filePath.filename());
  if (fileName.empty()) return nullptr;
  string baseFileName;
  if (fileName.length() > 6 && fileName[fileName.length() - 6] == '~' && isdigit(fileName[fileName.length() - 5])) {
    baseFileName = fileName.substr(0, fileName.length() - 6) + fileName.substr(fileName.length() - 4);
  } else {
    baseFileName = fileName;
  }

  CConfig MapCFG;
  MapCFG.SetBool("map.cfg.partial", true); // temporary
  MapCFG.SetUint8("map.cfg.schema_number", MAP_CONFIG_SCHEMA_NUMBER);
  ExportTemporaryToMap(&MapCFG);
  if (m_StandardPaths) MapCFG.SetBool("map.standard_path", true);

  {
    W3ModLocale gameLocaleMod = m_GameLocaleMod.value_or(m_Aura->m_Config.m_GameLocaleModDefault);
    array<string, 12> localeStrings = {"enUS", "deDE", "esES", "esMX", "frFR", "itIT", "koKR", "plPL", "ptBR", "ruRU", "zhCN", "zhTW"};
    assert((uint8_t)W3ModLocale::LAST == localeStrings.size());
    MapCFG.SetString("map.locale.mod", localeStrings[(uint8_t)gameLocaleMod]);
  }
  {
    uint16_t gameLocaleLangID = m_GameLocaleLangID.value_or(m_Aura->m_Config.m_GameLocaleLangID);
    MapCFG.SetUint16("map.locale.lang_id", gameLocaleLangID);
  }
  MapCFG.Set("map.path", R"(Maps\Download\)" + baseFileName);
  string localPath = isInMapsFolder && !m_StandardPaths ? fileName : PathToString(filePath);
  MapCFG.Set("map.local_path", localPath);

  if (m_IsMapDownloaded) {
    MapCFG.Set("map.meta.site", m_MapSiteUri);
    MapCFG.Set("map.meta.url", m_MapDownloadUri);
    MapCFG.Set("map.downloaded.by", m_Attribution);
  }

  shared_ptr<CMap> baseMap = nullptr;
  try {
    baseMap = make_shared<CMap>(m_Aura, &MapCFG);
  } catch (const exception& e) {
    if (!silent) m_Ctx->ErrorReply("Failed to load map.", CHAT_SEND_SOURCE_ALL);
    Print("[MAP] Failed to load map : " + string(e.what()));
    vector<uint8_t> bytes = MapCFG.Export();
    m_Aura->LogPersistent("[MAP] Failed to load [" + PathToString(filePath) + "] - config was:");
    m_Aura->LogPersistent(string(begin(bytes), end(bytes)));
    return baseMap;
  }
  string errorMessage = baseMap->CheckProblems();
  if (!errorMessage.empty()) {
    if (!silent) m_Ctx->ErrorReply("Failed to load map: " + errorMessage, CHAT_SEND_SOURCE_ALL);
    vector<uint8_t> bytes = MapCFG.Export();
    m_Aura->LogPersistent("[MAP] Failed to load [" + PathToString(filePath) + "] - config was:");
    m_Aura->LogPersistent(string(begin(bytes), end(bytes)));
    return nullptr;
  }

  if (m_Aura->m_Config.m_EnableCFGCache && isInMapsFolder && !m_StandardPaths) {
    string resolvedCFGName;
    if (m_SearchTarget.first == "local") {
      resolvedCFGName = m_SearchTarget.first + "-" + fileName + ".ini";
    } else {
      resolvedCFGName = m_SearchTarget.first + "-" + m_SearchTarget.second + ".ini";
    }
    filesystem::path resolvedCFGPath = (m_Aura->m_Config.m_MapCachePath / filesystem::path(resolvedCFGName)).lexically_normal();

    vector<uint8_t> bytes = MapCFG.Export();
    FileWrite(resolvedCFGPath, bytes.data(), bytes.size());
    m_Aura->m_CFGCacheNamesByMapNames[fileName] = resolvedCFGName;
    Print("[AURA] Cached map config for [" + fileName + "] as [" + PathToString(resolvedCFGPath) + "]");
  }

  if (!silent) m_Ctx->SendReply("Loaded OK [" + fileName + "]", CHAT_SEND_SOURCE_ALL | CHAT_LOG_INCIDENT);
  return baseMap;
}

shared_ptr<CMap> CGameSetup::GetBaseMapFromMapFileOrCache(const filesystem::path& mapPath, const bool silent)
{
  filesystem::path fileName = mapPath.filename();
  if (fileName.empty()) return nullptr;
  if (m_Aura->m_Config.m_EnableCFGCache) {
    bool cacheSuccess = false;
    if (m_Aura->MatchLogLevel(LogLevel::kDebug)) {
      Print("[AURA] Searching map in cache [" + PathToString(fileName) + "]");
    }
    if (m_Aura->m_CFGCacheNamesByMapNames.find(fileName) != m_Aura->m_CFGCacheNamesByMapNames.end()) {
      string cfgName = m_Aura->m_CFGCacheNamesByMapNames[fileName];
      filesystem::path cfgPath = m_Aura->m_Config.m_MapCachePath / filesystem::path(cfgName);
      shared_ptr<CMap> cachedResult = GetBaseMapFromConfigFile(cfgPath, true, true);
      if (cachedResult &&
        (
          cachedResult->GetServerPath() == fileName ||
          FileNameEquals(PathToString(cachedResult->GetServerPath()), PathToString(fileName))
        )
      ) {
        if (
          (!m_GameLocaleLangID.has_value() || cachedResult->GetGameLocaleLangID() == m_GameLocaleLangID.value()) &&
          (!m_GameLocaleMod.has_value() || cachedResult->GetGameLocaleMod() == m_GameLocaleMod.value())
        ) {
          cacheSuccess = true;
        }
      }
      if (cacheSuccess) {
        if (m_Aura->MatchLogLevel(LogLevel::kDebug)) {
          Print("[AURA] Map cache success");
        }
        if (!silent) m_Ctx->SendReply("Loaded OK [" + PathToString(fileName) + "]", CHAT_SEND_SOURCE_ALL | CHAT_LOG_INCIDENT);
        return cachedResult;
      } else {
        m_Aura->m_CFGCacheNamesByMapNames.erase(fileName);
      }
    }
    if (m_Aura->MatchLogLevel(LogLevel::kDebug)) {
      Print("[AURA] Map cache miss");
    }
  }
  return GetBaseMapFromMapFile(mapPath, silent);
}

bool CGameSetup::ApplyMapModifiers(CGameExtraOptions* extraOptions)
{
  bool failed = false;
  if (extraOptions->m_TeamsLocked.has_value()) {
    if (!m_Map->SetGameTeamsLocked(extraOptions->m_TeamsLocked.value()))
      failed = true;
  }
  if (extraOptions->m_TeamsTogether.has_value()) {
    if (!m_Map->SetGameTeamsTogether(extraOptions->m_TeamsTogether.value()))
      failed = true;
  }
  if (extraOptions->m_AdvancedSharedUnitControl.has_value()) {
    if (!m_Map->SetGameAdvancedSharedUnitControl(extraOptions->m_AdvancedSharedUnitControl.value()))
      failed = true;
  }
  if (extraOptions->m_RandomRaces.has_value()) {
    if (!m_Map->SetGameRandomRaces(extraOptions->m_RandomRaces.value()))
      failed = true;
  }
  if (extraOptions->m_RandomHeroes.has_value()) {
    if (!m_Map->SetGameRandomHeroes(extraOptions->m_RandomHeroes.value()))
      failed = true;
  }
  if (extraOptions->m_Visibility.has_value()) {
    if (!m_Map->SetGameVisibility(extraOptions->m_Visibility.value()))
      failed = true;
  }
  if (extraOptions->m_Speed.has_value()) {
    if (!m_Map->SetGameSpeed(extraOptions->m_Speed.value()))
      failed = true;
  }
  if (extraOptions->m_Observers.has_value()) {
    if (!m_Map->SetGameObservers(extraOptions->m_Observers.value())) {
      failed = true;
    }
  }
  return !failed;
}

#ifndef DISABLE_CPR
uint32_t CGameSetup::ResolveMapRepositoryTask()
{
  if (m_Aura->m_Net.m_Config.m_MapRepositories.find(m_SearchTarget.first) == m_Aura->m_Net.m_Config.m_MapRepositories.end()) {
    m_ErrorMessage = "Downloads from  " + m_SearchTarget.first + " are disabled.";
    return RESOLUTION_ERR;
  }

  string downloadUri, downloadFileName;
  uint64_t SearchTargetType = HashCode(m_SearchTarget.first);

  switch (SearchTargetType) {
    case HashCode("epicwar"): {
      m_MapSiteUri = "https://www.epicwar.com/maps/" + m_SearchTarget.second;
      Print("[NET] GET <" + m_MapSiteUri + ">");
      auto response = cpr::Get(
        cpr::Url{m_MapSiteUri},
        cpr::Timeout{m_DownloadTimeout},
        cpr::ProgressCallback(
          [this](cpr::cpr_off_t /* downloadTotal*/, cpr::cpr_off_t /* downloadNow*/, cpr::cpr_off_t /* uploadTotal*/, cpr::cpr_off_t /* uploadNow*/, intptr_t /* userdata*/) -> bool
          {
            return !this->m_ExitingSoon;
          }
        )
      );
      if (m_ExitingSoon) {
        m_ErrorMessage = "Shutting down.";
        return RESOLUTION_ERR;
      }
      if (response.status_code == 0) {
        m_ErrorMessage = "Failed to access " + m_SearchTarget.first + " repository (connectivity error).";
        return RESOLUTION_ERR;
      }
      if (response.status_code != 200) {
        m_ErrorMessage = "Failed to access repository (status code " + to_string(response.status_code) + ").";
        return RESOLUTION_ERR;
      }
      Print("[AURA] resolved " + m_SearchTarget.first + " entry in " + ToFormattedString(static_cast<float>(response.elapsed)) + " seconds");
      
      size_t downloadUriStartIndex = response.text.find("<a href=\"/maps/download/");
      if (downloadUriStartIndex == string::npos) return RESOLUTION_ERR;
      size_t downloadUriEndIndex = response.text.find("\"", downloadUriStartIndex + 24);
      if (downloadUriEndIndex == string::npos) {
        m_ErrorMessage = "Malformed API response";
        return RESOLUTION_ERR;
      }
      downloadUri = "https://epicwar.com" + response.text.substr(downloadUriStartIndex + 9, (downloadUriEndIndex) - (downloadUriStartIndex + 9));
      size_t lastSlashIndex = downloadUri.rfind("/");
      if (lastSlashIndex == string::npos) {
        m_ErrorMessage = "Malformed download URI";
        return RESOLUTION_ERR;
      }
      string encodedName = downloadUri.substr(lastSlashIndex + 1);
      downloadFileName = DecodeURIComponent(encodedName);
      break;
    }

    case HashCode("wc3maps"): {
      m_MapSiteUri = "https://www.wc3maps.com/api/download/" + m_SearchTarget.second;
      Print("[NET] GET <" + m_MapSiteUri + ">");
      auto response = cpr::Get(
        cpr::Url{m_MapSiteUri},
        cpr::Timeout{m_DownloadTimeout},
        cpr::Redirect{0, false, false, cpr::PostRedirectFlags::POST_ALL},
        cpr::ProgressCallback(
          [this](cpr::cpr_off_t /* downloadTotal*/, cpr::cpr_off_t /* downloadNow*/, cpr::cpr_off_t /* uploadTotal*/, cpr::cpr_off_t /* uploadNow*/, intptr_t /* userdata*/) -> bool
          {
            return !this->m_ExitingSoon;
          }
        )
      );
      if (m_ExitingSoon) {
        m_ErrorMessage = "Shutting down.";
        return RESOLUTION_ERR;
      }
      if (response.status_code == 0) {
        m_ErrorMessage = "Remote host unavailable (status code " + to_string(response.status_code) + ").";
        return RESOLUTION_ERR;
      }
      if (response.status_code < 300 || 399 < response.status_code) {
        m_ErrorMessage = "Failed to access repository (status code " + to_string(response.status_code) + ").";
        return RESOLUTION_ERR;
      }
      Print("[AURA] Resolved " + m_SearchTarget.first + " entry in " + ToFormattedString(static_cast<float>(response.elapsed)) + " seconds");
      downloadUri = response.header["location"];
      size_t lastSlashIndex = downloadUri.rfind("/");
      if (lastSlashIndex == string::npos) {
        m_ErrorMessage = "Malformed download URI.";
        return RESOLUTION_ERR;
      }
      downloadFileName = downloadUri.substr(lastSlashIndex + 1);
      downloadUri = downloadUri.substr(0, lastSlashIndex + 1) + EncodeURIComponent(downloadFileName);
      break;
    }

    default: {
      m_ErrorMessage = "Unsupported remote domain: " + m_SearchTarget.first;
      return RESOLUTION_ERR;
    }
  }

  if (downloadFileName.empty() || downloadFileName[0] == '.' || downloadFileName[0] == '-' || downloadFileName.length() > 80) {
    m_ErrorMessage = "Invalid map file.";
    return RESOLUTION_BAD_NAME;
  }

  std::string InvalidChars = "/\\\0\"*?:|<>;,";
  if (downloadFileName.find_first_of(InvalidChars) != string::npos) {
    m_ErrorMessage = "Invalid map file.";
    return RESOLUTION_BAD_NAME;
  }

  string fileExtension = ParseFileExtension(downloadFileName);
  if (fileExtension != ".w3m" && fileExtension != ".w3x") {
    m_ErrorMessage = "Invalid map file.";
    return RESOLUTION_BAD_NAME;
  }

  string downloadFileNameNoExt = downloadFileName.substr(0, downloadFileName.length() - fileExtension.length());
  downloadFileName = downloadFileNameNoExt + fileExtension; // case-sensitive name, but lowercase extension

  // CON, PRN, AUX, NUL, etc. are case-insensitive
  transform(begin(downloadFileNameNoExt), end(downloadFileNameNoExt), begin(downloadFileNameNoExt), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  if (downloadFileNameNoExt == "con" || downloadFileNameNoExt == "prn" || downloadFileNameNoExt == "aux" || downloadFileNameNoExt == "nul") {
    m_ErrorMessage = "Invalid map file.";
    return RESOLUTION_BAD_NAME;
  }
  if (downloadFileNameNoExt.length() == 4 && (downloadFileNameNoExt.substr(0, 3) == "com" || downloadFileNameNoExt.substr(0, 3) == "lpt")) {
    m_ErrorMessage = "Invalid map file.";
    return RESOLUTION_BAD_NAME;
  }

  m_IsStepDownloaded = true;
  m_BaseDownloadFileName = downloadFileName;
  m_MapDownloadUri = downloadUri;

  return RESOLUTION_OK;
}

void CGameSetup::RunResolveMapRepository()
{
  m_IsStepDownloading = true;
  m_AsyncStep = GAMESETUP_STEP_RESOLUTION;
  m_DownloadFuture = async(launch::async, &::CGameSetup::ResolveMapRepositoryTask, this);
}

uint32_t CGameSetup::RunResolveMapRepositorySync()
{
  m_IsStepDownloading = true;
  uint32_t result = ResolveMapRepositoryTask();
  m_IsStepDownloading = false;
  if (!m_IsStepDownloaded) {
    m_Ctx->ErrorReply(m_ErrorMessage, CHAT_SEND_SOURCE_ALL | CHAT_LOG_INCIDENT);
    return 0;
  }
  m_IsStepDownloading = false;
  m_IsStepDownloaded = false;
  return result;
}

void CGameSetup::OnResolveMapSuccess()
{
  if (PrepareDownloadMap()) {
    RunDownloadMap();
  } else {
    DPRINT_IF(LogLevel::kTrace, "[GAMESETUP] Unable to start map download.");
    OnLoadMapError();
  }  
}

void CGameSetup::SetDownloadFilePath(filesystem::path&& filePath)
{
  m_DownloadFilePath = move(filePath);
}

bool CGameSetup::PrepareDownloadMap()
{
  if (m_SearchTarget.first != "epicwar" && m_SearchTarget.first != "wc3maps") {
    Print("Error!! trying to download from unsupported repository [" + m_SearchTarget.first + "] !!");
    return false;
  }
  if (!m_Aura->m_Net.m_Config.m_AllowDownloads) {
    m_Ctx->ErrorReply("Map downloads are not allowed.", CHAT_SEND_SOURCE_ALL);
    return false;
  }

  string fileNameFragmentPost = ParseFileExtension(m_BaseDownloadFileName);
  string fileNameFragmentPre = m_BaseDownloadFileName.substr(0, m_BaseDownloadFileName.length() - fileNameFragmentPost.length());
  bool nameSuccess = false;
  string mapSuffix;
  for (uint8_t i = 0; i < 10; ++i) {
    if (i != 0) {
      mapSuffix = "~" + to_string(i);
    }
    string testFileName = fileNameFragmentPre + mapSuffix + fileNameFragmentPost;
    filesystem::path testFilePath = m_Aura->m_Config.m_MapPath / filesystem::path(testFileName);
    if (FileExists(testFilePath)) {
      // Map already exists.
      // I'd rather directly open the file with wx flags to avoid racing conditions,
      // but there is no standard C++ way to do this, and cstdio isn't very helpful.
      continue;
    }
    if (m_Aura->m_MapFilesTimedBusyLocks.find(testFileName) != m_Aura->m_MapFilesTimedBusyLocks.end()) {
      // Map already hosted.
      continue;
    }
    SetDownloadFilePath(move(testFilePath));
    nameSuccess = true;
    break;
  }
  if (!nameSuccess) {
    m_Ctx->ErrorReply("Download failed - duplicate map name [" + m_BaseDownloadFileName + "].", CHAT_SEND_SOURCE_ALL);
    return false;
  }
  Print("[NET] GET <" + m_MapDownloadUri + "> as [" + PathToString(m_DownloadFilePath.filename()) + "]...");
  m_DownloadFileStream = new ofstream(m_DownloadFilePath.native().c_str(), std::ios_base::out | std::ios_base::binary);
  return true;
}

uint32_t CGameSetup::DownloadMapTask()
{
  if (!m_DownloadFileStream) return 0;
  if (!m_DownloadFileStream->is_open()) {
    m_DownloadFileStream->close();
    delete m_DownloadFileStream;
    m_DownloadFileStream = nullptr;
    m_ErrorMessage = "Download failed - unable to write to disk.";
    return 0;
  }
  cpr::Response response = cpr::Download(
    *m_DownloadFileStream,
    cpr::Url{m_MapDownloadUri},
    cpr::Header{{"user-agent", "Mozilla/5.0 (Windows NT 6.1; Win64; x64; rv:109.0) Gecko/20100101 Firefox/115.0"}},
    cpr::Timeout{m_DownloadTimeout},
    cpr::ProgressCallback(
      [this](cpr::cpr_off_t /* downloadTotal*/, cpr::cpr_off_t /* downloadNow*/, cpr::cpr_off_t /* uploadTotal*/, cpr::cpr_off_t /* uploadNow*/, intptr_t /* userdata*/) -> bool
      {
        return !this->m_ExitingSoon;
      }
    )
  );
  m_DownloadFileStream->close();
  delete m_DownloadFileStream;
  m_DownloadFileStream = nullptr;
  if (m_ExitingSoon) {
    m_ErrorMessage = "Shutting down.";
    FileDelete(m_DownloadFilePath);
    return RESOLUTION_ERR;
  }
  if (response.status_code == 0) {
    m_ErrorMessage = "Failed to access " + m_SearchTarget.first + " repository (connectivity error).";
    FileDelete(m_DownloadFilePath);
    return 0;
  }
  if (response.status_code != 200) {
    m_ErrorMessage = "Map not found in " + m_SearchTarget.first + " repository (code " + to_string(response.status_code) + ").";
    FileDelete(m_DownloadFilePath);
    return 0;
  }
  const bool timedOut = cpr::ErrorCode::OPERATION_TIMEDOUT == response.error.code;
  if (timedOut) {
    m_ErrorMessage = "Map download took too long.";
    FileDelete(m_DownloadFilePath);
    return 0;
  }
  Print("[AURA] download finished in " + ToFormattedString(static_cast<float>(response.elapsed)) + " seconds");
  // Signals completion.
  m_IsStepDownloaded = true;

  return signed_cast_lossy<uint32_t>(response.downloaded_bytes);
}

void CGameSetup::RunDownloadMap()
{
  if (m_ExitingSoon) return;
  m_IsStepDownloading = true;
  m_AsyncStep = GAMESETUP_STEP_DOWNLOAD;
  m_DownloadFuture = async(launch::async, &::CGameSetup::DownloadMapTask, this);
}

uint32_t CGameSetup::RunDownloadMapSync()
{
  m_IsStepDownloading = true;
  uint32_t byteSize = DownloadMapTask();
  m_IsStepDownloading = false;
  if (!m_IsStepDownloaded) {
    m_Ctx->ErrorReply(m_ErrorMessage, CHAT_SEND_SOURCE_ALL | CHAT_LOG_INCIDENT);
    return 0;
  }
  m_IsMapDownloaded = true;
  m_IsStepDownloaded = false;
  return byteSize;
}

vector<pair<string, string>> CGameSetup::GetMapRepositorySuggestions(const string& pattern, const uint8_t maxCount)
{
  vector<pair<string, string>> suggestions;
  string searchUri = "https://www.epicwar.com/maps/search/?go=1&n=" + EncodeURIComponent(pattern) + "&a=&c=0&p=0&pf=0&roc=0&tft=0&order=desc&sort=downloads&page=1";
  Print("[AURA] Looking up suggestions...");
  Print("[AURA] GET <" + searchUri + ">");
  auto response = cpr::Get(
    cpr::Url{searchUri},
    cpr::Timeout{m_SuggestionsTimeout},
    cpr::ProgressCallback(
      [this](cpr::cpr_off_t /* downloadTotal*/, cpr::cpr_off_t /* downloadNow*/, cpr::cpr_off_t /* uploadTotal*/, cpr::cpr_off_t /* uploadNow*/, intptr_t /* userdata*/) -> bool
      {
        return !this->m_ExitingSoon;
      }
    )
  );
  if (m_ExitingSoon || response.status_code != 200) {
    return suggestions;
  }

  vector<pair<string, int>> matchingMaps = ExtractEpicWarMaps(response.text, maxCount);
  for (const auto& element : matchingMaps) {
    suggestions.push_back(make_pair(element.first, "epicwar-" + to_string(element.second)));
  }
  return suggestions;
}

#endif

void CGameSetup::LoadMap()
{
  if (m_Map) {
    DPRINT_IF(LogLevel::kTrace, "[GAMESETUP] Map is already loaded.");
    OnLoadMapSuccess();
    return;
  }

  ParseInput();
  pair<uint8_t, std::filesystem::path> searchResult = SearchInput();
  if (searchResult.first == MATCH_TYPE_FORBIDDEN) {
    OnLoadMapError();
    return;
  }
  if (searchResult.first == MATCH_TYPE_INVALID) {
    // Exclusive to standard paths mode.
    m_Ctx->ErrorReply("Invalid file extension for [" + PathToString(searchResult.second.filename()) + "]. Please use --search-type");
    OnLoadMapError();
    return;
  }
  if (searchResult.first != MATCH_TYPE_MAP && searchResult.first != MATCH_TYPE_CONFIG) {
    if (m_SearchType != SEARCH_TYPE_ANY || !m_IsDownloadable) {
      PRINT_IF(LogLevel::kDebug, "[GAMESETUP] No results found matching search criteria.");
      OnLoadMapError();
      return;
    }
    if (m_Aura->m_Config.m_EnableCFGCache) {
      filesystem::path cachePath = m_Aura->m_Config.m_MapCachePath / filesystem::path(m_SearchTarget.first + "-" + m_SearchTarget.second + ".ini");
      m_Map = GetBaseMapFromConfigFile(cachePath, true, true);
      if (m_Map) {
        DPRINT_IF(LogLevel::kTrace, "[GAMESETUP] Map loaded from cache.");
        OnLoadMapSuccess();
        return;
      }
    }
#ifndef DISABLE_CPR
    m_Ctx->SendReply("Resolving map repository...");
    RunResolveMapRepository();
    return;
#else
    DPRINT_IF(LogLevel::kTrace, "[GAMESETUP] Map downloads not supported in this Aura distribution");
    OnLoadMapError();
    return;
#endif
  }
  if (searchResult.first == MATCH_TYPE_CONFIG) {
    DPRINT_IF(LogLevel::kTrace, "[GAMESETUP] Loading config...");
    m_Map = GetBaseMapFromConfigFile(searchResult.second, false, false);
  } else {
    DPRINT_IF(LogLevel::kTrace, "[GAMESETUP] Loading from map or cache...");
    m_Map = GetBaseMapFromMapFileOrCache(searchResult.second, false);
  }
  if (m_Map) {
    DPRINT_IF(LogLevel::kTrace, "[GAMESETUP] Map loaded successfully.");
    OnLoadMapSuccess();
  } else {
    PRINT_IF(LogLevel::kDebug, "[GAMESETUP] Map failed to load");
    OnLoadMapError();
  }
//
}

void CGameSetup::OnLoadMapSuccess()
{
  if (m_ExitingSoon) {
    m_DeleteMe = true;
    return;
  }
  if (m_Ctx->GetPartiallyDestroyed()) {
    PRINT_IF(LogLevel::kError, "[GAMESETUP] Game setup aborted - context destroyed");
    m_DeleteMe = true;
    return;
  }
  if (m_MapExtraOptions) {
    if (!ApplyMapModifiers(m_MapExtraOptions)) {
      m_Ctx->ErrorReply("Invalid map options. Map has fixed player settings.", CHAT_SEND_SOURCE_ALL);
      m_DeleteMe = true;
      ClearExtraOptions();
      return;
    }
    ClearExtraOptions();
  }
  if (!m_SaveFile.empty()) {
    if (!RestoreFromSaveFile()) {
      m_Ctx->ErrorReply("Invalid save file.", CHAT_SEND_SOURCE_ALL);
      m_DeleteMe = true;
      return;
    }
  }

  if (m_MapReadyCallbackAction == MAP_ONREADY_ALIAS) {
    if (m_Aura->m_DB->AliasAdd(m_MapReadyCallbackData, m_Map->GetServerFileName())) {
      m_Ctx->SendReply("Alias " + SanitizeWrapUTF8(m_MapReadyCallbackData) + " added for " + SanitizeWrapUTF8(m_Map->GetServerFileName()));
    } else {
      m_Ctx->ErrorReply("Failed to add alias.");
    }
  } else if (m_MapReadyCallbackAction == MAP_ONREADY_HOST) {
    /*if (m_Aura->m_StartedGames.size() > m_Aura->m_Config.m_MaxStartedGames || m_Aura->m_StartedGames.size() == m_Aura->m_Config.m_MaxStartedGames && !m_Aura->m_Config.m_DoNotCountReplaceableLobby) {
      m_Ctx->ErrorReply("Games hosted quota reached.", CHAT_SEND_SOURCE_ALL);
      return;
	}*/
    SetBaseName(m_MapReadyCallbackData);
    shared_ptr<CGame> sourceGame = m_Ctx->GetSourceGame();
    shared_ptr<CGame> targetGame = m_Ctx->GetTargetGame();
    if (targetGame && !targetGame->GetCountDownStarted() && targetGame->GetIsReplaceable() && !targetGame->GetIsBeingReplaced()) {
      targetGame->SendAllChat("Another lobby is being created. This lobby will be closed soon.");
      targetGame->StartGameOverTimer();
      targetGame->SetIsBeingReplaced(true);
      ++m_Aura->m_ReplacingLobbiesCounter;
    }
    shared_ptr<CRealm> sourceRealm = m_Ctx->GetSourceRealm();
    if (m_Aura->m_Config.m_AutomaticallySetGameOwner) {
      SetOwner(m_Ctx->GetSender(), sourceRealm);
    }
    AcquireCreator();
    RunHost();
  }
}

void CGameSetup::OnLoadMapError()
{
  if (m_ExitingSoon || m_Ctx->GetPartiallyDestroyed()) {
    m_DeleteMe = true;
    return;
  }
  if (m_FoundSuggestions) {
    m_Ctx->ErrorReply("Not found. Use the epicwar-number identifiers.", CHAT_SEND_SOURCE_ALL);
  } else {
    m_Ctx->ErrorReply("Map not found", CHAT_SEND_SOURCE_ALL);
  }
  m_DeleteMe = true;
}

#ifndef DISABLE_CPR
void CGameSetup::OnDownloadMapSuccess()
{
  if (m_ExitingSoon || !m_Aura) {
    m_DeleteMe = true;
    return;
  }
  m_IsMapDownloaded = true;
  m_Map = GetBaseMapFromMapFileOrCache(m_DownloadFilePath, false);
  if (m_Map) {
    DPRINT_IF(LogLevel::kTrace, "[GAMESETUP] Downloaded map loaded successfully.");
    OnLoadMapSuccess();
  } else {
    DPRINT_IF(LogLevel::kTrace, "[GAMESETUP] Downloaded map failed to load.");
    OnLoadMapError();
  }
}

void CGameSetup::OnFetchSuggestionsEnd()
{
}
#endif

bool CGameSetup::GetMapLoaded() const
{
  return m_Map != nullptr;
}

bool CGameSetup::LoadMapSync()
{
  if (m_Map) return true;

  ParseInput();
  pair<uint8_t, std::filesystem::path> searchResult = SearchInput();
  if (searchResult.first == MATCH_TYPE_FORBIDDEN) {
    return false;
  }
  if (searchResult.first == MATCH_TYPE_INVALID) {
    // Exclusive to standard paths mode.
    m_Ctx->ErrorReply("Invalid file extension for [" + PathToString(searchResult.second.filename()) + "]. Please use --search-type");
    return false;
  }
  if (searchResult.first != MATCH_TYPE_MAP && searchResult.first != MATCH_TYPE_CONFIG) {
    if (m_SearchType != SEARCH_TYPE_ANY || !m_IsDownloadable) {
      return false;
    }
    if (m_Aura->m_Config.m_EnableCFGCache) {
      filesystem::path cachePath = m_Aura->m_Config.m_MapCachePath / filesystem::path(m_SearchTarget.first + "-" + m_SearchTarget.second + ".ini");
      m_Map = GetBaseMapFromConfigFile(cachePath, true, true);
      if (m_Map) return true;
    }
#ifndef DISABLE_CPR
    if (RunResolveMapRepositorySync() != RESOLUTION_OK) {
      Print("[AURA] Failed to resolve remote map.");
      return false;
    }
    if (!PrepareDownloadMap()) {
      return false;
    }
    uint32_t downloadSize = RunDownloadMapSync();
    if (downloadSize == 0) {
      Print("[AURA] Failed to download map.");
      return false;
    }
    m_Map = GetBaseMapFromMapFileOrCache(m_DownloadFilePath, false);
    return true;
#else
    return false;
#endif
  }
  if (searchResult.first == MATCH_TYPE_CONFIG) {
    m_Map = GetBaseMapFromConfigFile(searchResult.second, false, false);
  } else {
    m_Map = GetBaseMapFromMapFileOrCache(searchResult.second, false);
  }
  return m_Map != nullptr;
}

bool CGameSetup::GetIsActive()
{
  return m_Aura->m_GameSetup == shared_from_this();
}

void CGameSetup::SetActive()
{
  if (m_Aura->m_GameSetup) {
    if (!m_Aura->m_AutoRehostGameSetup || m_Aura->m_AutoRehostGameSetup.get() != m_Aura->m_GameSetup.get()) {
      DPRINT_IF(LogLevel::kTrace, "[GAMESETUP] Pending game setup destroyed");
    } else if (this != m_Aura->m_AutoRehostGameSetup.get()) {
      DPRINT_IF(LogLevel::kTrace, "[GAMESETUP] Auto-rehost game setup deprioritized");
    }
  }
  m_Aura->m_GameSetup = shared_from_this();
  m_ActiveTicks = m_Aura->GetLoopTicks();
}

bool CGameSetup::RestoreFromSaveFile()
{
  if (!m_GameVersion.has_value()) {
    m_Ctx->ErrorReply("Game version not specified", CHAT_SEND_SOURCE_ALL);
    return false;
  }
  m_RestoredGame = make_shared<CSaveGame>(m_Aura, m_SaveFile);
  if (!m_RestoredGame->Load()) return false;
  bool success = m_RestoredGame->Parse();
  m_RestoredGame->Unload();

  const GameStat& gameStat = m_RestoredGame->GetGameStat();
  vector<string> mismatchReasons;
  if (!m_Map->MatchMapScriptsBlizz(*m_GameVersion, gameStat.GetMapScriptsBlizzHash()).value_or(true)) {
    mismatchReasons.push_back("maps are different (save expects blizz hash: " + ByteArrayToDecString(gameStat.GetMapScriptsBlizzHash()) + ")");
  }
  if (gameStat.GetHasSHA1() && !m_Map->MatchMapScriptsSHA1(*m_GameVersion, gameStat.GetMapScriptsSHA1()).value_or(true)) {
    mismatchReasons.push_back("maps are different (save expects sha1 hash: " + ByteArrayToDecString(gameStat.GetMapScriptsSHA1()) + ")");
  }
  if (!CaseInsensitiveEquals(m_Map->GetClientFileName(), gameStat.GetMapClientFileName())) {
    mismatchReasons.push_back("filenames are different (save file: " + SanitizeWrapUTF8(gameStat.GetMapClientFileName()) + " vs game: " + SanitizeWrapUTF8(m_Map->GetClientFileName()));
  }
  if (!mismatchReasons.empty()) {
    Print("[GAMESETUP] Save file is not valid, because " + JoinStrings(mismatchReasons));
    success = false;
  }

  return success;
}

bool CGameSetup::RunHost()
{
  return m_Aura->CreateGame(shared_from_this());
}

uint32_t CGameSetup::GetGameIdentifier() const
{
  const GameHost* rawSource = m_Mirror.GetRawSource();
  if (!rawSource) {
    return m_Aura->NextHostCounter();
  }
  return rawSource->GetIdentifier();
}

uint32_t CGameSetup::GetEntryKey() const
{
  const GameHost* rawSource = m_Mirror.GetRawSource();
  if (!rawSource) {
    return GetRandomUInt32();
  }
  return rawSource->GetEntryKey();
}

const sockaddr_storage* CGameSetup::GetGameAddress() const
{
  const GameHost* rawSource = m_Mirror.GetRawSource();
  if (!rawSource) {
    return nullptr;
  }
  return rawSource->GetAddress();
}

void CGameSetup::AddIgnoredRealm(shared_ptr<const CRealm> nRealm)
{
  m_RealmsExcluded.insert(nRealm->GetServer());
}

void CGameSetup::RemoveIgnoredRealm(shared_ptr<const CRealm> nRealm)
{
  m_RealmsExcluded.erase(nRealm->GetServer());
}

void CGameSetup::SetOwner(const string& nOwner, shared_ptr<const CRealm> nRealm)
{
  if (nRealm == nullptr) {
    m_Owner = make_pair(nOwner, string());
  } else {
    m_Owner = make_pair(nOwner, nRealm->GetServer());
  }
}

void CGameSetup::RemoveCreator()
{
  m_Creator.Reset();
}

void CGameSetup::SetCreator(const ServiceType serviceType, const string& creatorName)
{
  m_Creator = ServiceUser(serviceType, creatorName);
}

void CGameSetup::AcquireCreator()
{
  if (!m_Ctx) return;
  m_Creator = m_Ctx->GetServiceSource();
}

template <typename T>
std::shared_ptr<T> CGameSetup::GetCreatedFrom() const
{
  return m_Creator.GetService<T>();
}

template shared_ptr<CGame> CGameSetup::GetCreatedFrom() const;
template shared_ptr<CRealm> CGameSetup::GetCreatedFrom() const;

bool CGameSetup::MatchesCreatedFrom(const ServiceType fromType) const
{
  return m_Creator.GetServiceType() == fromType;
}

bool CGameSetup::MatchesCreatedFrom(const ServiceType fromType, shared_ptr<const void> fromThing) const
{
  if (m_Creator.GetServiceType() != fromType) return false;
  switch (fromType) {
    case ServiceType::kGame:
      return static_pointer_cast<const CGame>(fromThing) == GetCreatedFrom<const CGame>();
    case ServiceType::kRealm:
      return static_pointer_cast<const CRealm>(fromThing) == GetCreatedFrom<const CRealm>();
    default:
      return true;
  }
}

bool CGameSetup::MatchesCreatedFromGame(shared_ptr<const CGame> nGame) const
{
  return MatchesCreatedFrom(ServiceType::kGame, static_pointer_cast<const void>(nGame));
}

bool CGameSetup::MatchesCreatedFromRealm(shared_ptr<const CRealm> nRealm) const
{
  return MatchesCreatedFrom(ServiceType::kRealm, static_pointer_cast<const void>(nRealm));
}

bool CGameSetup::MatchesCreatedFromIRC() const
{
  return MatchesCreatedFrom(ServiceType::kIRC);
}

bool CGameSetup::MatchesCreatedFromDiscord() const
{
  return MatchesCreatedFrom(ServiceType::kDiscord);
}

void CGameSetup::OnGameCreate()
{
  m_RestoredGame.reset();
  if (m_LobbyAutoRehosted) {
    m_CreationCounter = (m_CreationCounter + 1) % 36;
    if (m_CreationCounter == 0) ++m_CreationCounter;
  }
}

bool CGameSetup::Update()
{
#ifndef DISABLE_CPR
  if (!m_IsStepDownloading) return m_DeleteMe;
  auto status = m_DownloadFuture.wait_for(chrono::seconds(0));
  if (status != future_status::ready) return m_DeleteMe;
  const bool success = m_IsStepDownloaded;
  const uint8_t finishedStep = m_AsyncStep;
  m_IsStepDownloading = false;
  m_IsStepDownloaded = false;
  m_AsyncStep = GAMESETUP_STEP_MAIN;
  if (!success && finishedStep != GAMESETUP_STEP_SUGGESTIONS) {
    m_Ctx->ErrorReply(m_ErrorMessage, CHAT_SEND_SOURCE_ALL | CHAT_LOG_INCIDENT);
    PRINT_IF(LogLevel::kDebug, "[GAMESETUP] Task failed. Releasing game setup...");
    m_DeleteMe = true;
    return m_DeleteMe;
  }
  switch (finishedStep) {
    case GAMESETUP_STEP_RESOLUTION:
      DPRINT_IF(LogLevel::kTrace, "[GAMESETUP] Map resolution completed");
      OnResolveMapSuccess();
      break;
    case GAMESETUP_STEP_DOWNLOAD:
      DPRINT_IF(LogLevel::kTrace, "[GAMESETUP] Map download completed");
      OnDownloadMapSuccess();
      break;
    case GAMESETUP_STEP_SUGGESTIONS:
      DPRINT_IF(LogLevel::kTrace, "[GAMESETUP] Map suggestions fetched");
      OnFetchSuggestionsEnd();
      break;
    default:
      Print("[AURA] error - unhandled async task completion");
  }
#endif
  return m_DeleteMe;
}

void CGameSetup::AwaitSettled()
{
#ifndef DISABLE_DPP
  if (m_IsStepDownloading) {
    m_DownloadFuture.wait();
  }
#endif
}

void CGameSetup::SetGameSavedFile(const std::filesystem::path& filePath)
{
  if (m_StandardPaths) {
    m_SaveFile = filePath;
  } else if (filePath != filePath.filename()) {
    m_SaveFile = filePath;
  } else {
    m_SaveFile = m_Aura->m_Config.m_GameSavePath / filePath;
  }
}

void CGameSetup::ClearExtraOptions()
{
  if (m_MapExtraOptions) {
    delete m_MapExtraOptions;
    m_MapExtraOptions = nullptr;
  }
}

void CGameSetup::ExportTemporaryToMap(CConfig* mapCFG)
{
  if (m_GameVersion.has_value()) {
    // <map.cfg.hosting.game_versions.main> is temporary
    // overrides <map.hosting.game_versions.main>
    mapCFG->SetString("map.cfg.hosting.game_versions.main", ToVersionString(m_GameVersion.value()));
  }
  if (m_GameIsExpansion.has_value()) {
    // <map.cfg.hosting.game_versions.expansion.default> is temporary
    // overrides <map.hosting.game_versions.expansion.default>
    if (m_GameIsExpansion.value()) {
      mapCFG->SetString("map.cfg.hosting.game_versions.expansion.default", "tft");
    } else {
      mapCFG->SetString("map.cfg.hosting.game_versions.expansion.default", "roc");
    }
  }
}

void CGameSetup::DeleteTemporaryFromMap(CConfig* mapCFG)
{
  mapCFG->Delete("map.cfg.hosting.game_versions.main");
  mapCFG->Delete("map.cfg.hosting.game_versions.expansion.default");
}

string CGameSetup::NormalizeGameName(const string& baseGameName)
{
  string gameName = baseGameName;
  gameName = TrimString(RemoveDuplicateWhiteSpace(gameName));
  return gameName;
}

bool CGameSetup::AcquireCLIEarly(const CCLI* nCLI)
{
  if (nCLI->m_GameSavedPath.has_value()) SetGameSavedFile(nCLI->m_GameSavedPath.value());
  // CPR timeouts are int32_t - signed!
  if (nCLI->m_GameMapDownloadTimeout.has_value()) SetDownloadTimeout(nCLI->m_GameMapDownloadTimeout.value());
  WriteOpt(m_GameIsExpansion) << nCLI->m_GameIsExpansion;
  WriteOpt(m_GameVersion) << nCLI->m_GameVersion;
  WriteOpt(m_GameLocaleMod) << nCLI->m_GameLocaleMod;
  WriteOpt(m_GameLocaleLangID) << nCLI->m_GameLocaleLangID;

  if (!m_GameVersion.has_value()) {
    if (!m_Aura->m_GameDefaultConfig->m_GameVersion.has_value()) {
      Print("[GAMESETUP] Failed to resolve game version. You must use any of --game-version or <hosting.game_versions.main>");
      return false;
    }
    m_GameVersion = m_Aura->m_GameDefaultConfig->m_GameVersion.value();
  }
  return true;
}

void CGameSetup::AcquireHost(const CCLI* nCLI, const optional<string>& mpName)
{
  if (nCLI->m_GameName.has_value()) {
    SetBaseName(nCLI->m_GameName.value());
  } else {
    if (mpName.has_value()) {
      SetBaseName(mpName.value() + "'s game");
    } else {
      SetBaseName("Join and play");
    }
  }
  if (mpName.has_value()) {
    SetCreator(ServiceType::kCLI, mpName.value());
  }
  if (nCLI->m_GameOwner.has_value()) {
    pair<string, string> owner = SplitAddress(nCLI->m_GameOwner.value());
    SetOwner(ToLowerCase(owner.first), ToLowerCase(owner.second));
  } else if (nCLI->m_GameOwnerLess.value_or(false)) {
    SetOwnerLess(true);
  }
}

void CGameSetup::AcquireCLISimple(const CCLI* nCLI)
{
  WriteOpt(m_LobbyTimeoutMode) << nCLI->m_GameLobbyTimeoutMode;
  WriteOpt(m_LobbyOwnerTimeoutMode) << nCLI->m_GameLobbyOwnerTimeoutMode;
  WriteOpt(m_LoadingTimeoutMode) << nCLI->m_GameLoadingTimeoutMode;
  WriteOpt(m_PlayingTimeoutMode) << nCLI->m_GamePlayingTimeoutMode;

  WriteOpt(m_LobbyTimeout) << nCLI->m_GameLobbyTimeout;
  WriteOpt(m_LobbyOwnerTimeout) << nCLI->m_GameLobbyOwnerTimeout;
  WriteOpt(m_LoadingTimeout) << nCLI->m_GameLoadingTimeout;
  WriteOpt(m_PlayingTimeout) << nCLI->m_GamePlayingTimeout;

  WriteOpt(m_PlayingTimeoutWarningShortCountDown) << nCLI->m_GamePlayingTimeoutWarningShortCountDown;
  WriteOpt(m_PlayingTimeoutWarningShortInterval) << nCLI->m_GamePlayingTimeoutWarningShortInterval;
  WriteOpt(m_PlayingTimeoutWarningLargeCountDown) << nCLI->m_GamePlayingTimeoutWarningLargeCountDown;
  WriteOpt(m_PlayingTimeoutWarningLargeInterval) << nCLI->m_GamePlayingTimeoutWarningLargeInterval;

  WriteOpt(m_LobbyOwnerReleaseLANLeaver) << nCLI->m_GameLobbyOwnerReleaseLANLeaver;

  WriteOpt(m_LobbyCountDownInterval) << nCLI->m_GameLobbyCountDownInterval;
  WriteOpt(m_LobbyCountDownStartValue) << nCLI->m_GameLobbyCountDownStartValue;

  WriteOpt(m_CheckJoinable) << nCLI->m_GameCheckJoinable;
  WriteOpt(m_NotifyJoins) << nCLI->m_GameNotifyJoins;
  WriteOpt(m_SaveGameAllowed) << nCLI->m_GameSaveAllowed;
  WriteOpt(m_PauseGameAllowed) << nCLI->m_GamePauseAllowed;
  WriteOpt(m_ChecksReservation) << nCLI->m_GameCheckReservation;

  WriteOpt(m_AutoStartPlayers) << nCLI->m_GameAutoStartPlayers;
  WriteOpt(m_AutoStartSeconds) << nCLI->m_GameAutoStartSeconds;
  WriteOpt(m_AutoStartRequiresBalance) << nCLI->m_GameAutoStartRequiresBalance;

  WriteOpt(m_EnableLagScreen) << nCLI->m_GameEnableLagScreen;
  WriteOpt(m_LatencyAverage) << nCLI->m_GameLatencyAverage;
  WriteOpt(m_StartLagPlayersMinMs) << nCLI->m_GameStartLagPlayersMinMs;
  WriteOpt(m_StopLagPlayersMaxMs) << nCLI->m_GameStopLagPlayersMaxMs;
  WriteOpt(m_StartLagObserversMinMs) << nCLI->m_GameStartLagObserversMinMs;
  WriteOpt(m_StopLagObserversMaxMs) << nCLI->m_GameStopLagObserversMaxMs;

  WriteOpt(m_LatencyEqualizerEnabled) << nCLI->m_GameLatencyEqualizerEnabled;
  WriteOpt(m_LatencyEqualizerMaxDelay) << nCLI->m_GameLatencyEqualizerMaxDelay;

  WriteOpt(m_HCL) << nCLI->m_GameHCL;

  WriteOpt(m_NumPlayersToStartGameOver) << nCLI->m_GameNumPlayersToStartGameOver;

  WriteOpt(m_AutoKickPing) << nCLI->m_GameAutoKickPing;
  WriteOpt(m_WarnHighPing) << nCLI->m_GameWarnHighPing;
  WriteOpt(m_SafeHighPing) << nCLI->m_GameSafeHighPing;

  WriteOpt(m_SyncNormalize) << nCLI->m_GameSyncNormalize;

  WriteOpt(m_MaxAPM) << nCLI->m_GameMaxAPM;
  WriteOpt(m_MaxBurstAPM) << nCLI->m_GameMaxBurstAPM;

  WriteOpt(m_HideLobbyNames) << nCLI->m_GameHideLobbyNames;
  WriteOpt(m_HideInGameNames) << nCLI->m_GameHideLoadedNames;
  WriteOpt(m_ResultSource) << nCLI->m_GameResultSource;
  WriteOpt(m_LoadInGame) << nCLI->m_GameLoadInGame;
  WriteOpt(m_FakeUsersShareUnitsMode) << nCLI->m_GameFakeUsersShareUnitsMode;
  WriteOpt(m_PlayersReadyMode) << nCLI->m_GamePlayersReadyMode;
  WriteOpt(m_EnableJoinObserversInProgress) << nCLI->m_GameEnableJoinObserversInProgress;
  WriteOpt(m_EnableJoinPlayersInProgress) << nCLI->m_GameEnableJoinPlayersInProgress;
  WriteOpt(m_SpectatorDelay) << nCLI->m_GameSpectatorDelay;

  WriteOpt(m_LogCommands) << nCLI->m_GameLogCommands;

  WriteOpt(m_ReconnectionMode) << nCLI->m_GameReconnectionMode;
  WriteOpt(m_EnableLobbyChat) << nCLI->m_GameEnableLobbyChat;
  WriteOpt(m_EnableInGameChat) << nCLI->m_GameEnableInGameChat;
  WriteOpt(m_IPFloodHandler) << nCLI->m_GameIPFloodHandler;
  WriteOpt(m_LeaverHandler) << nCLI->m_GameLeaverHandler;
  WriteOpt(m_ShareUnitsHandler) << nCLI->m_GameShareUnitsHandler;
  WriteOpt(m_UnsafeNameHandler) << nCLI->m_GameUnsafeNameHandler;
  WriteOpt(m_BroadcastErrorHandler) << nCLI->m_GameBroadcastErrorHandler;
  WriteOpt(m_CrossPlayMode) << nCLI->m_GameCrossPlayMode;

  if (nCLI->m_GameFreeForAll.value_or(false)) {
    m_CustomLayout = CUSTOM_LAYOUT_FFA; // optional
  }

  // Write to mandatory members of CGameSetup
  m_Reservations = nCLI->m_GameReservations;
  m_Verbose = nCLI->m_Verbose;
  m_LobbyReplaceable = nCLI->m_GameLobbyReplaceable.value_or(false);
  m_LobbyAutoRehosted = nCLI->m_GameLobbyAutoRehosted.value_or(false);
  m_RealmsDisplayMode = nCLI->m_GameDisplayMode.value_or(GAME_DISPLAY_PUBLIC);
}

bool CGameSetup::AcquireCLIMirror(const CCLI* nCLI)
{
  if (nCLI->m_GameMirrorSource.index() == 0) {
    return false;
  }
  GameMirrorSetup& mirror = GetMirror();
  switch (nCLI->m_GameMirrorSourceType.value()) {
    case MirrorSourceType::kRaw: {
      const GameHost& gameHost = get<GameHost>(nCLI->m_GameMirrorSource);
      if (!mirror.SetRawSource(gameHost)) {
        return false;
      }
      break;
    }
    case MirrorSourceType::kRegistry: {
      const StringPair& registrySource = get<StringPair>(nCLI->m_GameMirrorSource);
      void* servicePtr = nullptr;
      ServiceType serviceType = m_Aura->FindServiceFromHostName(registrySource.second, servicePtr);
      if (serviceType != ServiceType::kRealm) {
        return false;
      }
      shared_ptr<CRealm> sourceRealm =  static_cast<CRealm*>(servicePtr)->shared_from_this();
      /*
      if (sourceRealm->GetIsCryptoHost(hostName)) {
        Print("[GAME] Games created by host [" + hostName + "] are encrypted and cannot be mirrored.");
        return false;
      }
      */
      if (!mirror.SetRegistrySource(registrySource)) {
        return false;
      }
      AddIgnoredRealm(sourceRealm);
      mirror.SetSourceRealm(sourceRealm);
      sourceRealm->QuerySearch(registrySource.first, shared_from_this());
      break;
    }
    IGNORE_ENUM_LAST(MirrorSourceType)
  }

  ReadOpt(nCLI->m_GameMirrorProxy) >> m_Mirror.m_EnableProxy;
  return true;
}

CGameSetup::~CGameSetup()
{
  ClearExtraOptions();

  m_Ctx.reset();

  m_RestoredGame.reset();

  m_Creator.Reset();
  m_Aura = nullptr;

  m_Map.reset();
}

#undef SEARCH_RESULT
