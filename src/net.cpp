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

#include "net.h"

#include <thread>

#ifndef DISABLE_CPR
#include <cpr/cpr.h>
#endif
#ifndef DISABLE_MINIUPNP
#include <miniupnpc.h>
#include <upnpcommands.h>
#endif

#include "command.h"
#include "config/config_net.h"
#include "connection.h"
#include "game.h"
#include "protocol/game_protocol.h"
#include "proxy/tcp_proxy.h"
#include "proxy/gproxy_server.h"
#include "game_seeker.h"
#include "game_user.h"
#include "async_observer.h"
#include "realm.h"
#include "socket.h"
#include "aura.h"

using namespace std;

//
// CGameTestConnection
//

CGameTestConnection::CGameTestConnection(CAura* nAura, shared_ptr<CRealm> nRealm, sockaddr_storage nTargetHost, const uint32_t nBaseHostCounter, const uint8_t nType, const string& nName)
  : m_TargetHost(nTargetHost),
    m_Aura(nAura),
    m_RealmInternalId(nRealm ? nRealm->GetInternalID() : 0),
    m_BaseHostCounter(nBaseHostCounter),
    m_Socket(new CTCPClient(static_cast<uint8_t>(nTargetHost.ss_family), nName)),
    m_Type(nType),
    m_Name(nName),
    m_TimeoutTicks(0),
    m_LastConnectionFailureTicks(0),
    m_SentJoinRequest(false)
{
}

CGameTestConnection::~CGameTestConnection()
{
  delete m_Socket;
}

uint32_t CGameTestConnection::SetFD(fd_set* fd, fd_set* send_fd, int32_t* nfds) const
{
  if (!m_Socket->HasError() && m_Socket->GetConnected())
  {
    m_Socket->SetFD(fd, send_fd, nfds);
    return 1;
  }

  return 0;
}

bool CGameTestConnection::GetIsRealmOnline() const
{
  if (m_RealmInternalId < 0x10) return true;
  string realmId = m_Aura->m_RealmsIdentifiers[m_RealmInternalId];
  shared_ptr<CRealm> realm = m_Aura->GetRealmByInputId(realmId);
  if (realm == nullptr) return false;
  return realm->GetLoggedIn();
}

bool CGameTestConnection::GetIsRealmListed() const
{
  if (m_RealmInternalId < 0x10) return true;
  string realmId = m_Aura->m_RealmsIdentifiers[m_RealmInternalId];
  shared_ptr<CRealm> realm = m_Aura->GetRealmByInputId(realmId);
  if (realm == nullptr) return false;
  return realm->GetIsGameBroadcastSucceeded();
}

uint32_t CGameTestConnection::GetHostCounter() const
{
  const shared_ptr<CGame> lobby = m_Aura->GetLobbyByHostCounterExact(m_BaseHostCounter);
  if (lobby && lobby->GetIsMirror()) {
    return m_BaseHostCounter;
  }
  uint32_t hostCounter = m_BaseHostCounter | (0x01 << 24); // informational bit
  string realmId = m_Aura->m_RealmsIdentifiers[m_RealmInternalId];
  shared_ptr<CRealm> realm = m_Aura->GetRealmByInputId(realmId);
  if (realm == nullptr) {
    return hostCounter;
  }
  hostCounter |= static_cast<uint32_t>(realm->GetHostCounterID()) << 24;
  return hostCounter;
}

uint16_t CGameTestConnection::GetPort() const
{
  if (m_TargetHost.ss_family == AF_INET6) {
    const sockaddr_in6* addr6 = reinterpret_cast<const sockaddr_in6*>(&m_TargetHost);
    return ntohs(addr6->sin6_port);
  } else if (m_TargetHost.ss_family == AF_INET) {
    const sockaddr_in* addr4 = reinterpret_cast<const sockaddr_in*>(&m_TargetHost);
    return ntohs(addr4->sin_port);
  } else {
    return 0;
  }
}

bool CGameTestConnection::QueryGameInfo()
{
  if (m_Socket->HasError() || !m_Socket->GetConnected()) {
    return false;
  }

  const shared_ptr<CGame> lobby = m_Aura->GetLobbyByHostCounterExact(m_BaseHostCounter);
  if (!lobby || (!lobby->GetIsLobbyStrict() && !lobby->GetIsMirror())) {
    return false;
  }

  const static string Name = "AuraBot";
  const vector<uint8_t> joinRequest = GameProtocol::SEND_W3GS_REQJOIN(
    GetHostCounter(),
    lobby->GetEntryKey(),
    Name
  );
  m_Socket->PutBytes(joinRequest);
  m_SentJoinRequest = true;
  return true;
}

bool CGameTestConnection::Update(fd_set* fd, fd_set* send_fd)
{
  static optional<sockaddr_storage> emptyBindAddress;

  if (m_Passed.has_value()) {
    return false;
  }

  if (m_Socket->HasError()) {
    if (!m_CanConnect.has_value()) m_CanConnect = false;
    m_LastConnectionFailureTicks = m_Aura->GetLoopTicks();
    m_Socket->Reset();
  } else if (m_Socket->GetConnected() && !m_Aura->GetTicksIsAfter(m_TimeoutTicks)) {
    bool gotJoinedMessage = false;
    if (m_Socket->DoRecv(fd)) {
      string* RecvBuffer = m_Socket->GetBytes();
      std::vector<uint8_t> Bytes = CreateByteArray((uint8_t*)RecvBuffer->c_str(), RecvBuffer->size());
      gotJoinedMessage = Bytes.size() >= 2 && Bytes[0] == GameProtocol::Magic::W3GS_HEADER && Bytes[1] == GameProtocol::Magic::SLOTINFOJOIN;
      m_Passed = true;
    }
    if (!m_SentJoinRequest) {
      if (QueryGameInfo()) {
        m_Socket->DoSend(send_fd);
      }
    } else if (gotJoinedMessage) {
      m_Socket->Reset();
      m_Socket->Disconnect();
    }
  } else if (m_Socket->GetConnecting() && m_Socket->CheckConnect()) {
    m_CanConnect = true;
  } else if (m_Aura->GetTicksIsAfter(m_TimeoutTicks) && (m_Socket->GetConnecting() || m_CanConnect.has_value())) {
    if (!m_CanConnect.has_value()) m_CanConnect = false;
    m_Passed = false;
    m_LastConnectionFailureTicks = m_Aura->GetLoopTicks();
    m_Socket->Reset();
    m_Socket->Disconnect();
  } else if (!m_Socket->GetConnecting() && !m_CanConnect.has_value() && m_Aura->GetTicksIsAfterDelay(m_LastConnectionFailureTicks, 900)) {
    m_Socket->Connect(emptyBindAddress, m_TargetHost);
    m_TimeoutTicks = m_Aura->GetLoopTicks() + GAME_TEST_TIMEOUT;
  }

  return !m_Passed.has_value();
}

//
// CIPAddressAPIConnection
//

CIPAddressAPIConnection::CIPAddressAPIConnection(CAura* nAura, const sockaddr_storage& nTargetHost, const string& nEndPoint, const string& nHostName)
  : m_TargetHost(nTargetHost),
    m_Aura(nAura),
    m_Socket(new CTCPClient(static_cast<uint8_t>(nTargetHost.ss_family), nTargetHost.ss_family == AF_INET6 ? "IPv6 Address" : "IPv4 Address")),
    m_EndPoint(nEndPoint),
    m_HostName(nHostName),
    m_TimeoutTicks(0),
    m_LastConnectionFailureTicks(0),
    m_SentQuery(false)
{
}

CIPAddressAPIConnection::~CIPAddressAPIConnection()
{
  delete m_Socket;
}

uint32_t CIPAddressAPIConnection::SetFD(fd_set* fd, fd_set* send_fd, int32_t* nfds)
{
  if (!m_Socket->HasError() && m_Socket->GetConnected())
  {
    m_Socket->SetFD(fd, send_fd, nfds);
    return 1;
  }

  return 0;
}

int64_t CNet::GetThrottleTime(const NetworkHost& host, int64_t minTime) const
{
  const auto it = m_OutgoingThrottles.find(host);
  if (it == m_OutgoingThrottles.end()) {
    return minTime;
  }
  const TimedUint8 throttled = it->second;
  if (throttled.second == 0) {
    return minTime;
  }
  return (int64_t) NET_BASE_RECONNECT_DELAY << (int64_t)throttled.second;
}

bool CNet::GetIsOutgoingThrottled(const NetworkHost& host) const
{
  if (m_OutgoingPendingConnections.count(host) > 0) {
    // Only one concurrent connection attempt per throttable host is allowed.
    return true;
  }

  const auto it = m_OutgoingThrottles.find(host);
  if (it == m_OutgoingThrottles.end()) {
    return false;
  }
  const TimedUint8& throttled = it->second;
  if (throttled.second == 0) {
    return false;
  }
  const int64_t dueTime = throttled.first + ((int64_t) NET_BASE_RECONNECT_DELAY << (int64_t)throttled.second);
  return m_Aura->GetLoopTime() < dueTime;
}

void CNet::ResetOutgoingThrottled(const NetworkHost& host)
{
  m_OutgoingThrottles.erase(host);
}

void CNet::OnThrottledConnectionStart(const NetworkHost& host)
{
  m_OutgoingPendingConnections.insert(host);
}

void CNet::OnThrottledConnectionSuccess(const NetworkHost& host)
{
  m_OutgoingPendingConnections.erase(host);
  ResetOutgoingThrottled(host);
}

void CNet::OnThrottledConnectionError(const NetworkHost& host)
{
  m_OutgoingPendingConnections.erase(host);
  auto it = m_OutgoingThrottles.find(host);
  if (it == m_OutgoingThrottles.end()) {
    m_OutgoingThrottles[host] = TimedUint8(m_Aura->GetLoopTime(), 0);
    return;
  }
  TimedUint8& throttled = it->second;
  throttled.first = m_Aura->GetLoopTime();
  if (throttled.second < NET_RECONNECT_MAX_BACKOFF) {
    // Max delay 45 << 12 seconds ~ 2 days
    throttled.second = throttled.second + 1;
  }
}

bool CIPAddressAPIConnection::QueryIPAddress()
{
  if (m_Socket->HasError() || !m_Socket->GetConnected()) {
    return false;
  }

  const vector<uint8_t> method = {0x47, 0x45, 0x54, 0x20}; // GET
  const vector<uint8_t> httpVersion = {0x20, 0x48, 0x54, 0x54, 0x50, 0x2f, 0x31, 0x2e, 0x31, 0xd, 0xa}; // HTTP/1.1\n\r
  const vector<uint8_t> hostHeader = {0x48, 0x4f, 0x53, 0x54, 0x3a, 0x20}; // HOST: 
  const vector<uint8_t> end = {0xd, 0xa, 0xd, 0xa}; // \n\r\n\r

  vector<uint8_t> query;
  AppendByteArrayFast(query, method);
  AppendByteArrayString(query, m_EndPoint, false);
  AppendByteArrayFast(query, httpVersion);
  AppendByteArrayFast(query, hostHeader);
  AppendByteArrayString(query, m_HostName, false);
  AppendByteArrayFast(query, end);
  m_Socket->PutBytes(query);
  m_SentQuery = true;
  return true;
}

