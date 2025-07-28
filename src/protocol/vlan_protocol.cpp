#include "vlan_protocol.h"

#include "../util.h"
#include "../game_stat.h"

using namespace std;

namespace VLANProtocol
{
  ///////////////////////
  // RECEIVE FUNCTIONS //
  ///////////////////////

  CIncomingVLanSearchGame RECEIVE_VLAN_SEARCHGAME(const string_view data)
  {
    // DEBUG_Print( "RECEIVED VLAN_SEARCHGAME");
    // DEBUG_Print(data);

    // 2 bytes          -> Header
    // 2 bytes          -> Length
    // 4 bytes          -> ProductID
    // 4 bytes          -> Version

    if (ValidateLength(data) && data.size() >= 12) {
      const uint32_t productID = ByteArrayToUInt32LE(data, 4);
      bool isExpansion = productID == ProductID_TFT_LE;
      if (!isExpansion && productID != ProductID_ROC_LE) {
        return CIncomingVLanSearchGame();
      }
      const uint32_t gameVersion = ByteArrayToUInt32LE(data, 8);
      if (gameVersion > 0xFF) {
        return CIncomingVLanSearchGame();
      }
      return CIncomingVLanSearchGame(true, isExpansion, GAMEVER(1u, static_cast<uint8_t>(gameVersion)));
    }
    return CIncomingVLanSearchGame();
  }

  CIncomingVLanGameInfo* RECEIVE_VLAN_GAMEINFO(const string_view data)
  {
    // DEBUG_Print( "RECEIVED VLAN_GAMEINFO");
    // DEBUG_Print(data);

    // 2 bytes          -> Header
    // 2 bytes          -> Length
    // 4 bytes          -> ProductID
    // 4 bytes          -> Version
    // 4 bytes          -> HostCounter
    // 4 bytes          -> EntryKey
    // null term string      -> GameName
    // null term string      -> StatString
    // 1 byte          -> SlotsTotal
    // 4 bytes          -> GameType
    // 4 bytes          -> SlotsOpen
    // 4 bytes          -> ElapsedTime
    // 4 bytes          -> IP
    // 2 bytes          -> Port

    if (ValidateLength(data) && data.size() >= 16) {
      uint32_t version = ByteArrayToUInt32LE(data, 8);
      uint32_t hostCounter = ByteArrayToUInt32LE(data, 12);
      uint32_t entryKey = ByteArrayToUInt32LE(data,  16);
      string_view gameName = ExtractUTF8View(data, 20, MAX_GAME_NAME_SIZE);
      if (gameName.empty()) {
        return nullptr;
      }
      string_view statString = (
        ExtractStringView<OOBPolicy::kCheck, NullTerminatorPolicy::kRequired, StringEncoding::kNone>(
          data, 22 + gameName.size(), 255
        )
      );
      if (statString.empty()) {
        return nullptr;
      }
      size_t i = 23 + gameName.size() + statString.size();
      uint32_t slotsTotal = ByteArrayToUInt16LE(data, i);
      uint32_t mapGameType = ByteArrayToUInt32LE(data, i + 4);
      uint32_t slotsOpen = ByteArrayToUInt32LE(data, i + 8);
      uint32_t elapsedTime = ByteArrayToUInt32LE(data, i + 12);
      array<uint8_t, 4> IP;
      copy_n(data.begin() + i + 16, 4, IP.begin());
      uint16_t port = ByteArrayToUInt16LE(data, i + 20);

      const uint32_t productID = ByteArrayToUInt32LE(data, 4);
      bool isExpansion = productID == ProductID_TFT_LE;
      if (!isExpansion && productID != ProductID_ROC_LE) {
        return nullptr;
      }

      return new CIncomingVLanGameInfo(isExpansion, version, mapGameType, string(gameName), elapsedTime, slotsTotal, slotsOpen, IP, port, hostCounter, entryKey, statString);
    }

    return nullptr;
  }

  ////////////////////
  // SEND FUNCTIONS //
  ////////////////////

