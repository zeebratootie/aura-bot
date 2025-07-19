#include "vlan_protocol.h"

#include "../util.h"
#include "../game_stat.h"

using namespace std;

namespace VLANProtocol
{
  ///////////////////////
  // RECEIVE FUNCTIONS //
  ///////////////////////

  CIncomingVLanSearchGame RECEIVE_VLAN_SEARCHGAME(const vector<uint8_t>& data)
  {
    // DEBUG_Print( "RECEIVED VLAN_SEARCHGAME");
    // DEBUG_Print(data);

    // 2 bytes          -> Header
    // 2 bytes          -> Length
    // 4 bytes          -> ProductID
    // 4 bytes          -> Version

    if (ValidateLength(data) && data.size() >= 12) {
      uint32_t Version = ByteArrayToUInt32(data, false, 8);
      if (memcmp(data.data() + 4, ProductID_TFT, 4) == 0)
        return CIncomingVLanSearchGame(true, true, Version);
      else if (memcmp(data.data() + 4, ProductID_ROC, 4) == 0)
        return CIncomingVLanSearchGame(true, false, Version);
      else
        return CIncomingVLanSearchGame(false, false, 0);
    }
    return CIncomingVLanSearchGame(false, false, 0);
  }

  CIncomingVLanGameInfo* RECEIVE_VLAN_GAMEINFO(const vector<uint8_t>& data)
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

    if (ValidateLength(data) && data.size() >= 16)
    {
      uint32_t Version = ByteArrayToUInt32(data, false, 8);
      uint32_t HostCounter = ByteArrayToUInt32(data, false, 12);
      uint32_t EntryKey = ByteArrayToUInt32(data, false, 16);
      vector<uint8_t> GameName = ExtractCString(data, 20);
      vector<uint8_t> StatString = ExtractCString(data, 22 + GameName.size());
      int i = 23 + GameName.size() + StatString.size();
      uint32_t SlotsTotal = ByteArrayToUInt16(data, false, i);
      uint32_t MapGameType = ByteArrayToUInt32(data, false, i + 4);
      uint32_t SlotsOpen = ByteArrayToUInt32(data, false, i + 8);
      uint32_t ElapsedTime = ByteArrayToUInt32(data, false, i + 12);
      array<uint8_t, 4> IP;
      copy_n(data.begin() + i + 16, 4, IP.begin());
      uint16_t Port = ByteArrayToUInt16(data, false, i + 20);

      bool TFT;

      if (memcmp(data.data() + 4, ProductID_TFT, 4) == 0)
        TFT = true;
      else if (memcmp(data.data() + 4, ProductID_ROC, 4) == 0)
        TFT = false;
      else
        return nullptr;

      return new CIncomingVLanGameInfo(TFT, Version, MapGameType, string(GameName.begin(), GameName.end()), ElapsedTime, SlotsTotal, SlotsOpen, IP, Port, HostCounter, EntryKey, StatString);
    }