bool CIPAddressAPIConnection::Update(fd_set* fd, fd_set* send_fd)
{
  static optional<sockaddr_storage> emptyBindAddress;

  if (m_Result.has_value()) {
    return false;
  }

  if (m_Socket->HasError()) {
    if (!m_CanConnect.has_value()) m_CanConnect = false;
    m_LastConnectionFailureTicks = m_Aura->GetLoopTicks();
    m_Socket->Reset();
  } else if (m_Socket->GetConnected() && !m_Aura->GetTicksIsAfter(m_TimeoutTicks)) {
    bool gotAddress = false;
    if (m_Socket->DoRecv(fd)) {
      string* RecvBuffer = m_Socket->GetBytes();
      std::vector<uint8_t> Bytes = CreateByteArray((uint8_t*)RecvBuffer->c_str(), RecvBuffer->size());
      uint16_t size = static_cast<uint16_t>(Bytes.size());
      const bool is200 = size >= 15 && Bytes[9] == 0x32 && Bytes[10] == 0x30 && Bytes[11] == 0x30;
      if (is200) {
        uint16_t endIndex = size;
        if (Bytes[endIndex - 1] == 0xa) endIndex = Bytes[endIndex - 2] == 0xd ? endIndex - 2 : endIndex - 1; // Ignore EOF
        uint16_t responseStart = endIndex;
        uint16_t index = endIndex;
        while (index--) {
          if (Bytes[index] == 0xa) {
            responseStart = index + 1;
            break;
          }
        }
        if (endIndex > responseStart) {
          string responseBody = string(Bytes.begin() + responseStart, Bytes.begin() + endIndex);
          optional<sockaddr_storage> maybeAddress = CNet::ParseAddress(responseBody, m_TargetHost.ss_family == AF_INET6 ? ACCEPT_IPV6 : ACCEPT_IPV4);
          if (maybeAddress.has_value()) {
            m_Result = move(maybeAddress);
            gotAddress = true;
          }
        }
      }
    }
    if (!m_SentQuery) {
      if (QueryIPAddress()) {
        m_Socket->DoSend(send_fd);
      }
    } else if (gotAddress) {
      m_Socket->Reset();
      m_Socket->Disconnect();
    }
  } else if (m_Socket->GetConnecting() && m_Socket->CheckConnect()) {
    m_CanConnect = true;
  } else if (m_Aura->GetTicksIsAfter(m_TimeoutTicks) && (m_Socket->GetConnecting() || m_CanConnect.has_value())) {
    if (!m_CanConnect.has_value()) m_CanConnect = false;
    m_LastConnectionFailureTicks = m_Aura->GetLoopTicks();
    m_Socket->Reset();
    m_Socket->Disconnect();
  } else if (!m_Socket->GetConnecting() && !m_CanConnect.has_value() && m_Aura->GetTicksIsAfterDelay(m_LastConnectionFailureTicks, 900)) {
    m_Socket->Connect(emptyBindAddress, m_TargetHost);
    m_TimeoutTicks = m_Aura->GetLoopTicks() + IP_ADDRESS_API_TIMEOUT;
  }

  return !m_Result.has_value();
}

//
// CNet
//

CNet::CNet(CConfig& nCFG)
  : m_Aura(nullptr),
    m_Config(CNetConfig(nCFG)),
    m_MDNS(nullptr),
    m_SupportUDPOverIPv6(false),
    m_UDPMainServerEnabled(false),
    m_UDPMainServer(nullptr),
    m_UDPDeafSocket(nullptr),
    m_UDPIPv6Server(nullptr),
    m_VLANServer(nullptr),
    m_UDP4TargetPort(6112), // Constant

    // GProxy's port is actually configurable client-side (lan_port),
    // but this port is always used by Aura in deaf UDP mode.
    m_UDP4TargetProxyPort(6116), // Constant
    m_UDP6TargetPort(5678), // Only unicast. <net.game_discovery.udp.ipv6.target_port>
    m_MainBroadcastTarget(new sockaddr_storage()),
    m_ProxyBroadcastTarget(new sockaddr_storage()),

    m_IPv4SelfCacheV(make_pair(string(), nullptr)),
    m_IPv4SelfCacheT(NET_PUBLIC_IP_ADDRESS_ALGORITHM_INVALID),
    m_IPv6SelfCacheV(make_pair(string(), nullptr)),
    m_IPv6SelfCacheT(NET_PUBLIC_IP_ADDRESS_ALGORITHM_INVALID),

    m_HealthCheckVerbose(false),
    m_HealthCheckInProgress(false),
    m_HealthCheckContext(nullptr),

    m_IPAddressFetchInProgress(false),

    m_LastHostPort(0),
    m_LastDownloadTicks(APP_MIN_TICKS),
    m_TransferredMapBytesThisUpdate(0)
{
}

void CNet::InitPersistentConfig()
{
  // Implements non-reloadable config entries.
  m_UDPMainServerEnabled = m_Config.m_UDPMainServerEnabled;
  m_SupportTCPOverIPv6 = m_Config.m_SupportTCPOverIPv6;
  m_SupportUDPOverIPv6 = m_Config.m_SupportUDPOverIPv6;
  m_UDPFallbackPort = m_Config.m_UDPFallbackPort;
  m_UDPIPv6Port = m_Config.m_UDPIPv6Port;
  m_VLANEnabled = m_Config.m_VLANEnabled;
  m_VLANPort = m_Config.m_VLANPort;
}

bool CNet::Init()
{
  InitPersistentConfig();

  // Main server is the only one that can send W3GS_REFRESHGAME packets. 
  // If disabled, UDP communication is one-way, and can only be through W3GS_GAMEINFO.

  if (m_UDPMainServerEnabled) {
    m_UDPMainServer = new CUDPServer(AF_INET);
    if (!m_UDPMainServer->Listen(m_Config.m_BindAddress4, 6112, false)) {
      Print("=======================================================================================");
      Print("[UDP] <net.udp_server.enabled = yes> requires active instances of Warcraft to be closed...");
      Print("[UDP] Please release port 6112, or set <net.udp_server.enabled = no>");
      Print("=======================================================================================");
      Print("[UDP] Waiting...");
      this_thread::sleep_for(chrono::milliseconds(5000));
      if (!m_UDPMainServer->Listen(m_Config.m_BindAddress4, 6112, true)) {
        if (m_Config.m_UDPBroadcastEnabled) {
          Print("[UDP] Failed to start UDP/IPv4 service on port 6112. Cannot start broadcast service.");
        } else {
          Print("[UDP] Failed to start UDP/IPv4 service on port 6112");
        }
        Print("=======================================================================================");
        return false;
      }
    }
#ifndef DISABLE_MINIUPNP
    if (m_Config.m_EnableUPnP) {
      RequestUPnP(NetProtocol::kUDP, 6112, 6112, LogLevel::kDebug);
    }
#endif
  }

  if (!m_UDPMainServerEnabled) {
    m_UDPDeafSocket = new CUDPServer(AF_INET);
    if (!m_UDPDeafSocket->Listen(m_Config.m_BindAddress4, m_UDPFallbackPort, true)) {
      Print("====================================================================");
      Print("[UDP] Failed to bind to fallback port " + to_string(m_UDPFallbackPort) + ".");
      Print("[UDP] For a random available port, set <net.udp_fallback.outbound_port = 0>");
      if (m_Config.m_UDPBroadcastEnabled) {
        Print("[UDP] Failed to start UDP/IPv4 service. Cannot start broadcast service.");
      } else {
        Print("[UDP] Failed to start UDP/IPv4 service");
      }
      Print("====================================================================");
      return false;
    }
  }

  if (m_SupportUDPOverIPv6) {
    m_UDPIPv6Server = new CUDPServer(AF_INET6);
    if (!m_UDPIPv6Server->Listen(m_Config.m_BindAddress6, m_UDPIPv6Port, true)) {
      Print("====================================================================");
      Print("[UDP] Failed to bind to port " + to_string(m_UDPIPv6Port) + ".");
      Print("[UDP] For a random available port, set <net.udp_ipv6.port = 0>");
      Print("[UDP] Failed to start UDP/IPv6 service");
      Print("====================================================================");
      return false;
    }
  } else {
    for (const auto& clientIp : m_Aura->m_GameDefaultConfig->m_ExtraDiscoveryAddresses) {
      if (clientIp.ss_family == AF_INET6) {
        Print("[CONFIG] Address " + AddressToString(clientIp) + " at <net.game_discovery.udp.extra_clients.ip_addresses> cannot receive game discovery messages, because IPv6 support hasn't been enabled");
        Print("[CONFIG] Set <net.ipv6.tcp.enabled = yes>, and <net.udp_ipv6.enabled = yes> if you want to enable it.");
      }
    }
  }

  m_UDP6TargetPort = m_Config.m_UDP6TargetPort;
  if (m_Config.m_UDPBroadcastEnabled) PropagateBroadcastEnabled(true);
  if (m_Config.m_UDPDoNotRouteEnabled) PropagateDoNotRouteEnabled(true);
  SetBroadcastTarget(m_Config.m_UDPBroadcastTarget);

  if (m_VLANEnabled) {
    m_VLANServer = GetOrCreateTCPServer(m_VLANPort, "VLAN Server");
  }

  m_MDNS = CMDNS::CreateManager();

#ifdef DISABLE_CPR
  QueryIPAddress();
#endif

  return true;
}

uint32_t CNet::SetFD(fd_set* fd, fd_set* send_fd, int32_t* nfds)
{
  uint32_t NumFDs = 0;

  for (auto& connection : m_HealthCheckClients)
    NumFDs += connection->SetFD(fd, send_fd, nfds);

  for (auto& connection : m_IPAddressFetchClients)
    NumFDs += connection->SetFD(fd, send_fd, nfds);

  if (m_UDPMainServerEnabled) {
    m_UDPMainServer->SetFD(fd, send_fd, nfds);
    ++NumFDs;
  } else if (m_UDPDeafSocket) {
    m_UDPDeafSocket->SetFD(fd, send_fd, nfds);
    ++NumFDs;
  }

  for (const auto& entry : m_GameServers) {
    if (auto server = entry.second) {
      server->SetFD(fd, send_fd, nfds);
      ++NumFDs;
    }
  }

  for (const auto& serverConnections : m_IncomingConnections) {
    // std::pair<uint16_t, vector<CConnection*>>
    for (const auto& connection : serverConnections.second) {
      NumFDs += connection->SetFD(fd, send_fd, nfds);;
    }
  }

  for (const auto& serverConnections : m_GameProxies) {
    // std::pair<uint16_t, vector<CTCPProxy*>>
    for (const auto& connection : serverConnections.second) {
      NumFDs += connection->SetFD(fd, send_fd, nfds);;
    }
  }

  for (const auto& serverConnections : m_GameSeekers) {
    // std::pair<uint16_t, vector<CGameSeeker*>>
    for (const auto& connection : serverConnections.second) {
      NumFDs += connection->SetFD(fd, send_fd, nfds);;
    }
  }

  for (const auto& serverConnections : m_GameObservers) {
    // std::pair<uint16_t, vector<CAsyncObserver*>>
    for (const auto& connection : serverConnections.second) {
      NumFDs += connection->SetFD(fd, send_fd, nfds);;
    }
  }  

  return NumFDs;
}