  vector<uint8_t> SEND_VLAN_SEARCHGAME(bool isExpansion, const Version& war3Version)
  {
    vector<uint8_t> packet;
    packet.push_back(VLANProtocol::Magic::VLAN_HEADER);               // VLAN header constant
    packet.push_back(VLANProtocol::Magic::SEARCHGAME);    // VLAN_SEARCHGAME
    packet.push_back(0);                                  // packet length will be assigned later
    packet.push_back(0);                                  // packet length will be assigned later

    if (isExpansion) {
      AppendNumberLE(packet, ProductID_TFT_LE);                     // Product ID (TFT)
    } else {
      AppendNumberLE(packet, ProductID_ROC_LE);                     // Product ID (ROC)
    }

    AppendNumberLE(packet, static_cast<uint32_t>(war3Version.second));          // Version
    AssignLength(packet);
    // DEBUG_Print("SENT W3GS_SEARCHGAME");
    // DEBUG_Print(packet);
    return packet;
  }

  vector<uint8_t> SEND_VLAN_GAMEINFO(bool isExpansion, const Version& war3Version, uint32_t mapGameType, uint32_t gameFlags, array<uint8_t, 2> mapWidth, array<uint8_t, 2> mapHeight, string gameName, string hostName, uint32_t elapsedTime, string_view mapPath, array<uint8_t, 4> mapBlizzHash, uint32_t slotsTotal, uint32_t slotsOpen, array<uint8_t, 4> ip, uint16_t port, uint32_t hostCounter, uint32_t entryKey)
  {
    vector<uint8_t> packet;

    if (gameName.empty() || hostName.empty() || mapPath.empty() || mapBlizzHash.size() != 4) {
      Print("[VLAN] invalid parameters passed to SEND_VLAN_GAMEINFO");
      return packet;
    }

    // make the stat string
    GameStat gameStat(gameFlags, ByteArrayToUInt16LE(mapWidth), ByteArrayToUInt16LE(mapHeight), mapPath, hostName, mapBlizzHash, nullopt);
    vector<uint8_t> statString = gameStat.Encode();

    // make the rest of the packet

    packet.push_back(VLANProtocol::Magic::VLAN_HEADER);               // VLAN header constant
    packet.push_back(VLANProtocol::Magic::GAMEINFO);      // VLAN_GAMEINFO
    packet.push_back(0);                                  // packet length will be assigned later
    packet.push_back(0);                                  // packet length will be assigned later

    if (isExpansion) {
      AppendNumberLE(packet, ProductID_TFT_LE);                     // Product ID (TFT)
    } else {
      AppendNumberLE(packet, ProductID_ROC_LE);                     // Product ID (ROC)
    }

    AppendNumberLE(packet, static_cast<uint32_t>(war3Version.second));          // Version
    AppendNumberLE(packet, hostCounter);          // Host Counter
    AppendNumberLE(packet, entryKey);             // Entry Key
    AppendByteArrayString(packet, gameName, true);                // Game Name
    packet.push_back(0);                                  // ??? (maybe game password)
    AppendContainer(packet, statString);              // Stat String
    packet.push_back(0);                                  // Stat String null terminator (the stat string is encoded to remove all even numbers i.e. zeros)
    AppendNumberLE(packet, slotsTotal);           // Slots Total
    AppendNumberLE(packet, mapGameType);          // Map Game Type
    AppendNumberLE(packet, slotsOpen);            // Slots Open
    AppendNumberLE(packet, elapsedTime);          // time since creation
    AppendContainer(packet, ip);                      // ip
    AppendNumberLE(packet, port);                 // port
    AssignLength(packet);

    // DEBUG_Print( "SENT VLAN_GAMEINFO");
    // DEBUG_Print(packet);
    return packet;
  }

