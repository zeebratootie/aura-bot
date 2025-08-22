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

#ifndef AURA_NET_H_
#define AURA_NET_H_

#include "includes.h"
#include "socket.h"
#include "mdns.h"
#include "config/config_net.h"

#pragma once

constexpr uint8_t NOT_CONNECTION_TYPE_CUSTOM_PORT = NOT_TINY(CONNECTION_TYPE_CUSTOM_PORT);
constexpr uint8_t NOT_HEALTH_CHECK_PUBLIC_IPV6 = NOT_TINY(HEALTH_CHECK_PUBLIC_IPV6);
constexpr uint8_t NOT_HEALTH_CHECK_LOOPBACK_IPV6 = NOT_TINY(HEALTH_CHECK_LOOPBACK_IPV6);

//
// CNet
//

class CGameTestConnection
{
public:
  CGameTestConnection(CAura* nAura, std::shared_ptr<CRealm> nRealm, sockaddr_storage nTargetHost, const uint32_t nBaseHostCounter, const uint8_t nType, const std::string& nName);
  ~CGameTestConnection();

  [[nodiscard]] uint32_t  SetFD(fd_set* fd, fd_set* send_fd, int32_t* nfds) const;
  [[nodiscard]] bool      Update(fd_set* fd, fd_set* send_fd);
  [[nodiscard]] bool      QueryGameInfo();
  [[nodiscard]] bool      GetIsRealmOnline() const;
  [[nodiscard]] bool      GetIsRealmListed() const;
  [[nodiscard]] uint32_t  GetHostCounter() const;
  [[nodiscard]] uint16_t  GetPort() const;

  sockaddr_storage            m_TargetHost;
  CAura*                      m_Aura;
  uint32_t                    m_RealmInternalId;
  uint32_t                    m_BaseHostCounter;
  CTCPClient*                 m_Socket;
  uint8_t                     m_Type;
  std::string                 m_Name;
  std::optional<bool>         m_Passed;
  std::optional<bool>         m_CanConnect;
  int64_t                     m_TimeoutTicks;
  int64_t                     m_LastConnectionFailureTicks;
  bool                        m_SentJoinRequest;
};

class CIPAddressAPIConnection
{
public:
  CIPAddressAPIConnection(CAura* nAura, const sockaddr_storage& nTargetHost, const std::string& nEndPoint, const std::string& nHostName);
  ~CIPAddressAPIConnection();

  [[nodiscard]] uint32_t  SetFD(fd_set* fd, fd_set* send_fd, int32_t* nfds);
  [[nodiscard]] bool      Update(fd_set* fd, fd_set* send_fd);
  bool                    QueryIPAddress();

  sockaddr_storage                  m_TargetHost;
  CAura*                            m_Aura;
  CTCPClient*                       m_Socket;
  std::string                       m_EndPoint;
  std::string                       m_HostName;
  std::optional<sockaddr_storage>   m_Result;
  std::optional<bool>               m_CanConnect;
  int64_t                           m_TimeoutTicks;
  int64_t                           m_LastConnectionFailureTicks;
  bool                              m_SentQuery;
};

class CNet
{
public:
  CNet(CConfig& nCFG);
  ~CNet();

  CAura*                                                      m_Aura;
  CNetConfig                                                  m_Config;
#ifndef DISABLE_MDNS
  DNSServiceRef                                               m_MDNS;                                                 
#else
  void*                                                       m_MDNS;
#endif

  // == SECTION START ==
  // Implements non-reloadable config entries.
  bool                                                        m_SupportUDPOverIPv6;
  bool                                                        m_SupportTCPOverIPv6;
  bool                                                        m_VLANEnabled;
  bool                                                        m_UDPMainServerEnabled;      // (IPv4) whether the bot should listen to UDP traffic in port 6112)
  uint16_t                                                    m_UDPFallbackPort;
  uint16_t                                                    m_UDPIPv6Port;
  uint16_t                                                    m_VLANPort;
  // == SECTION END ==

