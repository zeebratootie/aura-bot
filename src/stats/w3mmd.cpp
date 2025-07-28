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

   Copyright [2008] [Trevor Hogan]

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

   CODE PORTED FROM THE ORIGINAL GHOST PROJECT: http://ghost.pwner.org/

*/

#include "w3mmd.h"
#include "../game_controller_data.h"
#include "../game_result.h"
#include "../config/config_game.h"
#include "../game.h"
#include "../protocol/game_protocol.h"
#include "../game_slot.h"
#include "../util.h"

using namespace std;

//
// CW3MMDAction
//

CW3MMDAction::CW3MMDAction(shared_ptr<CGame> nGame, uint8_t nFromUID, uint32_t nID, uint8_t nType, uint8_t nSubType, uint8_t nSID)
  : m_Ticks(nGame->GetEffectiveTicks()),
    m_UpdateID(nID),
    m_Type(nType),
    m_SubType(nSubType),
    m_FromUID(nFromUID),
    m_FromColor(nGame->GetColorFromUID(nFromUID)),
    m_SID(nSID)
{
}

CW3MMDAction::~CW3MMDAction()
{
}

//
// CW3MMDDefinition
//

CW3MMDDefinition::CW3MMDDefinition(shared_ptr<CGame> nGame, uint8_t nFromUID, uint32_t nID, uint8_t nType, uint8_t nSubType, uint8_t nSID)
  : m_Ticks(nGame->GetEffectiveTicks()),
    m_UpdateID(nID),
    m_Type(nType),
    m_SubType(nSubType),
    m_FromUID(nFromUID),
    m_FromColor(nGame->GetColorFromUID(nFromUID)),
    m_SID(nSID)
{
}

CW3MMDDefinition::~CW3MMDDefinition()
{
}

//
// CW3MMD
//

CW3MMD::CW3MMD(shared_ptr<CGame> nGame)
  : m_Game(ref(*nGame)),
    m_GameOver(false),
    m_Error(false),
    m_Version(0),
    m_LastValueID(0)
    //m_NextCheckID(0)
{
  m_ResultVerbs[(uint8_t)GamePlayerResult::kLoser] = "lost";
  m_ResultVerbs[(uint8_t)GamePlayerResult::kDrawer] = "drew";
  m_ResultVerbs[(uint8_t)GamePlayerResult::kWinner] = "won";
}

CW3MMD::~CW3MMD()
{
}

shared_ptr<CGame> CW3MMD::GetGame()
{
  return m_Game.get().shared_from_this();
}