void CNet::UpdateBeforeGames(fd_set* fd, fd_set* send_fd)
{
  // if hosting a lobby, accept new connections to its game server

  for (const auto& entry : m_GameServers) {
    if (auto server = entry.second) {
      if (m_Aura->m_ExitingSoon) {
        server->Discard(fd);
        continue;
      }
      uint16_t localPort = entry.first;
      if (m_IncomingConnections[localPort].size() >= MAX_INCOMING_CONNECTIONS) {
        server->Discard(fd);
        continue;
      }
      CStreamIOSocket* socket = server->Accept(fd);
      if (socket) {
        if (m_Config.m_ProxyReconnect > 0) {
          CConnection* incomingConnection = new CConnection(m_Aura, localPort, socket);
          DPRINT_IF(LogLevel::kTrace2, "[AURA] incoming connection from " + incomingConnection->GetIPString())
          m_IncomingConnections[localPort].push_back(incomingConnection);
        } else if (m_Aura->m_Lobbies.empty() && m_Aura->m_JoinInProgressGames.empty()) {
          DPRINT_IF(LogLevel::kTrace2, "[AURA] connection to port " + to_string(localPort) + " rejected.")
          delete socket;
        } else {
          CConnection* incomingConnection = new CConnection(m_Aura, localPort, socket);
          DPRINT_IF(LogLevel::kTrace2, "[AURA] incoming connection from " + incomingConnection->GetIPString())
          m_IncomingConnections[localPort].push_back(incomingConnection);
        }
        if (m_IncomingConnections[localPort].size() >= MAX_INCOMING_CONNECTIONS) {
          PRINT_IF(LogLevel::kWarning, "[AURA] " + to_string(m_IncomingConnections[localPort].size()) + " connections at port " + to_string(localPort) + " - rejecting further connections")
        }
      }

      if (server->HasError()) {
        m_Aura->m_Exiting = true;
      }
    }
  }

  for (auto& serverConnections : m_IncomingConnections) {
    int64_t timeout = (int64_t)LinearInterpolation((float)serverConnections.second.size(), (float)1., (float)MAX_INCOMING_CONNECTIONS, (float)GAME_USER_CONNECTION_MAX_TIMEOUT, (float)GAME_USER_CONNECTION_MIN_TIMEOUT);
    for (auto i = begin(serverConnections.second); i != end(serverConnections.second);) {
      // *i is a pointer to a CConnection
      IncomingConnectionStatus result = (*i)->Update(fd, send_fd, timeout);
      if (result == IncomingConnectionStatus::kOk) {
        ++i;
        continue;
      }
      if ((*i)->GetSocket()) {
        (*i)->GetSocket()->DoSend(send_fd); // flush the socket
      }
      delete *i;
      i = serverConnections.second.erase(i);
    }
  }

  for (auto& serverConnections : m_GameProxies) {
    for (auto i = begin(serverConnections.second); i != end(serverConnections.second);) {
      // *i is a pointer to a CTCPProxy
      TCPProxyStatus result = (*i)->Update(fd, send_fd, GAME_USER_TIMEOUT_VANILLA);
      if (result == TCPProxyStatus::kOk) {
        ++i;
        continue;
      }
      if ((*i)->GetIncomingSocket()) {
        (*i)->GetIncomingSocket()->DoSend(send_fd); // flush the socket
      }
      if ((*i)->GetOutgoingSocket()) {
        (*i)->GetOutgoingSocket()->DoSend(send_fd); // flush the socket
      }
      delete *i;
      i = serverConnections.second.erase(i);
    }
  }

  for (auto& serverConnections : m_GameSeekers) {
    int64_t timeout = (int64_t)LinearInterpolation((float)serverConnections.second.size(), (float)1., (float)MAX_INCOMING_CONNECTIONS, (float)GAME_SEEKER_CONNECTION_MAX_TIMEOUT, (float)GAME_SEEKER_CONNECTION_MIN_TIMEOUT);
    for (auto i = begin(serverConnections.second); i != end(serverConnections.second);) {
      // *i is a pointer to a CGameSeeker
      GameSeekerStatus result = (*i)->Update(fd, send_fd, timeout);
      if (result == GameSeekerStatus::kOk) {
        ++i;
        continue;
      }
      if ((*i)->GetSocket()) {
        (*i)->GetSocket()->DoSend(send_fd); // flush the socket
      }
      delete *i;
      i = serverConnections.second.erase(i);
    }
  }

  for (auto& serverConnections : m_GameObservers) {
    for (auto i = begin(serverConnections.second); i != end(serverConnections.second);) {
      // *i is a pointer to a CAsyncObserver
      AsyncObserverStatus result = (*i)->Update(fd, send_fd, GAME_USER_TIMEOUT_VANILLA);
      if (result == AsyncObserverStatus::kOk) {
        ++i;
        continue;
      }
      if ((*i)->GetSocket()) {
        (*i)->GetSocket()->DoSend(send_fd); // flush the socket
      }
      delete *i;
      i = serverConnections.second.erase(i);
    }
  }
}

void CNet::UpdateAfterGames(fd_set* fd, fd_set* send_fd)
{
  if (m_HealthCheckInProgress) {
    bool anyPending = false;
    for (auto& testConnection : m_HealthCheckClients) {
      if (testConnection->Update(fd, send_fd)) {
        anyPending = true;
      }
    }
    if (!anyPending) {
      ReportHealthCheck();
    }
  }
  if (m_IPAddressFetchInProgress) {
    bool anyPending = false;
    for (auto& apiConnection : m_IPAddressFetchClients) {
      if (apiConnection->Update(fd, send_fd)) {
        anyPending = true;
      }
    }
    if (!anyPending) {
      HandleIPAddressFetchDone();
    }
  }

  if (m_UDPMainServerEnabled) {
    if (m_Aura->m_ExitingSoon) {
      m_UDPMainServer->Discard(fd);
    } else {
      UDPPkt* pkt = m_UDPMainServer->Accept(fd);
      if (pkt != nullptr) {
        HandleUDP(pkt);
        delete pkt->sender;
        delete pkt;
      }
    }
  } else if (m_UDPDeafSocket) {
    m_UDPDeafSocket->Discard(fd);
  }

  UpdateMapTransfers();
}

void CNet::UpdateMapTransfers()
{
  if (!m_Aura->GetTicksIsAfterDelay(m_LastDownloadTicks, 100)) {
    return;
  }

  m_TransferredMapBytesThisUpdate = 0;

  uint32_t downloadersCount = 0;
  multiset<shared_ptr<const CGame>> downloadersCountByGame;
  UserList downloaderPlayers;
  ObserverList downloaderObservers;

  for (const auto& game : m_Aura->m_Lobbies) {
    for (auto& user : game->GetUsers()) {
      if (!user->GetMapTransfer().GetIsInProgress()) continue;
      if (downloadersCount >= m_Config.m_MaxDownloaders) continue;
      // Sort to avoid cache deoptimization
      auto pos = lower_bound(downloaderPlayers.begin(), downloaderPlayers.end(), user, &GameUser::SortUsersByDownloadProgressAscending);
      downloaderPlayers.insert(pos, user);
      downloadersCountByGame.insert(game);
      ++downloadersCount;
    }
  }

  for (const auto& serverConnections : m_GameObservers) {
    for (const auto& observer : serverConnections.second) {
      if (!observer->GetMapTransfer().GetIsInProgress()) continue;
      if (!observer->GetGame() || observer->GetGame()->GetIsGameOver()) {
        observer->CloseConnection(false);
        continue;
      }
      if (downloadersCount >= m_Config.m_MaxDownloaders) continue;
      // Sort to avoid cache deoptimization
      auto pos = lower_bound(downloaderObservers.begin(), downloaderObservers.end(), observer, &CAsyncObserver::SortObserversByDownloadProgressAscending);
      downloaderObservers.insert(pos, observer);
      downloadersCountByGame.insert(observer->GetGame());
      ++downloadersCount;
    }
  }

  for (const auto& user : downloaderPlayers) {
    shared_ptr<CGame> game = user->GetGame();
    if (!game->GetMap()->GetMapFileIsValid()) {
      user->AddKickReason(GameUser::KickReason::MAP_MISSING);
      if (!user->HasLeftReason()) {
        user->SetLeftReason("autokicked - they don't have the map, and it cannot be transferred (corrupted)");
      }
      user->CloseConnection(false);
      continue;
    }

    bool mapIsInvalid = false;
    const uint8_t sendResult = user->NextSendMap();
    switch (sendResult) {
      case MAP_TRANSFER_IN_PROGRESS:
      case MAP_TRANSFER_DONE:
      case MAP_TRANSFER_NONE: // not really needed
      case MAP_TRANSFER_RATE_LIMITED: // not used
        break;
      case MAP_TRANSFER_MISSING:
      case MAP_TRANSFER_INVALID:
        mapIsInvalid = true;
        break;
    }

    if (mapIsInvalid) {
      // Flag the map as invalid so that new users joining the lobby get a notice that the map cannot be transferred (and get eventually kicked.)
      game->GetMap()->InvalidateMapFile();
      if (!user->HasLeftReason()) {
        user->SetLeftReason("autokicked - they don't have the map, and it cannot be transferred (deleted)");
      }
      user->CloseConnection(false);
    }
  }

  for (const auto& user : downloaderObservers) {
    shared_ptr<CGame> game = user->GetGame();
    if (!game->GetMap()->GetMapFileIsValid()) {
      if (!user->HasLeftReason()) {
        user->SetLeftReason("autokicked - they don't have the map, and it cannot be transferred (corrupted)");
      }
      user->CloseConnection(false);
      continue;
    }

    bool mapIsInvalid = false;
    const uint8_t sendResult = user->NextSendMap();
    switch (sendResult) {
      case MAP_TRANSFER_IN_PROGRESS:
      case MAP_TRANSFER_DONE:
      case MAP_TRANSFER_NONE: // not really needed
      case MAP_TRANSFER_RATE_LIMITED: // not used
        break;
      case MAP_TRANSFER_MISSING:
      case MAP_TRANSFER_INVALID:
        mapIsInvalid = true;
        break;
    }

    if (mapIsInvalid) {
      // Flag the map as invalid so that new users joining the lobby get a notice that the map cannot be transferred (and get eventually kicked.)
      game->GetMap()->InvalidateMapFile();
      if (!user->HasLeftReason()) {
        user->SetLeftReason("autokicked - they don't have the map, and it cannot be transferred (deleted)");
      }
      user->CloseConnection();
    }
  }

  m_LastDownloadTicks = m_Aura->GetLoopTicks();

  for (auto& game : m_Aura->GetJoinableGames()) {
    if (downloadersCountByGame.find(game) == downloadersCountByGame.end()) {
      // Keep memory usage in check.
      game->ClearLoadedMapChunk();
    }
  }
}

