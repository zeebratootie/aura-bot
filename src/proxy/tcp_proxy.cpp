/*

  Copyright [2025] [Leonardo Julca]

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

#include <utility>

#include "tcp_proxy.h"

#include "../aura.h"
#include "../game.h"
#include "../protocol/game_protocol.h"
#include "../game_user.h"
#include "../socket.h"

using namespace std;

//
// CTCPProxy
//

CTCPProxy::CTCPProxy(CConnection* nConnection, shared_ptr<CGame> nGame)
  : m_Aura(nConnection->m_Aura),
    m_Type(TCPProxyType::kDumb),
    m_Port(nConnection->GetPort()),
    m_DeleteMe(false),
    m_ClientPaused(false),
    m_ServerPaused(false),
    m_OutgoingSocket(nullptr),
    m_Game(nGame)
{
  m_IncomingSocket = nConnection->GetSocket();
  m_OutgoingSocket = new CTCPClient(AF_INET, nGame->GetGameName());
}

CTCPProxy::~CTCPProxy()
{
  delete m_IncomingSocket;
  delete m_OutgoingSocket;
}

void CTCPProxy::SetTimeout(const int64_t delta)
{
  m_TimeoutTicks = GetTicks() + delta;
}

bool CTCPProxy::CloseConnection()
{
  if (m_IncomingSocket->GetConnected()) {
    m_IncomingSocket->Close();
  }
  if (m_OutgoingSocket->GetConnected()) {
    m_OutgoingSocket->Close();
  }
  return true;
}

TCPProxyStatus CTCPProxy::TransferBuffer(fd_set* fd, CStreamIOSocket* fromSocket, CStreamIOSocket* toSocket, bool* pausedRecvFlag, int64_t timeout)
{
  constexpr size_t kHighWatermark = 65536;
  constexpr size_t kLowWatermark  = 8192;
  string::size_type pendingBytes = toSocket->GetSendBufferSize();

  if (pendingBytes >= kHighWatermark || (*pausedRecvFlag && pendingBytes > kLowWatermark)) {
    if (m_Aura->GetTicksIsAfterDelay(fromSocket->GetLastRecv(), timeout)) {
      PRINT_IF(LogLevel::kDebug, "Terminating stalled proxy.")
      m_DeleteMe = true;
      return TCPProxyStatus::kDestroy;
    }
    *pausedRecvFlag = true;
    return TCPProxyStatus::kOk;
  }

  *pausedRecvFlag = false;

  if (fromSocket->DoRecv(fd)) {
    AppendSwapString(fromSocket->m_RecvBuffer, toSocket->m_SendBuffer); 
    vector<uint8_t> byteArray = vector<uint8_t>(toSocket->m_SendBuffer.begin(), toSocket->m_SendBuffer.end());
    return TCPProxyStatus::kOk;
  }
  if (m_Aura->GetTicksIsAfterDelay(fromSocket->GetLastRecv(), timeout)) {
    PRINT_IF(LogLevel::kDebug, "Terminating inactive proxy.")
    m_DeleteMe = true;
    return TCPProxyStatus::kDestroy;
  }
  return TCPProxyStatus::kOk;
}

TCPProxyStatus CTCPProxy::Update(fd_set* fd, fd_set* send_fd, int64_t timeout)
{
  if (m_DeleteMe || !m_IncomingSocket || m_IncomingSocket->HasError()) {
    return TCPProxyStatus::kDestroy;
  }


  if (m_TimeoutTicks.has_value() && m_Aura->GetTicksIsAfter(m_TimeoutTicks.value())) {
    return TCPProxyStatus::kDestroy;
  }

  TCPProxyStatus result = TCPProxyStatus::kOk;
  if (m_OutgoingSocket->GetConnecting()) {
    if (!m_OutgoingSocket->CheckConnect()) {
      if (m_Aura->GetTicksIsAfterDelay(m_OutgoingSocket->GetConnectingStartTicks(), timeout)) {
        m_OutgoingSocket->Close();
        return TCPProxyStatus::kDestroy;
      }
      return result;
    }
    AppendSwapString(m_IncomingSocket->m_RecvBuffer, m_OutgoingSocket->m_SendBuffer);
    vector<uint8_t> byteArray = vector<uint8_t>(m_OutgoingSocket->m_SendBuffer.begin(), m_OutgoingSocket->m_SendBuffer.end());
    // falls through
  } else if (!m_OutgoingSocket->GetConnected() && !m_OutgoingSocket->HasError()) {
    auto game = m_Game.lock();
    if (!game || !game->GetPublicHostOverride()) return TCPProxyStatus::kDestroy;
    sockaddr_storage outgoingAddress = IPv4ArrayToAddress(game->GetPublicHostAddress());
    SetAddressPort(&outgoingAddress, game->GetPublicHostPort());
    m_OutgoingSocket->Connect(nullopt, outgoingAddress);
    return result;
  }

  TransferBuffer(fd, m_IncomingSocket, m_OutgoingSocket, &m_ClientPaused, timeout);
  TransferBuffer(fd, m_OutgoingSocket, m_IncomingSocket, &m_ServerPaused, timeout);

  if (m_DeleteMe) {
    return TCPProxyStatus::kDestroy;
  }
  if (!m_IncomingSocket->GetConnected() || m_IncomingSocket->HasError() || m_IncomingSocket->HasFin()) {
    return TCPProxyStatus::kDestroy;
  }
  if (!m_OutgoingSocket->GetConnected() || m_OutgoingSocket->HasError() || m_OutgoingSocket->HasFin()) {
    return TCPProxyStatus::kDestroy;
  }

  m_IncomingSocket->DoSend(send_fd);
  m_OutgoingSocket->DoSend(send_fd);

  return result;
}

uint32_t CTCPProxy::SetFD(fd_set* fd, fd_set* send_fd, int32_t* nfds) const
{
  uint32_t counter = 0;
  if (m_IncomingSocket) {
    m_IncomingSocket->SetFD(fd, send_fd, nfds);
    counter++; 
  }
  if (m_OutgoingSocket) {
    m_OutgoingSocket->SetFD(fd, send_fd, nfds);
    counter++;
  }
  return counter;
}

void CTCPProxy::SendClient(const std::vector<uint8_t>& data)
{
  if (m_IncomingSocket && !m_IncomingSocket->HasError()) {
    m_IncomingSocket->PutBytes(data);
  }
}

void CTCPProxy::SendServer(const std::vector<uint8_t>& data)
{
  if (m_OutgoingSocket && !m_OutgoingSocket->HasError()) {
    m_OutgoingSocket->PutBytes(data);
  }
}
