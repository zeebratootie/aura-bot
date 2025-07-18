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

#ifndef AURA_CONSTANTS_H_
#define AURA_CONSTANTS_H_

#include <string>
#include <type_traits>

template<typename T>
struct is_comparable_enum : std::false_type {};

// includes.h

enum class Endianness : bool {
  kLittle = true,
  kBig = false,
};

enum class OOBPolicy : bool {
  kCheck = true,
  kUnsafe = false,
};

enum class StringEncoding : bool {
  kUTF8 = true,
  kNone = false,
};

enum class CaseSensitive : bool {
  kStrict = true,
  kNormalize = false,
};

enum class NullTerminatorPolicy : bool
{
  kRequired = true,
  kOptional = false,
};

constexpr uint8_t LOG_LEVEL_EMERGENCY = 0;
constexpr uint8_t LOG_LEVEL_ALERT = 1;
constexpr uint8_t LOG_LEVEL_CRITICAL = 2;
constexpr uint8_t LOG_LEVEL_ERROR = 3;
constexpr uint8_t LOG_LEVEL_WARNING = 4;
constexpr uint8_t LOG_LEVEL_NOTICE = 5;
constexpr uint8_t LOG_LEVEL_INFO = 6;
constexpr uint8_t LOG_LEVEL_DEBUG = 7;
constexpr uint8_t LOG_LEVEL_TRACE = 8;
constexpr uint8_t LOG_LEVEL_TRACE2 = 9;
constexpr uint8_t LOG_LEVEL_TRACE3 = 10;

enum class LogLevel : uint8_t {
  kEmergency = LOG_LEVEL_EMERGENCY,
  kAlert = LOG_LEVEL_ALERT,
  kCritical = LOG_LEVEL_CRITICAL,
  kError = LOG_LEVEL_ERROR,
  kWarning = LOG_LEVEL_WARNING,
  kNotice = LOG_LEVEL_NOTICE,
  kInfo = LOG_LEVEL_INFO,
  kDebug = LOG_LEVEL_DEBUG,
#ifdef DEBUG
  kTrace = LOG_LEVEL_TRACE,
  kTrace2 = LOG_LEVEL_TRACE2,
  kTrace3 = LOG_LEVEL_TRACE3,
  LAST = LOG_LEVEL_TRACE3 + 1,
#else
  LAST = LOG_LEVEL_DEBUG + 1,
#endif
};

template<> struct is_comparable_enum<LogLevel> : std::true_type {};

enum class LogLevelExtra : uint8_t {
  kEmergency = LOG_LEVEL_EMERGENCY,
  kAlert = LOG_LEVEL_ALERT,
  kCritical = LOG_LEVEL_CRITICAL,
  kError = LOG_LEVEL_ERROR,
  kWarning = LOG_LEVEL_WARNING,
  kNotice = LOG_LEVEL_NOTICE,
  kInfo = LOG_LEVEL_INFO,
  kDebug = LOG_LEVEL_DEBUG,
  kTrace = LOG_LEVEL_TRACE,
  kTrace2 = LOG_LEVEL_TRACE2,
  kTrace3 = LOG_LEVEL_TRACE3,
  kExtra = LOG_LEVEL_TRACE3 + 1,
};

template<> struct is_comparable_enum<LogLevelExtra> : std::true_type {};

constexpr uint8_t ANTI_SPOOF_NONE = 0;
constexpr uint8_t ANTI_SPOOF_BASIC = 1;
constexpr uint8_t ANTI_SPOOF_EXTENDED = 2;
constexpr uint8_t ANTI_SPOOF_FULL = 3;

enum class AntiSpoofDepth : uint8_t {
  kNone = 0,
  kBasic = 1,
  kExtended = 2,
  kFull = 3,
  LAST = 4,
};

constexpr size_t MAX_READ_FILE_SIZE = 0x18000000;

constexpr double PERCENT_FACTOR = 100.;

constexpr unsigned char ProductID_ROC[4] = {51, 82, 65, 87};
constexpr unsigned char ProductID_TFT[4] = {80, 88, 51, 87};

// aura.h

enum class AppActionStatus : uint8_t {
  kDone = 0,
  kError = 1,
  kWait = 2,
  kTimeOut = 3,
  //LAST = 4,
};

enum class AppActionType : uint8_t {
  kHostActiveMirror = 0,
  kCommand = 1,
#ifndef DISABLE_MINIUPNP
  kUPnP = 2,
  //LAST = 3,
#endif
};

enum class AppActionMode : uint8_t {
  kNone = 0,
  kTCP = 1,
  kUDP = 2,
  //LAST = 3,
};

enum class OptionalDependencyMode : uint8_t
{
  kUnknown = 0u,
  kNotUseful = 1u,
  kOptEnhancement = 2u,
  kRequired = 3u,
  //LAST = 4,
};

enum class ServiceType : uint8_t
{
  kNone = 0u,
  kCLI = 1u,
  kLAN = 2u,
  kRealm = 3u,
  kIRC = 4u,
  kDiscord = 5u,
  kGame = 6u, // avoided in most places, in favor of kCLI/kLAN/kRealm
};

enum class GameCommandSource : uint8_t
{
  kNone = 0u,
  kUser = 1u,
  kSpectator = 2u,
  kReplay = 3u,
  LAST = 4,
};

enum class TaskType : uint8_t {
  kGameFrame = 0,
  kCheckJoinable = 1,
  kMapDownload = 2,
  kHMCHTTP = 3,
  kDBRead = 4,
  kDBWrite = 5,
  LAST = 6,
};

constexpr uint8_t LOG_C = 1u;
constexpr uint8_t LOG_P = 2u;
constexpr uint8_t LOG_R = 4u;
constexpr uint8_t LOG_ALL = LOG_C | LOG_P | LOG_R;

constexpr size_t MAX_GAME_NAME_SIZE = 31u;

constexpr uint8_t APP_FOUND_DEPS_NONE = 0u;
constexpr uint8_t APP_FOUND_DEPS_DPP = (1 << 0);
constexpr uint8_t APP_FOUND_DEPS_MDNS = (1 << 1);

// parser.h

constexpr uint8_t PARSER_BOOLEAN_EMPTY_USE_DEFAULT = 1u;
constexpr uint8_t PARSER_BOOLEAN_ALLOW_ENABLE = 2u;
constexpr uint8_t PARSER_BOOLEAN_ALLOW_TIME = 4u;
constexpr uint8_t PARSER_BOOLEAN_DEFAULT_FALSE = 8u;
constexpr uint8_t PARSER_BOOLEAN_ALLOW_ALL = PARSER_BOOLEAN_ALLOW_ENABLE | PARSER_BOOLEAN_ALLOW_TIME;

// fileutil.h

constexpr size_t FILE_SEARCH_FUZZY_MAX_RESULTS = 5;
constexpr std::string::size_type FILE_SEARCH_FUZZY_MAX_DISTANCE = 10;

// map.h

constexpr uint32_t MAX_MAP_SIZE_1_23 = 0x400000;
constexpr uint32_t MAX_MAP_SIZE_1_26 = 0x800000;
constexpr uint32_t MAX_MAP_SIZE_1_28 = 0x8000000;
constexpr uint32_t MAX_MAP_SIZE_RF = 0x20000000;

enum class W3ModLocale : uint8_t {
  kENUS = 0,
  kDEDE = 1,
  kESES = 2,
  kESMX = 3,
  kFRFR = 4,
  kITIT = 5,
  kKOKR = 6,
  kPLPL = 7,
  kPTBR = 8,
  kRURU = 9,
  kZHCN = 10,
  kZHTW = 11,
  LAST = 12,
};

enum class GameSpeed : uint8_t {
  kSlow = 0,
  kNormal = 1,
  kFast = 2,
  LAST = 3,
};

template<> struct is_comparable_enum<GameSpeed> : std::true_type {};

enum class GameVisibilityMode : uint8_t {
  kHideTerrain = 0,
  kExplored = 1,
  kAlwaysVisible = 2,
  kDefault = 3,
  LAST = 4,
};

enum class GameObserversMode : uint8_t {
  kNone = 0,
  kOnDefeat = 1,
  kStartOrOnDefeat = 2,
  kReferees = 3,
  LAST = 4,
};

constexpr uint8_t GAMEFLAG_TEAMSTOGETHER = 1u;
constexpr uint8_t GAMEFLAG_FIXEDTEAMS = 2u;
constexpr uint8_t GAMEFLAG_UNITSHARE = 4u;
constexpr uint8_t GAMEFLAG_RANDOMHERO = 8u;
constexpr uint8_t GAMEFLAG_RANDOMRACES = 16u;

constexpr uint32_t MAPOPT_HIDEMINIMAP = (1 << 0);
constexpr uint32_t MAPOPT_MODIFYALLYPRIORITIES = (1 << 1);
constexpr uint32_t MAPOPT_MELEE = (1 << 2); // the bot cares about this one...
constexpr uint32_t MAPOPT_REVEALTERRAIN = (1 << 4);
constexpr uint32_t MAPOPT_FIXEDPLAYERSETTINGS = (1 << 5); // and this one...
constexpr uint32_t MAPOPT_CUSTOMFORCES = (1 << 6);        // and this one, the rest don't affect the bot's logic
constexpr uint32_t MAPOPT_CUSTOMTECHTREE = (1 << 7);
constexpr uint32_t MAPOPT_CUSTOMABILITIES = (1 << 8);
constexpr uint32_t MAPOPT_CUSTOMUPGRADES = (1 << 9);
constexpr uint32_t MAPOPT_WATERWAVESONCLIFFSHORES = (1 << 11);
constexpr uint32_t MAPOPT_HASTERRAINFOG = (1 << 12);
constexpr uint32_t MAPOPT_REQUIRESEXPANSION = (1 << 13);
constexpr uint32_t MAPOPT_ITEMCLASSIFICATION = (1 << 14);
constexpr uint32_t MAPOPT_WATERTINTING = (1 << 15);
constexpr uint32_t MAPOPT_ACCURATERANDOM = (1 << 16);
constexpr uint32_t MAPOPT_ABILITYSKINS = (1 << 17);