void CNet::SetBroadcastTarget(sockaddr_storage& subnet)
{
  if (subnet.ss_family != AF_INET) {
    Print("Must use IPv4 address for broadcast target");
    return;
  }
  SetAddressPort(&subnet, m_UDP4TargetPort);
  memcpy(m_MainBroadcastTarget, &subnet, sizeof(sockaddr_storage));
  memcpy(m_ProxyBroadcastTarget, &subnet, sizeof(sockaddr_storage));
  SetAddressPort(m_ProxyBroadcastTarget, m_UDP4TargetProxyPort);

  if (reinterpret_cast<sockaddr_in*>(&subnet)->sin_addr.s_addr != htonl(INADDR_BROADCAST))
    Print("[UDP] broadcasting LAN games to [" + AddressToString(subnet) + "]");
}

bool CNet::SendBroadcast(const vector<uint8_t>& packet)
{
  if (!m_Config.m_UDPBroadcastEnabled)
    return false;

  bool mainSuccess = false;
  if (m_UDPMainServerEnabled) {
    if (m_UDPMainServer->Broadcast(m_MainBroadcastTarget, packet)) mainSuccess = true;
    if (m_Config.m_ProxyReconnect) m_UDPMainServer->Broadcast(m_ProxyBroadcastTarget, packet);
  } else {
    if (m_UDPDeafSocket->Broadcast(m_MainBroadcastTarget, packet)) mainSuccess = true;
    if (m_Config.m_ProxyReconnect) {
      m_UDPDeafSocket->Broadcast(m_ProxyBroadcastTarget, packet);
    }
  }

  return mainSuccess;
}

void CNet::Send(const sockaddr_storage* address, const vector<uint8_t>& packet) const
{
  if (address->ss_family == AF_INET6 && !m_SupportUDPOverIPv6) {
    Print("[CONFIG] Game discovery message to " + AddressToStringStrict(*address) + " cannot be sent, because IPv6 support hasn't been enabled");
    Print("[CONFIG] Set <net.udp_ipv6.enabled = yes> if you want to enable it.");
    return;
  }

  if (address->ss_family == AF_INET6) {
    m_UDPIPv6Server->SendTo(address, packet);
  } else {
    if (m_UDPMainServerEnabled) {
      m_UDPMainServer->SendTo(address, packet);
    } else {
      m_UDPDeafSocket->SendTo(address, packet);
    }
  }
}

void CNet::Send(const string& addressLiteral, const vector<uint8_t>& packet) const
{
  optional<sockaddr_storage> maybeAddress = ParseAddress(addressLiteral);
  if (!maybeAddress.has_value())
    return;

  sockaddr_storage* address = &(maybeAddress.value());
  SetAddressPort(address, address->ss_family == AF_INET6 ? m_UDP6TargetPort : m_UDP4TargetPort);
  Send(address, packet);
}

void CNet::Send(const string& addressLiteral, const uint16_t port, const vector<uint8_t>& packet) const
{
  optional<sockaddr_storage> maybeAddress = ParseAddress(addressLiteral);
  if (!maybeAddress.has_value())
    return;

  sockaddr_storage* address = &(maybeAddress.value());
  SetAddressPort(address, port);
  Send(address, packet);
}

void CNet::SendLoopback(const vector<uint8_t>& packet)
{
  Send("127.0.0.1", m_UDP4TargetPort, packet);
  if (m_Config.m_ProxyReconnect) {
    Send("127.0.0.1", m_UDP4TargetProxyPort, packet);
  }
}

void CNet::SendArbitraryUnicast(const string& addressLiteral, const uint16_t port, const vector<uint8_t>& packet)
{
  optional<sockaddr_storage> maybeAddress = ParseAddress(addressLiteral);
  if (!maybeAddress.has_value())
    return;

  sockaddr_storage* address = &(maybeAddress.value());
  SetAddressPort(address, port);

  if (m_Config.m_UDPBroadcastEnabled)
    PropagateBroadcastEnabled(false);

  Send(address, packet);

  if (m_Config.m_UDPBroadcastEnabled)
    PropagateBroadcastEnabled(true);
}

void CNet::SendGameDiscovery(const vector<uint8_t>& packet, const vector<sockaddr_storage>& clientIps)
{
  SendBroadcast(packet);

  if (!clientIps.empty()) {
    if (m_Config.m_UDPBroadcastEnabled)
      PropagateBroadcastEnabled(false);

    for (auto& clientIp : clientIps)
      Send(&clientIp, packet);

    if (m_Config.m_UDPBroadcastEnabled)
      PropagateBroadcastEnabled(true);
  }

  if (m_Config.m_EnableTCPWrapUDP || m_Config.m_VLANEnabled) {
    for (auto& serverConnections : m_GameSeekers) {
      for (auto& connection : serverConnections.second) {
        if (connection->GetDeleteMe()) continue;
        if (connection->GetIsUDPTunnel()) {
          connection->Send(packet);
        }
      }
    }
  }
}

bool CNet::IsIgnoredDatagramSource(string sourceIp)
{
  string element(sourceIp);
  return m_Config.m_UDPBlockedIPs.find(element) != m_Config.m_UDPBlockedIPs.end();
}

GameUser::CGameUser* CNet::GetReconnectTargetUser(const uint32_t gameID, const uint8_t UID) const
{
  GameUser::CGameUser* matchUser = nullptr;

  for (auto& game : m_Aura->m_StartedGames) {
    if (game->GetGameID() != gameID) continue;
    if (game->GetGameLoaded() && !game->GetIsGameOver() && game->GetIsProxyReconnectable()) {
      GameUser::CGameUser* user = game->GetUserFromUID(UID);
      if (user && !user->GetDeleteMe() && user->GetCanReconnect()) {
        matchUser = user;
      }
    }
    break;
  }

  return matchUser;
}

GameUser::CGameUser* CNet::GetReconnectTargetUserLegacy(const uint8_t UID, const uint32_t reconnectKey) const
{
  GameUser::CGameUser* matchUser = nullptr;

  for (auto& game : m_Aura->m_StartedGames) {
    if (game->GetGameLoaded() && !game->GetIsGameOver() && game->GetIsProxyReconnectable()) {
      GameUser::CGameUser* user = game->GetUserFromUID(UID);
      if (user && !user->GetDeleteMe() && user->GetCanReconnect() && !user->GetGProxy()->GetCheckGameID() && user->GetGProxy()->GetReconnectKey() == reconnectKey) {
        matchUser = user;
        break;
      }
    }
  }

  return matchUser;
}

void CNet::HandleUDP(UDPPkt* pkt)
{
  // pkt->buf->length at least W3GS_UDP_MIN_PACKET_SIZE

  if (pkt->sender->ss_family != AF_INET && pkt->sender->ss_family != AF_INET6) {
    return;
  }

  uint16_t remotePort = GetAddressPort(pkt->sender);
  string ipAddress = AddressToString(*(pkt->sender));

  if (IsIgnoredDatagramSource(ipAddress)) {
    return;
  }

  if (m_Config.m_UDPForwardTraffic) {
    RelayUDPPacket(pkt, ipAddress, remotePort);
  }

  if (pkt->buf[0] != GameProtocol::Magic::W3GS_HEADER) {
    return;
  }

  if (!(pkt->length >= 16 && pkt->buf[1] == GameProtocol::Magic::SEARCHGAME)) {
    return;
  }

  bool isExpansion = false;
  if (memcmp(pkt->buf + 4, ProductID_TFT, 4) == 0) {
    isExpansion = true;
  } else if (memcmp(pkt->buf + 4, ProductID_ROC, 4) != 0) {
    return;
  }

  const Version requestVersion = GAMEVER(1, pkt->buf[8]);

  DPRINT_IF(LogLevel::kTrace3, "[NET] IP " + ipAddress + " searching games from port " + to_string(remotePort) + "...")

  for (const auto& game : m_Aura->GetJoinableGames()) {
    if (!game->GetUDPEnabled() || !game->GetIsStageAcceptingJoins()) {
      continue;
    }
    if (isExpansion != game->GetIsExpansion()) {
      continue;
    }
    if (pkt->buf[8] == 0 || game->GetIsSupportedGameVersion(requestVersion)) {
      DPRINT_IF(LogLevel::kTrace3, "[NET] Sent game info to " + ipAddress + ":" + to_string(remotePort) + "...")
      game->ReplySearch(pkt->sender, pkt->socket, requestVersion);

      // When we get GAME_SEARCH from a remote port other than 6112, we still announce to port 6112.
      if (remotePort != m_UDP4TargetPort && GetInnerIPVersion(pkt->sender) == AF_INET) {
        game->AnnounceToAddress(ipAddress, requestVersion);
      }
    }
  }
}

void CNet::RelayUDPPacket(const UDPPkt* pkt, const string& fromAddress, const uint16_t fromPort) const
{
  vector<uint8_t> relayPacket = {GameProtocol::Magic::W3FW_HEADER, 0, 0, 0};
  AppendByteArrayString(relayPacket, fromAddress, true);
  size_t portOffset = relayPacket.size();
  relayPacket.resize(portOffset + 6 + pkt->length);
  relayPacket[portOffset] = static_cast<uint8_t>(fromPort >> 8); // Network-byte-order (Big-endian)
  relayPacket[portOffset + 1] = static_cast<uint8_t>(fromPort);
  memset(relayPacket.data() + portOffset + 2, 0, 4); // Game version unknown at this layer.
  memcpy(relayPacket.data() + portOffset + 6, &(pkt->buf), pkt->length);
  AssignLength(relayPacket);
  Send(&(m_Config.m_UDPForwardAddress), relayPacket);
}