bool CW3MMD::HandleTokens(uint8_t fromUID, uint32_t valueID, vector<string> Tokens)
{
  if (Tokens.empty()) {
    return false;
  }
  const string& actionType = Tokens[0];
  if (actionType == "init" && Tokens.size() >= 2) {
    if (Tokens[1] == "version" && Tokens.size() == 4) {
      // Tokens[2] = minimum
      // Tokens[3] = current

      optional<uint32_t> version = ToUint32(Tokens[2]);
      if (!version.has_value()) return false;
      optional<uint32_t> minVersion = ToUint32(Tokens[3]);
      if (!minVersion.has_value()) return false;
      if (version.value() > 1) {
        Print(GetLogPrefix() + "error - map requires MMD parser version " + SanitizeStringUTF8(Tokens[2]) + " or higher (using version 1)");
        m_Error = true;
      } else {
        Print(GetLogPrefix() + "map is using Warcraft 3 Map Meta Data library version " + SanitizeStringUTF8(Tokens[3]));
        m_Version = *version;
      }
    } else if (Tokens[1] == "pid" && Tokens.size() == 4) {
      // Tokens[2] = pid
      // Tokens[3] = name
      optional<uint32_t> SID = ToUint32(Tokens[2]);
      if (!SID.has_value()) return false;

      CW3MMDDefinition* def = new CW3MMDDefinition(GetGame(), fromUID, valueID, MMD_DEFINITION_TYPE_INIT, MMD_INIT_TYPE_PLAYER, (uint8_t)*SID);
      if (m_Game.get().m_Config.m_UnsafeNameHandler == OnUnsafeNameHandler::kCensorMayDesync) {
        def->SetName(CIncomingJoinRequest::CensorName(Tokens[3], m_Game.get().m_Config.m_PipeConsideredHarmful));
      } else {
        def->SetName(Tokens[3]);
      }
      m_DefQueue.push(def);
    }
  } else if (actionType == "DefVarP" && Tokens.size() == 5) {
    // Tokens[1] = name
    // Tokens[2] = value type
    // Tokens[3] = goal type (ignored here)
    // Tokens[4] = suggestion (ignored here)

    uint8_t subType;
    if (Tokens[2] == "int") {
      subType = MMD_VALUE_TYPE_INT;
    } else if (Tokens[2] == "real") {
      subType = MMD_VALUE_TYPE_REAL;
    } else if (Tokens[2] == "string") {
      subType = MMD_VALUE_TYPE_STRING;
    } else {
      Print(GetLogPrefix() + "invalid DefVarP type " + SanitizeStringUTF8(Tokens[2]) + " found, ignoring");
      return false;
    }
    CW3MMDDefinition* def = new CW3MMDDefinition(GetGame(), fromUID, valueID, MMD_DEFINITION_TYPE_VAR, subType);
    def->SetName(Tokens[1]);
    m_DefQueue.push(def);
  } else if (actionType == "VarP" && Tokens.size() == 5) {
    // Tokens[1] = pid
    // Tokens[2] = name
    // Tokens[3] = operation
    // Tokens[4] = value

    optional<uint32_t> SID = ToUint32(Tokens[1]);
    if (!SID.has_value()) {
      Print(GetLogPrefix() + "VarP " + SanitizeStringUTF8(Tokens[2]) + " has invalid SID " + SanitizeStringUTF8(Tokens[1]) + ", ignoring");
      return false;
    }
    uint8_t subType = 0xFFu;
    if (Tokens[3] == "=") {
      subType = MMD_OPERATOR_SET;
    } else if (Tokens[3] == "+=") {
      subType = MMD_OPERATOR_ADD;
    } else if (Tokens[3] == "-=") {
      subType = MMD_OPERATOR_SUBTRACT;
    } else {
      Print(GetLogPrefix() + "unknown VarP operation " + SanitizeStringUTF8(Tokens[3]) + " found, ignoring");
      return false;
    }
    CW3MMDAction* action = new CW3MMDAction(GetGame(), fromUID, valueID, MMD_ACTION_TYPE_VAR, subType, (uint8_t)*SID);
    action->SetName(Tokens[2]);
    action->AddValue(Tokens[4]);
    m_ActionQueue.push(action);
  } else if (actionType == "FlagP" && Tokens.size() == 3) {
    // Tokens[1] = pid
    // Tokens[2] = flag

    optional<uint32_t> SID = ToUint32(Tokens[1]);
    if (!SID.has_value()) {
      Print(GetLogPrefix() + "FlagP " + SanitizeStringUTF8(Tokens[2]) + " has invalid SID " + SanitizeStringUTF8(Tokens[1]) + ", ignoring");
      return false;
    }

    uint8_t subType = 0xFFu;
    if (Tokens[2] == "leaver") {
      //m_FlagsLeaver[*SID] = true;
      subType = MMD_FLAG_LEAVER;
    } else if (Tokens[2] == "practicing") {
      //m_FlagsPracticing[*SID] = true;
      subType = MMD_FLAG_PRACTICE;
    } else if (Tokens[2] == "drawer") {
      //m_FlagsPracticing[*SID] = true;
      subType = MMD_FLAG_DRAWER;
    } else if (Tokens[2] == "winner") {
      //m_FlagsPracticing[*SID] = true;
      subType = MMD_FLAG_WINNER;
    } else if (Tokens[2] == "loser") {
      //m_FlagsPracticing[*SID] = true;
      subType = MMD_FLAG_LOSER;
    } else {
      Print(GetLogPrefix() + "unknown flag " + SanitizeStringUTF8(Tokens[2]) + " found, ignoring");
      return false;
    }

    CW3MMDAction* action = new CW3MMDAction(GetGame(), fromUID, valueID, MMD_ACTION_TYPE_FLAG, subType, (uint8_t)*SID);
    m_ActionQueue.push(action);
  } else if (actionType == "DefEvent" && Tokens.size() >= 4) {
    // Tokens[1] = name
    // Tokens[2] = # of arguments (n)
    // Tokens[3..n+3] = arguments
    // Tokens[n+3] = format

    optional<uint32_t> arity = ToUint32(Tokens[2]);
    if (!arity.has_value() || arity.value() > MMD_MAX_ARITY) {
      Print(GetLogPrefix() + "DefEvent invalid arity " + SanitizeStringUTF8(Tokens[2]) + " found, ignoring");
      return false;
    }
    if (Tokens.size() != arity.value() + 4) {
      Print(GetLogPrefix() + "DefEvent " + SanitizeStringUTF8(Tokens[2]) + " tokens missing, ignoring");
      return false;
    }
    CW3MMDDefinition* def = new CW3MMDDefinition(GetGame(), fromUID, valueID, MMD_DEFINITION_TYPE_EVENT, (uint8_t)*arity);
    def->SetName(Tokens[1]);
    uint8_t i = 2;
    while (++i < Tokens.size()) {
      def->AddValue(Tokens[i]);
    }
    m_DefQueue.push(def);
  } else if (actionType == "Event" && Tokens.size() >= 2) {
    // Tokens[1] = name
    // Tokens[2..n+2] = arguments (where n is the # of arguments in the corresponding DefEvent)
    CW3MMDAction* action = new CW3MMDAction(GetGame(), fromUID, valueID, MMD_ACTION_TYPE_EVENT, 0);
    uint8_t i = 1;
    action->SetName(Tokens[i]);    
    while (++i < Tokens.size()) {
      action->AddValue(Tokens[i]);
    }
    m_ActionQueue.push(action);
  } else if (actionType == "Blank") {
    // ignore
  } else if (actionType == "Custom") {
    LogMetaData(m_Game.get().GetEffectiveTicks(), "custom: " + SanitizeStringUTF8(JoinStrings(Tokens)));
  } else {
    LogMetaData(m_Game.get().GetEffectiveTicks(), "unknown action type " + SanitizeStringUTF8(actionType) + " found, ignoring");
  }
  return true;
}

