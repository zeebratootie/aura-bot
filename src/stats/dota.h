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

#ifndef AURA_DOTASTATS_H_
#define AURA_DOTASTATS_H_

#include "../includes.h"
#include "../game_structs.h"

namespace Dota
{
  constexpr uint8_t SENTINEL_CREEPS_COLOR = 0u;
  constexpr uint8_t SENTINEL_HERO_MIN_COLOR = 1u;
  constexpr uint8_t SENTINEL_HERO_MAX_COLOR = 5u;
  constexpr uint8_t SENTINEL_ALLIANCE_MIN_COLOR = SENTINEL_CREEPS_COLOR;
  constexpr uint8_t SENTINEL_ALLIANCE_MAX_COLOR = SENTINEL_HERO_MAX_COLOR;
  constexpr uint8_t SCOURGE_CREEPS_COLOR = 6u;
  constexpr uint8_t SCOURGE_HERO_MIN_COLOR = 7u;
  constexpr uint8_t SCOURGE_HERO_MAX_COLOR = 11u;
  constexpr uint8_t SCOURGE_ALLIANCE_MIN_COLOR = SCOURGE_CREEPS_COLOR;
  constexpr uint8_t SCOURGE_ALLIANCE_MAX_COLOR = SCOURGE_HERO_MAX_COLOR;
  constexpr uint8_t NEUTRAL_CREEPS_COLOR = 12u;

  constexpr uint8_t WINNER_UNDECIDED = 0u;
  constexpr uint8_t WINNER_SENTINEL = 1u;
  constexpr uint8_t WINNER_SCOURGE = 2u;

  [[nodiscard]] inline bool GetIsValidColor(const uint32_t color) { return color < MAX_SLOTS_LEGACY; }
  [[nodiscard]] inline bool GetIsSentinelCreepsColor(const uint8_t color) { return color == Dota::SENTINEL_CREEPS_COLOR; }
  [[nodiscard]] inline bool GetIsSentinelCreepsColor(const uint32_t color) { return GetIsValidColor(color) && GetIsSentinelCreepsColor((uint8_t)(color)); }
  [[nodiscard]] inline bool GetIsSentinelHeroColor(const uint8_t color) { return SENTINEL_HERO_MIN_COLOR <= color && color <= SENTINEL_HERO_MAX_COLOR; }
  [[nodiscard]] inline bool GetIsSentinelColor(const uint8_t color) { return color < SCOURGE_CREEPS_COLOR; }
  [[nodiscard]] inline bool GetIsScourgeCreepsColor(const uint8_t color) { return color == Dota::SCOURGE_CREEPS_COLOR; }
  [[nodiscard]] inline bool GetIsScourgeCreepsColor(const uint32_t color) { return GetIsValidColor(color) && GetIsScourgeCreepsColor((uint8_t)(color)); }
  [[nodiscard]] inline bool GetIsNeutralCreepsColor(const uint8_t color) { return color == NEUTRAL_CREEPS_COLOR; }
  [[nodiscard]] inline bool GetIsNeutralCreepsColor(const uint32_t color) { return GetIsValidColor(color) && GetIsNeutralCreepsColor((uint8_t)(color)); }
  [[nodiscard]] inline bool GetIsScourgeHeroColor(const uint8_t color) { return SCOURGE_HERO_MIN_COLOR <= color && color <= SCOURGE_HERO_MAX_COLOR; }
  [[nodiscard]] inline bool GetIsScourgeColor(const uint8_t color) { return SCOURGE_CREEPS_COLOR <= color && color < NEUTRAL_CREEPS_COLOR; }
  [[nodiscard]] inline bool GetIsHeroColor(const uint8_t color) { return GetIsSentinelHeroColor(color) || GetIsScourgeHeroColor(color); }
  [[nodiscard]] inline bool GetIsHeroColor(const uint32_t color) { return GetIsValidColor(color) && GetIsHeroColor((uint8_t)color);}
  [[nodiscard]] inline bool GetAreSameTeamColors(const uint8_t a, const uint8_t b) {
    if (GetIsNeutralCreepsColor(a) || GetIsNeutralCreepsColor(b)) return a == b;
    return (a < SCOURGE_CREEPS_COLOR) == (b < SCOURGE_CREEPS_COLOR);
  }

