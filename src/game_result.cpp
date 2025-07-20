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

#include "config/config.h"
#include "game_result.h"
#include "game_controller_data.h"
#include "map.h"

using namespace std;

//
// GameResults
//

GameResults::GameResults()
{
}

GameResults::~GameResults()
{
}

vector<string> GameResults::GetWinnersNames() const
{
  vector<string> names;
  for (const auto& winner : GetWinners()) {
    names.push_back(winner->GetName());
  }
  return names;
}

void GameResults::Confirm()
{
  for (auto& winner : winners) {
    winner->SetGameResult(GamePlayerResult::kWinner);
  }
  for (auto& loser : losers) {
    loser->SetGameResult(GamePlayerResult::kLoser);
  }
  for (auto& drawer : drawers) {
    drawer->SetGameResult(GamePlayerResult::kDrawer);
  }
}

//
// GameResultConstraints
//

GameResultConstraints::GameResultConstraints()
  : m_IsValid(true),
    m_CanDraw(false),
    m_CanDrawAndWin(false),
    m_CanAllWin(false),
    m_CanAllLose(false),
    m_CanLeaverDraw(false),
    m_CanLeaverWin(false),
    // A "winner team" is a team with at least 1 winner
    m_CanTeamWithLeaverWin(false),
    m_RequireTeamWinExceptLeaver(false),
    m_UndecidedVirtualHandler(GameResultVirtualUndecidedHandler::kAuto),
    m_UndecidedUserHandler(GameResultUserUndecidedHandler::kLoserSelf),
    m_UndecidedComputerHandler(GameResultComputerUndecidedHandler::kAuto),
    m_ConflictHandler(GameResultConflictHandler::kMajorityOrVoid),
    m_SourceOfTruth(GameResultSourceSelect::kNone),

    m_MinPlayers(2),
    m_MaxPlayers(MAX_SLOTS_MODERN),
    m_MinWinnerPlayers(0),
    m_MaxWinnerPlayers(MAX_SLOTS_MODERN),
    m_MinLoserPlayers(0),
    m_MaxLoserPlayers(MAX_SLOTS_MODERN),

    m_MinTeams(2),
    m_MaxTeams(MAX_SLOTS_MODERN),
    // A "winner team" is a team with at least 1 winner
    m_MinTeamsWithWinners(0),
    m_MaxTeamsWithWinners(MAX_SLOTS_MODERN),
    m_MinTeamsWithNoWinners(0),
    m_MaxTeamsWithNoWinners(MAX_SLOTS_MODERN)
{
}