sockaddr_storage* CNet::GetPublicIPv4()
{
  switch (m_Config.m_PublicIPv4Algorithm) {
    case NET_PUBLIC_IP_ADDRESS_ALGORITHM_MANUAL: {
      if (m_IPv4SelfCacheV.first == m_Config.m_PublicIPv4Value && m_IPv4SelfCacheT == NET_PUBLIC_IP_ADDRESS_ALGORITHM_MANUAL) {
        return m_IPv4SelfCacheV.second;
      }
      if (m_IPv4SelfCacheV.second != nullptr) {
        delete m_IPv4SelfCacheV.second;
        m_IPv4SelfCacheV = make_pair(string(), nullptr);
      }

      optional<sockaddr_storage> maybeAddress = CNet::ParseAddress(m_Config.m_PublicIPv4Value, ACCEPT_IPV4);
      if (!maybeAddress.has_value()) return nullptr; // should never happen
      sockaddr_storage* cachedAddress = new sockaddr_storage();
      memcpy(cachedAddress, &(maybeAddress.value()), sizeof(sockaddr_storage));
      m_IPv4SelfCacheV = make_pair(m_Config.m_PublicIPv4Value, cachedAddress);
      m_IPv4SelfCacheT = NET_PUBLIC_IP_ADDRESS_ALGORITHM_MANUAL;
      return m_IPv4SelfCacheV.second;
    }
    case NET_PUBLIC_IP_ADDRESS_ALGORITHM_API: {
      if (m_IPv4SelfCacheV.first == m_Config.m_PublicIPv4Value && m_IPv4SelfCacheT == NET_PUBLIC_IP_ADDRESS_ALGORITHM_API) {
        return m_IPv4SelfCacheV.second;
      }
      if (m_IPv4SelfCacheV.second != nullptr) {
        delete m_IPv4SelfCacheV.second;
        m_IPv4SelfCacheV = make_pair(string(), nullptr);
      }
#ifndef DISABLE_CPR
      auto response = cpr::Get(cpr::Url{m_Config.m_PublicIPv4Value}, cpr::Timeout{3000});
      if (response.status_code != 200) {
        return nullptr;
      }

      optional<sockaddr_storage> maybeAddress = CNet::ParseAddress(response.text, ACCEPT_IPV4);
      if (!maybeAddress.has_value()) return nullptr;
      sockaddr_storage* cachedAddress = new sockaddr_storage();
      memcpy(cachedAddress, &(maybeAddress.value()), sizeof(sockaddr_storage));
      m_IPv4SelfCacheV = make_pair(m_Config.m_PublicIPv4Value, cachedAddress);
      m_IPv4SelfCacheT = NET_PUBLIC_IP_ADDRESS_ALGORITHM_API;
#endif
      return m_IPv4SelfCacheV.second;
    }
    case NET_PUBLIC_IP_ADDRESS_ALGORITHM_NONE:
    default:
      return nullptr;
  }
}

sockaddr_storage* CNet::GetPublicIPv6()
{
  switch (m_Config.m_PublicIPv6Algorithm) {
    case NET_PUBLIC_IP_ADDRESS_ALGORITHM_MANUAL: {
      if (m_IPv6SelfCacheV.first == m_Config.m_PublicIPv6Value && m_IPv6SelfCacheT == NET_PUBLIC_IP_ADDRESS_ALGORITHM_MANUAL) {
        return m_IPv6SelfCacheV.second;
      }
      if (m_IPv6SelfCacheV.second != nullptr) {
        delete m_IPv6SelfCacheV.second;
        m_IPv6SelfCacheV = make_pair(string(), nullptr);
      }

      optional<sockaddr_storage> maybeAddress = CNet::ParseAddress(m_Config.m_PublicIPv6Value, ACCEPT_IPV6);
      if (!maybeAddress.has_value()) return nullptr; // should never happen
      sockaddr_storage* cachedAddress = new sockaddr_storage();
      memcpy(cachedAddress, &(maybeAddress.value()), sizeof(sockaddr_storage));
      m_IPv6SelfCacheV = make_pair(m_Config.m_PublicIPv6Value, cachedAddress);
      m_IPv6SelfCacheT = NET_PUBLIC_IP_ADDRESS_ALGORITHM_MANUAL;
      return m_IPv6SelfCacheV.second;
    }
    case NET_PUBLIC_IP_ADDRESS_ALGORITHM_API: {
      if (m_IPv6SelfCacheV.first == m_Config.m_PublicIPv6Value && m_IPv6SelfCacheT == NET_PUBLIC_IP_ADDRESS_ALGORITHM_API) {
        return m_IPv6SelfCacheV.second;
      }
      if (m_IPv6SelfCacheV.second != nullptr) {
        delete m_IPv6SelfCacheV.second;
        m_IPv6SelfCacheV = make_pair(string(), nullptr);
      }
#ifndef DISABLE_CPR
      auto response = cpr::Get(cpr::Url{m_Config.m_PublicIPv6Value}, cpr::Timeout{3000});
      if (response.status_code != 200) {
        return nullptr;
      }

      optional<sockaddr_storage> maybeAddress = CNet::ParseAddress(response.text, ACCEPT_IPV6);
      if (!maybeAddress.has_value()) {
        return nullptr;
      }
      sockaddr_storage* cachedAddress = new sockaddr_storage();
      memcpy(cachedAddress, &(maybeAddress.value()), sizeof(sockaddr_storage));
      m_IPv6SelfCacheV = make_pair(m_Config.m_PublicIPv6Value, cachedAddress);
      m_IPv6SelfCacheT = NET_PUBLIC_IP_ADDRESS_ALGORITHM_API;
#endif
      return m_IPv6SelfCacheV.second;
    }
    case NET_PUBLIC_IP_ADDRESS_ALGORITHM_NONE:
    default:
      return nullptr;
  }
}

#ifndef DISABLE_MINIUPNP
uint8_t CNet::RequestUPnP(const NetProtocol protocolCode, const uint16_t externalPort, const uint16_t internalPort, const LogLevel logLevel, bool ignoreCache)
{
  struct UPNPDev* devlist = nullptr;
  struct UPNPDev* device;
  struct UPNPUrls urls;
  struct IGDdatas data;
  char lanaddr[64] = "unset";
  char wanaddr[64] = "unset";

  string protocol;
  switch (protocolCode) {
    case NetProtocol::kTCP: {
      protocol = "TCP";
      if (!ignoreCache) {
        auto cacheEntry = m_UPnPTCPCache.find(make_pair(externalPort, internalPort));
        if (cacheEntry != m_UPnPTCPCache.end() && !m_Aura->GetTimeIsAfterDelay(cacheEntry->second.first, 10800)) {
          return cacheEntry->second.second;
        }
      }
      break;
    }
    case NetProtocol::kUDP: {
      protocol = "UDP";
      if (!ignoreCache) {
        auto cacheEntry = m_UPnPUDPCache.find(make_pair(externalPort, internalPort));
        if (cacheEntry != m_UPnPUDPCache.end() && !m_Aura->GetTimeIsAfterDelay(cacheEntry->second.first, 10800)) {
          return cacheEntry->second.second;
        }
      }
      break;
    }
    IGNORE_ENUM_LAST(NetProtocol)
  }

  PRINT_IF(LogLevel::kNotice, "[NET] Requesting UPnP port-mapping (" + protocol + ") " + to_string(externalPort) + " -> " + to_string(internalPort))

  devlist = upnpDiscover(2000, nullptr, nullptr, 0, 0, 2, 0);
  uint8_t success = 0;

  string extPort = to_string(externalPort);
  string intPort = to_string(internalPort);

  // Iterate through the discovered devices
  for (device = devlist; device; device = device->pNext) {
    // Get the UPnP URLs and IGD data for this device
    int type = UPNP_GetValidIGD(device, &urls, &data, lanaddr, sizeof(lanaddr), wanaddr, sizeof(wanaddr));
    bool isSupported = false;
    switch (type)
    {
      case UPNP_NO_IGD: // 0
      case UPNP_DISCONNECTED_IGD: // 3
      case UPNP_UNKNOWN_DEVICE: { // 4
        if (logLevel >= LogLevel::kDebug) Print("[UPNP] unsupported device found: <" + string(device->descURL) + ">");
        break;
      }
      case UPNP_CONNECTED_IGD: { // 1
        if (logLevel >= LogLevel::kDebug) Print("[UPNP] connected gateway found: <" + string(urls.controlURL) + ">");
        isSupported = true;
        break;
      }
      case UPNP_PRIVATEIP_IGD: { // 2
        if (logLevel >= LogLevel::kDebug) Print("[UPNP] NAT3 found: <" + string(urls.controlURL) + ">");
        isSupported = true;
        break;
      }
    }
    if (!isSupported) {
      continue;
    }

    if (logLevel >= LogLevel::kInfo) Print("[UPNP] trying to forward traffic to LAN address " + string(lanaddr) + "...");

    int result = UPNP_AddPortMapping(urls.controlURL, data.first.servicetype, extPort.c_str(), intPort.c_str(), lanaddr, "Warcraft 3 game hosting", protocol.c_str(), nullptr, "86400");
    if (result == UPNPCOMMAND_SUCCESS) {
      success = success | (1 << (type - 1));
    } else if (logLevel >= LogLevel::kInfo) {
      switch (result) {
        case UPNPCOMMAND_UNKNOWN_ERROR:
          Print("[UPNP] failed to add port mapping - unknown error");
          break;
        case UPNPCOMMAND_INVALID_ARGS:
          Print("[UPNP] failed to add port mapping - invalid args");
          break;
        case UPNPCOMMAND_HTTP_ERROR:
          Print("[UPNP] failed to add port mapping - HTTP error");
          break;
        case UPNPCOMMAND_INVALID_RESPONSE:
          Print("[UPNP] failed to add port mapping - invalid response");
          break;
        case UPNPCOMMAND_MEM_ALLOC_ERROR:
          Print("[UPNP] failed to add port mapping - memory allocation error");
          break;
      }
    }

    // Free UPnP URLs and IGD data
    FreeUPNPUrls(&urls);
    //FreeIGDdatas(&data);
  }

  // Free the UPnP device list
  freeUPNPDevlist(devlist);

  if (logLevel >= LogLevel::kInfo) {
    if (success == 0) {
      Print("[UPNP] Universal Plug and Play is not supported by the host router.");
    } else if (0 != (success & 1)) {
      Print("[UPNP] forwarding " + protocol + " external port " + extPort + " to internal port " + intPort + " OK.");
    } else {
      Print("[UPNP] warning - multi-layer NAT detected, port-forwarding may fail.");
    }
  }

  switch (protocolCode) {
    case NetProtocol::kTCP: {
      m_UPnPTCPCache[make_pair(externalPort, internalPort)] = TimedUint8(m_Aura->GetLoopTime(), success);
      break;
    }
    case NetProtocol::kUDP: {
      m_UPnPUDPCache[make_pair(externalPort, internalPort)] = TimedUint8(m_Aura->GetLoopTime(), success);
      break;
    }
    IGNORE_ENUM_LAST(NetProtocol)
  }

  return success;
}
#endif