bool CW3MMD::EventGameCacheInteger(const uint8_t fromUID, const std::string& fileName, const std::string& missionKey, const std::string& key, const uint32_t /*cacheValue*/)
{
  if (m_Error) {
    return !m_Error;
  }

  if (fileName != "MMD.Dat") {
    return !m_Error;
  }

  if (missionKey.size() < 4) {
    Print(GetLogPrefix() + "unknown mission key [" + SanitizeStringUTF8(missionKey) + "] found, ignoring");
    return !m_Error;
  }

  // Print("[W3MMD] DEBUG: mkey [" + missionKey + "], key [" + KeyString + "], value [" + to_string(value) + "]");

  if (missionKey.compare(0, 4, "val:") == 0) {
    string ValueIDString = missionKey.substr(4);
    optional<uint32_t> ValueID = ToUint32(ValueIDString);
    vector<string> Tokens = TokenizeKey(key);
    if (!ValueID.has_value() || !HandleTokens(fromUID, ValueID.value(), Tokens)) {
      Print(GetLogPrefix() + "error parsing " + SanitizeStringUTF8(key));
    }
  } else if (missionKey.compare(0, 4, "chk:") == 0) {
    /*
    string CheckIDString = missionKey.substr(4);
    optional<uint32_t> CheckID = ToUint32(CheckIDString);

    // todotodo: cheat detection

     ++m_NextCheckID;
     */
  } else {
    Print(GetLogPrefix() + "unknown mission key " + SanitizeStringUTF8(missionKey) + " found, ignoring");
  }

  return !m_Error;
}

