#include <base64/base64.h>

#include "aura.h"
#include "mdns.h"
#include "game.h"
#include "os_util.h"

using namespace std;

CMDNS::CMDNS(CAura* nAura, const CGame* game, uint8_t interfaceType, uint16_t port, const Version& gameVersion)
 : m_Aura(nAura),
   m_Service(nullptr),
   m_Record(nullptr),
   m_InterfaceType(interfaceType),
   m_Port(port),
   m_GameIsExpansion(game->GetIsExpansion()),
   m_GameVersion(gameVersion)
{
#ifndef DISABLE_MDNS
  string regType = GetRegisterType();
  string gameName = game->GetDiscoveryNameLAN();
  DPRINT_IF(LogLevel::kTrace, "[MDNS] Initializing for <" + gameName + "> [" + ToVersionString(m_GameVersion) + "] ...");
  uint32_t interface = m_InterfaceType == GAME_DISCOVERY_INTERFACE_LOOPBACK ? kDNSServiceInterfaceIndexLocalOnly : kDNSServiceInterfaceIndexAny;

  /*
   * kDNSServiceFlagsShareConnection
   * For efficiency, clients that perform many concurrent operations may want to use a
   * single Unix Domain Socket connection with the background daemon, instead of having a
   * separate connection for each independent operation. To use this mode, clients first
   * call DNSServiceCreateConnection(&MainRef) to initialize the main DNSServiceRef.
   * For each subsequent operation that is to share that same connection, the client copies
   * the MainRef, and then passes the address of that copy, setting the ShareConnection flag
   * to tell the library that this DNSServiceRef is not a typical uninitialized DNSServiceRef;
   * it's a copy of an existing DNSServiceRef whose connection information should be reused.
   */

  m_Service = nAura->m_Net.m_MDNS;
  DNSServiceErrorType err = DNSServiceRegister(
    &m_Service, kDNSServiceFlagsShareConnection, interface, gameName.c_str(),
    regType.c_str(), "local", nullptr /* host */, htons(m_Port),
    0 /* TXT length */, nullptr /* TXT record */, nullptr /* cb */, nullptr /* ctx */
  );

  if (err) {
    Print("[MDNS] DNSServiceRegister ERR: " + CMDNS::ErrorCodeToString(err));
    m_Service = nullptr;
  } else {
    DPRINT_IF(LogLevel::kTrace, "[MDNS] DNSServiceRegister OK for <" + gameName + "> [" + ToVersionString(m_GameVersion) + "]");
  }
#endif
}

CMDNS::~CMDNS()
{
#ifndef DISABLE_MDNS
  DNSServiceRefDeallocate(m_Service);
  m_Service = nullptr;
#endif
}

#ifndef DISABLE_MDNS
DNSServiceRef CMDNS::CreateManager()
{
  DNSServiceRef manager = nullptr;
  DNSServiceErrorType err = DNSServiceCreateConnection(&manager);
  if (err) {
    Print("[MDNS] Failed to initialize MDNS service: " + CMDNS::ErrorCodeToString(err));
    return nullptr;
  }
  Print("[MDNS] MDNS service initialized");
  return manager;
}

void CMDNS::Destroy(DNSServiceRef service)
{
  if (service) {
    DNSServiceRefDeallocate(service);
  }
}