  [[nodiscard]] std::optional<uint8_t> EnsureHeroColor(uint32_t input);
  [[nodiscard]] std::optional<uint8_t> EnsureActorColor(uint32_t input);
  [[nodiscard]] std::optional<uint8_t> ParseHeroColor(const std::string& input);
  [[nodiscard]] std::optional<uint8_t> ParseActorColor(const std::string& input);
  [[nodiscard]] std::optional<uint8_t> ParseHeroColor(std::string_view input);
  [[nodiscard]] std::optional<uint8_t> ParseActorColor(std::string_view input);
  [[nodiscard]] std::string GetLaneName(const uint8_t code);
  [[nodiscard]] std::string GetTeamNameBaseZero(const uint8_t code);
  [[nodiscard]] std::string GetTeamNameBaseOne(const uint8_t code);
  [[nodiscard]] std::string GetRuneName(const uint8_t code);
  [[nodiscard]] std::string GetAbilityName(const uint32_t code);
  [[nodiscard]] std::string GetItemName(const uint32_t code);
  [[nodiscard]] std::string GetHeroName(const uint32_t code);

  // CDotaStats
  //

  // the stats class is passed a copy of every player action in ProcessAction when it's received
  // then when the game is over the Save function is called
  // so the idea is that you parse the actions to gather data about the game, storing the results in any member variables you need in your subclass
  // and in the Save function you write the results to the database
  // e.g. for dota the number of kills/deaths/assists, etc...

  class CDotaStats
  {
  public:
    std::reference_wrapper<CGame>                 m_Game;
    std::optional<uint64_t>                       m_GameOverTime;
    uint8_t                                       m_Winner;
    bool                                          m_SwitchEnabled;
    std::pair<uint32_t, uint32_t>                 m_Time;
    std::array<CDBDotAPlayer*, 12>                m_Players;

    explicit CDotaStats(std::shared_ptr<CGame> nGame);
    ~CDotaStats();
    CDotaStats(CDotaStats&) = delete;

    bool EventGameCacheInteger(const uint8_t UID, const std::string_view fileName, const std::string_view missionKey, const std::string_view key, const uint32_t value);
    bool UpdateQueue();
    void FlushQueue();
    void Save(CAura* nAura, CAuraDB* nDB);

    [[nodiscard]] GameUser::CGameUser* GetUserFromColor(const uint8_t color) const;
    [[nodiscard]] std::string GetUserNameFromColor(const uint8_t color) const;
    [[nodiscard]] std::string GetActorNameFromColor(const uint8_t color) const;
    [[nodiscard]] inline GameUser::CGameUser* GetUserFromColor(const uint32_t color) const { return Dota::GetIsValidColor(color) ? GetUserFromColor((uint8_t)(color)) : nullptr; }
    [[nodiscard]] inline std::string GetUserNameFromColor(const uint32_t color) const { return Dota::GetIsValidColor(color) ? GetUserNameFromColor((uint8_t)(color)) : std::string("Invalid"); }
    [[nodiscard]] inline std::string GetActorNameFromColor(const uint32_t color) const { return Dota::GetIsValidColor(color) ? GetActorNameFromColor((uint8_t)(color)) : std::string("Invalid"); }
    [[nodiscard]] std::vector<CGameController*> GetSentinelControllers() const;
    [[nodiscard]] std::vector<CGameController*> GetScourgeControllers() const;
    [[nodiscard]] std::optional<GameResults> GetGameResults(const GameResultConstraints& constraints) const;
    [[nodiscard]] inline bool GetIsGameOver() const { return m_GameOverTime.has_value(); }
    [[nodiscard]] inline uint64_t GetGameOverTime() const { return m_GameOverTime.value(); }
    [[nodiscard]] std::string GetLogPrefix() const;
    void LogMetaData(int64_t recvTicks, const std::string& text) const;
  };
};

#endif // AURA_DOTASTATS_H_