bool CW3MMD::ProcessDefinition(CW3MMDDefinition* definition)
{
  if (definition->GetType() == MMD_DEFINITION_TYPE_INIT) {
    if (definition->GetSubType() == MMD_INIT_TYPE_PLAYER) { // "pid"
      const uint8_t SID = definition->GetSID();
      const CGameController* controllerData = m_Game.get().GetGameControllerFromSID(SID);
      if (!controllerData) {
        Print(GetLogPrefix() + "cannot initialize player slot " + ToDecString(SID));
        return false;
      }
      if (controllerData->GetIsObserver()) {
        Print(GetLogPrefix() + "cannot initialize observer slot " + ToDecString(SID));
        return false;
      }
      const bool found = m_SIDToName.find(SID) != m_SIDToName.end();
      if (found) {
        Print(
          GetLogPrefix() + "Player [" + GetSenderName(definition) + "] overrode previous name " + SanitizeStringUTF8(m_SIDToName[SID]) +
          " with new name " + SanitizeStringUTF8(definition->GetName()) + " for SID [" + ToDecString(SID) + "]"
        );
      } else {
        Print(
          GetLogPrefix() + "Player [" + GetSenderName(definition) + "] initialized player ID [" + ToDecString(SID) +
          "] as " + SanitizeStringUTF8(definition->GetName())
        );
      }
      
      if (!found && m_SIDToName.size() >= m_Game.get().GetNumControllers()) {
        Print(GetLogPrefix() + "too many players initialized");
        return false;
      }
      m_SIDToName[SID] = definition->GetName(); // W3MMD will report the player name as seen by the WC3 client
      m_SIDToColor[SID] = controllerData->GetColor();
    }
    return true;
  } else if (definition->GetType() == MMD_DEFINITION_TYPE_VAR) { // DefVarP
    if (m_DefVarPs.find(definition->GetName()) != m_DefVarPs.end()) {
      Print(GetLogPrefix() + "duplicate DefVarP " + SanitizeStringUTF8(definition->GetName()) + " found, ignoring");
      return false;
    }
    if (definition->GetSubType() == MMD_VALUE_TYPE_INT) {
      m_DefVarPs[definition->GetName()] = MMD_VALUE_TYPE_INT;
    } else if (definition->GetSubType() == MMD_VALUE_TYPE_REAL) {
      m_DefVarPs[definition->GetName()] = MMD_VALUE_TYPE_REAL;
    } else { // if (definition->GetSubType() == MMD_VALUE_TYPE_STRING)
      m_DefVarPs[definition->GetName()] = MMD_VALUE_TYPE_STRING;
    }
    return true;
  } else { // if (definition->GetType() == MMD_DEFINITION_TYPE_EVENT) // DefEvent
    if (m_DefEvents.find(definition->GetName()) != m_DefEvents.end()) {
      Print(GetLogPrefix() + "duplicate DefEvent " + SanitizeStringUTF8(definition->GetName()) + " found, ignoring");
      return false;
    }
    m_DefEvents[definition->GetName()] = definition->CopyValues();
    return true;
  }
}