vector<uint8_t> CMDNS::GetGameBroadcastData(shared_ptr<const CGame> game, const string& gameName)
{
  uint8_t slotsOff = static_cast<uint8_t>(game->GetNumSlots() == game->GetSlotsOpen() ? game->GetNumSlots() : game->GetSlotsOpen() + 1);
  uint32_t slotsTotal = game->GetNumSlots();
  uint16_t hostPort = game->GetHostPortFromType(GAME_DISCOVERY_INTERFACE_IPV4);

  //string players_num = ToDecString(slotsOff); // slots taken ???
  string secret = to_string(game->GetEntryKey());

  vector<uint8_t> statInfo;
  AppendByteArray(statInfo, game->GetGameFlags(), false);
  statInfo.push_back(0);
  AppendByteArrayFast(statInfo, game->GetAnnounceWidth());
  AppendByteArrayFast(statInfo, game->GetAnnounceHeight());
  AppendByteArrayFast(statInfo, game->GetSourceFileHashBlizz(m_GameVersion));
  AppendByteArrayString(statInfo, game->GetSourceFilePath(), true);
  AppendByteArrayString(statInfo, game->GetIndexHostName(), true);
  statInfo.push_back(0);
  AppendByteArrayFast(statInfo, game->GetMap()->GetMapSHA1());

  if (statInfo.size() >= 0xFFFF) {
    return {};
  }

  vector<uint8_t> encodedStatString = EncodeStatString(statInfo);

  vector<uint8_t> game_data_d;
  if (m_GameVersion >= GAMEVER(1u, 32u)) {
    // try this for games with short names on pre32?
    const std::array<uint8_t, 4> magicNumber = {0x01, 0x20, 0x43, 0x00}; // 1.32.6700 ??
    AppendByteArrayString(game_data_d, gameName, true);
    game_data_d.push_back(0);
    AppendByteArrayFast(game_data_d, encodedStatString);
    game_data_d.push_back(0);
    AppendByteArray(game_data_d, (uint32_t)slotsTotal, false);
    AppendByteArrayFast(game_data_d, magicNumber);
    AppendByteArray(game_data_d, hostPort, false);
  } else {
    const uint32_t One = 1;
    AppendByteArray(game_data_d, One, false);
    AppendByteArrayString(game_data_d, gameName, true);
    AppendByteArrayFast(game_data_d, encodedStatString);
    game_data_d.push_back(0);
    AppendByteArray(game_data_d, (uint32_t)slotsTotal, false);
    AppendByteArrayString(game_data_d, gameName, true);
    game_data_d.push_back(0);
    AppendByteArray(game_data_d, hostPort, false);
  }

  string game_data = Base64::Encode(reinterpret_cast<unsigned char*>(game_data_d.data()), game_data_d.size(), false);

  vector<uint8_t> w66;
  w66.reserve(512);

  // Game name
  w66.push_back(0x0A);
  w66.push_back((uint8_t)gameName.size());
  AppendByteArrayString(w66, gameName, false);
  w66.push_back(0x10);
  w66.push_back(0);

  if (m_GameVersion >= GAMEVER(1u, 31u)) {
    AppendProtoBufferFromLengthDelimitedS2S(w66, "players_num", ToDecString(slotsOff)); // slots taken??
    AppendProtoBufferFromLengthDelimitedS2S(w66, "_name", gameName);
    AppendProtoBufferFromLengthDelimitedS2S(w66, "players_max", to_string(slotsTotal));
    AppendProtoBufferFromLengthDelimitedS2S(w66, "game_create_time", to_string(game->GetUptime())); // creation time uint32_t ???
    AppendProtoBufferFromLengthDelimitedS2C(w66, "_type", 0x31);
    AppendProtoBufferFromLengthDelimitedS2C(w66, "_subtype", 0x30);
    AppendProtoBufferFromLengthDelimitedS2S(w66, "game_secret", secret);

    // game_data
    w66.push_back(0x1A);
    w66.push_back((uint8_t)(14 + game_data.size()));
    w66.push_back(0x01); // Extra 0x01 ?!
    w66.push_back(0x0A);
    w66.push_back(0x09);
    AppendByteArrayString(w66, "game_data", false);
    w66.push_back(0x12);
    w66.push_back((uint8_t)game_data.size());
    w66.push_back(0x01); // Extra 0x01 ?!
    AppendByteArrayString(w66, game_data, false);

    AppendProtoBufferFromLengthDelimitedS2C(w66, "game_id", ((m_GameVersion >= GAMEVER(1u, 32u) ? 0x31: 0x32)));
    AppendProtoBufferFromLengthDelimitedS2C(w66, "_flags", 0x30);
  } else {
    AppendProtoBufferFromLengthDelimitedS2S(w66, "game_secret", secret);
    AppendProtoBufferFromLengthDelimitedS2C(w66, "_type", 0x31);
    AppendProtoBufferFromLengthDelimitedS2C(w66, "_subtype", 0x30);
    AppendProtoBufferFromLengthDelimitedS2C(w66, "game_id", 0x31);
    AppendProtoBufferFromLengthDelimitedS2S(w66, "_name", gameName);
    AppendProtoBufferFromLengthDelimitedS2S(w66, "players_max", to_string(slotsTotal));
    AppendProtoBufferFromLengthDelimitedS2C(w66, "_flags", 0x30);
    AppendProtoBufferFromLengthDelimitedS2S(w66, "players_num", ToDecString(slotsOff)); // slots taken??
    AppendProtoBufferFromLengthDelimitedS2S(w66, "game_create_time", to_string(game->GetUptime())); // creation time uint32_t ???

    // game_data
    w66.push_back(0x1A);
    w66.push_back((uint8_t)(14 + game_data.size()));
    w66.push_back(0x01); // Extra 0x01 ?!
    w66.push_back(0x0A);
    w66.push_back(0x09);
    AppendByteArrayString(w66, "game_data", false);
    w66.push_back(0x12);
    w66.push_back((uint8_t)game_data.size());
    w66.push_back(0x01); // Extra 0x01 ?!
    AppendByteArrayString(w66, game_data, false);
  }

  return w66;
}

