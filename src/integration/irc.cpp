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

   Copyright [2010] [Josko Nikolic]

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

   CODE PORTED FROM THE ORIGINAL GHOST PROJECT

 */

#include "irc.h"
#include "../command.h"
#include "../aura.h"
#include "../socket.h"
#include "../util.h"
#include "../protocol/bnet_protocol.h"
#include "../realm.h"
#include "../net.h"

#include <utility>

constexpr char LF = ('\x0A');
constexpr uint32_t IRC_TCP_KEEPALIVE_IDLE_TIME = 300;

using namespace std;

//////////////
//// CIRC ////
//////////////

CIRC::CIRC(CConfig& nCFG)
  : m_Aura(nullptr),
    m_Socket(new CTCPClient(AF_INET, "IRC")),
    m_LastConnectionAttemptTime(APP_MIN_TICKS),
    m_LastPacketTime(APP_MIN_TICKS),
    m_LastAntiIdleTime(APP_MIN_TICKS),
    m_WaitingToConnect(true),
    m_LoggedIn(false),
    //m_NickName(string()),
    m_Config(CIRCConfig(nCFG))
{
  //m_Socket->SetKeepAlive(true, IRC_TCP_KEEPALIVE_IDLE_TIME);
}

CIRC::~CIRC()
{
  delete m_Socket;

  for (const auto& ptr : m_Aura->m_ActiveContexts) {
    auto ctx = ptr.lock();
    if (ctx && ctx->GetServiceSourceType() == ServiceType::kIRC) {
      ctx->ResetServiceSource();
      ctx->SetPartiallyDestroyed();
    }
  }
}

bool CIRC::MatchHostName(const string& hostName) const
{
  if (hostName == m_Config.m_HostName) return true;
  if (hostName == m_Config.m_VerifiedDomain) return true;
  return false;
}

uint32_t CIRC::SetFD(fd_set* fd, fd_set* send_fd, int32_t* nfds) const
{
  // irc socket

  if (!m_Socket->HasError() && !m_Socket->HasFin() && m_Socket->GetConnected())
  {
    m_Socket->SetFD(fd, send_fd, nfds);
    return 0;
  }

  return 1;
}

void CIRC::ResetConnection()
{
  m_Socket->Reset();
  //m_Socket->SetKeepAlive(true, IRC_TCP_KEEPALIVE_IDLE_TIME);
  m_WaitingToConnect = true;
  m_LoggedIn = false;
}

void CIRC::Update(fd_set* fd, fd_set* send_fd)
{
  if (!m_Config.m_Enabled) {
    if (m_Socket && m_Socket->GetConnected()) {
      Print("[IRC: " + m_Config.m_HostName + "] disconnected");
      ResetConnection();
      m_WaitingToConnect = false;
    }
    return;
  }

  if (m_Socket->HasError() || m_Socket->HasFin())
  {
    if (m_Socket->HasError()) {
      // the socket has an error
      Print("[IRC: " + m_Config.m_HostName + "] disconnected due to socket error");
    } else {
      // remote end terminated the connection
      Print("[IRC: " + m_Config.m_HostName + "] remote terminated the connection");
    }
    Print("[IRC: " + m_Config.m_HostName + "] waiting 60 seconds to reconnect");
    ResetConnection();
    m_LastConnectionAttemptTime = m_Aura->GetLoopTime();
    return;
  }

  if (m_Socket->GetConnected())
  {
    // the socket is connected and everything appears to be working properly

    if (m_Aura->GetTimeIsAfterDelay(m_LastPacketTime, 210))
    {
      Print("[IRC: " + m_Config.m_HostName + "] ping timeout, reconnecting...");
      ResetConnection();
      return;
    }

    if (m_Aura->GetTimeIsAfterDelay(m_LastAntiIdleTime, 60))
    {
      Send("TIME");
      m_LastAntiIdleTime = m_Aura->GetLoopTime();
    }

    if (m_Socket->DoRecv(fd)) {
      ExtractPackets();
    }
    if (m_Socket->HasError() || m_Socket->HasFin()) {
      return;
    }
    m_Socket->DoSend(send_fd);
    return;
  }

  if (!m_Socket->GetConnecting() && !m_Socket->GetConnected() && !m_WaitingToConnect)
  {
    // the socket was disconnected

    Print("[IRC: " + m_Config.m_HostName + "] disconnected, waiting 60 seconds to reconnect");
    ResetConnection();
    m_LastConnectionAttemptTime = m_Aura->GetLoopTime();
    return;
  }

  if (m_Socket->GetConnecting())
  {
    // we are currently attempting to connect to irc

    if (m_Socket->CheckConnect())
    {
      // the connection attempt completed
      m_Socket->SetKeepAlive(true, IRC_TCP_KEEPALIVE_IDLE_TIME);

      m_NickName = m_Config.m_NickName;

      if (m_Config.m_HostName.find("quakenet.org") == string::npos && !m_Config.m_Password.empty())
        Send("PASS " + m_Config.m_Password);

      Send("NICK " + m_Config.m_NickName);
      Send("USER " + m_Config.m_UserName + " " + m_Config.m_NickName + " " + m_Config.m_UserName + " :aura-bot");

      m_Socket->DoSend(send_fd);

      m_LoggedIn = true;
      Print("[IRC: " + m_Config.m_HostName + "] connected");

      m_LastPacketTime = m_Aura->GetLoopTime();

      return;
    }
    else if (m_Aura->GetTimeIsAfterDelay(m_LastConnectionAttemptTime, 15))
    {
      // the connection attempt timed out (15 seconds)

      Print("[IRC: " + m_Config.m_HostName + "] connect timed out, waiting 60 seconds to reconnect");
      ResetConnection();
      m_LastConnectionAttemptTime = m_Aura->GetLoopTime();
      return;
    }
  }

  if (!m_Socket->GetConnecting() && !m_Socket->GetConnected() && m_Aura->GetTimeIsAfterDelay(m_LastConnectionAttemptTime, 60)) {
    // attempt to connect to irc

    Print("[IRC: " + m_Config.m_HostName + "] connecting to server [" + m_Config.m_HostName + "] on port " + to_string(m_Config.m_Port));
    optional<sockaddr_storage> emptyBindAddress;
    sockaddr_storage resolvedAddress;
    if (m_Aura->m_Net.ResolveHostName(resolvedAddress, ACCEPT_ANY, m_Config.m_HostName, m_Config.m_Port)) {
      m_Socket->Connect(emptyBindAddress, resolvedAddress);
    } else {
      m_Socket->m_HasError = true;
    }
    m_WaitingToConnect          = false;
    m_LastConnectionAttemptTime = m_Aura->GetLoopTime();
  }

  return;
}

