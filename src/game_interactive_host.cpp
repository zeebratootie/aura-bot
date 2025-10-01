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

#include "game_interactive_host.h"
#include "game.h"
#include "game_user.h"

using namespace std;

// GameInteraction

//
// CGameInteractiveHost
//

CGameInteractiveHost::CGameInteractiveHost(shared_ptr<CGame> game, const string& fileName)
 : m_Game(game),
   m_Error(false),
   m_FileName(fileName)
{
}

CGameInteractiveHost::~CGameInteractiveHost()
{
}

string CGameInteractiveHost::GetProcedureDescription(const uint32_t interactionId, const uint32_t procedureType, string_view key)
{
  string description = Concat("procedure ", to_string(procedureType));
  if (procedureType == W3HMC_PROCEDURE_EXEC) {
    optional<uint32_t> maybeRequestType = ToUint32(key);
    description.append(Concat(" request type ", to_string(*maybeRequestType)));
  } else {
    description.append(Concat(" value ", EnsureWrapUTF8(key)));
  }

  auto instanceMatch = m_Interactions.find(interactionId);
  if (instanceMatch == m_Interactions.end()) {
    description.append("uninitialized interaction #");
  } else if (instanceMatch->second.GetIsDone()) {
    description.append("finished interaction #");
  } else if (instanceMatch->second.GetIsRunning()) {
    description.append("running interaction #");
  } else {
    description.append("pending interaction #");
  }
  description.append(to_string(interactionId));
  return description;
}

bool CGameInteractiveHost::CheckInitInstance(const uint32_t interactionId, const uint32_t procedureType, string_view key)
{
  bool isInit = false;
  if (procedureType == W3HMC_PROCEDURE_EXEC) {
    optional<uint32_t> maybeRequestType = ToUint32(key);
    if (!maybeRequestType.has_value()) {
      Print(Concat(GetLogPrefix(), "error - key cannot be parsed as an integer"));
      m_Error = true;
      return true;
    }
    isInit = (*maybeRequestType == W3HMC_REQUEST_INIT);
  }

  auto instanceMatch = m_Interactions.find(interactionId);
  if (instanceMatch == m_Interactions.end()) {
    if (!isInit) {
      Print(Concat(GetLogPrefix(), "error - got ", GetProcedureDescription(interactionId, procedureType, key)));
    }
  } else if (isInit) {
    if (!InitInstance(interactionId)) {
      m_Error = true;
    }
    return true;
  }
  return false;
}

bool CGameInteractiveHost::InitInstance(const uint32_t interactionId)
{
  m_Interactions[interactionId];
  return true;
}

void CGameInteractiveHost::Send(const string& message)
{
  if (auto game = GetGame()) {
    game->SendHMC(message);
  }
}

void CGameInteractiveHost::SendResult(const uint32_t instance, const string& result)
{
  Send(Concat(to_string(instance), " ", result));
}

void CGameInteractiveHost::ResolveInteraction(pair<const uint32_t, GameInteraction>& entry, const string& result)
{
  SendResult(entry.first, result);
  entry.second.SetDone();
}

bool CGameInteractiveHost::EventGameCacheInteger(const uint8_t fromUID, const string_view fileName, const string_view missionKey, const string_view key, const uint32_t cacheValue)
{
  if (m_Error) {
    return !m_Error;
  }

  if (fileName != m_FileName) {
    return !m_Error;
  }

  optional<uint32_t> maybeInteractionId = ToUint32(missionKey);
  if (!maybeInteractionId.has_value()) {
    Print(Concat(GetLogPrefix() + "error - mission key cannot be parsed as an integer"));
    m_Error = true;
    return !m_Error;
  }

  if (GetIsGameExpired()) {
    m_Error = true;
    return !m_Error;
  }

  uint32_t interactionId = *maybeInteractionId;
  if (CheckInitInstance(interactionId, cacheValue, key)) {
    // Also checks that key is a numeral if cacheValue is W3HMC_PROCEDURE_EXEC 
    return !m_Error;
  }

  auto instanceMatch = m_Interactions.find(interactionId);
  if (!instanceMatch->second.GetIsPending()) {
    // If the interactionId is already running we ignore duplicate requests.
    // This lets the player with the lowest latency start requests.
    Print(Concat(GetLogPrefix(), "error - got ", GetProcedureDescription(interactionId, cacheValue, key)));
    // recoverable
    return !m_Error;
  }

  switch (cacheValue) {
    case W3HMC_PROCEDURE_SET_ARGS:
      instanceMatch->second.SetArgs(key);
      Print(Concat(GetLogPrefix(), "interaction #", to_string(interactionId), " arguments set to <<", key, ">>"));
      break;
    case W3HMC_PROCEDURE_EXEC: {
      optional<uint32_t> maybeRequestType = ToUint32(key);
      Print(Concat(GetLogPrefix(), "info - got ", GetProcedureDescription(interactionId, cacheValue, key)));
      switch (*maybeRequestType) {
        case W3HMC_REQUEST_PLAYERREALM: {
          const GameUser::CGameUser* user = GetGame()->GetUserFromUID(fromUID);
          ResolveInteraction(*instanceMatch, string(user->GetRealmHostName()));
          break;
        }
        case W3HMC_REQUEST_HTTP: {
          ResolveInteraction(*instanceMatch, "Forbidden.");
          break;
        }
        case W3HMC_REQUEST_DATETIME: {
          const bool isGameStart = false;
          if (isGameStart) {
            ResolveInteraction(*instanceMatch, to_string(GetGame()->GetMapGameStartTime()));
          } else {
            ResolveInteraction(*instanceMatch, to_string(CGameInteractiveHost::GetMapTime()));
          }
          break;
        }
        default:
          Print(Concat(GetLogPrefix(), "error - request type ", to_string(*maybeRequestType), " is not valid."));
          break;
      }
      break;
    }
    default:
      Print(Concat(GetLogPrefix(), "unknown procedure [", to_string(cacheValue), "] found, ignoring"));
  }

  return !m_Error;
}

string CGameInteractiveHost::GetLogPrefix() const
{
  if (auto game = GetGame()) {
    return Concat("[W3HMC: ", game->GetGameName(), "] ");
  } else {
    return "[W3HMC] ";
  }
}

long CGameInteractiveHost::GetMapTime()
{
  auto now = chrono::system_clock::to_time_t(chrono::system_clock::now());
  return (long)now;
}