bool CNet::QueryHealthCheck(shared_ptr<CCommandContext> ctx, const uint8_t checkMode, shared_ptr<CRealm> targetRealm, shared_ptr<const CGame> game)
{
  if (m_Aura->m_ExitingSoon || m_HealthCheckInProgress) {
    return false;
  }

  bool isVerbose = 0 != (checkMode & HEALTH_CHECK_VERBOSE);
  const uint16_t gamePort = game->GetHostPortFromType(GAME_DISCOVERY_INTERFACE_IPV4);
  const uint32_t hostCounter = game->GetHostCounter();

  if (0 != (checkMode & HEALTH_CHECK_LOOPBACK_IPV4)) {
    sockaddr_storage loopBackAddress;
    memset(&loopBackAddress, 0, sizeof(sockaddr_storage));
    loopBackAddress.ss_family = AF_INET;
    sockaddr_in* addr4 = reinterpret_cast<sockaddr_in*>(&loopBackAddress);
    addr4->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr4->sin_port = htons(gamePort);
    m_HealthCheckClients.push_back(new CGameTestConnection(m_Aura, nullptr, loopBackAddress, hostCounter, CONNECTION_TYPE_LOOPBACK, "[Loopback]"));
  }

  if (0 != (checkMode & HEALTH_CHECK_LOOPBACK_IPV6)) {
    sockaddr_storage loopBackAddress;
    memset(&loopBackAddress, 0, sizeof(sockaddr_storage));
    loopBackAddress.ss_family = AF_INET6;
    sockaddr_in6* addr6 = reinterpret_cast<sockaddr_in6*>(&loopBackAddress);
    memcpy(&(addr6->sin6_addr), &in6addr_loopback, sizeof(in6_addr));
    addr6->sin6_port = htons(gamePort);
    m_HealthCheckClients.push_back(new CGameTestConnection(m_Aura, nullptr, loopBackAddress, hostCounter, CONNECTION_TYPE_LOOPBACK, "[Loopback IPv6]"));
  }

  sockaddr_storage* publicIPv4 = GetPublicIPv4();
  sockaddr_storage* publicIPv6 = GetPublicIPv6();
  if (publicIPv4 == nullptr && (0 != (checkMode & HEALTH_CHECK_PUBLIC_IPV4)) && m_Config.m_PublicIPv4Algorithm != NET_PUBLIC_IP_ADDRESS_ALGORITHM_NONE) {
    Print("[NET] Public IPv4 address unknown - check <net.ipv4.public_address.algorithm>, <net.ipv4.public_address.value>");
  }
  if (publicIPv6 == nullptr && (0 != (checkMode & HEALTH_CHECK_PUBLIC_IPV6)) && m_Config.m_PublicIPv6Algorithm != NET_PUBLIC_IP_ADDRESS_ALGORITHM_NONE) {
    Print("[NET] Public IPv6 address unknown - check <net.ipv6.public_address.algorithm>, <net.ipv6.public_address.value>");
  }

  bool anySendsPublicIp = false;
  for (const auto& realm : m_Aura->m_Realms) {
    if ((0 == (checkMode & HEALTH_CHECK_REALM)) && realm != targetRealm) {
      continue;
    }
    const sockaddr_storage* selfIPInThisRealm = realm->GetUsesCustomIPAddress() ? realm->GetPublicHostAddress() : publicIPv4;
    if (publicIPv4 == nullptr) {
      continue;
    }
    uint8_t connectionType = CONNECTION_TYPE_DEFAULT;
    uint16_t port = realm->GetUsesCustomPort() ? realm->GetPublicHostPort() : gamePort;
    if (realm->GetIsVPN()) {
      connectionType = connectionType | CONNECTION_TYPE_VPN;
    }
    if (realm->GetUsesCustomIPAddress()) {
      connectionType = connectionType | CONNECTION_TYPE_CUSTOM_IP_ADDRESS;
    }
    if (realm->GetUsesCustomPort()) {
      connectionType = connectionType | CONNECTION_TYPE_CUSTOM_PORT;
    }

    sockaddr_storage targetHost;
    memcpy(&targetHost, selfIPInThisRealm, sizeof(sockaddr_storage));
    SetAddressPort(&targetHost, port);
    m_HealthCheckClients.push_back(new CGameTestConnection(m_Aura, realm, targetHost, hostCounter, connectionType, realm->GetUniqueDisplayName()));

    if (reinterpret_cast<const sockaddr_in*>(selfIPInThisRealm)->sin_addr.s_addr == reinterpret_cast<const sockaddr_in*>(publicIPv4)->sin_addr.s_addr) {
      anySendsPublicIp = true;
    }
  }
  if (!anySendsPublicIp && publicIPv4 != nullptr && (
    (0 != (checkMode & HEALTH_CHECK_PUBLIC_IPV4)))
  ) {
    sockaddr_storage targetHost;
    memcpy(&targetHost, publicIPv4, sizeof(sockaddr_storage));
    SetAddressPort(&targetHost, gamePort);
    m_HealthCheckClients.push_back(new CGameTestConnection(m_Aura, nullptr, targetHost, hostCounter, 0, "[Public IPv4]"));
  }
  if (publicIPv6 != nullptr && !IN6_IS_ADDR_UNSPECIFIED(&(reinterpret_cast<sockaddr_in6*>(publicIPv6)->sin6_addr)) && (
    (0 != (checkMode & HEALTH_CHECK_PUBLIC_IPV6))
    )) {
    sockaddr_storage targetHost;
    memcpy(&targetHost, publicIPv6, sizeof(sockaddr_storage));
    SetAddressPort(&targetHost, gamePort);
    m_HealthCheckClients.push_back(new CGameTestConnection(m_Aura, nullptr, targetHost, hostCounter, CONNECTION_TYPE_IPV6, "[Public IPv6]"));
  }

  if (m_HealthCheckClients.empty()) {
    return false;
  }

  m_HealthCheckVerbose = isVerbose;
  m_HealthCheckContext = ctx;
  m_HealthCheckInProgress = true;
  return true;
}

void CNet::ResetHealthCheck()
{
  if (!m_HealthCheckInProgress)
    return;

  if (m_Aura->MatchLogLevel(LogLevel::kDebug)) {
    Print("[NET] Reset health check");
  }

  for (auto& testConnection : m_HealthCheckClients) {
    delete testConnection;
  }
  m_HealthCheckClients.clear();
  m_HealthCheckInProgress = false;
  m_HealthCheckContext.reset();
}

void CNet::ReportHealthCheck()
{
  bool hasDirectAttempts = false;
  bool anyDirectSuccess = false;
  vector<string> ChatReport;
  set<uint16_t> failedPorts;
  bool isIPv6Reachable = false;
  for (auto& testConnection : m_HealthCheckClients) {
    string listedSuffix;
    if (!testConnection->GetIsRealmListed()) {
      listedSuffix = ", unlisted";
    }
    bool success = false;
    string ResultText;
    if (!testConnection->GetIsRealmOnline()) {
      ResultText = "Realm offline";
    } else {
      if (testConnection->m_Passed.value_or(false)) {
        success = true;
        ResultText = "OK" + listedSuffix;
      } else if (testConnection->m_CanConnect.value_or(false)) {
        ResultText = "Error" + listedSuffix;
      } else {
        ResultText = "Offline" + listedSuffix;
      }
    }
    ChatReport.push_back(testConnection->m_Name + " - " + ResultText);
    if (!m_HealthCheckContext->GetWritesToStdout()) {
      Print("[AURA] Game at " + testConnection->m_Name + " - " + ResultText);
    }
    if (0 == (testConnection->m_Type & ~(CONNECTION_TYPE_CUSTOM_PORT))) {
      hasDirectAttempts = true;
      if (success) anyDirectSuccess = true;
    }
    if (0 != (testConnection->m_Type & (CONNECTION_TYPE_IPV6))) {
      if (success) isIPv6Reachable = true;
    }
    if (m_HealthCheckVerbose && !success) {
      failedPorts.insert(testConnection->GetPort());
    }
  }
  sockaddr_storage* publicIPv4 = GetPublicIPv4();
  if (publicIPv4 != nullptr && hasDirectAttempts) {
    string portForwardInstructions;
    if (m_HealthCheckVerbose && m_HealthCheckContext->GetSourceGame() != nullptr && m_HealthCheckContext->GetSourceGame()->GetIsLobbyStrict()) {
      portForwardInstructions = "About port-forwarding: Setup your router to forward external port(s) {" + JoinStrings(failedPorts, false) + "} to internal port(s) {" + JoinStrings(GetPotentialGamePorts(), false) + "}";
    }
    if (anyDirectSuccess) {
      Print("[NET] This bot CAN be reached through the IPv4 Internet. Address: " + AddressToString(*publicIPv4));
      if (!m_HealthCheckContext->GetWritesToStdout()) {
        m_HealthCheckContext->SendAll("This bot CAN be reached through the IPv4 Internet.");
      }
    } else {
      Print("[NET] This bot is disconnected from the IPv4 Internet, because its public address is unreachable. Address: " + AddressToString(*publicIPv4));
      Print("[NET] Please setup port-forwarding to allow connections.");
      if (m_HealthCheckVerbose) {
        if (!portForwardInstructions.empty())
          Print("[NET] " + portForwardInstructions);
#ifndef DISABLE_MINIUPNP
        Print("[NET] If your router has Universal Plug and Play, the command [upnp] will automatically setup port-forwarding.");
#endif
        Print("[NET] Note that you may still play online if you got a VPN, or an active tunnel. See NETWORKING.md for details.");
        Print("[NET] But make sure your firewall allows Aura inbound TCP connections.");
        if (!m_HealthCheckContext->GetWritesToStdout()) {
          m_HealthCheckContext->SendAll("============= READ IF YOU ARE RUNNING AURA =====================================");
          m_HealthCheckContext->SendAll("This bot is disconnected from the IPv4 Internet, because its public IPv4 address is unreachable.");
          m_HealthCheckContext->SendAll("Please setup port-forwarding to allow connections.");
          m_HealthCheckContext->SendAll(portForwardInstructions);
#ifndef DISABLE_MINIUPNP
          m_HealthCheckContext->SendAll("If your router has Universal Plug and Play, the command [upnp] will automatically setup port-forwarding.");
#endif
          m_HealthCheckContext->SendAll("Note that you may still play online if you got a VPN, or an active tunnel. See NETWORKING.md for details.");
          m_HealthCheckContext->SendAll("But make sure your firewall allows Aura inbound TCP connections.");
          m_HealthCheckContext->SendAll("=================================================================================");
        }
      }
    }
  }
  sockaddr_storage* publicIPv6 = GetPublicIPv6();
  if (publicIPv6 != nullptr) {
    if (isIPv6Reachable) {
      Print("[NET] This bot CAN be reached through the IPv6 Internet. Address: " + AddressToString(*publicIPv6));
      Print("[NET] See NETWORKING.md for instructions to use IPv6 TCP tunneling.");
    } else {
      Print("[NET] This bot is disconnected from the IPv6 Internet, because its public address is unreachable. Address: " + AddressToString(*publicIPv6));
    }
    if (!m_HealthCheckContext->GetWritesToStdout()) {
      if (isIPv6Reachable) {
        m_HealthCheckContext->SendAll("This bot CAN be reached through the IPv6 Internet.");
        m_HealthCheckContext->SendAll("See NETWORKING.md for instructions to use IPv6 TCP tunneling.");
        m_HealthCheckContext->SendAll("=================================================================================");
      } else {
        Print("[NET] This bot is disconnected from the IPv6 Internet, because its public address is unreachable. Address: " + AddressToString(*publicIPv6) + ".");
        m_HealthCheckContext->SendAll("This bot is disconnected from the IPv6 Internet, because its public address is unreachable.");
        m_HealthCheckContext->SendAll("=================================================================================");
      }
    }
  }
  m_HealthCheckContext->SendAll(JoinStrings(ChatReport, " | ", false));
  ResetHealthCheck();
}

