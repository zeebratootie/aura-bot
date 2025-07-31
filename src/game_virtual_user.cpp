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

#include "game_virtual_user.h"
#include "game_controller_data.h"

#include "protocol/game_protocol.h"
#include "game.h"

using namespace std;

//
// CGameVirtualUser
//

CGameVirtualUser::CGameVirtualUser(shared_ptr<CGame> nGame, uint8_t nSID, uint8_t nUID, string nName)
  : m_Game(ref(*nGame)),
    m_Observer(false),
    m_LeftMessageSent(false),
    m_HasPlayerIntent(false),
    m_Status(USERSTATUS_LOBBY),
    m_SID(nSID),
    m_UID(nUID),
    m_OldUID(0xFF),
    m_PseudonymUID(0xFF),
    m_AllowedActions(VIRTUAL_USER_ALLOW_ACTIONS_ANY),
    m_AllowedConnections(VIRTUAL_USER_ALLOW_CONNECTIONS_NONE),
    m_RemainingSaves(GAME_SAVES_PER_PLAYER),
    m_RemainingPauses(GAME_PAUSES_PER_PLAYER),
    m_LeftCode(PLAYERLEAVE_LOBBY),
    m_Name(std::move(nName))
{
}

string CGameVirtualUser::GetLowerName() const
{
  return ToLowerCase(m_Name);
}

string CGameVirtualUser::GetDisplayName(optional<bool> overrideLoaded) const
{
  if (overrideLoaded.value_or(m_Game.get().GetGameLoaded())) {
    return m_Name;
  } else {
    // This information is important for letting hosts know which !open, !close, commands to execute.
    return "User[" + ToDecString(m_SID + TINY_ONE) + "]";
  }
}

bool CGameVirtualUser::GetCanPause() const
{
  if (!(m_AllowedActions & VIRTUAL_USER_ALLOW_ACTIONS_PAUSE)) return false;
  if (m_RemainingPauses == 0) return false;

  // Referees can pause the game without limit.
  // Full observers can never pause the game.
  if (!m_Observer) return true;
  return !m_Observer || m_Game.get().GetHasReferees();
}

bool CGameVirtualUser::GetCanResume() const
{
  if (!(m_AllowedActions & VIRTUAL_USER_ALLOW_ACTIONS_RESUME)) return false;

  // Referees can unpause the game, but full observers cannot.
  return !m_Observer || m_Game.get().GetHasReferees();
}

bool CGameVirtualUser::GetCanSave() const
{
  if (!(m_AllowedActions & VIRTUAL_USER_ALLOW_ACTIONS_SAVE)) return false;
  if (m_RemainingSaves == 0) return false;

  // Referees can save the game without limit.
  // Full observers can never save the game.
  return !m_Observer || m_Game.get().GetHasReferees();
}

bool CGameVirtualUser::GetCanSaveEnded() const
{
  if (!(m_AllowedActions & VIRTUAL_USER_ALLOW_ACTIONS_SAVE)) return false;
  return true;
}

bool CGameVirtualUser::GetCanShare() const
{
  if (!(m_AllowedActions & VIRTUAL_USER_ALLOW_ACTIONS_SHARE_UNITS)) return false;
  return true;
}

bool CGameVirtualUser::GetCanShare(const uint8_t SID) const
{
  if (SID == m_SID) return false;
  if (!(m_AllowedActions & VIRTUAL_USER_ALLOW_ACTIONS_SHARE_UNITS)) return false;
  const CGameController* gameController = m_Game.get().GetGameControllerFromUID(m_UID);
  const CGameSlot* slot = m_Game.get().InspectSlot(SID);
  if (!slot || !m_Game.get().GetIsRealPlayerSlot(SID)) return false;
  switch (m_Game.get().m_Config.m_FakeUsersShareUnitsMode) {
    case FakeUsersShareUnitsMode::kAuto:
      if (!(m_Game.get().GetMap()->GetMapDataSet() == MAP_DATASET_MELEE && (m_Game.get().GetMap()->GetMapFlags() & GAMEFLAG_FIXEDTEAMS))) {
        return false;
      }
      // falls through
    case FakeUsersShareUnitsMode::kTeam: {
      return slot->GetTeam() == gameController->GetTeam();
    }
    case FakeUsersShareUnitsMode::kAll:
    default: { // LAST
      return true;
    }
  }
}

bool CGameVirtualUser::GetCanTrade(const uint8_t /*SID*/) const
{
  if (!(m_AllowedActions & VIRTUAL_USER_ALLOW_ACTIONS_TRADE)) return false;
  return true;
}

bool CGameVirtualUser::GetCanMiniMapSignal(const GameUser::CGameUser* /*user*/) const
{
  if (!(m_AllowedActions & VIRTUAL_USER_ALLOW_ACTIONS_MINIMAP_SIGNAL)) return false;
  return true;
}

vector<uint8_t> CGameVirtualUser::GetPlayerInfoBytes(optional<bool> overrideLoaded) const
{
  const array<uint8_t, 4> IP = {0, 0, 0, 0};
  return GameProtocol::SEND_W3GS_PLAYERINFO(m_Game.get().GetVersion(), m_UID, GetDisplayName(overrideLoaded), IP, IP);
}

vector<uint8_t> CGameVirtualUser::GetGameLoadedBytes() const
{
  return GameProtocol::SEND_W3GS_GAMELOADED_OTHERS(m_UID);
}

vector<uint8_t> CGameVirtualUser::GetGameQuitBytes(const uint8_t leftCode) const
{
  return GameProtocol::SEND_W3GS_PLAYERLEAVE_OTHERS(m_UID, leftCode);
}

void CGameVirtualUser::RefreshUID()
{
  m_OldUID = m_UID;
  m_UID = m_Game.get().GetNewUID();
}