constexpr uint8_t MAPFILTER_MAKER_USER = 1;
constexpr uint8_t MAPFILTER_MAKER_BLIZZARD = 2;

enum class MapFilterMaker : uint8_t {
  kUser = 1,
  kBlizzard = 2,
  LAST = 3,
};

constexpr uint8_t MAPFILTER_TYPE_MELEE = 1;
constexpr uint8_t MAPFILTER_TYPE_SCENARIO = 2;

enum class MapFilterType : uint8_t {
  kMelee = 1,
  kScenario = 2,
  LAST = 3,
};

constexpr uint8_t MAPFILTER_SIZE_SMALL = 1;
constexpr uint8_t MAPFILTER_SIZE_MEDIUM = 2;
constexpr uint8_t MAPFILTER_SIZE_LARGE = 4;

constexpr uint8_t MAPFILTER_OBS_FULL = 1;
constexpr uint8_t MAPFILTER_OBS_ONDEATH = 2;
constexpr uint8_t MAPFILTER_OBS_NONE = 4;

constexpr uint32_t MAPGAMETYPE_UNKNOWN0 = (1); // always set except for saved games?
constexpr uint32_t MAPGAMETYPE_BLIZZARD = (1 << 3);
constexpr uint32_t MAPGAMETYPE_MELEE = (1 << 5);
constexpr uint32_t MAPGAMETYPE_SAVEDGAME = (1 << 9);
constexpr uint32_t MAPGAMETYPE_PRIVATEGAME = (1 << 11);
constexpr uint32_t MAPGAMETYPE_MAKERUSER = (1 << 13);
constexpr uint32_t MAPGAMETYPE_MAKERBLIZZARD = (1 << 14);
constexpr uint32_t MAPGAMETYPE_TYPEMELEE = (1 << 15);
constexpr uint32_t MAPGAMETYPE_TYPESCENARIO = (1 << 16);
constexpr uint32_t MAPGAMETYPE_SIZESMALL = (1 << 17);
constexpr uint32_t MAPGAMETYPE_SIZEMEDIUM = (1 << 18);
constexpr uint32_t MAPGAMETYPE_SIZELARGE = (1 << 19);
constexpr uint32_t MAPGAMETYPE_OBSFULL = (1 << 20);
constexpr uint32_t MAPGAMETYPE_OBSONDEATH = (1 << 21);
constexpr uint32_t MAPGAMETYPE_OBSNONE = (1 << 22);

constexpr uint8_t MAPLAYOUT_ANY = 0u;
constexpr uint8_t MAPLAYOUT_CUSTOM_FORCES = 1u;
constexpr uint8_t MAPLAYOUT_FIXED_PLAYERS = 3u;

constexpr uint8_t MAP_DATASET_DEFAULT = 0u;
constexpr uint8_t MAP_DATASET_CUSTOM = 1u;
constexpr uint8_t MAP_DATASET_MELEE = 2u;

enum class MapDataset : uint8_t {
  kDefault = 0,
  kCustom = 1,
  kMelee = 2,
  LAST = 3,
};

constexpr uint8_t CACHE_REVALIDATION_NEVER = 0;
constexpr uint8_t CACHE_REVALIDATION_ALWAYS = 1u;
constexpr uint8_t CACHE_REVALIDATION_MODIFIED = 2u;

enum class CacheRevalidationMethod : uint8_t {
  kNever = 0,
  kAlways = 1,
  kModified = 2,
  LAST = 3,
};

constexpr uint8_t MAP_FEATURE_TOGGLE_DISABLED = 0u;
constexpr uint8_t MAP_FEATURE_TOGGLE_OPTIONAL = 1u;
constexpr uint8_t MAP_FEATURE_TOGGLE_REQUIRED = 2u;

enum class MapFeatureToggle : uint8_t {
  kDisabled = 0,
  kOptional = 1,
  kRequired = 2,
  LAST = 3,
};

// Load map fragments in memory max 8 MB at a time.
constexpr uint32_t MAP_FILE_MAX_CHUNK_SIZE = 0x800000;
// May also choose a chunk size different from the max cache chunk size.
constexpr uint32_t MAP_FILE_PROCESSING_CHUNK_SIZE = 0x800000;

constexpr uint8_t MAP_CONFIG_SCHEMA_NUMBER = 5;

constexpr uint8_t MAP_FILE_SOURCE_CATEGORY_NONE = 0u;
constexpr uint8_t MAP_FILE_SOURCE_CATEGORY_MPQ = 1u;
constexpr uint8_t MAP_FILE_SOURCE_CATEGORY_FS = 2u;

enum class MapFileSourceCategory : uint8_t {
  kNone = 0,
  kMPQ = 1,
  kFS = 2,
  LAST = 3,
};

constexpr uint8_t MAP_ALLOW_LUA_NEVER = 0u;
constexpr uint8_t MAP_ALLOW_LUA_ALWAYS = 1u;
constexpr uint8_t MAP_ALLOW_LUA_AUTO = 2u;

enum class MapAllowLuaMode : uint8_t {
  kNever = 0,
  kAlways = 1,
  kAuto = 2,
  LAST = 3,
};

constexpr const char* HCL_CHARSET_STANDARD = "abcdefghijklmnopqrstuvwxyz0123456789 -=,."; // 41 characters
constexpr const char* HCL_CHARSET_SMALL = "0123456789abcdef-\" \\"; // 20 characters

constexpr uint8_t MMD_TYPE_STANDARD = 0u;
constexpr uint8_t MMD_TYPE_DOTA = 1u;

enum class MMDType : uint8_t {
  kStandard = 0,
  kDota = 1,
  LAST = 2,
};

constexpr uint8_t AI_TYPE_NONE = 0u;
constexpr uint8_t AI_TYPE_MELEE = 1u;
constexpr uint8_t AI_TYPE_AMAI = 2u;
constexpr uint8_t AI_TYPE_CUSTOM = 3u;

enum class AIType : uint8_t {
  kNone = 0,
  kMelee = 1,
  kAmai = 2,
  kCustom = 3,
  LAST = 4,
};

// 142 UTF-16 chracters, including Latin, Cyrillic, digits, and punctuation
constexpr const wchar_t* AHCL_DEFAULT_CHARSET = L"\u0030\u0031\u0032\u0033\u0034\u0035\u0036\u0037\u0038\u0039\u0061\u0062\u0063\u0064\u0065\u0066\u0067\u0068\u0069\u006a\u006b\u006c\u006d\u006e\u006f\u0070\u0071\u0072\u0073\u0074\u0075\u0076\u0077\u0078\u0079\u007a\u0041\u0042\u0043\u0044\u0045\u0046\u0047\u0048\u0049\u004a\u004b\u004c\u004d\u004e\u004f\u0050\u0051\u0052\u0053\u0054\u0055\u0056\u0057\u0058\u0059\u005a\u0430\u0431\u0432\u0433\u0434\u0435\u0451\u0436\u0437\u0438\u0439\u043a\u043b\u043c\u043d\u043e\u043f\u0440\u0441\u0442\u0443\u0444\u0445\u0446\u0447\u0448\u0449\u044a\u044b\u044c\u044d\u044e\u044f\u0410\u0411\u0412\u0413\u0414\u0415\u0401\u0416\u0417\u0418\u0419\u041a\u041b\u041c\u041d\u041e\u041f\u0420\u0421\u0422\u0423\u0424\u0425\u0426\u0427\u0428\u0429\u042a\u042b\u042c\u042d\u042e\u042f\u0020\u002d\u003d\u002c\u002e\u0021\u003f\u002a\u005f\u002b\u003d\u007c\u003a\u003b";

constexpr uint8_t MAP_TRANSFER_NONE = 0u;
constexpr uint8_t MAP_TRANSFER_DONE = 1u;
constexpr uint8_t MAP_TRANSFER_IN_PROGRESS = 2u;
constexpr uint8_t MAP_TRANSFER_RATE_LIMITED = 3u;
constexpr uint8_t MAP_TRANSFER_MISSING = 4u;
constexpr uint8_t MAP_TRANSFER_INVALID = 5u;

enum class MapTransferStatus : uint8_t {
  kNone = 0,
  kDone = 1,
  kInProgress = 2,
  kRateLimited = 3,
  kMissing = 4,
  kInvalid = 5,
  LAST = 6,
};

constexpr uint8_t MAP_TRANSFER_CHECK_ALLOWED = 0u;
constexpr uint8_t MAP_TRANSFER_CHECK_INVALID = 1u;
constexpr uint8_t MAP_TRANSFER_CHECK_MISSING = 2u;
constexpr uint8_t MAP_TRANSFER_CHECK_DISABLED = 3u;
constexpr uint8_t MAP_TRANSFER_CHECK_TOO_LARGE_VERSION = 4u;
constexpr uint8_t MAP_TRANSFER_CHECK_TOO_LARGE_CONFIG = 5u;
constexpr uint8_t MAP_TRANSFER_CHECK_BUFFERBLOAT = 6u;

