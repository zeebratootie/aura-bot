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

#include "game_slot.h"

using namespace std;

//
// CGameSlot
//

CGameSlot::CGameSlot(const std::vector<uint8_t>& n)
  : m_Type(SLOTTYPE_AUTO),
    m_UID(0),
    m_DownloadStatus(255),
    m_SlotStatus(SLOTSTATUS_OPEN),
    m_Computer(0),
    m_Team(0),
    m_Color(1),
    m_Race(SLOTRACE_RANDOM),
    m_ComputerType(SLOTCOMP_NORMAL),
    m_Handicap(100)
{
  const size_t size = n.size();
  if (size < 7) return;

  switch (size) {
    case 10:
      m_Type = n[9];
      // falls through
    case 9:
      m_Handicap = n[8];
      // falls through
    case 8:
      m_ComputerType = n[7];
      // falls through
    default:
      m_UID            = n[0];
      m_DownloadStatus = n[1];
      m_SlotStatus     = n[2];
      m_Computer       = n[3];
      m_Team           = n[4];
      m_Color          = n[5];
      m_Race           = n[6];
  }
}

CGameSlot::CGameSlot(const uint8_t nType, const uint8_t nUID, const uint8_t nDownloadStatus, const uint8_t nSlotStatus, const uint8_t nComputer, const uint8_t nTeam, const uint8_t nColor, const uint8_t nRace, const uint8_t nComputerType, const uint8_t nHandicap)
  : m_Type(nType),
    m_UID(nUID),
    m_DownloadStatus(nDownloadStatus),
    m_SlotStatus(nSlotStatus),
    m_Computer(nComputer),
    m_Team(nTeam),
    m_Color(nColor),
    m_Race(nRace),
    m_ComputerType(nComputerType),
    m_Handicap(nHandicap)
{
}

CGameSlot::~CGameSlot() = default;

vector<uint8_t> CGameSlot::GetProtocolArray(const uint8_t sentinelObserverValue, const uint8_t actualObserverValue) const
{
  vector<uint8_t> bytes = GetProtocolArray();
  if (sentinelObserverValue != actualObserverValue) {
    for (size_t i = 4; i <= 5; ++i) { //  team, color
      if (bytes[i] == sentinelObserverValue) {
        bytes[i] = actualObserverValue;
      }
    }
  }
  return bytes;
}

// CGameSlotsConfig

CGameSlotsConfig::CGameSlotsConfig()
 : layout(0),
   observerSentinel(0)
{
}

CGameSlotsConfig::CGameSlotsConfig(uint8_t nLayout, uint8_t nObserverSentinel)
 : layout(nLayout),
   observerSentinel(nObserverSentinel)
{
}

CGameSlotsConfig::~CGameSlotsConfig()
{
}

uint8_t CGameSlotsConfig::GetOccupiedCount() const
{
  uint8_t count = 0;
  for (const auto& slot : slots) {
    if (slot.GetSlotStatus() == SLOTSTATUS_OCCUPIED)
      ++count;
  }
  return count;
}

uint8_t CGameSlotsConfig::GetOpenCount() const
{
  uint8_t count = 0;
  for (const auto& slot : slots) {
    if (slot.GetSlotStatus() == SLOTSTATUS_OPEN)
      ++count;
  }
  return count;
}

bool CGameSlotsConfig::GetIsAnyOpen() const
{
  for (const auto& slot : slots) {
    if (slot.GetSlotStatus() == SLOTSTATUS_OPEN)
      return true;
  }
  return false;
}

uint8_t CGameSlotsConfig::GetOccupiedControllersCount() const
{
  uint8_t count = 0;
  for (const auto& slot : slots) {
    if (slot.GetSlotStatus() == SLOTSTATUS_OCCUPIED && slot.GetTeam() != observerSentinel) {
      ++count;
    }
  }
  return count;
}

uint8_t CGameSlotsConfig::GetComputersCount() const
{
  uint8_t count = 0;
  for (const auto& slot : slots) {
    if (slot.GetSlotStatus() == SLOTSTATUS_OCCUPIED && slot.GetIsComputer()) {
      ++count;
    }
  }
  return count;
}

uint8_t CGameSlotsConfig::GetOccupiedTeamsCount() const
{
  std::bitset<MAX_SLOTS_MODERN> teams;
  for (const auto& slot : slots) {
    if (slot.GetSlotStatus() != SLOTSTATUS_OCCUPIED) continue;
    if (slot.GetTeam() == observerSentinel) continue;
    teams.set(slot.GetTeam());
  }
  return static_cast<uint8_t>(teams.count());
}

bool CGameSlotsConfig::GetHasAnyActiveTeam() const
{
  std::bitset<MAX_SLOTS_MODERN> usedTeams;
  for (const auto& slot : slots) {
    const uint8_t team = slot.GetTeam();
    if (team == observerSentinel) continue;
    if (slot.GetSlotStatus() == SLOTSTATUS_OCCUPIED) {
      if (usedTeams.test(team)) {
        return true;
      } else {
        usedTeams.set(team);
      }
    }
  }
  return false;
}

bool CGameSlotsConfig::GetIsObserver(const size_t SID) const
{
  return slots[SID].GetTeam() == observerSentinel;
}

bool CGameSlotsConfig::GetIsOpen(const size_t SID) const
{
  return slots[SID].GetSlotStatus() == SLOTSTATUS_OPEN;
}

bool CGameSlotsConfig::GetIsClosed(const size_t SID) const
{
  return slots[SID].GetSlotStatus() == SLOTSTATUS_CLOSED;
}

bool CGameSlotsConfig::GetIsOccupied(const size_t SID) const
{
  return slots[SID].GetSlotStatus() == SLOTSTATUS_OCCUPIED;
}

bool CGameSlotsConfig::GetIsComputer(const size_t SID) const
{
  return slots[SID].GetIsComputer();
}