void CMDNS::PushRecord(shared_ptr<const CGame> game)
{
  string gameName = game->GetDiscoveryNameLAN();
  vector<uint8_t> broadcastData = GetGameBroadcastData(game, gameName);

  DNSServiceErrorType err = 0;
  if (m_Record) {
    err = DNSServiceUpdateRecord(m_Service, m_Record, kDNSServiceFlagsForce /* deprecated, see also kDNSServiceFlagsKnownUnique */, (uint16_t)broadcastData.size(), reinterpret_cast<const char*>(broadcastData.data()), 0);
    if (err) Print("[MDNS] DNSServiceUpdateRecord ERR: " + CMDNS::ErrorCodeToString(err));
  } else {
    err = DNSServiceAddRecord(m_Service, &m_Record, 0, 66 /* rrtype */, (uint16_t)broadcastData.size(), reinterpret_cast<const char*>(broadcastData.data()), 0);
    if (err) Print("[MDNS] DNSServiceAddRecord ERR: " + CMDNS::ErrorCodeToString(err));
  }
  if (!err) {
    PRINT_IF(LogLevel::kInfo, "[MDNS] Recorded <" + gameName + ">");
  }
}

#endif

bool CMDNS::CheckLibrary()
{
  PLATFORM_STRING_TYPE serviceName = PLATFORM_STRING("MDNS");
  return CheckDynamicLibrary(PLATFORM_STRING("dnssd"), serviceName);
}

string CMDNS::GetRegisterType()
{
  string regType;
  regType.reserve(24);
  regType.append("_blizzard._udp"); // 14 chars
  if (m_GameIsExpansion) {
    regType.append(",_w3xp"); // 6 chars
  } else {
    regType.append(",_war3");
  }
  regType.append(ToHexString(ToVersionFlattened(m_GameVersion))); // 4 chars
  return regType;
}

string CMDNS::ErrorCodeToString(DNSServiceErrorType errCode)
{
  switch (errCode) {
    case kDNSServiceErr_NoError:
      return "ok";
    case kDNSServiceErr_Unknown:
      return "unknown";
    case kDNSServiceErr_NoSuchName:
      return "no such name";
    case kDNSServiceErr_NoMemory:
      return "no memory";
    case kDNSServiceErr_BadParam:
      return "bad param";
    case kDNSServiceErr_BadReference:
      return "bad reference";
    case kDNSServiceErr_BadState:
      return "bad state";
    case kDNSServiceErr_BadFlags:
      return "bad flags";
    case kDNSServiceErr_Unsupported:
      return "unsupported";
    case kDNSServiceErr_NotInitialized:
      return "not initialized";
    case kDNSServiceErr_AlreadyRegistered:
      return "already registered";
    case kDNSServiceErr_NameConflict:
      return "name conflict";
    case kDNSServiceErr_Invalid:
      return "invalid";
    case kDNSServiceErr_Firewall:
      return "firewall";
    case kDNSServiceErr_Incompatible:
      return "incompatible";
    case kDNSServiceErr_BadInterfaceIndex:
      return "bad interface index";
    case kDNSServiceErr_Refused:
      return "refused";
    case kDNSServiceErr_NoSuchRecord:
      return "no such record";
    case kDNSServiceErr_NoAuth:
      return "no auth";
    case kDNSServiceErr_NoSuchKey:
      return "no such key";
    case kDNSServiceErr_NATTraversal:
      return "NAT traversal";
    case kDNSServiceErr_DoubleNAT:
      return "double NAT";
    case kDNSServiceErr_BadTime:
      return "bad time";
    case kDNSServiceErr_BadSig:
      return "bad signature";
    case kDNSServiceErr_BadKey:
      return "bad key";
    case kDNSServiceErr_Transient:
      return "transient";
    case kDNSServiceErr_ServiceNotRunning:
      return "service not running";
    case kDNSServiceErr_NATPortMappingUnsupported:
      return "NAT port mapping unsupported";
    case kDNSServiceErr_NATPortMappingDisabled:
      return "NAT port mapping disabled";
    case kDNSServiceErr_NoRouter:
      return "no router";
    case kDNSServiceErr_PollingMode:
      return "polling mode";
    case kDNSServiceErr_Timeout:
      return "timeout";
    default:
      return "unknown " + to_string(errCode);
  }
}