enum class MapTransferCheckResult : uint8_t {
  kAllowed = 0,
  kInvalid = 1,
  kMissing = 2,
  kDisabled = 3,
  kTooLargeVersion = 4,
  kTooLargeConfig = 5,
  kBufferBloat = 6,
  LAST = 7,
};

// game.h
constexpr uint8_t SLOTS_UNCHANGED = 0;
constexpr uint8_t SLOTS_ALIGNMENT_CHANGED = (1 << 0);
constexpr uint8_t SLOTS_DOWNLOAD_PROGRESS_CHANGED = (1 << 1);
constexpr uint8_t SLOTS_HCL_INJECTED = (1 << 2);

constexpr uint8_t SAVE_ON_LEAVE_NEVER = 0u;
constexpr uint8_t SAVE_ON_LEAVE_AUTO = 1u;
constexpr uint8_t SAVE_ON_LEAVE_ALWAYS = 2u;

enum class SaveOnLeaveMode : uint8_t {
  kNever = 0,
  kAuto = 1,
  kAlways = 2,
  LAST = 3,
};

constexpr uint8_t CUSTOM_LAYOUT_NONE = 0u;
constexpr uint8_t CUSTOM_LAYOUT_ONE_VS_ALL = 1u;
constexpr uint8_t CUSTOM_LAYOUT_HUMANS_VS_AI = 2u;
constexpr uint8_t CUSTOM_LAYOUT_FFA = 4u;
constexpr uint8_t CUSTOM_LAYOUT_DRAFT = 8u;
constexpr uint8_t CUSTOM_LAYOUT_LOCKTEAMS = 15u;
constexpr uint8_t CUSTOM_LAYOUT_COMPACT = 16u;
constexpr uint8_t CUSTOM_LAYOUT_ISOPLAYERS = 32u;

constexpr uint16_t REFRESH_PERIOD_MIN_SUGGESTED = 30;
constexpr uint16_t REFRESH_PERIOD_MAX_SUGGESTED = 300;
constexpr uint8_t GAME_BANNABLE_MAX_HISTORY_SIZE = 32;

constexpr size_t MAX_GAME_VERSION_ERROR_USERS_STORED = 500;

constexpr uint8_t GAME_PAUSES_PER_PLAYER = 3u;
constexpr uint8_t GAME_SAVES_PER_PLAYER = 1u;
constexpr uint8_t GAME_SAVES_PER_REFEREE_ANTIABUSE = 3u;
constexpr uint8_t GAME_SAVES_PER_REFEREE_DEFAULT = 255u;

constexpr uint8_t GAME_DISCOVERY_INTERFACE_NONE = 0u;
constexpr uint8_t GAME_DISCOVERY_INTERFACE_LOOPBACK = 1u;
constexpr uint8_t GAME_DISCOVERY_INTERFACE_IPV4 = 2u;
constexpr uint8_t GAME_DISCOVERY_INTERFACE_IPV6 = 3u;

enum class GameDiscoveryInterfaceType : uint8_t {
  kNone = 0,
  kLoopback = 1,
  kIPv4 = 2,
  kIPv6 = 3,
  LAST = 4,
};

constexpr uint8_t GAME_ONGOING = 0u;
constexpr uint8_t GAME_OVER_TRUSTED = 1u;
constexpr uint8_t GAME_OVER_MMD = 2u;

enum class GameOverStatus : uint8_t {
  kOngoing = 0,
  kTrusted = 1,
  kMMD = 2,
  LAST = 3,
};

constexpr uint8_t HIDDEN_PLAYERS_NONE = 0u;
constexpr uint8_t HIDDEN_PLAYERS_LOBBY = 1 << 0;
constexpr uint8_t HIDDEN_PLAYERS_GAME = 1 << 1;
constexpr uint8_t HIDDEN_PLAYERS_ALL = HIDDEN_PLAYERS_LOBBY | HIDDEN_PLAYERS_GAME;

enum class HiddenPlayersMode : uint8_t {
  kNone = 0,
  kLobby = 1,
  kGame = 2,
  kAll = 3,
  LAST = 4,
};

constexpr int64_t HIGH_PING_KICK_DELAY = 10000;
constexpr uint8_t MAX_SCOPE_BANS = 100u;
constexpr int64_t REALM_HOST_COOLDOWN_TICKS = 30000;
constexpr int64_t AUTO_REHOST_COOLDOWN_TICKS = 180000;

constexpr uint8_t ON_SEND_ACTIONS_NONE = 0u;
constexpr uint8_t ON_SEND_ACTIONS_PAUSE = 1u;
constexpr uint8_t ON_SEND_ACTIONS_RESUME = 2u;

enum class OnSendActions : uint8_t {
  kNone = 0,
  kPause = 1,
  kResume = 2,
  LAST = 3,
};

constexpr int64_t PING_EQUALIZER_PERIOD_TICKS = 10000;
constexpr uint8_t PING_EQUALIZER_DEFAULT_FRAMES = 7u;

// 10 players x 150 APM x (1 min / 60000 ms) x (100 ms latency) = 2.5
constexpr size_t DEFAULT_ACTIONS_PER_FRAME = 3u;

constexpr uint8_t SYNCHRONIZATION_CHECK_MIN_FRAMES = 5u;

constexpr uint8_t BUFFERING_ENABLED_NONE = 0u;
constexpr uint8_t BUFFERING_ENABLED_PLAYING = 1u;
constexpr uint8_t BUFFERING_ENABLED_LOADING = 2u;
constexpr uint8_t BUFFERING_ENABLED_SLOTS = 4u;
constexpr uint8_t BUFFERING_ENABLED_LOBBY = 8u;
constexpr uint8_t BUFFERING_ENABLED_ALL = 15u;

constexpr uint8_t GAME_FRAME_TYPE_ACTIONS = 0u;
constexpr uint8_t GAME_FRAME_TYPE_PAUSED = 1u;
constexpr uint8_t GAME_FRAME_TYPE_LEAVER = 2u;
constexpr uint8_t GAME_FRAME_TYPE_CHAT = 3u;
constexpr uint8_t GAME_FRAME_TYPE_LATENCY = 4u;
constexpr uint8_t GAME_FRAME_TYPE_GPROXY = 5u;

enum class GameFrameType : uint8_t {
  kActions = 0,
  kPaused = 1,
  kLeaver = 2,
  kChat = 3,
  kLatency = 4,
  kGProxy = 5,
  LAST = 6,
};

constexpr uint8_t GAME_DISCOVERY_CHANGED_NONE = 0u;
constexpr uint8_t GAME_DISCOVERY_CHANGED_SLOTS = 1u;
constexpr uint8_t GAME_DISCOVERY_CHANGED_STAT = 2u;
constexpr uint8_t GAME_DISCOVERY_CHANGED_NET = 4u;
constexpr uint8_t GAME_DISCOVERY_CHANGED_MAJOR = GAME_DISCOVERY_CHANGED_STAT | GAME_DISCOVERY_CHANGED_NET;
constexpr uint8_t GAME_DISCOVERY_CHANGED_NEW = GAME_DISCOVERY_CHANGED_SLOTS | GAME_DISCOVERY_CHANGED_MAJOR;

constexpr uint8_t GAME_RESULT_SOURCE_NONE = 0u;
constexpr uint8_t GAME_RESULT_SOURCE_LEAVECODE = 1u;
constexpr uint8_t GAME_RESULT_SOURCE_MMD = 2u;

enum class GameResultSource : uint8_t {
  kNone = 0,
  kLeaveCode = 1,
  kMMD = 2,
  LAST = 3,
};

constexpr uint8_t GAME_RESULT_SOURCE_SELECT_NONE = 0u;
constexpr uint8_t GAME_RESULT_SOURCE_SELECT_ONLY_LEAVECODE = 1u;
constexpr uint8_t GAME_RESULT_SOURCE_SELECT_ONLY_MMD = 2u;
constexpr uint8_t GAME_RESULT_SOURCE_SELECT_PREFER_LEAVECODE = 3u;
constexpr uint8_t GAME_RESULT_SOURCE_SELECT_PREFER_MMD = 4u;

enum class GameResultSourceSelect : uint8_t {
  kNone = 0,
  kOnlyLeaveCode = 1,
  kOnlyMMD = 2,
  kPreferLeaveCode = 3,
  kPreferMMD = 4,
  LAST = 5,
};

constexpr uint8_t GAME_RESULT_CONFLICT_HANDLER_VOID = 0u; // vulnerable to griefing
constexpr uint8_t GAME_RESULT_CONFLICT_HANDLER_PESSIMISTIC = 1u; // protects against self-boosting, but vulnerable to griefing
constexpr uint8_t GAME_RESULT_CONFLICT_HANDLER_OPTIMISTIC = 2u; // protects against griefing; together with max winner teams = 1, self-boosting results in void

// majority handlers assume that democracy is good
// but cheaters may complot, and ties may happen
// ties are resolved with the _OR_SOMETHING part
constexpr uint8_t GAME_RESULT_CONFLICT_HANDLER_MAJORITY_OR_VOID = 3u;
constexpr uint8_t GAME_RESULT_CONFLICT_HANDLER_MAJORITY_OR_PESSIMISTIC = 4u;
constexpr uint8_t GAME_RESULT_CONFLICT_HANDLER_MAJORITY_OR_OPTIMISTIC = 5u;

enum class GameResultConflictHandler : uint8_t {
  kVoid = 0,
  kPessimistic = 1,
  kOptimistic = 2,
  kMajorityOrVoid = 3,
  kMajorityOrPessimistic = 4,
  kMajorityOrOptimistic = 5,
  LAST = 6,
};

