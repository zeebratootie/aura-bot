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

#include "realm_games.h"

#include "protocol/bnet_protocol.h"
#include "includes.h"
#include "aura.h"
#include "action.h"
#include "map.h"
#include "game_host.h"
#include "game_setup.h"

using namespace std;

//
// GameSearchQuery
//

GameSearchQuery::GameSearchQuery()
 : m_CallbackType(GameSearchQueryCallback::kNone)
{
}

GameSearchQuery::GameSearchQuery(const string& gameName, const string& hostName, shared_ptr<CMap> map)
 : m_Map(map),
   m_GameName(gameName),
   m_HostName(hostName),
   m_CallbackType(GameSearchQueryCallback::kNone)
{
}

GameSearchQuery::~GameSearchQuery()
{
}

bool GameSearchQuery::GetIsMatch(const Version& gameVersion, const NetworkGameInfo& gameInfo) const
{
  if (!m_GameName.empty() && gameInfo.GetGameName() != m_GameName) {
    //Print("[SEARCH] Game names do not match");
    return false;
  }
  if (!m_HostName.empty() && gameInfo.GetHostName() != m_HostName) {
    //Print("[SEARCH] Game hosts do not match");
    return false;
  }
  if (m_Map) {
    vector<string> mismatchReasons;
    if (!m_Map->MatchMapScriptsBlizz(gameVersion, gameInfo.GetMapScriptsBlizzHash()).value_or(true)) {
      mismatchReasons.push_back("maps are different (registry expects blizz hash: " + ByteArrayToDecString(gameInfo.GetMapScriptsBlizzHash()) + ")");
    }
    if (gameInfo.GetHasSHA1() && !m_Map->MatchMapScriptsSHA1(gameVersion, gameInfo.GetMapScriptsSHA1()).value_or(true)) {
      mismatchReasons.push_back("maps are different (registry expects sha1 hash: " + ByteArrayToDecString(gameInfo.GetMapScriptsSHA1()) + ")");
    }
    if (!CaseInsensitiveEquals(m_Map->GetClientFileName(), gameInfo.GetMapClientFileName())) {
      mismatchReasons.push_back("filenames are different (registry: [" + gameInfo.GetMapClientFileName() + "] vs local: [" + m_Map->GetClientFileName() + "]");
    }
    if (!mismatchReasons.empty()) {
      Print(
        "[SEARCH] Found game [" + string(gameInfo.GetGameName()) + "] by [" + string(gameInfo.GetHostName()) + "], "
        "but " + JoinStrings(mismatchReasons) + "."
      );
      return false;
    }
  }
  return true;
}

bool GameSearchQuery::EventMatch(const NetworkGameInfo& gameInfo)
{
  switch (m_CallbackType) {
    case GameSearchQueryCallback::kHostActiveMirror: {
      auto gameSetup = m_CallbackTarget.lock();
      if (!gameSetup || !gameSetup->GetIsActive()) return false;
      auto sourceRealm = gameSetup->GetMirror().GetSourceRealm();
      string hostName = string(gameInfo.GetHostName());
      if (sourceRealm && sourceRealm->GetIsCryptoHost(hostName)) {
        Print("[SEARCH] Found game [" + string(gameInfo.GetGameName()) + "] by [" + hostName + "], but host is encrypted and it cannot be mirrored.");
        gameSetup->m_Aura->m_GameSetup.reset();
        return false;
      }
      if (m_Map) m_Map->SetGameConvertedFlags(gameInfo.GetGameFlags());
      gameSetup->GetMirror().SetRawSource(gameInfo.m_Host);
      AppAction hostAction = AppAction(AppActionType::kHostActiveMirror, AppActionMode::kNone);
      gameSetup->m_Aura->m_PendingActions.push(hostAction);
      return false; // stop searching
    }
    case GameSearchQueryCallback::kNone: {
      return true;
    }
    IGNORE_ENUM_LAST(GameSearchQueryCallback)
    default:
      return false;
  }
}

void GameSearchQuery::SetCallback(const GameSearchQueryCallback callbackType, shared_ptr<CGameSetup> callbackTarget)
{
  m_CallbackType = callbackType;
  m_CallbackTarget = callbackTarget;
}
