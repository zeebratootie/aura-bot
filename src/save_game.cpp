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

#include "save_game.h"

#include "util.h"
#include "game_slot.h"
#include "game_stat.h"
#include "packed.h"

#include "aura.h"

using namespace std;

//
// CSaveGame
//

CSaveGame::CSaveGame(CAura* nAura, const std::filesystem::path& fromPath)
  : m_Aura(nAura),
    m_Packed(new CPacked(nAura)),
    m_Valid(false),
    m_NumSlots(0),
    m_RandomSeed(0),
    m_ClientPath("Save\\Multiplayer\\" + PathToString(fromPath.filename())),
    m_ServerPath(fromPath)
{
}

CSaveGame::~CSaveGame()
{
  delete m_Packed;
}

bool CSaveGame::Load()
{
  m_Packed->Load(m_ServerPath, false);
  if (!m_Packed->GetValid() || m_Packed->GetFlags() != 0) {
    PRINT_IF(LogLevel::kWarning, "[SAVEGAME] invalid file type (flags mismatch)");
    Unload();
    return false;
  }
  return true;
}

void CSaveGame::Unload()
{
  delete m_Packed;
  m_Packed = nullptr;
}

bool CSaveGame::Parse()
{
  istringstream ISS(m_Packed->GetDecompressed());

  // savegame format figured out by Varlock:
  // string    -> map path
  // 0 (string?)  -> ??? (no idea what this is)
  // string    -> game name
  // 0 (string?)  -> ??? (maybe original game password)
  // string    -> stat string
  // 4 bytes    -> ??? (seems to be # of slots)
  // 4 bytes    -> ??? (seems to be 0x01 0x28 0x49 0x00 on both of the savegames examined; this also changes... seen 0x01 0x20 0x49 0x00)
  // 2 bytes    -> ??? (no idea what this is)
  // slot structure
  // 4 bytes    -> magic number
 
  uint8_t gameFlags = 0;
  uint8_t Garbage1 = 0;
  uint16_t Garbage2 = 0;
  uint32_t Garbage4 = 0;
  string GarbageString;

  string statString;
  uint32_t saveHash = 0;

  getline(ISS, m_ClientMapPath, '\0');        // map path
  getline(ISS, GarbageString, '\0');      // ???
  getline(ISS, m_GameName, '\0');        // game name
  getline(ISS, GarbageString, '\0');      // ???
  getline(ISS, statString, '\0');      // stat string
  ISS.read(reinterpret_cast<char*>(&Garbage4), 4);        // ???
  ISS.read(reinterpret_cast<char*>(&Garbage4), 4);        // ???
  ISS.read(reinterpret_cast<char*>(&Garbage2), 2);        // ???
  ISS.read(reinterpret_cast<char*>(&m_NumSlots), 1);      // number of slots

  if (m_NumSlots == 0 || m_NumSlots > MAX_SLOTS_MODERN) {
    PRINT_IF(LogLevel::kWarning, "[SAVEGAME] invalid savegame (slot count invalid)");
    return false;
  }

  if (m_NumSlots > MAX_SLOTS_LEGACY && !m_Aura->m_SupportsModernSlots) {
    PRINT_IF(LogLevel::kWarning, "[SAVEGAME] invalid savegame (more than 12 slots requires support for v1.29+)");
    return false;
  }

  for (uint8_t i = 0; i < m_NumSlots; i++) {
    uint8_t SlotData[9];
    ISS.read(reinterpret_cast<char*>(SlotData), 9);      // slot data
    // SlotData[8] seems to be always 100?
    // i, [0] download, [1] slot status, [2] computer, [3] team, [4] color, [5] race, [6] difficulty, [7] handicap
    m_Slots.emplace_back(i, SlotData[0], SlotData[1], SlotData[2], SlotData[3], SlotData[4], SlotData[5], SlotData[6], SlotData[7]/*, SlotData[8]*/);
  }

  ISS.read(reinterpret_cast<char*>(&m_RandomSeed), 4);      // random seed
  ISS.read(reinterpret_cast<char*>(&gameFlags), 1);        // game flags?
  ISS.read(reinterpret_cast<char*>(&Garbage1), 1);        // number of controller slots (12)
  ISS.read(reinterpret_cast<char*>(&saveHash), 4);      // magic number

  if (ISS.eof() || ISS.fail()) {
    PRINT_IF(LogLevel::kWarning, "[SAVEGAME] failed to parse savegame header");
    return false;
  }

  m_GameStat = GameStat::Parse(statString);
  if (!m_GameStat.GetIsValid()) {
    PRINT_IF(LogLevel::kWarning, "[SAVEGAME] failed to parse savegame stat data (blizz hash is <" + ByteArrayToDecString(m_GameStat.GetMapScriptsBlizzHash()) + ">)");
    m_Valid = true;
    return false;
  }

  m_SaveHash = CreateFixedByteArray<Endianness::kLittle>(saveHash);
  m_Valid = true;
  return m_Valid;
}

uint8_t CSaveGame::GetNumHumanSlots() const
{
  uint8_t count = 0;
  for (const auto& slot : m_Slots) {
    if (slot.GetIsPlayerOrFake()) {
      ++count;
    }
  }
  return count;
}

const CGameSlot* CSaveGame::InspectSlot(const uint8_t SID) const
{
  if (SID >= m_Slots.size()) return nullptr;
  return &(m_Slots[SID]);
}