void CIRC::ExtractPackets()
{
  string_view data = m_Socket->GetRecvBufferView();
  while (!data.empty()) {
    // separate packets using the CRLF delimiter
    string_view::size_type crlfIndex = data.find("\r\n");
    if (crlfIndex == string_view::npos) break;
    ProcessPacket(data.substr(0, crlfIndex));
    data.remove_prefix(crlfIndex + 2);
  }

  if (data.size() != m_Socket->GetRecvBufferSize()) {
    m_Socket->UpdateRecvBuffer(data);
  }
}

void CIRC::ProcessPacket(string_view packet)
{
  // track timeouts

  m_LastPacketTime = m_Aura->GetLoopTime();

  // ping packet
  // in:  PING :2748459196
  // out: PONG :2748459196
  // respond to the packet sent by the server

  if (packet.substr(0, 4) == "PING") {
    Send(Concat("PONG :", packet.substr(6)));
    return;
  }

  // notice packet
  // in: NOTICE AUTH :*** Checking Ident
  // not actually important

  if (packet.substr(0, 6) == "NOTICE") {
    //Print(Concat("[IRC: ", m_Config.m_HostName, "] ", packet));
    return;
  }

  // now we need to further tokenize each packet
  // the delimiter is space
  // we use a std::vector so we can check its number of tokens

  const vector<string_view> tokens = SplitTokens(packet, ' ');

  // privmsg packet
  // in:  :nickname!~username@hostname PRIVMSG #channel :message
  // print the message, check if it's a command then execute if it is

  if (tokens.size() > 3 && tokens[1] == "PRIVMSG" && m_Config.m_CommandCFG->m_Enabled)
  {
    // don't bother parsing if the message is very short (1 character)
    // since it's surely not a command

    if (tokens[3].size() < 3)
      return;

    string_view::size_type fragmentStartPos = 1;
    if (tokens[0].size() <= fragmentStartPos) {
      return;
    }
    // nickname
    string_view::size_type fragmentEndPos = tokens[0].find('!', fragmentStartPos);
    if (fragmentEndPos == string_view::npos) {
      return;
    }
    string nickName(tokens[0].substr(fragmentStartPos, fragmentEndPos - fragmentStartPos));

    fragmentStartPos = fragmentEndPos + 1;
    if (tokens[0].size() <= fragmentStartPos) {
      return;
    }

    // username
    fragmentEndPos = tokens[0].find('@', fragmentStartPos);
    if (fragmentEndPos == string_view::npos) {
      return;
    }

    fragmentStartPos = fragmentEndPos + 1;
    if (tokens[0].size() <= fragmentStartPos) {
      return;
    }

    string hostName(tokens[0].substr(fragmentStartPos, tokens[0].size() - fragmentStartPos));

    // get the channel

    string channel(tokens[2]);

    // get the message

    string message(packet.substr(tokens[0].size() + tokens[1].size() + tokens[2].size() + 4));

    if (message.empty() || channel.empty())
      return;

    if (
      !IsArbitraryStringUTF8Safe(nickName) || !IsArbitraryStringUTF8Safe(message) ||
      !IsArbitraryStringUTF8Safe(channel) || !IsArbitraryStringUTF8Safe(hostName)
    ) {
      return;
    }

    CommandTokensView commandTokens;
    ExtractMessageTokensAny(message, m_Config.m_PrivateCmdToken, m_Config.m_BroadcastCmdToken, commandTokens);
    if (commandTokens.matchType != CommandTokensMatchType::kNone) {
      string cmdToken(commandTokens.token);
      string command = ToLowerCase(commandTokens.cmd);
      string target(commandTokens.target);
      const bool isWhisper = channel[0] != '#';
      shared_ptr<CCommandContext> ctx = nullptr;
      try {
        ctx = make_shared<CCommandContext>(
          ServiceType::kIRC, m_Aura, m_Config.m_CommandCFG,
          channel, nickName, isWhisper,
          hostName, (!isWhisper && commandTokens.matchType == CommandTokensMatchType::kBroadcast), &std::cout
       );
      } catch (...) {
      }
      if (ctx) {
        ctx->UpdatePermissions();
        ctx->Run(cmdToken, command, target);
      }
    }
  }

  // kick packet
  // in:  :nickname!~username@hostname KICK #channel nickname :reason
  // out: JOIN #channel
  // rejoin the channel if we're the victim

  if (tokens.size() == 5 && tokens[1] == "KICK") {
    if (tokens[3] == m_NickName) {
      Send(Concat("JOIN ", tokens[2]));
    }

    return;
  }

  // message of the day end packet
  // in: :server 376 nickname :End of /MOTD command.
  // out: JOIN #channel
  // join channels and auth and set +x on QuakeNet

  if (tokens.size() >= 2 && tokens[1] == "376") {
    // auth if the server is QuakeNet

    if (m_Config.m_HostName.find("quakenet.org") != string::npos && !m_Config.m_Password.empty()) {
      SendUser(Concat("AUTH ", m_Config.m_UserName, " ", m_Config.m_Password), "Q@CServe.quakenet.org");
      Send(Concat("MODE ", m_Config.m_NickName, " +x"));
    }

    // join channels

    for (const auto& channel : m_Config.m_Channels)
      Send(Concat("JOIN ", channel));

    return;
  }

  // nick taken packet
  // in:  :server 433 CurrentNickname WantedNickname :Nickname is already in use.
  // out: NICK NewNickname
  // append an underscore and send the new nickname

  if (tokens.size() >= 2 && tokens[1] == "433") {
    // nick taken, append _

    m_NickName += '_';
    Send(Concat("NICK ", m_NickName));
    return;
  }
}