  vector<uint8_t> SEND_VLAN_CREATEGAME(bool isExpansion, const Version& war3Version, uint32_t hostCounter, array<uint8_t, 4> ip, uint16_t port)
  {
    vector<uint8_t> packet;
    packet.push_back(VLANProtocol::Magic::VLAN_HEADER);               // VLAN header constant
    packet.push_back(VLANProtocol::Magic::CREATEGAME);    // VLAN_CREATEGAME
    packet.push_back(0);                                  // packet length will be assigned later
    packet.push_back(0);                                  // packet length will be assigned later

    if (isExpansion) {
      AppendNumberLE(packet, ProductID_TFT_LE);                     // Product ID (TFT)
    } else {
      AppendNumberLE(packet, ProductID_ROC_LE);                     // Product ID (ROC)
    }

    AppendNumberLE(packet, static_cast<uint32_t>(war3Version.second));          // Version
    AppendNumberLE(packet, hostCounter);          // Host Counter
    AppendContainer(packet, ip);                      // IP - added by h3rmit
    AppendNumberLE(packet, port);                 // Port - added by h3rmit
    AssignLength(packet);
    // DEBUG_Print("SENT VLAN_CREATEGAME");
    // DEBUG_Print(packet);
    return packet;
  }

  vector<uint8_t> SEND_VLAN_REFRESHGAME(uint32_t hostCounter, uint32_t players, uint32_t playerSlots, array<uint8_t, 4> ip, uint16_t port)
  {
    vector<uint8_t> packet;
    packet.push_back(VLANProtocol::Magic::VLAN_HEADER);               // VLAN header constant
    packet.push_back(VLANProtocol::Magic::REFRESHGAME);   // VLAN_REFRESHGAME
    packet.push_back(0);                                  // packet length will be assigned later
    packet.push_back(0);                                  // packet length will be assigned later
    AppendNumberLE(packet, hostCounter);          // Host Counter
    AppendNumberLE(packet, players);              // Players
    AppendNumberLE(packet, playerSlots);          // Player Slots
    AppendContainer(packet, ip);                      // IP - added by h3rmit
    AppendNumberLE(packet, port);                 // Port - added by h3rmit
    AssignLength(packet);
    // DEBUG_Print("SENT VLAN_REFRESHGAME");
    // DEBUG_Print(packet);
    return packet;
  }

  vector<uint8_t> SEND_VLAN_DECREATEGAME(uint32_t hostCounter, array<uint8_t, 4> ip, uint16_t port)
  {
    vector<uint8_t> packet;
    packet.push_back(VLANProtocol::Magic::VLAN_HEADER);               // VLAN header constant
    packet.push_back(VLANProtocol::Magic::DECREATEGAME);  // VLAN_DECREATEGAME
    packet.push_back(0);                                  // packet length will be assigned later
    packet.push_back(0);                                  // packet length will be assigned later
    AppendNumberLE(packet, hostCounter);          // Host Counter
    AppendContainer(packet, ip);                      // IP - added by h3rmit
    AppendNumberLE(packet, port);                 // Port - added by h3rmit
    AssignLength(packet);
    // DEBUG_Print("SENT VLAN_DECREATEGAME");
    // DEBUG_Print(packet);
    return packet;
  }
}

//
// CIncomingVLanGameInfo
//

CIncomingVLanGameInfo::CIncomingVLanGameInfo( bool nTFT, uint32_t nVersion, uint32_t nMapGameType, string nGameName, uint32_t nElapsedTime, uint32_t nSlotsTotal, uint32_t nSlotsOpen, const array<uint8_t, 4>& nIP, uint16_t nPort, uint32_t nHostCounter, uint32_t nEntryKey, const string_view nStatString )
{
  m_TFT = nTFT;
  m_Version = nVersion;
  m_MapGameType = nMapGameType;
  m_StatString = string(nStatString);
  m_GameName = nGameName;
  m_ElapsedTime = nElapsedTime;
  m_SlotsTotal = nSlotsTotal;
  m_SlotsOpen = nSlotsOpen;
  m_IP = nIP;
  m_Port = nPort;
  m_HostCounter = nHostCounter;
  m_EntryKey = nEntryKey;
  m_ReceivedTime = GetTime();

  // decode stat string

  GameStat statData = GameStat::Parse(m_StatString);
  if (statData.GetIsValid()) {
    m_GameFlags = statData.GetGameFlags();
    m_MapWidth = statData.GetMapWidth();
    m_MapHeight = statData.GetMapHeight();
    copy_n(statData.GetMapScriptsBlizzHash().begin(), 4, m_MapScriptsBlizzHash.begin());
    m_MapPath = statData.m_MapPath;
    m_HostName = statData.m_HostName;
  }
}

CIncomingVLanGameInfo::~CIncomingVLanGameInfo()
{
}
