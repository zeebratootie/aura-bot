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

#ifndef AURA_GPROXY_SERVER_H_
#define AURA_GPROXY_SERVER_H_

#include "../includes.h"
#include "../socket.h"
#include "../protocol/game_protocol.h"

//
// CGProxyServer
//

class CGProxyServer
{
public:
  CAura*                                            m_Aura;
  std::reference_wrapper<CConnection>               m_Connection;
  bool                                              m_IsEnabled;              // if the player is using GProxy
  bool                                              m_IsExtended;             // if the player is using GProxyDLL
  bool                                              m_SupportsExtended;
  bool                                              m_CheckGameID;
  uint8_t                                           m_UID;
  uint16_t                                          m_Port;                   // port where  will try to reconnect
  uint32_t                                          m_Key;                    // the ++ reconnect key
  uint32_t                                          m_Version;
  size_t                                            m_BufferSize;
  size_t                                            m_TotalRecvPackets;
  size_t                                            m_TotalSentPackets;
  std::optional<int64_t>                            m_LastAckTicks;           // when we last acknowledged GProxy packet
  std::queue<GameProtocol::PacketWrapper>           m_Buffer;                 // buffer with data used with GProxy

  CGProxyServer(CConnection* nConnection);
  ~CGProxyServer();

  [[nodiscard]] CStreamIOSocket&             GetSocket() const;
  [[nodiscard]] inline size_t                GetRecvPacketsCount() const { return m_TotalRecvPackets; }
  [[nodiscard]] inline size_t                GetSentPacketsCount() const { return m_TotalSentPackets; }
  [[nodiscard]] inline size_t                GetUnqueuedPacketsCount() const { return m_TotalSentPackets - m_BufferSize; }
  [[nodiscard]] inline uint32_t              GetReconnectKey() const { return m_Key; }
  [[nodiscard]] inline bool                  GetCheckGameID() const { return m_CheckGameID; }
  [[nodiscard]] inline bool                  GetIsEnabled() const { return m_IsEnabled; }
  [[nodiscard]] inline bool                  GetIsLegacy() const { return m_IsEnabled && !m_IsExtended; }
  [[nodiscard]] inline bool                  GetIsExtended() const { return m_IsExtended; }

  [[nodiscard]] bool                         ValidateReconnect(const uint32_t reconnectKey, const uint32_t lastPacket) const;

  inline void                                AddRecvPacket() { ++m_TotalRecvPackets; }

  void CheckSendAck();
  bool UnqueuePackets(const size_t lastPacket);
  void SynchronizeReconnectKeyFromClient(const uint32_t reconnectKey);
  void RotateReconnectKey() const;
  void Init(uint8_t UID, uint32_t version, uint16_t port, uint8_t emptyActions, bool supportsExtended, int64_t extendedWaitTicks, uint64_t gameID);
  void Disable();

  void EventGameStart();
  void EventSendData(const std::vector<uint8_t>& data, bool isLoaded);
  void EventSendData(const GameProtocol::PacketWrapper& data, bool isLoaded);
  void SynchronizeFromBuffer();

  void StartExtendedHandShake(int64_t waitTicks, uint32_t gameID) const;
  GProxyExtendedClientResult ConfirmExtended(const std::string_view data);
  void UpdateEmptyActions(uint8_t emptyActions) const;
  void CheckExtendedStartHandShake() const;
};

#endif // AURA_GPROXY_SERVER_H_