constexpr uint8_t GAME_RESULT_VIRTUAL_UNDECIDED_HANDLER_NONE = 0u;
constexpr uint8_t GAME_RESULT_VIRTUAL_UNDECIDED_HANDLER_LOSER_SELF = 1u;
constexpr uint8_t GAME_RESULT_VIRTUAL_UNDECIDED_HANDLER_AUTO = 2u;

enum class GameResultVirtualUndecidedHandler : uint8_t {
  kNone = 0,
  kLoserSelf = 1,
  kAuto = 2,
  LAST = 3,
};

// Note: We ignore "leaver" flag and just treat them as undecided
constexpr uint8_t GAME_RESULT_USER_UNDECIDED_HANDLER_NONE = 0u;
constexpr uint8_t GAME_RESULT_USER_UNDECIDED_HANDLER_LOSER_SELF = 1u;
constexpr uint8_t GAME_RESULT_USER_UNDECIDED_HANDLER_LOSER_SELF_AND_ALLIES = 2u; // causes allied computers/virtual users to lose

enum class GameResultUserUndecidedHandler : uint8_t {
  kNone = 0,
  kLoserSelf = 1,
  kLoserSelfAndAllies = 2,
  LAST = 3,
};

constexpr uint8_t GAME_RESULT_COMPUTER_UNDECIDED_HANDLER_NONE = 0u;
constexpr uint8_t GAME_RESULT_COMPUTER_UNDECIDED_HANDLER_LOSER_SELF = 1u;
constexpr uint8_t GAME_RESULT_COMPUTER_UNDECIDED_HANDLER_AUTO = 2u; // can win!

enum class GameResultComputerUndecidedHandler : uint8_t {
  kNone = 0,
  kLoserSelf = 1,
  kAuto = 2,
  LAST = 3,
};

constexpr uint8_t GAME_RESULT_LOSER = 0u;
constexpr uint8_t GAME_RESULT_DRAWER = 1u;
constexpr uint8_t GAME_RESULT_WINNER = 2u;
constexpr uint8_t GAME_RESULT_UNDECIDED = 3u;

enum class GamePlayerResult : uint8_t {
  kLoser = 0,
  kDrawer = 1,
  kWinner = 2,
  kUndecided = 3,
  LAST = 4,
};

constexpr uint8_t ACTION_SOURCE_NONE = 0u;
constexpr uint8_t ACTION_SOURCE_PLAYER = 1u;
constexpr uint8_t ACTION_SOURCE_OBSERVER = 2u;
constexpr uint8_t ACTION_SOURCE_REFEREE = 4u;
constexpr uint8_t ACTION_SOURCE_OBSERVER_ANY = ACTION_SOURCE_OBSERVER | ACTION_SOURCE_REFEREE;
constexpr uint8_t ACTION_SOURCE_ANY = ACTION_SOURCE_PLAYER | ACTION_SOURCE_OBSERVER_ANY;

// game_slot.h

constexpr uint8_t UID_ZERO = 0;

constexpr uint8_t SLOTSTATUS_OPEN = 0u;
constexpr uint8_t SLOTSTATUS_CLOSED = 1u;
constexpr uint8_t SLOTSTATUS_OCCUPIED = 2u;
constexpr uint8_t SLOTSTATUS_VALID = 3u;
constexpr uint8_t SLOTSTATUS_VALID_INITIAL_NON_COMPUTER = 1u;

constexpr uint8_t SLOTRACE_HUMAN = 1u;
constexpr uint8_t SLOTRACE_ORC = 2u;
constexpr uint8_t SLOTRACE_NIGHTELF = 4u;
constexpr uint8_t SLOTRACE_UNDEAD = 8u;
constexpr uint8_t SLOTRACE_RANDOM = 32u;
constexpr uint8_t SLOTRACE_SELECTABLE = 64u;
constexpr uint8_t SLOTRACE_PICKRANDOM = 128u;
constexpr uint8_t SLOTRACE_INVALID = 255u;

constexpr uint8_t SLOTCOMP_EASY = 0u;
constexpr uint8_t SLOTCOMP_NORMAL = 1u;
constexpr uint8_t SLOTCOMP_HARD = 2u;
constexpr uint8_t SLOTCOMP_VALID = 3u;
constexpr uint8_t SLOTCOMP_INVALID = 255u;

constexpr uint8_t SLOTCOMP_NO = 0u;
constexpr uint8_t SLOTCOMP_YES = 1u;

constexpr uint8_t SLOTTYPE_NONE = 0u;
constexpr uint8_t SLOTTYPE_USER = 1u;
constexpr uint8_t SLOTTYPE_COMP = 2u;
constexpr uint8_t SLOTTYPE_NEUTRAL = 3u;
constexpr uint8_t SLOTTYPE_RESCUEABLE = 4u;
constexpr uint8_t SLOTTYPE_AUTO = 255u;

constexpr uint8_t SLOTPROG_NEW = 0u;
constexpr uint8_t SLOTPROG_RDY = 100u;
constexpr uint8_t SLOTPROG_RST = 255u;

constexpr unsigned int MAX_SLOTS_MODERN = 24;
constexpr unsigned int MAX_SLOTS_LEGACY = 12;

// game_controller_data.h

enum class GameControllerType : uint8_t
{
  kVirtual = 0u,
  kUser = 1u,
  kComputer = 2u,
  LAST = 3,
};

// connection.h

constexpr uint8_t JOIN_RESULT_FAIL = 0u;
constexpr uint8_t JOIN_RESULT_PLAYER = 1u;
constexpr uint8_t JOIN_RESULT_OBSERVER = 2u;

enum class JoinResult : uint8_t {
  kFail = 0,
  kPlayer = 1,
  kObserver = 2,
  LAST = 3,
};

// game_user.h

constexpr uint8_t CONSISTENT_PINGS_COUNT = 3u;
constexpr size_t MAXIMUM_PINGS_COUNT = 6u;
constexpr uint32_t MAX_PING_WEIGHT = 4u;

constexpr uint8_t SMART_COMMAND_NONE = 0u;
constexpr uint8_t SMART_COMMAND_GO = 1u;

enum class SmartCommand : uint8_t {
  kNone = 0,
  kGo = 1,
  LAST = 2,
};

constexpr int64_t GAME_USER_UNVERIFIED_KICK_TICKS = 60000;
constexpr int64_t AUTO_REALM_VERIFY_LATENCY = 5000;
constexpr int64_t CHECK_STATUS_LATENCY = 5000;
constexpr int64_t READY_REMINDER_PERIOD = 20000;

constexpr int64_t SYSTEM_RTT_POLLING_PERIOD = 10000;

constexpr uint8_t USERSTATUS_LOBBY = 0u;
constexpr uint8_t USERSTATUS_LOADING_SCREEN = 1u;
constexpr uint8_t USERSTATUS_PLAYING = 2u;
constexpr uint8_t USERSTATUS_ENDING = 3u;
constexpr uint8_t USERSTATUS_ENDED = 4u;

enum class UserStatus : uint8_t {
  kLobby = 0,
  kLoadingScreen = 1,
  kPlaying = 2,
  kEnding = 3,
  kEnded = 4,
  LAST = 5,
};

enum class GProxyExtendedClientResult : uint8_t {
  kInvalid = 0,
  kAlready = 1,
  kNormal = 2,
  kCheckGameID = 3,
};

// game_async_observer.h

enum class AsyncObserverGoal : uint8_t {
  kSpectator = 0,
  kPlayer = 1,
  LAST = 2,
};

constexpr uint8_t ASYNC_OBSERVER_OK = 0u;
constexpr uint8_t ASYNC_OBSERVER_DESTROY = 1u;
constexpr uint8_t ASYNC_OBSERVER_PROMOTED = 2u;

constexpr size_t MAXIMUM_TIMESTAMPS_COUNT = 20;
constexpr int TIMESTAMPS_SAMPLE_RATE = 10;

// game_setup.h

constexpr uint8_t SEARCH_TYPE_ONLY_MAP = 1;
constexpr uint8_t SEARCH_TYPE_ONLY_CONFIG = 2;
constexpr uint8_t SEARCH_TYPE_ONLY_FILE = SEARCH_TYPE_ONLY_MAP | SEARCH_TYPE_ONLY_CONFIG;
constexpr uint8_t SEARCH_TYPE_NETWORK = 4;
constexpr uint8_t SEARCH_TYPE_ANY = SEARCH_TYPE_ONLY_FILE | SEARCH_TYPE_NETWORK;

constexpr uint8_t MATCH_TYPE_NONE = 0u;
constexpr uint8_t MATCH_TYPE_MAP = 1u;
constexpr uint8_t MATCH_TYPE_CONFIG = 2u;
constexpr uint8_t MATCH_TYPE_INVALID = 4u;
constexpr uint8_t MATCH_TYPE_FORBIDDEN = 8u;

constexpr uint8_t RESOLUTION_OK = 0;
constexpr uint8_t RESOLUTION_ERR = 1;
constexpr uint8_t RESOLUTION_BAD_NAME = 2;

enum class MapResolutionStatus : uint8_t {
  kOk = 0,
  kErr = 1,
  kBadName = 2,
  LAST = 3,
};

constexpr bool SETUP_USE_STANDARD_PATHS = true;
constexpr bool SETUP_PROTECT_ARBITRARY_TRAVERSAL = false;

#ifdef _WIN32
#define FILE_EXTENSIONS_MAP {L".w3x", L".w3m"}
#define FILE_EXTENSIONS_CONFIG {L".ini"}
#else
#define FILE_EXTENSIONS_MAP {".w3x", ".w3m"}
#define FILE_EXTENSIONS_CONFIG {".ini"}
#endif