bool CW3MMD::ProcessAction(CW3MMDAction* action)
{
  if (action->GetType() == MMD_ACTION_TYPE_FLAG) {
    if (m_SIDToName.find(action->GetSID()) == m_SIDToName.end()) {
      Print(GetLogPrefix() + "FlagP " + SanitizeStringUTF8(action->GetName()) + " has undefined SID [" + ToDecString(action->GetSID()) + "], ignoring");
      return false;
    }
    GamePlayerResult result = GamePlayerResult::kUndecided;
    switch (action->GetSubType()) {
      case MMD_FLAG_LEAVER: {
        m_FlagsLeaver[action->GetSID()] = true;
        break;
      }
      case MMD_FLAG_PRACTICE: {
        m_FlagsPracticing[action->GetSID()] = true;
        break;
      }
      case MMD_FLAG_DRAWER: {
        result = GamePlayerResult::kDrawer;
        break;
      }
      case MMD_FLAG_WINNER: {
        result = GamePlayerResult::kWinner;
        break;
      }
      default: {
        result = GamePlayerResult::kLoser;
        break;
      }
    }
    if (result == GamePlayerResult::kUndecided) {
      return true;
    }
    auto previousResultIt = m_GameResults.find(action->GetSID());
    if (previousResultIt != m_GameResults.end()) {
      if (previousResultIt->second == result) {
        return true;
      }
      Print(
        GetLogPrefix() + "previous flag [" + ToDecString((uint8_t)previousResultIt->second) + "] would be overriden with new flag [" +
        ToDecString((uint8_t)result) + "] for SID [" + ToDecString(action->GetSID()) + "] - ignoring"
      );
      return false;
    }
    m_GameResults[action->GetSID()] = result;
    if (result == GamePlayerResult::kWinner) {
      m_GameOver = true;
    }
    LogMetaData(action->GetRecvTicks(), GetStoredPlayerName(action->GetSID()) + " " + m_ResultVerbs[(uint8_t)result] + " the game.");
    return true;
  } else if (action->GetType() == MMD_ACTION_TYPE_VAR) {
    if (m_DefVarPs.find(action->GetName()) == m_DefVarPs.end()) {
      Print(GetLogPrefix() + "VarP " + SanitizeStringUTF8(action->GetName()) + " found without a corresponding DefVarP, ignoring");
      return false;
    }
    uint8_t valueType = m_DefVarPs[action->GetName()];
    if (action->GetSubType() == MMD_OPERATOR_SET) {
      std::string operand = action->GetFirstValue();
      if (valueType == MMD_VALUE_TYPE_REAL) {
        optional<double> realValue = ToDouble(operand);
        if (!realValue.has_value()) {
          Print(GetLogPrefix() + "invalid real VarP " + SanitizeStringUTF8(action->GetName()) + " value " + SanitizeStringUTF8(operand) + " found, ignoring");
          return false;
        }
        VarP VP = VarP(action->GetSID(), action->GetName());
        m_VarPReals[VP] = *realValue;
        return true;
      } else if (valueType == MMD_VALUE_TYPE_INT) {
        optional<uint32_t> intValue = ToUint32(operand);
        if (!intValue.has_value()) {
          Print(GetLogPrefix() + "invalid int VarP " + SanitizeStringUTF8(action->GetName()) + " value " + SanitizeStringUTF8(operand) + " found, ignoring");
          return false;
        }
        VarP VP = VarP(action->GetSID(), action->GetName());
        m_VarPInts[VP] = *intValue;
        return true;
      } else { // MMD_VALUE_TYPE_STRING
        VarP VP = VarP(action->GetSID(), action->GetName());
        m_VarPStrings[VP] = operand;
        return true;
      }
    } else {
      if (valueType == MMD_VALUE_TYPE_STRING) {
        Print(GetLogPrefix() + "VarP " + SanitizeStringUTF8(action->GetName()) + " of type string cannot accept +=, -= operators, ignoring");
        return false;
      }
      std::string operand = action->GetFirstValue();
      if (valueType == MMD_VALUE_TYPE_REAL) {
        optional<double> realValue = ToDouble(operand);
        if (!realValue.has_value()) {
          Print(GetLogPrefix() + "invalid real VarP " + SanitizeStringUTF8(action->GetName()) + " value " + SanitizeStringUTF8(operand) + " found, ignoring");
          return false;
        }
        VarP VP = VarP(action->GetSID(), action->GetName());
        if (action->GetSubType() == MMD_OPERATOR_ADD) {
          m_VarPReals[VP] += *realValue;
        } else { // MMD_OPERATOR_SUBTRACT
          m_VarPReals[VP] -= *realValue;
        }
      } else { // MMD_VALUE_TYPE_INT
        optional<uint32_t> intValue = ToUint32(operand);
        if (!intValue.has_value()) {
          Print(GetLogPrefix() + "invalid int VarP " + SanitizeStringUTF8(action->GetName()) + " value " + SanitizeStringUTF8(operand) + " found, ignoring");
          return false;
        }
        VarP VP = VarP(action->GetSID(), action->GetName());
        if (action->GetSubType() == MMD_OPERATOR_ADD) {
          m_VarPInts[VP] += *intValue;
        } else { // MMD_OPERATOR_SUBTRACT
          m_VarPInts[VP] -= *intValue;
        }
      }
      return true;
    }
  } else { // if (action->GetType() == MMD_ACTION_TYPE_EVENT) 
    auto defEventIt = m_DefEvents.find(action->GetName());
    if (defEventIt == m_DefEvents.end()) {
      Print(GetLogPrefix() + "Event " + SanitizeStringUTF8(action->GetName()) + " found without a corresponding DefEvent, ignoring");
      return false;
    }
    const std::vector<std::string>& values = action->RefValues();
    const vector<string>& DefEvent = defEventIt->second;
    if (values.size() != DefEvent.size() - 1) {
      Print(GetLogPrefix() + "Event " + SanitizeStringUTF8(action->GetName()) + " found with " + to_string(values.size()) + " arguments but expected " + to_string(DefEvent.size() - 1) + " arguments, ignoring");
      return false;
    }
    if (DefEvent.empty()) {
      LogMetaData(action->GetRecvTicks(), "Event " + SanitizeStringUTF8(action->GetName()));
      return true;
    }

    string Format = DefEvent[DefEvent.size() - 1];

    // replace the markers in the format string with the arguments
    for (uint32_t i = 0; i < values.size(); ++i) {
      // check if the marker is a SID marker

      if (DefEvent[i].substr(0, 4) == "pid:") {
        // replace it with the player's name rather than their SID
        optional<uint32_t> SID = ToUint32(values[i]);
        if (!SID.has_value() || SID.value() > 0xFF) {
          Print(GetLogPrefix() + "Event " + SanitizeStringUTF8(action->GetName()) + " passed invalid PID " + SanitizeStringUTF8(values[i]));
          return false;
        }
        auto it = m_SIDToName.find((uint8_t)(*SID));
        if (it == m_SIDToName.end()) {
          Print(GetLogPrefix() + "Event " + SanitizeStringUTF8(action->GetName()) + " passed undefined PID " + to_string(*SID));
          ReplaceText(Format, "{" + to_string(i) + "}", "SID:" + values[i]);
        } else {
          ReplaceText(Format, "{" + to_string(i) + "}", it->second);
        }
      } else {
        ReplaceText(Format, "{" + to_string(i) + "}", values[i]);
      }
    }
    LogMetaData(action->GetRecvTicks(), "Event " + SanitizeStringUTF8(action->GetName()) + ": " + SanitizeStringUTF8(Format));
    return true;
  }
}