  CUDPServer*                                                 m_UDPMainServer;             // (IPv4) UDP I/O at port 6112. Supports broadcasts. May also act as reverse-proxy for UDP traffic.
  CUDPServer*                                                 m_UDPDeafSocket;             // (IPv4) UDP outbound traffic. Uses <net.udp_fallback.outbound_port> (should NOT be 6112). Supports broadcasts.
  CUDPServer*                                                 m_UDPIPv6Server;
  std::shared_ptr<CTCPServer>                                 m_VLANServer;

  uint16_t                                                    m_UDP4TargetPort;
  uint16_t                                                    m_UDP4TargetGProxyLANPort;
  uint16_t                                                    m_UDP6TargetPort;             // only unicast
  sockaddr_storage                                            m_MainBroadcastTarget;
  sockaddr_storage                                            m_ProxyReconnectLANBroadcastTarget;

  std::map<uint16_t, std::shared_ptr<CTCPServer>>             m_GameServers;
  std::map<uint16_t, std::vector<CConnection*>>               m_IncomingConnections;        // connections that haven't identified their protocol yet
  std::map<uint16_t, std::vector<CTCPProxy*>>                 m_GameProxies;
  std::map<uint16_t, std::vector<CGameSeeker*>>               m_GameSeekers;                // connections that use complementary protocols, such as VLAN, or UDP over TCP
  std::map<uint16_t, std::vector<CAsyncObserver*>>            m_GameObservers;              // connections that are CAsyncObserver
  std::queue<std::pair<uint16_t, CConnection*>>               m_DownGradedConnections;      // connections that are waiting for insertion into m_IncomingConnections, built from a stale CStreamIOSocket
  std::map<std::pair<uint16_t, uint16_t>, TimedUint8>         m_UPnPTCPCache;
  std::map<std::pair<uint16_t, uint16_t>, TimedUint8>         m_UPnPUDPCache;
  std::map<std::string, sockaddr_storage*>                    m_IPv4DNSCache;
  std::map<std::string, sockaddr_storage*>                    m_IPv6DNSCache;
  std::pair<std::string, sockaddr_storage*>                   m_IPv4SelfCacheV;
  uint8_t                                                     m_IPv4SelfCacheT;
  std::pair<std::string, sockaddr_storage*>                   m_IPv6SelfCacheV;
  uint8_t                                                     m_IPv6SelfCacheT;
  std::multiset<NetworkHost>                                  m_OutgoingPendingConnections;
  std::map<NetworkHost, TimedUint8>                           m_OutgoingThrottles;

  std::vector<CGameTestConnection*>                           m_HealthCheckClients;
  std::vector<CIPAddressAPIConnection*>                       m_IPAddressFetchClients;
  bool                                                        m_HealthCheckVerbose;
  bool                                                        m_HealthCheckInProgress;
  std::shared_ptr<CCommandContext>                            m_HealthCheckContext;
  bool                                                        m_IPAddressFetchInProgress;
  uint16_t                                                    m_LastHostPort;               // the port of the last hosted game

  int64_t                                                     m_LastDownloadTicks;             // when the last map download cycle was performed
  uint64_t                                                    m_TransferredMapBytesThisUpdate;

  void InitPersistentConfig();
  bool Init();
  [[nodiscard]] uint32_t SetFD(fd_set* fd, fd_set* send_fd, int32_t* nfds);

  void UpdateBeforeGames(fd_set* fd, fd_set* send_fd);
  void UpdateAfterGames(fd_set* fd, fd_set* send_fd);
  void UpdateMapTransfers();

  void SendBroadcast(const std::vector<uint8_t>& packet);
  void Send(const sockaddr_storage* address, const std::vector<uint8_t>& packet) const;
  void Send(const std::string& addressLiteral, const std::vector<uint8_t>& packet) const;
  void Send(const std::string& addressLiteral, const uint16_t port, const std::vector<uint8_t>& packet) const;
  void SendLoopback(const std::vector<uint8_t>& packet);
  void SendArbitraryUnicast(const std::string& addressLiteral, const uint16_t port, const std::vector<uint8_t>& packet);
  void SendGameDiscovery(const std::vector<uint8_t>& packet, const std::vector<sockaddr_storage>& clientIps);
  void HandleUDP(UDPPkt pkt);
  void RelayUDPPacket(const UDPPkt pkt, const std::string& fromAddress, const uint16_t fromPort) const;