constexpr uint8_t GAMESETUP_STEP_MAIN = 0;
constexpr uint8_t GAMESETUP_STEP_RESOLUTION = 1;
constexpr uint8_t GAMESETUP_STEP_SUGGESTIONS = 2;
constexpr uint8_t GAMESETUP_STEP_DOWNLOAD = 3;

enum class GameSetupStep : uint8_t {
  kMain = 0,
  kResolution = 1,
  kSuggestions = 2,
  kDownload = 3,
  LAST = 4,
};

constexpr uint8_t MAP_ONREADY_SET_ACTIVE = 1;
constexpr uint8_t MAP_ONREADY_HOST = 2;
constexpr uint8_t MAP_ONREADY_ALIAS = 3;

enum class MapOnready : uint8_t {
  kSetActive = 1,
  kHost = 2,
  kAlias = 3,
  LAST = 4,
};

constexpr int64_t SUGGESTIONS_TIMEOUT = 3000;
constexpr int64_t GAMESETUP_STALE_TICKS = 180000;

// game_protocol.h

constexpr int W3GS_UDP_MIN_PACKET_SIZE = 4;
constexpr size_t W3GS_ACTION_MAX_PACKET_SIZE = 1024;

constexpr uint8_t GAME_DISPLAY_NONE = 0; // this case isn't part of the protocol, it's for internal use only
constexpr uint8_t GAME_DISPLAY_FULL = 2;
constexpr uint8_t GAME_DISPLAY_PUBLIC = 16;
constexpr uint8_t GAME_DISPLAY_PRIVATE = 17;

constexpr uint8_t PLAYERLEAVE_DISCONNECT = 1u;
constexpr uint8_t PLAYERLEAVE_LOST = 7u;
constexpr uint8_t PLAYERLEAVE_LOSTBUILDINGS = 8u;
constexpr uint8_t PLAYERLEAVE_WON = 9u;
constexpr uint8_t PLAYERLEAVE_DRAW = 10u;
constexpr uint8_t PLAYERLEAVE_OBSERVER = 11u;
constexpr uint8_t PLAYERLEAVE_BADSAVE = 12u;
constexpr uint8_t PLAYERLEAVE_LOBBY = 13u;
constexpr uint8_t PLAYERLEAVE_GPROXY = 100u;

constexpr uint8_t REJECTJOIN_FULL = 9;
constexpr uint8_t REJECTJOIN_STARTED = 10;
constexpr uint8_t REJECTJOIN_WRONGPASSWORD = 27;

constexpr uint8_t ACTION_PAUSE = 0x01;
constexpr uint8_t ACTION_RESUME = 0x02;
constexpr uint8_t ACTION_SAVE = 0x06;
constexpr uint8_t ACTION_SAVE_ENDED = 0x07;
constexpr uint8_t ACTION_ABILITY_TARGET_NONE = 0x10;
constexpr uint8_t ACTION_ABILITY_TARGET_POINT = 0x11;
constexpr uint8_t ACTION_ABILITY_TARGET_OBJECT = 0x12;
constexpr uint8_t ACTION_GIVE_OR_DROP_ITEM = 0x13;
constexpr uint8_t ACTION_ABILITY_TARGET_FOG = 0x14;
constexpr uint8_t ACTION_ABILITY_TARGET_FOG_HANDLE = 0x15;
constexpr uint8_t ACTION_SELECTION = 0x16;
constexpr uint8_t ACTION_GROUP_HOTKEY_ASSIGN = 0x17;
constexpr uint8_t ACTION_GROUP_HOTKEY_SELECT = 0x18;
constexpr uint8_t ACTION_SELECTION_SUBGROUP = 0x19;
constexpr uint8_t ACTION_SELECTION_REFRESH_SUBGROUP = 0x1A;
constexpr uint8_t ACTION_SELECTION_EVENT = 0x1B;
constexpr uint8_t ACTION_SELECTION_GROUND_ITEM = 0x1C;
constexpr uint8_t ACTION_CANCEL_REVIVAL = 0x1D;
constexpr uint8_t ACTION_CANCEL_TRAINING = 0x1E;
constexpr uint8_t ACTION_UNKNOWN_0x21 = 0x21;
constexpr uint8_t ACTION_ALLIANCE_SETTINGS = 0x50;
constexpr uint8_t ACTION_TRANSFER_RESOURCES = 0x51;
constexpr uint8_t ACTION_CHAT_TRIGGER = 0x60;
constexpr uint8_t ACTION_ESCAPE = 0x61;
constexpr uint8_t ACTION_SCENARIO_TRIGGER = 0x62;
constexpr uint8_t ACTION_SCENARIO_TRIGGER_SYNC_READY = 0x63;
constexpr uint8_t ACTION_TRACKABLE_HIT = 0x64;
constexpr uint8_t ACTION_TRACKABLE_TRACK = 0x65;
constexpr uint8_t ACTION_SKILLTREE = 0x66;
constexpr uint8_t ACTION_BUILDMENU = 0x67;
constexpr uint8_t ACTION_MINIMAPSIGNAL = 0x68;
constexpr uint8_t ACTION_MODAL_BTN_CLICK = 0x69;
constexpr uint8_t ACTION_MODAL_BTN = 0x6A;
constexpr uint8_t ACTION_GAME_CACHE_INT = 0x6B;
constexpr uint8_t ACTION_GAME_CACHE_REAL = 0x6C;
constexpr uint8_t ACTION_GAME_CACHE_BOOL = 0x6D;
constexpr uint8_t ACTION_GAME_CACHE_UNIT = 0x6E; // pending
constexpr uint8_t ACTION_GAME_CACHE_STRING = 0x6F;
constexpr uint8_t ACTION_GAME_CACHE_CLEAR_INT = 0x70;
constexpr uint8_t ACTION_GAME_CACHE_CLEAR_REAL = 0x71;
constexpr uint8_t ACTION_GAME_CACHE_CLEAR_BOOL = 0x72;
constexpr uint8_t ACTION_GAME_CACHE_CLEAR_UNIT = 0x73;
constexpr uint8_t ACTION_GAME_CACHE_CLEAR_STRING = 0x74;
constexpr uint8_t ACTION_ARROW_KEY = 0x75;
constexpr uint8_t ACTION_MOUSE = 0x76;
constexpr uint8_t ACTION_W3API = 0x77;
constexpr uint8_t ACTION_SYNCHRONIZE = 0x78;
constexpr uint8_t ACTION_FRAME_EVENT = 0x79;
constexpr uint8_t ACTION_KEY_EVENT = 0x7A;
constexpr uint8_t ACTION_CLICK = 0x7B;
constexpr uint8_t ACTION_REPLAY_FIRST = 0x80; // pending
constexpr uint8_t ACTION_REPLAY_COMPRESSED = 0x81; // pending
constexpr uint8_t ACTION_REPLAY_RAW = 0x82; // pending
constexpr uint8_t ACTION_REPLAY_DONE = 0x83; // pending
constexpr uint8_t ACTION_REPLAY_FASTER = 0x84; // pending
constexpr uint8_t ACTION_REPLAY_SLOWER = 0x85; // pending
constexpr uint8_t ACTION_REPLAY_LAST = 0x86; // pending
constexpr uint8_t ACTION_UNKNOWN_0x94 = 0x94;
constexpr uint8_t ACTION_LAST = 0xA2; // pending

constexpr uint8_t ACTION_SELECTION_MODE_ADD = 1;
constexpr uint8_t ACTION_SELECTION_MODE_REMOVE = 2;

constexpr uint8_t JN_ALLIANCE_SETTINGS_SYNC_DATA = 0xF0;
constexpr uint8_t MH_DOTA_SETTINGS_SYNC_DATA = 0xFF;

constexpr uint32_t ALLIANCE_SETTINGS_ALLY = 0x1Fu;
constexpr uint32_t ALLIANCE_SETTINGS_SHARED_VISION = 0x20u;
constexpr uint32_t ALLIANCE_SETTINGS_SHARED_CONTROL = 0x40u;
constexpr uint32_t ALLIANCE_SETTINGS_SHARED_CONTROL_FAMILY = ALLIANCE_SETTINGS_ALLY | ALLIANCE_SETTINGS_SHARED_VISION | ALLIANCE_SETTINGS_SHARED_CONTROL;
constexpr uint32_t ALLIANCE_SETTINGS_SHARED_VICTORY = 0x400u;

constexpr size_t MAX_PLAYER_NAME_SIZE = 15;

constexpr uint8_t CHAT_RECV_ALL = 0;
constexpr uint8_t CHAT_RECV_ALLY = 1;
constexpr uint8_t CHAT_RECV_OBS = 2;

enum class ChatRecv : uint8_t {
  kAll = 0,
  kAlly = 1,
  kObs = 2,
  LAST = 3,
};

constexpr std::string::size_type MAX_LOBBY_CHAT_SIZE = 220;
constexpr std::string::size_type MAX_IN_GAME_CHAT_SIZE = 120;

// game_virtual_user.h

constexpr uint8_t VIRTUAL_USER_ALLOW_ACTIONS_NONE = 0u;
constexpr uint8_t VIRTUAL_USER_ALLOW_ACTIONS_PAUSE = 1u;
constexpr uint8_t VIRTUAL_USER_ALLOW_ACTIONS_RESUME = 2u;
constexpr uint8_t VIRTUAL_USER_ALLOW_ACTIONS_SAVE = 4u;
constexpr uint8_t VIRTUAL_USER_ALLOW_ACTIONS_SHARE_UNITS = 8u;
constexpr uint8_t VIRTUAL_USER_ALLOW_ACTIONS_TRADE = 16u;
constexpr uint8_t VIRTUAL_USER_ALLOW_ACTIONS_MINIMAP_SIGNAL = 32u;
constexpr uint8_t VIRTUAL_USER_ALLOW_ACTIONS_CHAT_TRIGGER = 64u;
constexpr uint8_t VIRTUAL_USER_ALLOW_ACTIONS_SYNC_INTEGER = 128u;
constexpr uint8_t VIRTUAL_USER_ALLOW_ACTIONS_ANY = 255u;