bool CNet::QueryIPAddress()
{
  if (m_IPAddressFetchInProgress) {
    return false;
  }

  if (m_Config.m_PublicIPv4Algorithm == NET_PUBLIC_IP_ADDRESS_ALGORITHM_API) {
    if (m_IPv4SelfCacheV.first != m_Config.m_PublicIPv4Value || m_IPv4SelfCacheT != NET_PUBLIC_IP_ADDRESS_ALGORITHM_API) {
      if (m_IPv4SelfCacheV.second != nullptr) {
        delete m_IPv4SelfCacheV.second;
        m_IPv4SelfCacheV = make_pair(string(), nullptr);
      }
      optional<tuple<string, string, uint16_t, string>> parsedURL = CNet::ParseURL(m_Config.m_PublicIPv4Value);
      if (parsedURL.has_value() && get<0>(parsedURL.value()) == "http:") {
        string hostName = get<1>(parsedURL.value());
        uint16_t port = get<2>(parsedURL.value());
        string path = get<3>(parsedURL.value());
        sockaddr_storage resolvedAddress;
        if (ResolveHostName(resolvedAddress, ACCEPT_IPV4, hostName, port)) {
          CIPAddressAPIConnection* client = new CIPAddressAPIConnection(m_Aura, resolvedAddress, path, hostName);
          m_IPAddressFetchClients.push_back(client);
          m_IPAddressFetchInProgress = true;
        }
      }
    }
  }
  if (m_Config.m_PublicIPv6Algorithm == NET_PUBLIC_IP_ADDRESS_ALGORITHM_API) {
    if (m_IPv6SelfCacheV.first != m_Config.m_PublicIPv6Value || m_IPv6SelfCacheT != NET_PUBLIC_IP_ADDRESS_ALGORITHM_API) {
      if (m_IPv6SelfCacheV.second != nullptr) {
        delete m_IPv6SelfCacheV.second;
        m_IPv6SelfCacheV = make_pair(string(), nullptr);
      }
      optional<tuple<string, string, uint16_t, string>> parsedURL = CNet::ParseURL(m_Config.m_PublicIPv6Value);
      if (parsedURL.has_value() && get<0>(parsedURL.value()) == "http:") {
        string hostName = get<1>(parsedURL.value());
        uint16_t port = get<2>(parsedURL.value());
        string path = get<3>(parsedURL.value());
        sockaddr_storage resolvedAddress;
        if (ResolveHostName(resolvedAddress, ACCEPT_IPV6, hostName, port)) {
          CIPAddressAPIConnection* client = new CIPAddressAPIConnection(m_Aura, resolvedAddress, path, hostName);
          m_IPAddressFetchClients.push_back(client);
          m_IPAddressFetchInProgress = true;
        }
      }
    }
  }

  return m_IPAddressFetchInProgress;
}

void CNet::ResetIPAddressFetch()
{
  if (!m_IPAddressFetchInProgress)
    return;

  if (m_Aura->MatchLogLevel(LogLevel::kDebug)) {
    Print("[NET] Reset IP address fetch");
  }

  for (auto& apiClient : m_IPAddressFetchClients) {
    delete apiClient;
  }
  m_IPAddressFetchClients.clear();
  m_IPAddressFetchInProgress = false;
}

void CNet::HandleIPAddressFetchDone()
{
  for (auto& apiClient : m_IPAddressFetchClients) {
    if (!apiClient->m_Result.has_value()) continue;
    sockaddr_storage* cachedAddress = new sockaddr_storage();
    memcpy(cachedAddress, &(apiClient->m_Result.value()), sizeof(sockaddr_storage));
    if (apiClient->m_TargetHost.ss_family == AF_INET6) {
      m_IPv6SelfCacheV = make_pair(m_Config.m_PublicIPv6Value, cachedAddress);
      m_IPv6SelfCacheT = m_Config.m_PublicIPv6Algorithm;
    } else {
      m_IPv4SelfCacheV = make_pair(m_Config.m_PublicIPv4Value, cachedAddress);
      m_IPv4SelfCacheT = m_Config.m_PublicIPv4Algorithm;
    }
  }
  ResetIPAddressFetch();
  HandleIPAddressFetchDoneCallback();
}

void CNet::HandleIPAddressFetchDoneCallback()
{
  // CAura::CreateGame() skips joinable checks if we are still figuring out our IP address,
  // so let's do them now that we know our address.
  CheckJoinableLobbies();
}

void CNet::CheckJoinableLobbies()
{
  // TODO: Missing support for concurrent (or even serial) joinable checks for multiple lobbies.
  for (const auto& lobby : m_Aura->m_Lobbies) {
    if (lobby->GetIsCheckJoinable()) {
      uint8_t checkMode = HEALTH_CHECK_ALL;
      if (!m_SupportTCPOverIPv6) {
        checkMode &= ~HEALTH_CHECK_PUBLIC_IPV6;
        checkMode &= ~HEALTH_CHECK_LOOPBACK_IPV6;
      }
      if (lobby->GetIsVerbose()) {
        checkMode |= HEALTH_CHECK_VERBOSE;
      }
      shared_ptr<CCommandContext> ctx = nullptr;
      try {
        ctx = make_shared<CCommandContext>(ServiceType::kCLI /* using CLI for CNet::CheckJoinableLobbies() so that it outputs to console only */, m_Aura, string(), false, &cout);
      } catch (...) {
      }
      if (ctx) {
        QueryHealthCheck(ctx, checkMode, nullptr, lobby);
      }
      lobby->SetIsCheckJoinable(false);
    }
  }
}

uint16_t CNet::GetUDPPort(const uint8_t protocol) const
{
  if (protocol == AF_INET) {
    return m_UDPMainServerEnabled ? m_UDPMainServer->GetPort() : m_UDPDeafSocket->GetPort();
  } else if (protocol == AF_INET6) {
    if (m_SupportUDPOverIPv6) {
      return m_UDPIPv6Server->GetPort();
    }
  }
  return 0;
}

uint16_t CNet::NextHostPort()
{
  ++m_LastHostPort;
  if (m_LastHostPort > m_Config.m_MaxHostPort || m_LastHostPort < m_Config.m_MinHostPort) {
    m_LastHostPort = m_Config.m_MinHostPort;
  }
  return m_LastHostPort;
}

void CNet::MergeDownGradedConnections()
{
  // when a GProxy reconnect is triggered, while there is still a CStreamIOSocket assigned to the GameUser::CGameUser,
  // the old CStreamIOSocket is assigned to a new CGameConnection, which is queued for insertion into m_IncomingConnections
  // this method takes care of the insertion
  while (!m_DownGradedConnections.empty()) {
    const auto& entry = m_DownGradedConnections.front();
    m_IncomingConnections[entry.first].push_back(entry.second);
    m_DownGradedConnections.pop();
  }
}

void CNet::UpdateSelectBlockTime(int64_t& usecBlockTime) const
{
  if (usecBlockTime == 0) {
    return;
  }

  const int64_t hiResTicks = GetTicks();
  int64_t byTicks = APP_MAX_TICKS;
  for (const auto& serverConnections : m_GameObservers) {
    for (const auto& connection : serverConnections.second) {
      const int64_t thisByTicks = connection->GetNextTimedActionByTicks();
      if (thisByTicks <= hiResTicks) {
        usecBlockTime = 0;
        return;
      }
      if (thisByTicks < byTicks) {
        byTicks = thisByTicks;
      }
    }
  }
  if (byTicks != APP_MAX_TICKS) { // avoid overflow
    int64_t maybeBlockTime = (byTicks - hiResTicks) * 1000;
    if (maybeBlockTime < usecBlockTime) {
      usecBlockTime = maybeBlockTime;
    }
  }
}

vector<uint16_t> CNet::GetPotentialGamePorts() const
{
  vector<uint16_t> result;

  uint16_t port = m_Config.m_MinHostPort;
  if (m_Config.m_UDPEnableCustomPortTCP4 && m_Config.m_UDPCustomPortTCP4 < port) {
    result.push_back(m_Config.m_UDPCustomPortTCP4);
  }
  while (port <= m_Config.m_MaxHostPort) {
    result.push_back(port);
    ++port;
  }
  if (m_Config.m_UDPEnableCustomPortTCP4 && m_Config.m_UDPCustomPortTCP4 > port) {
    result.push_back(m_Config.m_UDPCustomPortTCP4);
  }
  
  return result;
}

optional<tuple<string, string, uint16_t, string>> CNet::ParseURL(const string& address)
{
  if (address.empty()) return nullopt;
  const size_t colonIndex = address.find(':');
  if (colonIndex == string::npos || (colonIndex != 4 && colonIndex != 5)) return nullopt;
  string protocol = address.substr(0, colonIndex + 1);
  const uint16_t port = protocol == "https:" ? 443 : 80;
  if (address.length() <= protocol.length() + 2 ||
    address[protocol.length()] != '/' ||
    address[protocol.length() + 1] != '/'
  ) {
    return nullopt;
  }
  size_t pathIndex = address.find('/', protocol.length() + 2);
  if (pathIndex == string::npos) pathIndex = address.size();
  string hostName = address.substr(protocol.length() + 2, pathIndex - (protocol.length() + 2));
  string path = pathIndex >= address.length() ? "/" : address.substr(pathIndex);
  tuple<string, string, uint16_t, string> URL(protocol, hostName, port, path);
  optional<tuple<string, string, uint16_t, string>> result;
  result = URL;
  return result;
}

optional<sockaddr_storage> CNet::ParseAddress(const string& address, const uint8_t inputMode)
{
  std::optional<sockaddr_storage> result;
  if (0 != (inputMode & ACCEPT_IPV4)) {
    sockaddr_in addr4;
    memset(&addr4, 0, sizeof(addr4));
    addr4.sin_family = AF_INET;
    if (inet_pton(AF_INET, address.c_str(), &addr4.sin_addr) == 1) {
      struct sockaddr_storage ipv4;
      memset(&ipv4, 0, sizeof(ipv4));
      ipv4.ss_family = AF_INET;
      memcpy(&ipv4, &addr4, sizeof(addr4));
      result = ipv4;
      return result;
    }
  }

  if (0 != (inputMode & ACCEPT_IPV6)) {
    sockaddr_in6 addr6;
    memset(&addr6, 0, sizeof(addr6));
    addr6.sin6_family = AF_INET6;
    if (inet_pton(AF_INET6, address.c_str(), &addr6.sin6_addr) == 1) {
      struct sockaddr_storage ipv6;
      memset(&ipv6, 0, sizeof(ipv6));
      ipv6.ss_family = AF_INET6;
      memcpy(&ipv6, &addr6, sizeof(addr6));
      result = ipv6;
      return result;
    }
  }

  return result;
}

