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

#ifndef AURA_TCP_PROXY_H_
#define AURA_TCP_PROXY_H_

#include "../includes.h"
#include "../socket.h"
#include "../game.h"

enum class TCPProxyStatus : uint8_t
{
  kOk = 0u,
  kDestroy = 1u,
  kPromoted = 2u,
};

enum class TCPProxyType : uint8_t
{
  kDumb = 0u,
  kParser = 1u,
  kReconnectableIncoming = 2u,
  kReconnectableOutgoing = 4u,
  kExiting = 8u,
};

//
// CTCPProxy
//

class CTCPProxy
{
public:
  CAura*                                            m_Aura;
  TCPProxyType                                      m_Type;
  std::optional<Version>                            m_GameVersion;
  uint16_t                                          m_Port;
  bool                                              m_DeleteMe;
  bool                                              m_ClientPaused;
  bool                                              m_ServerPaused;
  CStreamIOSocket*                                  m_IncomingSocket;
  CTCPClient*                                       m_OutgoingSocket;
  std::weak_ptr<CGame>                              m_Game;
  std::optional<int64_t>                            m_TimeoutTicks;

  CTCPProxy(CConnection* nConnection, std::shared_ptr<CGame> nGame);
  ~CTCPProxy();

  [[nodiscard]] inline bool HasGameVersion() const { return m_GameVersion.has_value(); }
  [[nodiscard]] inline const std::optional<Version>& GetMaybeGameVersion() const { return m_GameVersion; }
  [[nodiscard]] inline const Version& GetGameVersion() const { return m_GameVersion.value(); }
  [[nodiscard]] inline uint16_t GetPort() const { return m_Port; }
  [[nodiscard]] inline bool GetDeleteMe() const { return m_DeleteMe; }
  [[nodiscard]] inline CStreamIOSocket* GetIncomingSocket() const { return m_IncomingSocket; }
  [[nodiscard]] inline CStreamIOSocket* GetOutgoingSocket() const { return m_OutgoingSocket; }
  inline void SetType(TCPProxyType nType) { m_Type = nType; }

  // processing functions

  void TryRewriteGame();
  void SetTimeout(const int64_t nTicks);
  bool CloseConnection();
  TCPProxyStatus TransferBuffer(fd_set* fd, CStreamIOSocket* fromSocket, CStreamIOSocket* toSocket, bool* pausedRecvFlag, int64_t timeout);
  [[nodiscard]] TCPProxyStatus Update(fd_set* fd, fd_set* send_fd, int64_t timeout);

  // other functions

  uint32_t SetFD(fd_set* fd, fd_set* send_fd, int32_t* nfds) const;
  void SendClient(const std::vector<uint8_t>& data);
  void SendServer(const std::vector<uint8_t>& data);
};

#endif // AURA_TCP_PROXY_H_
