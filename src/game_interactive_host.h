/*

  Copyright [2025] [Leonardo Julca]

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

#ifndef AURA_GAME_INTERACTIVE_HOST_H_
#define AURA_GAME_INTERACTIVE_HOST_H_

#include "includes.h"
#include "list.h"
#include "protocol/game_protocol.h"

struct GameInteraction
{
  uint8_t status;
  std::string args;

  GameInteraction()
   : status(GAME_INTERACTION_STATUS_PENDING)
  {
  }

  ~GameInteraction()
  {
  }

  inline bool GetIsPending() const { return status == GAME_INTERACTION_STATUS_PENDING; }
  inline bool GetIsRunning() const { return status == GAME_INTERACTION_STATUS_RUNNING; }
  inline bool GetIsDone() const { return status == GAME_INTERACTION_STATUS_DONE; }
  inline void SetRunning() { status = GAME_INTERACTION_STATUS_RUNNING; }
  inline void SetDone() { status = GAME_INTERACTION_STATUS_DONE; }

  inline const std::string& GetArgs() const { return args; }
  inline void SetArgs(std::string_view nArgs) { args = std::string(nArgs); }
};

class CGameInteractiveHost
{
  std::weak_ptr<CGame>                    m_Game;
  bool                                    m_Error;
  std::string                             m_FileName;
  std::map<uint32_t, GameInteraction>     m_Interactions;

public:
  CGameInteractiveHost(std::shared_ptr<CGame> nGame, const std::string& fileName);
  ~CGameInteractiveHost();

  [[nodiscard]] inline bool GetIsGameExpired() const { return m_Game.expired(); }
  [[nodiscard]] inline std::shared_ptr<CGame> GetGame() const { return m_Game.lock(); }
  [[nodiscard]] std::string GetProcedureDescription(const uint32_t interactionId, const uint32_t procedureType, std::string_view key);
  [[nodiscard]] bool CheckInitInstance(const uint32_t interactionId, const uint32_t procedureType, std::string_view key);
  [[nodiscard]] bool InitInstance(const uint32_t interactionId);
  void Send(const std::string& message);
  void SendResult(const uint32_t interactionId, const std::string& result);
  void ResolveInteraction(std::pair<const uint32_t, GameInteraction>&, const std::string& result);
  bool EventGameCacheInteger(const uint8_t UID, const std::string_view fileName, const std::string_view missionKey, const std::string_view key, const uint32_t value);
  std::string GetLogPrefix() const;
  [[nodiscard]] static long GetMapTime();
};

#endif // AURA_GAME_INTERACTIVE_HOST_H_
