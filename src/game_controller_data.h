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

#ifndef AURA_GAME_CONTROLLER_DATA_H_
#define AURA_GAME_CONTROLLER_DATA_H_

#include "includes.h"

//
// CGameController
//

class CGameController
{
private:
  GameControllerType                    m_Type;
  bool                                  m_Observer;
  uint8_t                               m_UID;
  uint8_t                               m_SID;
  uint8_t                               m_Color;
  uint8_t                               m_Team;
  GamePlayerResult                      m_GameResult;
  std::string                           m_Name;

  // These are exclusive for GameControllerType::kUser
  std::string                           m_Server;
  std::string                           m_IP;
 
  std::optional<uint64_t>               m_LoadingTime;

  uint8_t                               m_ServerLeftCode;
  std::optional<uint8_t>                m_ClientLeftCode;
  std::optional<uint64_t>               m_LeftGameTime;

public:
  CGameController(const IndexedGameSlot& idxSlot);
  CGameController(const uint8_t type, const IndexedGameSlot& idxSlot);
  CGameController(const GameUser::CGameUser* user, const IndexedGameSlot& idxSlot);
  CGameController(const CGameVirtualUser* virtualUser, const IndexedGameSlot& idxSlot);
  ~CGameController();

  [[nodiscard]] inline GameControllerType                                 GetType() const { return m_Type; }
  [[nodiscard]] inline bool                                               GetIsObserver() const { return m_Observer; }
  [[nodiscard]] inline uint8_t                                            GetUID() const { return m_UID; }
  [[nodiscard]] inline uint8_t                                            GetSID() const { return m_SID; }
  [[nodiscard]] inline uint8_t                                            GetColor() const { return m_Color; }
  [[nodiscard]] inline uint8_t                                            GetTeam() const { return m_Team; }
  [[nodiscard]] inline GamePlayerResult                                   GetGameResult() const { return m_GameResult; }
  [[nodiscard]] inline std::string                                        GetName() const { return m_Name; }

  [[nodiscard]] inline std::string                                        GetServer() const { return m_Server; }
  [[nodiscard]] inline std::string                                        GetIP() const { return m_IP; }

  [[nodiscard]] inline bool                                               GetHasLoadedGame() const { return m_LoadingTime.has_value(); }
  [[nodiscard]] inline uint64_t                                           GetLoadingGameTime() const { return m_LoadingTime.value(); }

  [[nodiscard]] inline uint8_t                                            GetServerLeftCode() const { return m_ServerLeftCode; }
  [[nodiscard]] inline bool                                               GetHasClientLeftCode() const { return m_ClientLeftCode.has_value(); }
  [[nodiscard]] inline uint8_t                                            GetClientLeftCode() const { return m_ClientLeftCode.value(); }
  [[nodiscard]] inline bool                                               GetHasLeftGame() const { return m_LeftGameTime.has_value(); }
  [[nodiscard]] inline uint64_t                                           GetLeftGameTime() const { return m_LeftGameTime.value(); }

  inline void SetGameResult(GamePlayerResult nGameResult) { m_GameResult = nGameResult; }

  inline void SetLoadingTime(uint64_t nLoadingTime) { m_LoadingTime = nLoadingTime; }

  inline void SetServerLeftCode(uint8_t nServerLeftCode) { m_ServerLeftCode = nServerLeftCode; }
  inline void SetClientLeftCode(uint8_t nClientLeftCode) { m_ClientLeftCode = nClientLeftCode; }
  inline void SetLeftGameTime(uint64_t nGameTime) { m_LeftGameTime = nGameTime; }

  [[nodiscard]] std::string GetShortName() const;

  [[nodiscard]] std::string static GetAIName(uint8_t nDifficulty);
  [[nodiscard]] std::string static GetAMAIName(uint8_t nDifficulty);
};

struct TestCD
{
  bool owo;
};

#endif // AURA_GAME_CONTROLLER_DATA_H_