void CIRC::Send(string_view message)
{
  // max message length is 512 bytes including the trailing CRLF

  if (!m_Socket->GetConnected()) {
    return;
  }

  string line = Concat(message, string_view(&LF, 1));
  m_Socket->PutBytes(line);
}

void CIRC::SendUser(string_view message, string_view target)
{
  // max message length is 512 bytes including the trailing CRLF

  if (!m_Socket->GetConnected()) {
    return;
  }

  while (message.size() > 450) {
    string line = Concat(message.substr(0, 450), string_view(&LF, 1));
    m_Socket->PutBytes("PRIVMSG " + string(target) + " :" + line);
    message.remove_prefix(450);
  }
  if (!message.empty()) {
    string line = Concat(message, string_view(&LF, 1));
    m_Socket->PutBytes("PRIVMSG " + string(target) + " :" + line);
  }
}

void CIRC::SendChannel(string_view message, string_view target)
{
  // Sending messages to channels or to user works exactly the same, except that channels start with #.
  SendUser(message, target);
}

void CIRC::SendAllChannels(string_view message)
{
  for (const auto& channel : m_Config.m_Channels) {
    SendChannel(message, channel);
  }
}

CCommandConfig* CIRC::GetCommandConfig() const
{
  return m_Config.m_CommandCFG;
}

bool CIRC::GetIsModerator(const std::string& nHostName)
{
  return m_Config.m_Admins.find(nHostName) != m_Config.m_Admins.end();
}

bool CIRC::GetIsSudoer(const std::string& nHostName)
{
  return m_Config.m_SudoUsers.find(nHostName) != m_Config.m_SudoUsers.end();
}