bool CW3MMD::UpdateQueue()
{
  const int64_t gameTicks = m_Game.get().GetEffectiveTicks();
  if (m_Game.get().GetIsPaused()) return true;
  if (gameTicks < MMD_PROCESSING_INITIAL_DELAY) return true;
  while (!m_DefQueue.empty()) {
    CW3MMDDefinition* def = m_DefQueue.front();
    if (gameTicks < def->GetRecvTicks() + MMD_PROCESSING_STREAM_DEF_DELAY) {
      break;
    }
    ProcessDefinition(def);
    if (def->GetUpdateID() > m_LastValueID) {
      m_LastValueID = def->GetUpdateID();
    }
    delete def;
    m_DefQueue.pop();
  }
  if (!m_DefQueue.empty()) {
    return true;
  }
  while (!m_ActionQueue.empty()) {
    CW3MMDAction* action = m_ActionQueue.front();
    if (gameTicks < action->GetRecvTicks() + MMD_PROCESSING_STREAM_ACTION_DELAY) {
      break;
    }
    ProcessAction(action);
    if (action->GetUpdateID() > m_LastValueID) {
      m_LastValueID = action->GetUpdateID();
    }
    delete action;
    m_ActionQueue.pop();
  }
  return !m_GameOver;
}

bool CW3MMD::FlushQueue()
{
  while (!m_DefQueue.empty()) {
    CW3MMDDefinition* def = m_DefQueue.front();
    ProcessDefinition(def);
    delete def;
    m_DefQueue.pop();
  }
  while (!m_ActionQueue.empty()) {
    CW3MMDAction* action = m_ActionQueue.front();
    ProcessAction(action);
    delete action;
    m_ActionQueue.pop();
  }
  return !m_GameOver;
}

vector<string> CW3MMD::TokenizeKey(string key) const
{
  vector<string> tokens;
  string token;
  bool escaping = false;

  for (string::iterator i = key.begin(); i != key.end(); ++i) {
    if (escaping) {
      if (*i == ' ') {
        token += ' ';
      } else if (*i == '\\') {
        token += '\\';
      } else {
        Print(GetLogPrefix() + "error tokenizing key " + SanitizeStringUTF8(key) + ", invalid escape sequence found, ignoring");
        return vector<string>();
      }
      escaping = false;
    } else {
      if (*i == ' ') {
        if (token.empty()) {
          Print(GetLogPrefix() + "error tokenizing key " + SanitizeStringUTF8(key) + ", empty token found, ignoring");
          return vector<string>();
        }
        tokens.push_back(token);
        token.clear();
      } else if (*i == '\\') {
        escaping = true;
      } else {
        token += *i;
      }
    }
  }

  if (token.empty()) {
    Print(GetLogPrefix() + "error tokenizing key " + SanitizeStringUTF8(key) + ", empty token found, ignoring");
    return vector<string>();
  }

  tokens.push_back(token);
  return tokens;
}