GameResultConstraints::GameResultConstraints(const CMap* map, CConfig& CFG)
  : m_IsValid(true),
    m_CanDraw(!map->GetMapIsMelee()),
    m_CanDrawAndWin(false),
    m_CanAllWin(false),
    m_CanAllLose(false),
    m_CanLeaverDraw(false),
    m_CanLeaverWin(false),
    // A "winner team" is a team with at least 1 winner
    m_CanTeamWithLeaverWin(false),
    m_RequireTeamWinExceptLeaver(false),
    m_UndecidedVirtualHandler(GameResultVirtualUndecidedHandler::kAuto),
    m_UndecidedUserHandler(GameResultUserUndecidedHandler::kLoserSelf),
    m_UndecidedComputerHandler(GameResultComputerUndecidedHandler::kAuto),
    m_ConflictHandler(GameResultConflictHandler::kMajorityOrVoid),
    m_SourceOfTruth(map->GetMMDEnabled() ? GameResultSourceSelect::kPreferMMD : GameResultSourceSelect::kOnlyLeaveCode),

    m_MinPlayers(2),
    m_MaxPlayers(map->GetMapNumControllers()),
    m_MinWinnerPlayers(0),
    m_MaxWinnerPlayers(map->GetMapNumControllers()),
    m_MinLoserPlayers(0),
    m_MaxLoserPlayers(map->GetMapNumControllers()),

    m_MinTeams(2),
    m_MaxTeams(map->GetMapNumTeams()),
    // A "winner team" is a team with at least 1 winner
    m_MinTeamsWithWinners(0),
    m_MaxTeamsWithWinners(map->GetMapNumTeams()),
    m_MinTeamsWithNoWinners(0),
    m_MaxTeamsWithNoWinners(map->GetMapNumTeams())
{
  const array<string, 5> truthSourceOptions = {"none", "only-exit", "only-mmd", "prefer-exit", "prefer-mmd"};
  assert((uint8_t)GameResultSourceSelect::LAST == truthSourceOptions.size());
  if (CFG.Exists("map.game_result.source")) {
    m_SourceOfTruth = CFG.GetEnum<GameResultSourceSelect>("map.game_result.source", truthSourceOptions, m_SourceOfTruth);
    CFG.FailIfErrorLast();
  } else {
    CFG.SetString("map.game_result.source", truthSourceOptions[(uint8_t)m_SourceOfTruth]);
  }

  // Generic
  array<string, 3> undecidedVirtualHandlers = {"none", "loser-self", "auto"};
  if (CFG.Exists("map.game_result.resolution.undecided_handler.virtual")) {
    m_UndecidedVirtualHandler = CFG.GetEnum<GameResultVirtualUndecidedHandler>("map.game_result.resolution.undecided_handler.virtual", undecidedVirtualHandlers, m_UndecidedVirtualHandler);
  } else {
    CFG.SetString("map.game_result.resolution.undecided_handler.virtual", undecidedVirtualHandlers[(uint8_t)m_UndecidedVirtualHandler]);
  }

  // Generic
  array<string, 3> undecidedUserHandlers = {"none", "loser-self", "loser-self-and-non-user-allies"};
  if (CFG.Exists("map.game_result.resolution.undecided_handler.user")) {
    m_UndecidedUserHandler = CFG.GetEnum<GameResultUserUndecidedHandler>("map.game_result.resolution.undecided_handler.user", undecidedUserHandlers, m_UndecidedUserHandler);
  } else {
    CFG.SetString("map.game_result.resolution.undecided_handler.user", undecidedUserHandlers[(uint8_t)m_UndecidedUserHandler]);
  }

  // Generic
  array<string, 3> undecidedComputerHandlers = {"none", "loser-self", "auto"};
  if (CFG.Exists("map.game_result.resolution.undecided_handler.computer")) {
    m_UndecidedComputerHandler = CFG.GetEnum<GameResultComputerUndecidedHandler>("map.game_result.resolution.undecided_handler.computer", undecidedComputerHandlers, m_UndecidedComputerHandler);
  } else {
    CFG.SetString("map.game_result.resolution.undecided_handler.computer", undecidedComputerHandlers[(uint8_t)m_UndecidedComputerHandler]);
  }

  // MMD-specific
  const array<string, 6> conflictHandlerOptions = {"void", "pessimistic", "optimistic", "majority-or-void", "majority-or-pessimistic", "majority-or-optimistic"};
    if (CFG.Exists("map.game_result.resolution.conflict_handler")) {
    m_ConflictHandler = CFG.GetEnum<GameResultConflictHandler>("map.game_result.resolution.conflict_handler", conflictHandlerOptions, m_ConflictHandler);
    CFG.FailIfErrorLast();
  } else {
    CFG.SetString("map.game_result.resolution.conflict_handler", conflictHandlerOptions[(uint8_t)m_ConflictHandler]);
  }

  if (CFG.Exists("map.game_result.constraints.draw.allowed")) {
    m_CanDraw = CFG.GetBool("map.game_result.constraints.draw.allowed", m_CanDraw);
  } else {
    CFG.SetBool("map.game_result.constraints.draw.allowed", m_CanDraw);
  }

  if (CFG.Exists("map.game_result.constraints.draw_and_win.allowed")) {
    m_CanDrawAndWin = CFG.GetBool("map.game_result.constraints.draw_and_win.allowed", m_CanDrawAndWin);
  } else {
    CFG.SetBool("map.game_result.constraints.draw_and_win.allowed", m_CanDrawAndWin);
  }

  if (CFG.Exists("map.game_result.constraints.shared_winners.all.allowed")) {
    m_CanAllWin = CFG.GetBool("map.game_result.constraints.shared_winners.all.allowed", m_CanAllWin);
  } else {
    CFG.SetBool("map.game_result.constraints.shared_winners.all.allowed", m_CanAllWin);
  }

  if (CFG.Exists("map.game_result.constraints.shared_losers.all.allowed")) {
    m_CanAllLose = CFG.GetBool("map.game_result.constraints.shared_losers.all.allowed", m_CanAllLose);
  } else {
    CFG.SetBool("map.game_result.constraints.shared_losers.all.allowed", m_CanAllLose);
  }

  if (CFG.Exists("map.game_result.constraints.drawing_leavers.allowed")) {
    m_CanLeaverDraw = CFG.GetBool("map.game_result.constraints.drawing_leavers.allowed", m_CanLeaverDraw);
  } else {
    CFG.SetBool("map.game_result.constraints.drawing_leavers.allowed", m_CanLeaverDraw);
  }

  m_CanLeaverWin = false; // TODO: This should be configurable with a grace period
  if (CFG.Exists("map.game_result.constraints.winning_leavers.allowed")) {
    m_CanLeaverWin = CFG.GetBool("map.game_result.constraints.winning_leavers.allowed", m_CanLeaverWin);
  } else {
    CFG.SetBool("map.game_result.constraints.winning_leavers.allowed", m_CanLeaverWin);
  }

  if (CFG.Exists("map.game_result.constraints.winner_has_allied_leaver.allowed")) {
    m_CanTeamWithLeaverWin = CFG.GetBool("map.game_result.constraints.winner_has_allied_leaver.allowed", m_CanTeamWithLeaverWin);
  } else {
    CFG.SetBool("map.game_result.constraints.winner_has_allied_leaver.allowed", m_CanTeamWithLeaverWin);
  }

  if (CFG.Exists("map.game_result.constraints.allied_victory_except_leaver.required")) {
    m_RequireTeamWinExceptLeaver = CFG.GetBool("map.game_result.constraints.allied_victory_except_leaver.required", m_RequireTeamWinExceptLeaver);
  } else {
    CFG.SetBool("map.game_result.constraints.allied_victory_except_leaver.required", m_RequireTeamWinExceptLeaver);
  }

  bool wasStrict = CFG.GetStrictMode();
  if (CFG.Exists("map.game_result.constraints.controllers.max")) {
    m_MaxPlayers = CFG.GetPlayerCount("map.game_result.constraints.controllers.max", m_MaxPlayers, m_MaxPlayers);
  } else {
    CFG.SetUint8("map.game_result.constraints.controllers.max", m_MaxPlayers);
  }
  if (CFG.Exists("map.game_result.constraints.controllers.min")) {
    m_MinPlayers = CFG.GetPlayerCount("map.game_result.constraints.controllers.min", m_MaxPlayers, m_MinPlayers);
  } else {
    CFG.SetUint8("map.game_result.constraints.controllers.min", m_MinPlayers);
  }
  if (CFG.Exists("map.game_result.constraints.controllers.winners.max")) {
    m_MaxWinnerPlayers = CFG.GetPlayerCount("map.game_result.constraints.controllers.winners.max", m_MaxPlayers, m_MaxWinnerPlayers);
  } else {
    CFG.SetUint8("map.game_result.constraints.controllers.winners.max", m_MaxWinnerPlayers);
  }
  if (CFG.Exists("map.game_result.constraints.controllers.winners.min")) {
    m_MinWinnerPlayers = CFG.GetPlayerCount("map.game_result.constraints.controllers.winners.min", m_MaxPlayers, m_MinWinnerPlayers);
  } else {
    CFG.SetUint8("map.game_result.constraints.controllers.winners.min", m_MinWinnerPlayers);
  }
  if (CFG.Exists("map.game_result.constraints.controllers.losers.max")) {
    m_MaxLoserPlayers = CFG.GetPlayerCount("map.game_result.constraints.controllers.losers.max", m_MaxPlayers, m_MaxLoserPlayers);
  } else {
    CFG.SetUint8("map.game_result.constraints.controllers.losers.max", m_MaxLoserPlayers);
  }
  if (CFG.Exists("map.game_result.constraints.controllers.losers.min")) {
    m_MinLoserPlayers = CFG.GetPlayerCount("map.game_result.constraints.controllers.losers.min", m_MaxPlayers, m_MinLoserPlayers);
  } else {
    CFG.SetUint8("map.game_result.constraints.controllers.losers.min", m_MinLoserPlayers);
  }

  if (CFG.Exists("map.game_result.constraints.teams.max")) {
    m_MaxTeams = CFG.GetPlayerCount("map.game_result.constraints.teams.max", m_MaxTeams, m_MaxTeams);
  } else {
    CFG.SetUint8("map.game_result.constraints.teams.max", m_MaxTeams);
  }
  if (CFG.Exists("map.game_result.constraints.teams.min")) {
    m_MinTeams = CFG.GetPlayerCount("map.game_result.constraints.teams.min", m_MaxTeams, m_MinTeams);
  } else {
    CFG.SetUint8("map.game_result.constraints.teams.min", m_MinTeams);
  }
  if (CFG.Exists("map.game_result.constraints.teams.winners.max")) {
    m_MaxTeamsWithWinners = CFG.GetPlayerCount("map.game_result.constraints.teams.winners.max", m_MaxTeams, m_MaxTeamsWithWinners);
  } else {
    CFG.SetUint8("map.game_result.constraints.teams.winners.max", m_MaxTeamsWithWinners);
  }
  if (CFG.Exists("map.game_result.constraints.teams.winners.min")) {
    m_MinTeamsWithWinners = CFG.GetPlayerCount("map.game_result.constraints.teams.winners.min", m_MaxTeams, m_MinTeamsWithWinners);
  } else {
    CFG.SetUint8("map.game_result.constraints.teams.winners.min", m_MinTeamsWithWinners);
  }
  if (CFG.Exists("map.game_result.constraints.teams.losers.max")) {
    m_MaxTeamsWithNoWinners = CFG.GetPlayerCount("map.game_result.constraints.teams.losers.max", m_MaxTeams, m_MaxTeamsWithNoWinners);
  } else {
    CFG.SetUint8("map.game_result.constraints.teams.losers.max", m_MaxTeamsWithNoWinners);
  }
  if (CFG.Exists("map.game_result.constraints.teams.losers.min")) {
    m_MinTeamsWithNoWinners = CFG.GetPlayerCount("map.game_result.constraints.teams.losers.min", m_MaxTeams, m_MinTeamsWithNoWinners);
  } else {
    CFG.SetUint8("map.game_result.constraints.teams.losers.min", m_MinTeamsWithNoWinners);
  }

  CFG.SetStrictMode(wasStrict);

  if (
    m_UndecidedUserHandler == GameResultUserUndecidedHandler::kLoserSelfAndAllies && (
      m_UndecidedVirtualHandler != GameResultVirtualUndecidedHandler::kAuto ||
      m_UndecidedComputerHandler != GameResultComputerUndecidedHandler::kAuto
    )
  ) {
    Print("[MAP] <map.game_result.resolution.undecided_handler.user = loser-self-and-non-user-allies> requires other handlers set to auto");
    m_IsValid = false;
  }

  if (m_MinPlayers > m_MaxPlayers) m_IsValid = false;
  if (m_MinWinnerPlayers > m_MaxWinnerPlayers) m_IsValid = false;
  if (m_MinLoserPlayers > m_MaxLoserPlayers) m_IsValid = false;
  if (m_MinTeams > m_MaxTeams) m_IsValid = false;
  if (m_MinTeamsWithWinners > m_MaxTeamsWithWinners) m_IsValid = false;
  if (m_MinTeamsWithNoWinners > m_MaxTeamsWithNoWinners) m_IsValid = false;

  if (!m_IsValid) {
    CFG.SetFailed();
  }
}

GameResultConstraints::~GameResultConstraints()
{
}