bool CNet::ResolveHostNameInner(sockaddr_storage& address, const string& hostName, const uint16_t port, const uint8_t family, map<string, sockaddr_storage*>& cache)
{
  auto it = cache.find(hostName);
  if (it != end(cache)) {
    // Output to address argument
    memset(&address, 0, sizeof(sockaddr_storage));
    memcpy(&address, it->second, sizeof(sockaddr_storage));
    SetAddressPort(&address, port);
    return true;
  }

  struct addrinfo hints, *p;
  int status;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = family;
  hints.ai_socktype = SOCK_STREAM;

  if ((status = getaddrinfo(hostName.c_str(), nullptr, &hints, &p)) != 0) {
#ifdef _WIN32
    Print("[DNS] cannot resolve address for " + hostName + " - error " + to_string(status));
#else
    Print("[DNS] cannot resolve address for " + hostName + gai_strerror(status));
#endif
    Print("[DNS] warning - check your Internet connection");
    return false;
  }
  
  if (p == nullptr) {
    return false;
  }

  // Save to cache
  sockaddr_storage* cacheAddress = new sockaddr_storage();
  memcpy(cacheAddress, reinterpret_cast<sockaddr_storage*>(p->ai_addr), sizeof(sockaddr_storage));
  SetAddressPort(cacheAddress, port);
  cache[hostName] = cacheAddress;

  // Output to address argument
  memcpy(&address, cacheAddress, sizeof(sockaddr_storage));

  freeaddrinfo(p);
  return true;
}

bool CNet::ResolveHostName(sockaddr_storage& address, const uint8_t acceptFamily, const string& hostName, const uint16_t port)
{
  optional<sockaddr_storage> parseResult = ParseAddress(hostName, acceptFamily);
  if (parseResult.has_value()) {
    memcpy(&address, &(parseResult.value()), sizeof(sockaddr_storage));
    SetAddressPort(&address, port);
    return true;
  }
  if (0 != (acceptFamily & ACCEPT_IPV4)) {
    if (ResolveHostNameInner(address, hostName, port, AF_INET, m_IPv4DNSCache)) {
      return true;
    }
  }
  if (0 != (acceptFamily & ACCEPT_IPV6)) {
    if (ResolveHostNameInner(address, hostName, port, AF_INET6, m_IPv6DNSCache)) {
      return true;
    }
  }
  return false;
}

shared_ptr<CTCPServer> CNet::GetOrCreateTCPServer(uint16_t inputPort, const string& name)
{
  {
    auto it = m_GameServers.find(inputPort);
    if (it != m_GameServers.end()) {
      Print("[TCP] " + name + " assigned to port " + to_string(inputPort));
      return it->second;
    }
    // it maybe invalidated
  }

  shared_ptr<CTCPServer> gameServer = make_shared<CTCPServer>(m_SupportTCPOverIPv6 ? AF_INET6 : AF_INET);
  if (!gameServer->Listen(m_SupportTCPOverIPv6 ? m_Config.m_BindAddress6 : m_Config.m_BindAddress4, inputPort, false)) {
    Print("[TCP] " + name + " Error listening on port " + to_string(inputPort));
    gameServer.reset();
    return gameServer;
  }
  uint16_t assignedPort = gameServer->GetPort();
  m_GameServers[assignedPort] = gameServer;
  m_IncomingConnections[assignedPort] = vector<CConnection*>();
  m_GameProxies[assignedPort] = vector<CTCPProxy*>();
  m_GameSeekers[assignedPort] = vector<CGameSeeker*>();
  m_GameObservers[assignedPort] = vector<CAsyncObserver*>();

  Print("[TCP] " + name + " listening on port " + to_string(assignedPort));
  return gameServer;
}

void CNet::FlushDNSCache()
{
  if (m_Aura->MatchLogLevel(LogLevel::kDebug)) {
    Print("[NET] Flushing DNS cache");
  }
  for (auto& entry : m_IPv4DNSCache) {
    if (entry.second != nullptr) {
      delete entry.second;
      entry.second = nullptr;
    }
  }
  for (auto& entry : m_IPv6DNSCache) {
    if (entry.second != nullptr) {
      delete entry.second;
      entry.second = nullptr;
    }
  }
  m_IPv4DNSCache.clear();
  m_IPv6DNSCache.clear();
}

void CNet::FlushSelfIPCache()
{
  if (m_Aura->MatchLogLevel(LogLevel::kDebug)) {
    Print("[NET] Flushing self IP cache");
  }
  if (m_IPv4SelfCacheV.second != nullptr)
    delete m_IPv4SelfCacheV.second;
  if (m_IPv6SelfCacheV.second != nullptr)
    delete m_IPv6SelfCacheV.second;
  m_IPv4SelfCacheV = make_pair(string(), nullptr);
  m_IPv4SelfCacheT = NET_PUBLIC_IP_ADDRESS_ALGORITHM_INVALID;
  m_IPv6SelfCacheV = make_pair(string(), nullptr);
  m_IPv6SelfCacheT = NET_PUBLIC_IP_ADDRESS_ALGORITHM_INVALID;
}

void CNet::FlushOutgoingThrottles()
{
  m_OutgoingThrottles.clear();
}

void CNet::PropagateBroadcastEnabled(const bool nEnable)
{
  if (m_UDPMainServerEnabled) {
    m_UDPMainServer->SetBroadcastEnabled(nEnable);
  } else {
    m_UDPDeafSocket->SetBroadcastEnabled(nEnable);
  }
}

void CNet::PropagateDoNotRouteEnabled(const bool nEnable)
{
  if (m_UDPMainServerEnabled) {
    m_UDPMainServer->SetDontRoute(nEnable);
  } else {
    m_UDPDeafSocket->SetDontRoute(nEnable);
  }

  if (m_SupportUDPOverIPv6) {
    m_UDPIPv6Server->SetDontRoute(nEnable);
  }
}

void CNet::OnConfigReload()
{
  m_UDP6TargetPort = m_Config.m_UDP6TargetPort;
  PropagateBroadcastEnabled(m_Config.m_UDPBroadcastEnabled);
  PropagateDoNotRouteEnabled(m_Config.m_UDPDoNotRouteEnabled);
  SetBroadcastTarget(m_Config.m_UDPBroadcastTarget);
}

void CNet::OnUserKicked(GameUser::CGameUser* user, bool deferred)
{
  CStreamIOSocket* socket = user->GetSocket();
  if (!socket) return;
  socket->ClearRecvBuffer();
  CConnection* connection = new CConnection(*user);
  connection->SetType(IncomingConnectionType::kKickedPlayer);
  connection->SetTimeout(2000);
  if (deferred) {
    m_DownGradedConnections.push(make_pair(connection->GetPort(), connection));
  } else {
    m_IncomingConnections[connection->GetPort()].push_back(connection);
  }
  user->SetSocket(nullptr);
}

void CNet::RegisterGameProxy(CConnection* connection, shared_ptr<CGame> game)
{
  CStreamIOSocket* socket = connection->GetSocket();
  if (!socket) return;
  CTCPProxy* proxy = new CTCPProxy(connection, game);
  m_GameProxies[proxy->GetPort()].push_back(proxy);
  connection->SetSocket(nullptr);
}

void CNet::RegisterGameSeeker(CConnection* connection, IncomingConnectionType nType)
{
  CStreamIOSocket* socket = connection->GetSocket();
  if (!socket) return;
  DPRINT_IF(LogLevel::kTrace, "[NET] registering game seeker from " + connection->GetIPString() + " (type " + ToDecString(nType) + ")")
  CGameSeeker* seeker = new CGameSeeker(connection, nType);
  m_GameSeekers[seeker->GetPort()].push_back(seeker);
  connection->SetSocket(nullptr);
  seeker->Init();
}

void CNet::EventGameReset(shared_ptr<const CGame> game)
{
  for (auto& serverConnections : m_GameObservers) {
    for (auto& connection : serverConnections.second) {
      connection->EventGameReset(game);
    }
  }
}

void CNet::EventRealmDeleted(shared_ptr<const CRealm> realm)
{
  for (auto& serverConnections : m_GameObservers) {
    for (auto& connection : serverConnections.second) {
      connection->EventRealmDeleted(realm);
    }
  }
}

void CNet::ClearStaleServers()
{
  // noop, hold onto our ports
  /*
  for (auto it = begin(m_GameServers); it != end(m_GameServers);) {
    if (it->second.expired()) {
      it = m_GameServers.erase(it);
    } else {
      ++it;
    }
  }
  */
}

void CNet::GracefulExit()
{
  ResetHealthCheck();
  ResetIPAddressFetch();

  for (auto& serverConnections : m_IncomingConnections) {
    for (auto& connection : serverConnections.second) {
      connection->SetType(IncomingConnectionType::kKickedPlayer);
      connection->SetTimeout(2000);
      connection->GetSocket()->ClearRecvBuffer();
    }
  }

  for (auto& serverConnections : m_GameProxies) {
    for (auto& connection : serverConnections.second) {
      connection->SetType(TCPProxyType::kExiting);
      connection->SetTimeout(2000);
      if (connection->GetIncomingSocket()) {
        connection->GetIncomingSocket()->ClearRecvBuffer();
      }
      if (connection->GetOutgoingSocket()) {
        connection->GetOutgoingSocket()->ClearRecvBuffer();
      }
    }
  }

  for (auto& serverConnections : m_GameSeekers) {
    for (auto& connection : serverConnections.second) {
      connection->SetType(IncomingConnectionType::kKickedPlayer);
      connection->SetTimeout(2000);
      connection->GetSocket()->ClearRecvBuffer();
    }
  }

  for (auto& serverConnections : m_GameObservers) {
    for (auto& connection : serverConnections.second) {
      connection->SetType(IncomingConnectionType::kKickedPlayer);
      connection->SetTimeout(2000);
      connection->GetSocket()->ClearRecvBuffer();
    }
  }  
}

bool CNet::CheckGracefulExit() const
{
  for (const auto& serverConnections : m_IncomingConnections) {
    if (!serverConnections.second.empty()) {
      return false;
    }
  }

  for (const auto& serverConnections : m_GameProxies) {
    if (!serverConnections.second.empty()) {
      return false;
    }
  }

  for (const auto& serverConnections : m_GameSeekers) {
    if (!serverConnections.second.empty()) {
      return false;
    }
  }

  // m_GameObservers are already checked in GetIsStandby()

  if (!m_DownGradedConnections.empty()) {
    return false;
  }

  return true;  
}

bool CNet::GetIsStandby() const
{
  if (m_HealthCheckInProgress) return false;

  for (const auto& serverConnections : m_GameObservers) {
    if (!serverConnections.second.empty()) {
      return false;
    }
  }

  return true;
}

CNet::~CNet()
{
  if (m_Aura->MatchLogLevel(LogLevel::kDebug)) {
    Print("[NET] shutting down");
  }

  delete m_UDPMainServer;
  delete m_UDPDeafSocket;
  delete m_UDPIPv6Server;
  delete m_MainBroadcastTarget;
  delete m_ProxyBroadcastTarget;

  for (auto& entry : m_GameServers) {
    entry.second.reset();
  }
  m_GameServers.clear();

  if (m_VLANServer) {
    m_VLANServer.reset();
  }

  FlushDNSCache();
  FlushSelfIPCache();
  ResetHealthCheck();
  ResetIPAddressFetch();
  FlushOutgoingThrottles();
  m_HealthCheckContext.reset();

  CMDNS::Destroy(m_MDNS);

  if (m_Aura->MatchLogLevel(LogLevel::kDebug)) {
    Print("[NET] shutdown ok");
  }
}