string CW3MMD::GetStoredPlayerName(uint8_t SID) const
{
  auto nameIterator = m_SIDToName.find(SID);
  if (nameIterator == m_SIDToName.end()) {
    return "SID " + ToDecString(SID);
  } else {
    return nameIterator->second;
  }
}

string CW3MMD::GetTrustedPlayerNameFromColor(uint8_t color) const
{
  string playerName;
  CGameController* controllerData = m_Game.get().GetGameControllerFromColor(color);
  if (controllerData) {
    playerName = controllerData->GetName();
  } else {
    Print(GetLogPrefix() + "error retrieving name of player color [" + ToDecString(color) + "] (" + GetColorName(color) + ")");
  }
  return playerName;
}

string CW3MMD::GetSenderName(CW3MMDDefinition* definition) const
{
  return GetTrustedPlayerNameFromColor(definition->GetFromColor());
}

string CW3MMD::GetSenderName(CW3MMDAction* action) const
{
  return GetTrustedPlayerNameFromColor(action->GetFromColor());
}

GameResultTeamAnalysis CW3MMD::GetGameResultTeamAnalysis() const
{
  GameResultTeamAnalysis analysis;

  for (const auto& entry : m_SIDToName) {
    CGameController* controllerData = m_Game.get().GetGameControllerFromSID(entry.first);
    const auto& match = m_GameResults.find(entry.first);
    bitset<MAX_SLOTS_MODERN>* targetBitSet = nullptr;
    if (match == m_GameResults.end()) {
      switch (controllerData->GetType()) {
        case GameControllerType::kVirtual:
          targetBitSet = &analysis.undecidedVirtualTeams;
          break;
        case GameControllerType::kUser:
          targetBitSet = &analysis.undecidedUserTeams;
          break;
        case GameControllerType::kComputer:
          targetBitSet = &analysis.undecidedComputerTeams;
          break;
        IGNORE_ENUM_LAST(GameControllerType)
      }
    } else {
      GamePlayerResult result = match->second;
      switch (result) {
        case GamePlayerResult::kWinner:
          targetBitSet = &analysis.winnerTeams;
          break;
        case GamePlayerResult::kLoser:
          targetBitSet = &analysis.loserTeams;
          break;
        case GamePlayerResult::kDrawer:
          targetBitSet = &analysis.drawerTeams;
          break;
        case GamePlayerResult::kUndecided:
          break;
        IGNORE_ENUM_LAST(GamePlayerResult)
      }
    }
    targetBitSet->set(controllerData->GetTeam());

    if (m_FlagsLeaver.find(entry.first) != m_FlagsLeaver.end()) {
      analysis.leaverTeams.set(controllerData->GetTeam());
    }
  }

  return analysis;
}

optional<GameResults> CW3MMD::GetGameResults(const GameResultConstraints& constraints)
{
  optional<GameResults> gameResults;
  if (m_GameResults.empty()) {
    return gameResults;
  }

  gameResults.emplace();
  GameResultTeamAnalysis teamAnalysis = GetGameResultTeamAnalysis();

  for (const auto& entry : m_SIDToName) {
    CGameController* controllerData = m_Game.get().GetGameControllerFromSID(entry.first);
    /*
    if (!controllerData || controllerData->GetIsObserver()) {
      continue;
    }
    */
    const auto& match = m_GameResults.find(entry.first);
    vector<CGameController*>* resultGroup = nullptr;
    GamePlayerResult result = GamePlayerResult::kUndecided;
    if (match == m_GameResults.end()) {
      result = m_Game.get().ResolveUndecidedController(controllerData, constraints, teamAnalysis);
    } else {
      result = match->second;
    }
    switch (result) {
      case GamePlayerResult::kWinner:
        resultGroup = &gameResults->winners;
        break;
      case GamePlayerResult::kLoser:
        resultGroup = &gameResults->losers;
        break;
      case GamePlayerResult::kDrawer:
        resultGroup = &gameResults->drawers;
        break;
      default:
        resultGroup = &gameResults->undecided;
    }
    resultGroup->push_back(controllerData);
  }

  return gameResults;
}

string CW3MMD::GetLogPrefix() const
{
  return "[W3MMD: " + m_Game.get().GetShortNameLAN() + "] ";
}

void CW3MMD::LogMetaData(int64_t gameTicks, const string& text) const
{
  m_Game.get().Log(text, gameTicks);
}