    return nullptr;
  }

  ////////////////////
  // SEND FUNCTIONS //
  ////////////////////

  vector<uint8_t> SEND_VLAN_SEARCHGAME(bool TFT, const Version& war3Version)
  {
    vector<uint8_t> packet;
    packet.push_back(VLANProtocol::Magic::VLAN_HEADER);               // VLAN header constant
    packet.push_back(VLANProtocol::Magic::SEARCHGAME);    // VLAN_SEARCHGAME
    packet.push_back(0);                                  // packet length will be assigned later
    packet.push_back(0);                                  // packet length will be assigned later

    if (TFT)
      AppendByteArray(packet, reinterpret_cast<const uint8_t*>(ProductID_TFT), 4);          // Product ID (TFT)
    else
      AppendByteArray(packet, reinterpret_cast<const uint8_t*>(ProductID_ROC), 4);          // Product ID (ROC)

    AppendByteArray(packet, static_cast<uint32_t>(war3Version.second), false);          // Version
    AssignLength(packet);
    // DEBUG_Print("SENT W3GS_SEARCHGAME");
    // DEBUG_Print(packet);
    return packet;
  }

  vector<uint8_t> SEND_VLAN_GAMEINFO(bool TFT, const Version& war3Version, uint32_t mapGameType, uint32_t gameFlags, array<uint8_t, 2> mapWidth, array<uint8_t, 2> mapHeight, string gameName, string hostName, uint32_t elapsedTime, string_view mapPath, array<uint8_t, 4> mapBlizzHash, uint32_t slotsTotal, uint32_t slotsOpen, array<uint8_t, 4> ip, uint16_t port, uint32_t hostCounter, uint32_t entryKey)
  {
    vector<uint8_t> packet;

    if (gameName.empty() || hostName.empty() || mapPath.empty() || mapBlizzHash.size() != 4) {
      Print("[VLAN] invalid parameters passed to SEND_VLAN_GAMEINFO");
      return packet;
    }

    // make the stat string
    GameStat gameStat(gameFlags, ByteArrayToUInt16(mapWidth, false), ByteArrayToUInt16(mapHeight, false), mapPath, hostName, mapBlizzHash, nullopt);
    vector<uint8_t> statString = gameStat.Encode();

    // make the rest of the packet

    packet.push_back(VLANProtocol::Magic::VLAN_HEADER);               // VLAN header constant
    packet.push_back(VLANProtocol::Magic::GAMEINFO);      // VLAN_GAMEINFO
    packet.push_back(0);                                  // packet length will be assigned later
    packet.push_back(0);                                  // packet length will be assigned later

    if (TFT)
      AppendByteArray(packet, reinterpret_cast<const uint8_t*>(ProductID_TFT), 4);          // Product ID (TFT)
    else
      AppendByteArray(packet, reinterpret_cast<const uint8_t*>(ProductID_ROC), 4);          // Product ID (ROC)

    AppendByteArray(packet, static_cast<uint32_t>(war3Version.second), false);          // Version
    AppendByteArray(packet, hostCounter, false);          // Host Counter
    AppendByteArray(packet, entryKey, false);             // Entry Key
    AppendByteArrayString(packet, gameName, true);                // Game Name
    packet.push_back(0);                                  // ??? (maybe game password)
    AppendByteArrayFast(packet, statString);              // Stat String
    packet.push_back(0);                                  // Stat String null terminator (the stat string is encoded to remove all even numbers i.e. zeros)
    AppendByteArray(packet, slotsTotal, false);           // Slots Total
    AppendByteArray(packet, mapGameType, false);          // Map Game Type
    AppendByteArray(packet, slotsOpen, false);            // Slots Open
    AppendByteArray(packet, elapsedTime, false);          // time since creation
    AppendByteArrayFast(packet, ip);                      // ip
    AppendByteArray(packet, port, false);                 // port
    AssignLength(packet);

    // DEBUG_Print( "SENT VLAN_GAMEINFO");
    // DEBUG_Print(packet);
    return packet;
  }

  vector<uint8_t> SEND_VLAN_CREATEGAME(bool TFT, const Version& war3Version, uint32_t hostCounter, array<uint8_t, 4> ip, uint16_t port)
  {
    vector<uint8_t> packet;
    packet.push_back(VLANProtocol::Magic::VLAN_HEADER);               // VLAN header constant
    packet.push_back(VLANProtocol::Magic::CREATEGAME);    // VLAN_CREATEGAME
    packet.push_back(0);                                  // packet length will be assigned later
    packet.push_back(0);                                  // packet length will be assigned later

    if (TFT)
      AppendByteArray(packet, reinterpret_cast<const uint8_t*>(ProductID_TFT), 4);          // Product ID (TFT)
    else
      AppendByteArray(packet, reinterpret_cast<const uint8_t*>(ProductID_ROC), 4);          // Product ID (ROC)

    AppendByteArray(packet, static_cast<uint32_t>(war3Version.second), false);          // Version
    AppendByteArray(packet, hostCounter, false);          // Host Counter
    AppendByteArrayFast(packet, ip);                      // IP - added by h3rmit
    AppendByteArray(packet, port, false);                 // Port - added by h3rmit
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
    AppendByteArray(packet, hostCounter, false);          // Host Counter
    AppendByteArray(packet, players, false);              // Players
    AppendByteArray(packet, playerSlots, false);          // Player Slots
    AppendByteArrayFast(packet, ip);                      // IP - added by h3rmit
    AppendByteArray(packet, port, false);                 // Port - added by h3rmit
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
    AppendByteArray(packet, hostCounter, false);          // Host Counter
    AppendByteArrayFast(packet, ip);                      // IP - added by h3rmit
    AppendByteArray(packet, port, false);                 // Port - added by h3rmit
    AssignLength(packet);
    // DEBUG_Print("SENT VLAN_DECREATEGAME");
    // DEBUG_Print(packet);
    return packet;
  }
}

//
// CIncomingVLanGameInfo
//

CIncomingVLanGameInfo::CIncomingVLanGameInfo( bool nTFT, uint32_t nVersion, uint32_t nMapGameType, string nGameName, uint32_t nElapsedTime, uint32_t nSlotsTotal, uint32_t nSlotsOpen, const array<uint8_t, 4>& nIP, uint16_t nPort, uint32_t nHostCounter, uint32_t nEntryKey, const vector<uint8_t>& nStatString )
{
  m_TFT = nTFT;
  m_Version = nVersion;
  m_MapGameType = nMapGameType;
  m_StatString = nStatString;
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