constexpr uint8_t VIRTUAL_USER_ALLOW_CONNECTIONS_NONE = 0u;
constexpr uint8_t VIRTUAL_USER_ALLOW_CONNECTIONS_OBSERVER = 1u;
constexpr uint8_t VIRTUAL_USER_ALLOW_CONNECTIONS_PLAYER = 2u;

enum class VirtualUserAllowConnections : uint8_t {
  kNone = 0,
  kObserver = 1,
  kPlayer = 2,
  LAST = 3,
};

constexpr uint8_t VIRTUAL_USER_ALLOW_CONNECTIONS_ANY = VIRTUAL_USER_ALLOW_CONNECTIONS_OBSERVER | VIRTUAL_USER_ALLOW_CONNECTIONS_PLAYER;

// game_interactive_host.h

constexpr uint8_t GAME_INTERACTION_STATUS_PENDING = 0u;
constexpr uint8_t GAME_INTERACTION_STATUS_RUNNING = 1u;
constexpr uint8_t GAME_INTERACTION_STATUS_DONE = 2u;

enum class GameInteractionStatus : uint8_t {
  kPending = 0,
  kRunning = 1,
  kDone = 2,
  LAST = 3,
};

constexpr uint8_t W3HMC_REQUEST_INIT = 1;
constexpr uint8_t W3HMC_REQUEST_HTTP = 2;
constexpr uint8_t W3HMC_REQUEST_PLAYERREALM = 3;
constexpr uint8_t W3HMC_REQUEST_DATETIME = 4;

enum class W3HMCRequest : uint8_t {
  kInit = 1,
  kHTTP = 2,
  kPlayerRealm = 3,
  kDateTime = 4,
  LAST = 5,
};

constexpr uint8_t W3HMC_PROCEDURE_SET_ARGS = 1;
constexpr uint8_t W3HMC_PROCEDURE_EXEC = 2;

enum class W3HMCProcedure : uint8_t {
  kSetArgs = 1,
  kExec = 2,
  LAST = 3,
};

constexpr uint8_t W3HMC_ARG_CURL_URL = 1;
constexpr uint8_t W3HMC_ARG_CURL_POST = 2;
constexpr uint8_t W3HMC_ARG_CURL_NOREPLY = 3;
constexpr uint8_t W3HMC_ARG_CURL_APPENDSECRET = 4;
constexpr uint8_t W3HMC_ARG_CURL_PARAMETERS = 5;
constexpr uint8_t W3HMC_ARG_CURL_APPENDREALM = 6;
constexpr uint8_t W3HMC_ARG_CURL_APPENDNAME = 7;
constexpr uint8_t W3HMC_ARG_CURL_ADDHEADER = 8;
constexpr uint8_t W3HMC_ARG_CURL_FOLLOWLOC = 9;

enum class W3HMCArgCurl : uint8_t {
  kUrl = 1,
  kPost = 2,
  kNoReply = 3,
  kAppendSecret = 4,
  kParameters = 5,
  kAppendRealm = 6,
  kAppendName = 7,
  kAddHeader = 8,
  kFollowLoc = 9,
  LAST = 10,
};

// gps_protocol.h

constexpr uint32_t REJECTGPS_INVALID = 1;
constexpr uint32_t REJECTGPS_NOTFOUND = 2;
constexpr int64_t GPS_ACK_PERIOD = 10000u;

// chat.h

constexpr uint8_t CHAT_SEND_SOURCE_ALL = (1 << 0);
constexpr uint8_t CHAT_SEND_TARGET_ALL = (1 << 1);
constexpr uint8_t CHAT_LOG_INCIDENT = (1 << 2);
constexpr uint8_t CHAT_TYPE_INFO = (1 << 3);
constexpr uint8_t CHAT_TYPE_DONE = (1 << 4);
constexpr uint8_t CHAT_TYPE_ERROR = (1 << 5);
constexpr uint8_t CHAT_WRITE_TARGETS = (CHAT_SEND_SOURCE_ALL | CHAT_SEND_TARGET_ALL | CHAT_LOG_INCIDENT);
constexpr uint8_t CHAT_RESPONSE_TYPES = (CHAT_TYPE_INFO | CHAT_TYPE_DONE | CHAT_TYPE_ERROR);

constexpr uint16_t USER_PERMISSIONS_NONE = 0;
constexpr uint16_t USER_PERMISSIONS_GAME_PLAYER = (1 << 0);
constexpr uint16_t USER_PERMISSIONS_GAME_OWNER = (1 << 1);
constexpr uint16_t USER_PERMISSIONS_CHANNEL_VERIFIED = (1 << 2);
constexpr uint16_t USER_PERMISSIONS_CHANNEL_ADMIN = (1 << 3);
constexpr uint16_t USER_PERMISSIONS_CHANNEL_ROOTADMIN = (1 << 4);
constexpr uint16_t USER_PERMISSIONS_BOT_SUDO_SPOOFABLE = (1 << 6);
constexpr uint16_t USER_PERMISSIONS_BOT_SUDO_OK = (1 << 7);

constexpr uint16_t SET_USER_PERMISSIONS_ALL = (0xFFFF);

constexpr uint8_t COMMAND_TOKEN_MATCH_NONE = 0;
constexpr uint8_t COMMAND_TOKEN_MATCH_PRIVATE = 1;
constexpr uint8_t COMMAND_TOKEN_MATCH_BROADCAST = 2;

enum class CommandTokenMatch : uint8_t {
  kNone = 0,
  kPrivate = 1,
  kBroadcast = 2,
  LAST = 3,
};

// config_bot.h
constexpr uint8_t LOG_GAME_CHAT_NEVER = 0u;
constexpr uint8_t LOG_GAME_CHAT_ALLOWED = 1u;
constexpr uint8_t LOG_GAME_CHAT_ALWAYS = 2u;

enum class LogGameChat : uint8_t {
  kNever = 0,
  kAllowed = 1,
  kAlways = 2,
  LAST = 3,
};

constexpr uint8_t LOG_REMOTE_MODE_NONE = 0u;
constexpr uint8_t LOG_REMOTE_MODE_FILE = 1u;
constexpr uint8_t LOG_REMOTE_MODE_NETWORK = 2u;
constexpr uint8_t LOG_REMOTE_MODE_MIXED = 3u;

enum class LogRemoteMode : uint8_t {
  kNone = 0,
  kFile = 1,
  kNetwork = 2,
  kMixed = 3,
  LAST = 4,
};

// config_realm.h

constexpr uint8_t REALM_TYPE_PVPGN = 0u;
constexpr uint8_t REALM_TYPE_BATTLENET_CLASSIC = 1u;

enum class RealmType : uint8_t {
  kPvPGN = 0,
  kBattleNetClassic = 1,
  LAST = 2,
};

constexpr uint8_t REALM_AUTH_PVPGN = 0u;
constexpr uint8_t REALM_AUTH_BATTLENET = 1u;

enum class RealmAuth : uint8_t {
  kPvPGN = 0,
  kBattleNet = 1,
  LAST = 2,
};

enum class RealmBroadcastDisplayPriority : uint8_t {
  kNone = 0,
  kLow = 1,
  kHigh = 2,
  LAST = 3,
};

template<> struct is_comparable_enum<RealmBroadcastDisplayPriority> : std::true_type {};

enum class PvPGNLocale : uint8_t {
  kENUS = 0,
  kCSCZ = 1,
  kDEDE = 2,
  kESES = 3,
  kFRFR = 4,
  kITIT = 5,
  kJAJA = 6,
  kKOKR = 7,
  kPLPL = 8,
  kRURU = 9,
  kZHCN = 10,
  kZHTW = 11,
  LAST = 12,
};

// config_net.h 

constexpr uint8_t MAP_TRANSFERS_NEVER = 0u;
constexpr uint8_t MAP_TRANSFERS_AUTOMATIC = 1u;
constexpr uint8_t MAP_TRANSFERS_MANUAL = 2u;

enum class MapTransfersMode : uint8_t {
  kNever = 0,
  kAutomatic = 1,
  kManual = 2,
  LAST = 3,
};

constexpr uint8_t UDP_DISCOVERY_MODE_FREE = 0u;
constexpr uint8_t UDP_DISCOVERY_MODE_LAX = 1u;
constexpr uint8_t UDP_DISCOVERY_MODE_STRICT = 2u;

enum class UDPDiscoveryMode : uint8_t {
  kFree = 0,
  kLax = 1,
  kStrict = 2,
  LAST = 4,
};

// config_game.h

constexpr uint8_t ON_DESYNC_NONE = 0u;
constexpr uint8_t ON_DESYNC_NOTIFY = 1u;
constexpr uint8_t ON_DESYNC_DROP = 2u;

enum class OnDesyncHandler : uint8_t {
  kNone = 0,
  kNotify = 1,
  kDrop = 2,
  LAST = 3,
};

constexpr uint8_t ON_IPFLOOD_NONE = 0u;
constexpr uint8_t ON_IPFLOOD_NOTIFY = 1u;
constexpr uint8_t ON_IPFLOOD_DENY = 2u;