  [[nodiscard]] sockaddr_storage*               GetPublicIPv4();
  [[nodiscard]] sockaddr_storage*               GetPublicIPv6();
  
  [[nodiscard]] std::vector<uint16_t>           GetPotentialGamePorts() const;
  [[nodiscard]] uint16_t                        GetUDPPort(const uint8_t protocol) const;

  bool                                          ResolveHostName(sockaddr_storage& address, const uint8_t nAcceptFamily, const std::string& hostName, const uint16_t port);
  bool                                          ResolveHostNameInner(sockaddr_storage& address, const std::string& hostName, const uint16_t port, const uint8_t nFamily, std::map<std::string, sockaddr_storage*>&);
  [[nodiscard]] std::shared_ptr<CTCPServer>     GetOrCreateTCPServer(uint16_t, const std::string& name);
  void                                          FlushDNSCache();
  void                                          FlushSelfIPCache();
  void                                          FlushOutgoingThrottles();

#ifndef DISABLE_MINIUPNP
  uint8_t RequestUPnP(const NetProtocol protocolCode, const uint16_t externalPort, const uint16_t internalPort, const LogLevel logLevel, bool ignoreCache = false);
#endif
  bool QueryHealthCheck(std::shared_ptr<CCommandContext> ctx, const uint32_t checkMode, std::shared_ptr<CRealm> realm, std::shared_ptr<const CGame> game);
  void ResetHealthCheck();
  void ReportHealthCheck();

  int64_t GetThrottleTime(const NetworkHost& host, int64_t minTime = 0) const;
  bool GetIsOutgoingThrottled(const NetworkHost& host) const;
  void ResetOutgoingThrottled(const NetworkHost& host);
  void OnThrottledConnectionStart(const NetworkHost& host);
  void OnThrottledConnectionSuccess(const NetworkHost& host);
  void OnThrottledConnectionError(const NetworkHost& host);
  bool QueryIPAddress();
  void ResetIPAddressFetch();
  void HandleIPAddressFetchDone();
  void HandleIPAddressFetchDoneCallback();
  void CheckJoinableLobbies();
  
  uint16_t NextHostPort();
  void     MergeDownGradedConnections();

  void UpdateSelectBlockTime(int64_t& usecBlockTime) const;

  [[nodiscard]] static std::optional<std::tuple<std::string, std::string, uint16_t, std::string>> ParseURL(const std::string& address);
  [[nodiscard]] static std::optional<sockaddr_storage> ParseAddress(const std::string& address, const uint8_t inputMode = ACCEPT_ANY);

  void                                   SetBroadcastTarget(sockaddr_storage& subnet);
  void                                   PropagateBroadcastEnabled(const bool nEnable);
  void                                   PropagateDoNotRouteEnabled(const bool nEnable);
  void                                   OnConfigReload();
  void                                   OnUserKicked(GameUser::CGameUser* user, bool deferred = false);
  void                                   RegisterGameProxy(CConnection* connection, std::shared_ptr<CGame> nGame);
  void                                   RegisterGameSeeker(CConnection* connection, IncomingConnectionType nType);
  void                                   EventGameReset(std::shared_ptr<const CGame> nGame);
  void                                   EventRealmDeleted(std::shared_ptr<const CRealm> nRealm);
  void                                   ClearStaleServers();
  void                                   GracefulExit();
  bool                                   CheckGracefulExit() const;
  bool                                   GetIsStandby() const;

  [[nodiscard]] bool                                   IsIgnoredDatagramSource(std::string sourceIp);
  [[nodiscard]] bool                                   GetIsFetchingIPAddresses() const { return m_IPAddressFetchInProgress; }
  [[nodiscard]] GameUser::CGameUser*                             GetReconnectTargetUser(const uint32_t gameID, const uint8_t UID) const;
  [[nodiscard]] GameUser::CGameUser*                             GetReconnectTargetUserLegacy(const uint8_t UID, const uint32_t reconnectKey) const;
};

#endif // AURA_NET_H_
