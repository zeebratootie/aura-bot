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

#include "game_controller_data.h"
#include "game_slot.h"
#include "game_user.h"
#include "game_virtual_user.h"

using namespace std;

//
// CGameController
//

CGameController::CGameController(const IndexedGameSlot& idxSlot)
  : m_Type(GameControllerType::kComputer),
    m_Observer(false),
    m_UID(idxSlot.second->GetUID()),
    m_SID(idxSlot.first),
    m_Color(idxSlot.second->GetColor()),
    m_Team(idxSlot.second->GetTeam()),
    m_GameResult(GamePlayerResult::kUndecided),
    m_Name(CGameController::GetAIName(idxSlot.second->GetComputerType())),
    m_LoadingTime(0)
{
}

CGameController::CGameController(const uint8_t type, const IndexedGameSlot& idxSlot)
  : m_Type(GameControllerType::kComputer),
    m_Observer(false),
    m_UID(idxSlot.second->GetUID()),
    m_SID(idxSlot.first),
    m_Color(idxSlot.second->GetColor()),
    m_Team(idxSlot.second->GetTeam()),
    m_GameResult(GamePlayerResult::kUndecided),
    m_Name(type == AI_TYPE_AMAI ? CGameController::GetAMAIName(idxSlot.second->GetComputerType()) : CGameController::GetAIName(idxSlot.second->GetComputerType())),
    m_LoadingTime(0)
{
}

CGameController::CGameController(const GameUser::CGameUser* user, const IndexedGameSlot& idxSlot)
  : m_Type(GameControllerType::kUser),
    m_Observer(user->GetIsObserver()),
    m_UID(idxSlot.second->GetUID()),
    m_SID(idxSlot.first),
    m_Color(idxSlot.second->GetColor()),
    m_Team(idxSlot.second->GetTeam()),
    m_GameResult(GamePlayerResult::kUndecided),
    m_Name(user->GetName()),
    m_Server(user->GetRealmHostName()),
    m_IP(user->GetIPStringStrict()),
    m_LoadingTime(0)
{
}

CGameController::CGameController(const CGameVirtualUser* virtualUser, const IndexedGameSlot& idxSlot)
  : m_Type(GameControllerType::kVirtual),
    m_Observer(virtualUser->GetIsObserver()),
    m_UID(idxSlot.second->GetUID()),
    m_SID(idxSlot.first),
    m_Color(idxSlot.second->GetColor()),
    m_Team(idxSlot.second->GetTeam()),
    m_GameResult(GamePlayerResult::kUndecided),
    m_Name(virtualUser->GetName()),
    m_LoadingTime(0)
{
}

CGameController::~CGameController()
{
}

string CGameController::GetAIName(uint8_t nDifficulty)
{
  switch (nDifficulty) {
    case SLOTCOMP_EASY: return "Computer (Easy)";
    case SLOTCOMP_NORMAL: return "Computer (Normal)";
    case SLOTCOMP_HARD: return "Computer (Insane)";
    default: return "Computer";
  }
}

string CGameController::GetAMAIName(uint8_t nDifficulty)
{
  switch (nDifficulty) {
    case SLOTCOMP_EASY: return "AMAI Easy";
    case SLOTCOMP_NORMAL: return "AMAI Normal";
    case SLOTCOMP_HARD: return "AMAI Insane";
    default: return "AMAI";
  }
}

string CGameController::GetShortName() const
{
  if (m_Type == GameControllerType::kComputer) return "AI";
  return m_Name;
}