enum class OnIPFloodHandler : uint8_t {
  kNone = 0,
  kNotify = 1,
  kDeny = 2,
  LAST = 3,
};

constexpr uint8_t ON_UNSAFE_NAME_NONE = 0u;
constexpr uint8_t ON_UNSAFE_NAME_CENSOR_MAY_DESYNC = 1u;
constexpr uint8_t ON_UNSAFE_NAME_DENY = 2u;

enum class OnUnsafeNameHandler : uint8_t {
  kNone = 0,
  kCensorMayDesync = 1,
  kDeny = 2,
  LAST = 3,
};

constexpr uint8_t ON_PLAYER_LEAVE_NONE = 0u;
constexpr uint8_t ON_PLAYER_LEAVE_NATIVE = 1u;
constexpr uint8_t ON_PLAYER_LEAVE_SHARE_UNITS = 2u;

enum class OnPlayerLeaveHandler : uint8_t {
  kNone = 0,
  kNative = 1,
  kShareUnits = 2,
  LAST = 3,
};

constexpr uint8_t ON_SHARE_UNITS_NATIVE = 0u;
constexpr uint8_t ON_SHARE_UNITS_KICK = 1u;
constexpr uint8_t ON_SHARE_UNITS_RESTRICT = 2u;

enum class OnShareUnitsHandler : uint8_t {
  kNative = 0,
  kKickSharer = 1,
  kRestrictSharee = 2,
  LAST = 3,
};

constexpr uint8_t SELECT_EXPANSION_ROC = 0u;
constexpr uint8_t SELECT_EXPANSION_TFT = 1u;

enum class GameExpansionCode : uint8_t {
  kRoC = 0,
  kTFT = 1,
  LAST = 2,
};

enum class CrossPlayMode : uint8_t {
  kNone = 0,
  kConservative = 1,
  kOptimistic = 2,
  kForce = 3,
  LAST = 4,
};

enum class PlayersReadyMode : uint8_t {
  kFast = 0,
  kExpectRace = 1,
  kExplicit = 2,
  LAST = 3,
};

enum class HideIGNMode : uint8_t {
  kNever = 0,
  kHost = 1,
  kAlways = 2,
  kAuto = 3,
  LAST = 4,
};

enum class FakeUsersShareUnitsMode : uint8_t {
  kNever = 0,
  kAuto = 1,
  kTeam = 2,
  kAll = 3,
  LAST = 4,
};

enum class OnRealmBroadcastErrorHandler : uint8_t {
  kIgnoreErrors = 0,
  kExitOnMainError = 1,
  kExitOnMainErrorIfEmpty = 2,
  kExitOnAnyError = 3,
  kExitOnAnyErrorIfEmpty = 4,
  kExitOnMaxErrors = 5,
  LAST = 6,
};

enum class MirrorSourceType : uint8_t {
  kRaw = 0,
  kRegistry = 1,
  LAST = 2,
};

enum class LobbyTimeoutMode : uint8_t {
  kNever = 0,
  kEmpty = 1,
  kOwnerMissing = 2,
  kStrict = 3,
  LAST = 4,
};

enum class MirrorTimeoutMode : uint8_t {
  kNever = 0,
  kStarted = 1,
  kUnwatched = 2,
  kStrict = 3,
  LAST = 4,
};

enum class LobbyOwnerTimeoutMode : uint8_t {
  kNever = 0,
  kAbsent = 1,
  kStrict = 2,
  LAST = 3,
};

enum class GameLoadingTimeoutMode : uint8_t {
  kNever = 0,
  kStrict = 1,
  LAST = 2,
};

enum class GamePlayingTimeoutMode : uint8_t {
  kNever = 0,
  kDry = 1,
  kStrict = 2,
  LAST = 3,
};

constexpr uint8_t LOG_CHAT_TYPE_ASCII = 1u;
constexpr uint8_t LOG_CHAT_TYPE_NON_ASCII = 2u;
constexpr uint8_t LOG_CHAT_TYPE_COMMANDS = 4u;

constexpr uint8_t MAX_GAME_VERSION_OVERRIDES = 24;

constexpr uint16_t APM_RATE_LIMITER_MIN = 30;
constexpr uint16_t APM_RATE_LIMITER_MAX = 800;

constexpr int64_t APM_RATE_LIMITER_TICK_INTERVAL = 5000;
constexpr double APM_RATE_LIMITER_TICK_SCALING_FACTOR = APM_RATE_LIMITER_TICK_INTERVAL / 60000.;
constexpr double APM_RATE_LIMITER_BURST_ACTIONS = 100.;

constexpr size_t GAME_ACTION_HOLD_QUEUE_MAX_SIZE = 100;
constexpr size_t GAME_ACTION_HOLD_QUEUE_MIN_WARNING_SIZE = 50;
constexpr size_t GAME_ACTION_HOLD_QUEUE_MOD_WARNING_SIZE = 8;

// config_discord.h

constexpr uint8_t FILTER_ALLOW_ALL = 0;
constexpr uint8_t FILTER_DENY_ALL = 1;
constexpr uint8_t FILTER_ALLOW_LIST = 2;
constexpr uint8_t FILTER_DENY_LIST = 3;

enum class PermissionsFilterMode : uint8_t {
  kAllowAll = 0,
  kDenyAll = 1,
  kAllowList = 2,
  kDenyList = 3,
  LAST = 4,
};

// config_commands.h

constexpr uint8_t COMMAND_PERMISSIONS_DISABLED = 0;
constexpr uint8_t COMMAND_PERMISSIONS_SUDO = 1;
constexpr uint8_t COMMAND_PERMISSIONS_SUDO_UNSAFE = 2;
constexpr uint8_t COMMAND_PERMISSIONS_ROOTADMIN = 3;
constexpr uint8_t COMMAND_PERMISSIONS_ADMIN = 4;
constexpr uint8_t COMMAND_PERMISSIONS_VERIFIED_OWNER = 5;
constexpr uint8_t COMMAND_PERMISSIONS_OWNER = 6;
constexpr uint8_t COMMAND_PERMISSIONS_VERIFIED = 7;
constexpr uint8_t COMMAND_PERMISSIONS_AUTO = 8;
constexpr uint8_t COMMAND_PERMISSIONS_POTENTIAL_OWNER = 9;
constexpr uint8_t COMMAND_PERMISSIONS_START_GAME = 10;
constexpr uint8_t COMMAND_PERMISSIONS_UNVERIFIED = 11;

enum class CommandPermissions : uint8_t {
  kDisabled = 0,
  kSudo = 1,
  kSudoUnsafe = 2,
  kRootAdmin = 3,
  kAdmin = 4,
  kVerifiedOwner = 5,
  kOwner = 6,
  kVerified = 7,
  kAuto = 8,
  kPotentialOwner = 9,
  kStartGame = 10,
  kUnverified = 11,
  LAST = 12,
};

// net.h

constexpr uint8_t CONNECTION_TYPE_DEFAULT = 0;
constexpr uint8_t CONNECTION_TYPE_LOOPBACK = (1 << 0);
constexpr uint8_t CONNECTION_TYPE_CUSTOM_PORT = (1 << 1);
constexpr uint8_t CONNECTION_TYPE_CUSTOM_IP_ADDRESS = (1 << 2);
constexpr uint8_t CONNECTION_TYPE_VPN = (1 << 3);
constexpr uint8_t CONNECTION_TYPE_IPV6 = (1 << 4);

constexpr uint8_t NET_PUBLIC_IP_ADDRESS_ALGORITHM_NONE = 0;
constexpr uint8_t NET_PUBLIC_IP_ADDRESS_ALGORITHM_MANUAL = 1;
constexpr uint8_t NET_PUBLIC_IP_ADDRESS_ALGORITHM_API = 2;
constexpr uint8_t NET_PUBLIC_IP_ADDRESS_ALGORITHM_INVALID = 3;

enum class NetPublicIPAddressAlgorithm : uint8_t {
  kNone = 0,
  kManual = 1,
  kAPI = 2,
  kInvalid = 3,
  LAST = 4,
};

constexpr uint8_t ACCEPT_IPV4 = (1 << 0);
constexpr uint8_t ACCEPT_IPV6 = (1 << 1);
constexpr uint8_t ACCEPT_ANY = (ACCEPT_IPV4 | ACCEPT_IPV6);

constexpr uint8_t HEALTH_CHECK_PUBLIC_IPV4 = (1 << 0);
constexpr uint8_t HEALTH_CHECK_PUBLIC_IPV6 = (1 << 1);
constexpr uint8_t HEALTH_CHECK_LOOPBACK_IPV4 = (1 << 2);
constexpr uint8_t HEALTH_CHECK_LOOPBACK_IPV6 = (1 << 3);
constexpr uint8_t HEALTH_CHECK_REALM = (1 << 4);
constexpr uint8_t HEALTH_CHECK_ALL = (HEALTH_CHECK_PUBLIC_IPV4 | HEALTH_CHECK_PUBLIC_IPV6 | HEALTH_CHECK_LOOPBACK_IPV4 | HEALTH_CHECK_LOOPBACK_IPV6 | HEALTH_CHECK_REALM);
constexpr uint8_t HEALTH_CHECK_VERBOSE = (1 << 5);

constexpr int64_t GAME_TEST_TIMEOUT = 3000;
constexpr int64_t IP_ADDRESS_API_TIMEOUT = 3000;

constexpr uint8_t RECONNECT_DISABLED = 0;
constexpr uint8_t RECONNECT_ENABLED_GPROXY_BASIC = 1;
constexpr uint8_t RECONNECT_ENABLED_GPROXY_EXTENDED = 2;
constexpr uint8_t RECONNECT_ENABLED_GPROXY_ALL = RECONNECT_ENABLED_GPROXY_BASIC | RECONNECT_ENABLED_GPROXY_EXTENDED;

constexpr size_t MAX_INCOMING_CONNECTIONS = 255;
constexpr float GAME_USER_CONNECTION_MAX_TIMEOUT = 5000.;
constexpr float GAME_USER_CONNECTION_MIN_TIMEOUT = 500.;
constexpr float GAME_SEEKER_CONNECTION_MAX_TIMEOUT = 60000.;
constexpr float GAME_SEEKER_CONNECTION_MIN_TIMEOUT = 6000.;
constexpr int64_t GAME_USER_TIMEOUT_VANILLA = 70000;
constexpr int64_t GAME_USER_TIMEOUT_RECONNECTABLE = 20000;

constexpr uint16_t GAME_DEFAULT_UDP_PORT = 6112u;
constexpr uint8_t UDP_DISCOVERY_MAX_EXTRA_ADDRESSES = 30u;

enum class NetProtocol : uint8_t {
  kTCP = 0,
  kUDP = 1,
  LAST = 2,
};

// Exponential backoff starts at twice this value (45x2=90 seconds)
constexpr int64_t NET_BASE_RECONNECT_DELAY = 45;
constexpr uint8_t NET_RECONNECT_MAX_BACKOFF = 12;

// realm.h

constexpr uint32_t REALM_TCP_KEEPALIVE_IDLE_TIME = 900;
constexpr int64_t REALM_APP_KEEPALIVE_IDLE_TICKS = 180000;
constexpr int64_t REALM_APP_KEEPALIVE_INTERVAL = 30000;
constexpr int64_t REALM_APP_KEEPALIVE_MAX_MISSED = 4;

// realm_chat.h

constexpr uint8_t RECV_SELECTOR_SYSTEM = 1;
constexpr uint8_t RECV_SELECTOR_ONLY_WHISPER = 2;
constexpr uint8_t RECV_SELECTOR_ONLY_PUBLIC = 3;
constexpr uint8_t RECV_SELECTOR_ONLY_PUBLIC_OR_DROP = 4;
constexpr uint8_t RECV_SELECTOR_PREFER_PUBLIC = 5;

enum class ChatRecvSelector : uint8_t {
  kSystem = 1,
  kOnlyWhisper = 2,
  kOnlyPublic = 3,
  kOnlyPublicOrDrop = 4,
  kPreferPublic = 5,
  LAST = 6,
};

constexpr uint8_t CHAT_RECV_SELECTED_NONE = 0;
constexpr uint8_t CHAT_RECV_SELECTED_SYSTEM = 1;
constexpr uint8_t CHAT_RECV_SELECTED_PUBLIC = 2;
constexpr uint8_t CHAT_RECV_SELECTED_WHISPER = 3;
constexpr uint8_t CHAT_RECV_SELECTED_DROP = 4;

enum class ChatRecvSelected : uint8_t {
  kNone = 0,
  kSystem = 1,
  kPublic = 2,
  kWhisper = 3,
  kDrop = 4,
  LAST = 5,
};

constexpr uint8_t CHAT_CALLBACK_NONE = 0;
constexpr uint8_t CHAT_CALLBACK_REFRESH_GAME = 1;
constexpr uint8_t CHAT_CALLBACK_RESET = 2;

enum class QueuedChatMessageSendCallback : uint8_t {
  kNone = 0,
  kRefreshGame = 1,
  kReset = 2,
  LAST = 3,
};

constexpr uint8_t CHAT_VALIDATOR_NONE = 0;
constexpr uint8_t CHAT_VALIDATOR_LOBBY_JOINABLE = 1;

enum class QueuedChatMessageValidator : uint8_t {
  kNone = 0,
  kLobbyJoinable = 1,
  LAST = 2,
};

// realm_games.h

enum class GameSearchQueryCallback : uint8_t {
  kNone = 0,
  kHostActiveMirror = 1,
  LAST = 2,
};

// pjass.h

constexpr uint8_t PJASS_PERMISSIVE = 0u;
constexpr uint8_t PJASS_STRICT = 1u;

enum class PJassStrictLevel : uint8_t {
  kPermissive = 0,
  kStrict = 1,
  LAST = 2,
};

constexpr uint8_t PJASS_OPTIONS_NOSYNTAXERROR = 0u;
constexpr uint8_t PJASS_OPTIONS_NOSEMANTICERROR = 1u;
constexpr uint8_t PJASS_OPTIONS_NORUNTIMEERROR = 2u;
constexpr uint8_t PJASS_OPTIONS_RB = 3u;
constexpr uint8_t PJASS_OPTIONS_NOMODULOOPERATOR = 4u;
constexpr uint8_t PJASS_OPTIONS_SHADOW = 5u;
constexpr uint8_t PJASS_OPTIONS_CHECKLONGNAMES = 6u;
constexpr uint8_t PJASS_OPTIONS_FILTER = 7u;
constexpr uint8_t PJASS_OPTIONS_CHECKGLOBALSINIT = 8u;
constexpr uint8_t PJASS_OPTIONS_CHECKSTRINGHASH = 9u;
constexpr uint8_t PJASS_OPTIONS_CHECKNUMBERLITERALS = 10u;

enum class PjassOptions : uint8_t {
  kNoSyntaxError = 0,
  kNoSemanticError = 1,
  kNoRuntimeError = 2,
  kRB = 3,
  kNoModuloOperator = 4,
  kShadow = 5,
  kCheckLongNames = 6,
  kFilter = 7,
  kCheckGlobalsInit = 8,
  kCheckStringHash = 9,
  kCheckNumberLiterals = 10,
  LAST = 11,
};

// w3mmd.h

constexpr uint8_t MMD_ACTION_TYPE_VAR = 0u;
constexpr uint8_t MMD_ACTION_TYPE_FLAG = 1u;
constexpr uint8_t MMD_ACTION_TYPE_EVENT = 2u;
constexpr uint8_t MMD_ACTION_TYPE_BLANK = 3u;
constexpr uint8_t MMD_ACTION_TYPE_CUSTOM = 4u;

enum class MmdActionType : uint8_t {
  kVar = 0,
  kFlag = 1,
  kEvent = 2,
  kBlank = 3,
  kCustom = 4,
  LAST = 5,
};

constexpr uint8_t MMD_DEFINITION_TYPE_INIT = 0u;
constexpr uint8_t MMD_DEFINITION_TYPE_VAR = 1u;
constexpr uint8_t MMD_DEFINITION_TYPE_EVENT = 2u;

enum class MmdDefinitionType : uint8_t {
  kInit = 0,
  kVar = 1,
  kEvent = 2,
  LAST = 3,
};

constexpr uint8_t MMD_INIT_TYPE_VERSION = 0u;
constexpr uint8_t MMD_INIT_TYPE_PLAYER = 1u;

enum class MmdInitType : uint8_t {
  kVersion = 0,
  kPlayer = 1,
  LAST = 2,
};

constexpr uint8_t MMD_VALUE_TYPE_INT = 0u;
constexpr uint8_t MMD_VALUE_TYPE_REAL = 1u;
constexpr uint8_t MMD_VALUE_TYPE_STRING = 2u;

enum class MmdValueType : uint8_t {
  kInt = 0,
  kReal = 1,
  kString = 2,
  LAST = 3,
};

constexpr uint8_t MMD_OPERATOR_SET = 0u;
constexpr uint8_t MMD_OPERATOR_ADD = 1u;
constexpr uint8_t MMD_OPERATOR_SUBTRACT = 2u;

enum class MmdOperator : uint8_t {
  kSet = 0,
  kAdd = 1,
  kSubtract = 2,
  LAST = 3,
};

constexpr uint8_t MMD_FLAG_LOSER = 0u;
constexpr uint8_t MMD_FLAG_DRAWER = 1u;
constexpr uint8_t MMD_FLAG_WINNER = 2u;
constexpr uint8_t MMD_FLAG_LEAVER = 3u;
constexpr uint8_t MMD_FLAG_PRACTICE = 4u;

enum class MmdFlag : uint8_t {
  kLoser = 0,
  kDrawer = 1,
  kWinner = 2,
  kLeaver = 3,
  kPractice = 4,
  LAST = 5,
};

constexpr int64_t MMD_PROCESSING_INITIAL_DELAY = 60000;
constexpr int64_t MMD_PROCESSING_STREAM_DEF_DELAY = 60000;
constexpr int64_t MMD_PROCESSING_STREAM_ACTION_DELAY = 180000;

constexpr uint32_t MMD_MAX_ARITY = 64u;

template<typename T>
constexpr auto operator<(T lhs, T rhs)
  -> std::enable_if_t<is_comparable_enum<T>::value, bool>
{
  return static_cast<std::underlying_type_t<T>>(lhs) < static_cast<std::underlying_type_t<T>>(rhs);
}

template<typename T>
constexpr auto operator<=(T lhs, T rhs)
  -> std::enable_if_t<is_comparable_enum<T>::value, bool>
{
  return !(rhs < lhs);
}

template<typename T>
constexpr auto operator>(T lhs, T rhs)
  -> std::enable_if_t<is_comparable_enum<T>::value, bool>
{
  return rhs < lhs;
}

template<typename T>
constexpr auto operator>=(T lhs, T rhs)
  -> std::enable_if_t<is_comparable_enum<T>::value, bool>
{
  return !(lhs < rhs);
}

#endif // AURA_CONSTANTS_H_
