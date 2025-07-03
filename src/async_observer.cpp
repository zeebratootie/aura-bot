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

#include <utility>
#include <random>

#include "async_observer.h"
#include "aura.h"
#include "command.h"
#include "config/config_bot.h"
#include "game.h"
#include "game_user.h"
#include "map.h"
#include "net.h"
#include "realm.h"
#include "socket.h"
#include "protocol/game_protocol.h"
#include "protocol/gps_protocol.h"
#include "protocol/vlan_protocol.h"

using namespace std;

//
// CAsyncObserver
//

CAsyncObserver::CAsyncObserver(shared_ptr<CGame> nGame, CConnection* nConnection, uint8_t nUID, const bool gameVersionIsExact, const Version& gameVersion, shared_ptr<CRealm> nFromRealm, string nName)
  : CConnection(*nConnection),
    m_Game(nGame),
    m_GameHistory(nGame->GetGameHistory()),
    m_FromRealm(nFromRealm),
    m_MapChecked(false),
    m_MapReady(false),
    m_StateSynchronized(true),
    m_TimeSynchronized(false),
    m_Offset(0),
    m_Goal(ASYNC_OBSERVER_GOAL_OBSERVER),
    m_UID(nUID),
    m_SID(nGame->GetSIDFromUID(nUID)),
    m_Color(nGame->GetColorFromUID(nUID)),
    m_GameVersionIsExact(gameVersionIsExact),
    m_GameVersion(gameVersion),
    m_MissingLog(0),
    m_FrameRate(2),
    m_Latency(nGame->GetGameHistory()->GetDefaultLatency()),
    m_SyncCounter(0),
    m_ActionFrameCounter(0),
    m_FrameSampler(UniformFrameSampler(TIMESTAMPS_SAMPLE_RATE)),
    m_StartedLoading(false),
    m_StartedLoadingTicks(0),
    m_FinishedLoading(false),
    m_FinishedLoadingTicks(0),
    m_GameTicks(0),
    m_SentGameLoadedReport(false),
    m_PlaybackEnded(false),
    m_LastPingTicks(APP_MIN_TICKS),
    m_LastProgressReportTime(APP_MIN_TICKS),
    m_LastProgressReportLog(0),
    m_Name(std::move(nName))
{
  m_Socket->SetLogErrors(true);
}

CAsyncObserver::~CAsyncObserver()
{
  m_GameHistory.reset();

  if (HasLeftReason()) {
    Print(GetLogPrefix() + "destroyed - " + GetLeftReason());
  } else {
    Print(GetLogPrefix() + "destroyed");
  }

  for (const auto& ptr : m_Aura->m_ActiveContexts) {
    shared_ptr<CCommandContext> ctx = ptr.lock();
    if (ctx && ctx->GetGameSource().GetIsSpectator() && ctx->GetGameSource().GetSpectator() == this) {
      ctx->SetPartiallyDestroyed();
      ctx->GetGameSource().Reset();
    }
  }
}

void CAsyncObserver::SetTimeout(const int64_t delta)
{
  m_TimeoutTicks = GetTicks() + delta;
}

void CAsyncObserver::SetTimeoutAtLatest(const int64_t atLatestTicks)
{
  if (!m_TimeoutTicks.has_value() || atLatestTicks < m_TimeoutTicks.value()) {
    m_TimeoutTicks = atLatestTicks;
  }
}

bool CAsyncObserver::CloseConnection(bool /*recoverable*/)
{
  if (!m_Socket->GetConnected()) return false;
  m_Socket->Close();
  return true;
}

void CAsyncObserver::Init()
{
}

uint8_t CAsyncObserver::Update(fd_set* fd, fd_set* send_fd, int64_t timeout)
{
  if (!m_Socket || m_Socket->HasError()) {
    return ASYNC_OBSERVER_DESTROY;
  }

  if (m_DeleteMe) {
    m_Socket->ClearRecvBuffer(); // in case there are pending bytes from a previous recv
    m_Socket->Discard(fd);
    return ASYNC_OBSERVER_DESTROY;
  }

  const int64_t Time = GetTime(), Ticks = GetTicks();

  if (m_TimeoutTicks.has_value() && m_TimeoutTicks.value() < Ticks) {
    SetLeftReasonGeneric("observer timeout");
    return ASYNC_OBSERVER_DESTROY;
  }

  uint8_t result = ASYNC_OBSERVER_OK;
  bool Abort = false;
  if (m_Type == INCON_TYPE_KICKED_PLAYER) {
    m_Socket->Discard(fd);
  } else if (m_Socket->DoRecv(fd)) {
    // extract as many packets as possible from the socket's receive buffer and process them
    string*              RecvBuffer         = m_Socket->GetBytes();
    std::vector<uint8_t> Bytes              = CreateByteArray((uint8_t*)RecvBuffer->c_str(), RecvBuffer->size());
    uint32_t             LengthProcessed    = 0;

    // a packet is at least 4 bytes so loop as long as the buffer contains 4 bytes

    while (Bytes.size() >= 4) {
      // bytes 2 and 3 contain the length of the packet
      const uint16_t Length = ByteArrayToUInt16(Bytes, false, 2);
      if (Length < 4) {
        EventProtocolError();
        Abort = true;
        break;
      }
      if (Bytes.size() < Length) break;
      const std::vector<uint8_t> Data = std::vector<uint8_t>(begin(Bytes), begin(Bytes) + Length);

      switch (Bytes[0]) {
        case GameProtocol::Magic::W3GS_HEADER: {
          switch (Bytes[1]) {
            case GameProtocol::Magic::LEAVEGAME: {
              if (ValidateLength(Data) && Data.size() >= 8) {
                const uint32_t reason = ByteArrayToUInt32(Data, false, 4);
                EventLeft(reason);
                //m_Socket->SetLogErrors(false);
              } else {
                EventProtocolError();
              }
              Abort = true;
              break;
            }

            case GameProtocol::Magic::GAMELOADED_SELF: {
              if (GameProtocol::RECEIVE_W3GS_GAMELOADED_SELF(Data)) {
                if (m_StartedLoading && !m_FinishedLoading) {
                  m_FinishedLoading      = true;
                  m_FinishedLoadingTicks = GetTicks();
                  m_LastFrameTicks = m_FinishedLoadingTicks;
                  EventGameLoaded();
                }
              }

              break;
            }

            case GameProtocol::Magic::OUTGOING_ACTION: {
              // Ignore all actions performed by observers,
              // and let's see how this turns out.
              if (Length < 9) {
                EventProtocolError();
                Abort = true;
                break;
              }
              bool skipActions = false;
              switch (Data[8]) {
                case ACTION_SCENARIO_TRIGGER: // seen in WarChasers, WormWar
                  skipActions = (Length - 8) % GameProtocol::GetActionSize(Data[8]) == 0;
                  break;
                case ACTION_MINIMAPSIGNAL:
                case ACTION_MODAL_BTN_CLICK:
                case ACTION_MODAL_BTN:
                case ACTION_PAUSE:
                case ACTION_RESUME:
                case ACTION_SAVE:
                  skipActions = (Length == 8 + GameProtocol::GetActionSize(Data[8]));
                  break;
                case ACTION_SAVE_ENDED:
                  skipActions = true;
                  break;
              }
              if (!skipActions) {
                Print(GetLogPrefix() + "got action <" + ByteArrayToHexString(Data.data() + 8, Length - 8) + ">");
              }
              break;
            }

            case GameProtocol::Magic::OUTGOING_KEEPALIVE: {
              EventClientGameState(GameProtocol::RECEIVE_W3GS_OUTGOING_KEEPALIVE(Data));

              if (!m_Socket->GetConnected()) {
                Abort = true;
              }
              break;
            }

            case GameProtocol::Magic::CHAT_TO_HOST: {
              CIncomingChatMessage incomingChatMessage = GameProtocol::RECEIVE_W3GS_CHAT_TO_HOST(Data);

              if (incomingChatMessage.GetIsValid()) {
                EventChatOrPlayerSettings(incomingChatMessage);
              }
              break;
            }

            case GameProtocol::Magic::MAPSIZE: {
              if (m_MapReady) {
                // Protection against rogue clients
                break;
              }
              shared_ptr<CGame> game = m_Game.lock();
              if (!game || game->GetIsGameOver()) {
                // Protection against rogue clients
                break;
              }

              CIncomingMapFileSize incomingMapSize = GameProtocol::RECEIVE_W3GS_MAPSIZE(Data);

              if (incomingMapSize.GetIsValid()) {
                game->EventObserverMapSize(this, incomingMapSize);
              }

              if (!m_Socket->GetConnected()) {
                Abort = true;
              }

              break;
            }

            case GameProtocol::Magic::DROPREQ:
            case GameProtocol::Magic::PONG_TO_HOST: {
              // ignore these
              break;
            }
          }
          break;
        }

        case GPSProtocol::Magic::GPS_HEADER: {
          // GProxy unsupported for observers
          //shared_ptr<CGame> game = m_Game.lock();
          if (/*game && game->GetIsProxyReconnectable() && */Bytes[1] == GPSProtocol::Magic::INIT) {
            Print(GetLogPrefix() + "client started GProxy handshake ");
          }
          break;
        }

        default: {
          Abort = true;
        }
      }

      LengthProcessed += Length;

      if (Abort) {
        // Process no more packets
        break;
      }

      Bytes = std::vector<uint8_t>(begin(Bytes) + Length, end(Bytes));
    }

    if (Abort && result != ASYNC_OBSERVER_PROMOTED) {
      result = ASYNC_OBSERVER_DESTROY;
      RecvBuffer->clear();
    } else if (LengthProcessed > 0) {
      *RecvBuffer = RecvBuffer->substr(LengthProcessed);
    }
  } else if (Ticks >= m_Socket->GetLastRecv() + timeout) {
    SetLeftReasonGeneric("connection timed out");
    return ASYNC_OBSERVER_DESTROY;
  }

  if (m_DeleteMe || !m_Socket->GetConnected() || m_Socket->HasError() || m_Socket->HasFin()) {
    SetLeftReasonGeneric("observer decomissioned");
    return ASYNC_OBSERVER_DESTROY;
  }

  if (m_FinishedLoading && !m_PlaybackEnded) {
    // If we don't wait a few seconds, the message gets lost to the F12 Chat Log ??
    const bool canSendChat =  m_FinishedLoadingTicks + 3000 <= Ticks;
    if (!m_SentGameLoadedReport && canSendChat) {
      // Grace period so that chat messages are visible
      SendGameLoadedReport();
    }
    //const size_t beforeCounter = m_ActionFrameCounter;
    if (PushGameFrames()) {
      /*
      const size_t delta = SubtractClampZero(m_ActionFrameCounter, beforeCounter);
      if (beforeCounter <= 50 || delta > 1) Print(GetLogPrefix() + "pushed " + to_string(delta) + " action frames");
      //*/
      if (m_FrameRate > 1 && canSendChat) {
        if ((m_LastProgressReportTime + 30 <= Time && Ticks <= m_FinishedLoadingTicks + 120000) || m_LastProgressReportTime + 75 <= Time) {
          SendProgressReport();
          m_MissingLog = GetClientMissingLog();
        } else if (m_LastProgressReportTime + 5 <= Time) {
          uint8_t missingLog = GetClientMissingLog();
          if (m_MissingLog < missingLog) {
            // Ensure progress reports around 75% 87.5% 91.25% ...
            SendProgressReport();
            m_MissingLog = missingLog;
          }
        }
      }
    }
    CheckPlayBackOver();
  }

  if (m_LastPingTicks + 5000 <= Ticks) {
    Send(GameProtocol::SEND_W3GS_PING_FROM_HOST(m_Aura->GetLoopTicks()));
    m_LastPingTicks = Ticks;
  }

  m_Socket->DoSend(send_fd);
  return result;
}

bool CAsyncObserver::GetIsGameOver() const
{
  return m_Game.expired() || m_Game.lock()->GetIsGameOver();
}

void CAsyncObserver::CheckPlayBackOver()
{
  if (!GetIsGameOver()) return;
  FlushGameFrames();
  if (m_GameHistory->m_PlayingBuffer.size() <= m_Offset) {
    m_PlaybackEnded = true;
    Print(GetLogPrefix() + "playback ended");
    SendChat("Playback ended. Game will exit automatically in 10 seconds.");

    // Kick after 10 seconds
    SetTimeoutAtLatest(GetTicks() + 10000);
  }
}

int64_t CAsyncObserver::GetNextTimedActionByTicks() const
{
  if (m_GameHistory->GetNumActionFrames() <= m_ActionFrameCounter) {
    return APP_MAX_TICKS;
  }
  return m_LastFrameTicks + m_Latency / m_FrameRate;
}

bool CAsyncObserver::GetClientIsBehindFrames(const uint32_t limit) const
{
  return (m_ActionFrameCounter >= m_SyncCounter) && (m_ActionFrameCounter - m_SyncCounter >= limit);
}

bool CAsyncObserver::PushGameFrames(bool isFlush)
{
  int64_t Ticks = GetTicks();
  // Note: Actually, each frame may have its own custom latency.
  int64_t gameDurationWanted = m_FrameRate * (Ticks - m_LastFrameTicks);
  if (gameDurationWanted < m_Latency) {
    // Fast path for the common case (there will never be a GAME_FRAME_TYPE_LATENCY hanging)
    return false;
  }
  if (!isFlush && m_ActionFrameCounter >= m_GameHistory->GetNumActionFrames()) {
    if (!m_TimeSynchronized) {
      if (m_FrameRate > 1) m_FrameRate = 1;
      m_TimeSynchronized = true;
      SendChat("You are now synchronized with the live game.");
    }
    return false;
  }

  bool success = false;
  auto it = begin(m_GameHistory->m_PlayingBuffer) + m_Offset;
  auto itEnd = end(m_GameHistory->m_PlayingBuffer);
  while (it != itEnd && (m_Latency <= gameDurationWanted || it->GetType() == GAME_FRAME_TYPE_LATENCY)) {
    //Print(GetLogPrefix() + "sending " + it->GetTypeName() + " frame");
    switch (it->GetType()) {
      case GAME_FRAME_TYPE_GPROXY:
        // if stored, GAME_FRAME_TYPE_GPROXY always precedes GAME_FRAME_TYPE_ACTIONS
        Send(GameProtocol::SEND_W3GS_EMPTY_ACTIONS(m_GameHistory->GetGProxyEmptyActions()));
        break;
      case GAME_FRAME_TYPE_LATENCY:
        // it stored, GAME_FRAME_TYPE_LATENCY always goes after GAME_FRAME_TYPE_ACTIONS
        m_Latency = ByteArrayToUInt16(it->GetBytes(), false, 0);
        ResetClientFrameRate();
        break;
      case GAME_FRAME_TYPE_ACTIONS:  
        gameDurationWanted -= m_Latency;
        m_GameTicks += m_Latency;
        // falls through
      case GAME_FRAME_TYPE_PAUSED:
        success = true;
        m_LastFrameTicks = Ticks;
        ++m_ActionFrameCounter;
        Send(it->GetBytes());
        break;
      default:
        // GAME_FRAME_TYPE_LEAVER, GAME_FRAME_TYPE_CHAT
        Send(it->GetBytes());
    }
    ++it;
    ++m_Offset;
  }

  return success;
}

void CAsyncObserver::EventGameReset(shared_ptr<const CGame> nGame)
{
  if (m_Game.lock() == nGame) {
    m_Game.reset();
  }
}

void CAsyncObserver::EventRealmDeleted(shared_ptr<const CRealm> nRealm)
{
  if (m_FromRealm.lock() == nRealm) {
    m_FromRealm.reset();
  }
}

void CAsyncObserver::EventClientGameState(const uint32_t checkSum)
{
  ++m_SyncCounter;

  if (!UpdateClientGameState(checkSum)) {
    m_StateSynchronized = false;
  }

  if (m_FrameSampler.GetBernoulli()) {
    if (m_CheckSumsTimeStamps.size() >= MAXIMUM_TIMESTAMPS_COUNT) {
      m_CheckSumsTimeStamps.pop_front();
    }
    m_CheckSumsTimeStamps.push_back(m_Aura->GetLoopTicks());
  }
}

bool CAsyncObserver::UpdateClientGameState(const uint32_t checkSum)
{
  if (!m_StateSynchronized) return false;

  if (!m_Game.expired() && m_Game.lock()->GetSyncCounter() < m_SyncCounter) {
    string text = GetLogPrefix() + "incorrectly ahead of sync";
    Print(text);
    m_Aura->LogPersistent(text);
    return false;
  }
  if (m_GameHistory->GetDesynchronized() && m_SyncCounter > m_GameHistory->GetNumCheckSums()) {
    return false;
  }

  m_CheckSums.push(checkSum);
  return CheckClientGameState();
}

bool CAsyncObserver::CheckClientGameState()
{
  bool success = true;
  size_t nextCheckSumIndex = m_SyncCounter - m_CheckSums.size();
  while (!m_CheckSums.empty() && nextCheckSumIndex < m_GameHistory->GetNumCheckSums()) {
    uint32_t nextCheckSum = m_CheckSums.front();
    if (nextCheckSum != m_GameHistory->GetCheckSum(nextCheckSumIndex)) {
      success = false;
      break;
    }
    ++nextCheckSumIndex;
    m_CheckSums.pop();
  }

  if (!success) {
    EventDesync();
  }
  return success;
}

void CAsyncObserver::UpdateDownloadProgression(const uint8_t downloadProgression)
{
  if (m_Game.expired()) return;
  vector<uint8_t> slotInfo = m_Game.lock()->GetSlotInfo();
  constexpr static uint16_t fixedOffset = (
    2 /* W3GS type headers */ +
    2 /* W3GS packet byte size */ +
    2 /* EncodeSlotInfo() byte size */ +
    1 /* number of slots */ +
    1 /* download status offset in CGameSlot::GetProtocolArray() */
  );
  uint16_t progressionIndex = 9 * m_SID + fixedOffset;
  slotInfo[progressionIndex] = downloadProgression;
  Send(slotInfo);
}

uint8_t CAsyncObserver::NextSendMap()
{
  if (m_Game.expired()) return MAP_TRANSFER_NONE;
  return m_Game.lock()->NextSendMap(this, GetUID(), GetMapTransfer()); 
}

void CAsyncObserver::EventDesync()
{
  while (!m_CheckSums.empty()) {
    m_CheckSums.pop();
  }
  string text = GetLogPrefix() + "desynchronized on " + ToOrdinalName(m_SyncCounter) + " checksum - sent " + to_string(m_Offset) + " total frames (" + to_string(m_ActionFrameCounter) + " actions)";
  Print(text);
  m_Aura->LogPersistent(text);

  if (!m_GameHistory->GetSoftDesynchronized()) {
    m_GameHistory->SetSoftDesynchronized();
  }
  if (m_Game.expired() || m_Game.lock()->GetAllowsDesync()) {
    SendChat("Desync detected!! Your game client failed to replicate the game state.");
    SendChat("You may continue to watch the game, but it may be corrupted.");
    return;
  }
  if (!CloseConnection()) {
    return;
  }
  SetLeftReasonGeneric("desynchronized");
  SetDeleteMe(true);
}

void CAsyncObserver::EventMapReady()
{
  m_MapReady = true;

  StartLoading();
}

void CAsyncObserver::StartLoading()
{
  if (m_StartedLoading) return;
  Print(GetLogPrefix() + "started loading");
  Send(GameProtocol::SEND_W3GS_COUNTDOWN_START());
  Send(GameProtocol::SEND_W3GS_COUNTDOWN_END());
  m_StartedLoading = true;
}

void CAsyncObserver::EventGameLoaded()
{
  Print(GetLogPrefix() + "finished loading");
  Send(m_GameHistory->m_LoadingRealBuffer);
  Send(m_GameHistory->m_LoadingVirtualBuffer);
}

void CAsyncObserver::EventChat(const CIncomingChatMessage& incomingChatMessage)
{
  const bool isLobbyChat = incomingChatMessage.GetType() == GameProtocol::ChatToHostType::CTH_MESSAGE_LOBBY;
  if (isLobbyChat == m_StartedLoading) {
    // Racing condition
    PRINT_IF(LogLevel::kDebug, "Chat message from [" + GetName() + "] ignored (game stage mismatch)")
    return;
  }

  bool shouldRelay = !isLobbyChat && false; // relay the chat message to other users

  if (!isLobbyChat && m_Aura->m_Config.m_LogGameChat == LOG_GAME_CHAT_ALWAYS) {
    Print(GetLogPrefix() + "[" + GetName() + "] " + incomingChatMessage.GetMessage());
  }

  CGameConfig* gameConfig;
  shared_ptr<CGame> game = GetGame();
  if (game) {
    gameConfig = &game->m_Config;
  } else {
    gameConfig = m_Aura->m_GameDefaultConfig;
  }

  CommandHistory* cmdHistory = GetCommandHistory();

  // handle bot commands
  {
    shared_ptr<CRealm> realm = GetRealm();
    CCommandConfig* commandCFG = realm ? realm->GetCommandConfig() : m_Aura->m_Config.m_LANCommandCFG;
    const bool commandsEnabled = commandCFG->m_Enabled && (
      !realm || !(commandCFG->m_RequireVerified && !GetIsRealmVerified())
    );
    bool isCommand = false;
    // TODO: CAsyncObserver smart commands
    //const uint8_t activeSmartCommand = cmdHistory->GetSmartCommand();
    //cmdHistory->ClearSmartCommand();
    if (commandsEnabled) {
      const string message = incomingChatMessage.GetMessage();
      string cmdToken, command, target;
      uint8_t tokenMatch = ExtractMessageTokensAny(message, gameConfig->m_PrivateCmdToken, gameConfig->m_BroadcastCmdToken, cmdToken, command, target);
      isCommand = tokenMatch != COMMAND_TOKEN_MATCH_NONE;
      if (isCommand) {
        cmdHistory->SetUsedAnyCommands(true);
        // If we want users identities hidden, we must keep bot responses private.
        if (shouldRelay) {
          //SendChat(incomingChatMessage);
          shouldRelay = false;
        }
        shared_ptr<CCommandContext> ctx = nullptr;
        try {
          ctx = make_shared<CCommandContext>(ServiceType::kLAN /* or realm, actually*/, m_Aura, commandCFG, game, this, false, &std::cout);
        } catch (...) {}
        if (ctx) ctx->Run(cmdToken, command, target);
      } else if (message == "?trigger") {
        if (shouldRelay) {
          //SendChat(incomingChatMessage);
          shouldRelay = false;
        }
        //TODO:SendCommandsHelp()
        //game->SendCommandsHelp(gameConfig->m_BroadcastCmdToken.empty() ? gameConfig->m_PrivateCmdToken : gameConfig->m_BroadcastCmdToken, this, false);
      } else if (message == "/p" || message == "/ping" || message == "/game") {
        // Note that when the WC3 client is connected to a realm, all slash commands are sent to the bnet server.
        // Therefore, these commands are only effective over LAN.
        if (shouldRelay) {
          //SendChat(incomingChatMessage);
          shouldRelay = false;
        }
        shared_ptr<CCommandContext> ctx = nullptr;
        try {
          ctx = make_shared<CCommandContext>(ServiceType::kLAN /* or realm, actually*/, m_Aura, commandCFG, game, this, false, &std::cout);
        } catch (...) {}
        if (ctx) {
          cmdToken = gameConfig->m_PrivateCmdToken;
          command = message.substr(1);
          ctx->Run(cmdToken, command, target);
        }
      } else if (isLobbyChat && !cmdHistory->GetUsedAnyCommands()) {
        if (shouldRelay) {
          //SendChat(incomingChatMessage);
          shouldRelay = false;
        }
        /*
        // TODO: CAsyncObserver smart commands
        if (!game->CheckSmartCommands(this, message, activeSmartCommand, commandCFG) && !GetCommandHistory()->GetSentAutoCommandsHelp()) {
          bool anySentCommands = false;
          for (const auto& otherPlayer : m_Users) {
            if (otherPlayer->GetCommandHistory()->GetUsedAnyCommands()) anySentCommands = true;
          }
          if (!anySentCommands) {
            SendCommandsHelp(gameConfig->m_BroadcastCmdToken.empty() ? gameConfig->m_PrivateCmdToken : gameConfig->m_BroadcastCmdToken, this, true);
          }
        }
        */
      }
    }
    if (!isCommand) {
      cmdHistory->ClearLastCommand();
      SendChat("You are in spectator mode. Chat is RESTRICTED.");
    }
    if (shouldRelay) {
      //SendChat(incomingChatMessage);
      shouldRelay = false;
    }
  }
}

void CAsyncObserver::EventChatOrPlayerSettings(const CIncomingChatMessage& incomingChatMessage)
{
  if (incomingChatMessage.GetFromUID() != GetUID()) {
    return;
  }

  switch (incomingChatMessage.GetType()) {
    case GameProtocol::ChatToHostType::CTH_MESSAGE_LOBBY:
    case GameProtocol::ChatToHostType::CTH_MESSAGE_INGAME:
      EventChat(incomingChatMessage);
      break;
    case GameProtocol::ChatToHostType::CTH_TEAMCHANGE:
    case GameProtocol::ChatToHostType::CTH_COLOURCHANGE:
    case GameProtocol::ChatToHostType::CTH_RACECHANGE:
    case GameProtocol::ChatToHostType::CTH_HANDICAPCHANGE:
      SendChat("This game has already started. Player settings cannot be changed.");
      break;
  }
}

void CAsyncObserver::EventLeft(const uint32_t clientReason)
{
  if (!CloseConnection()) {
    return;
  }
  if (m_StartedLoading) {
    Print(GetLogPrefix() + "left the game at [" + ToFormattedTimeStamp(m_GameTicks / 1000) + "] (" + GameProtocol::LeftCodeToString(clientReason) + ")");
    /*
    if (m_GameHistory->m_PlayingBuffer.size() <= m_Offset) {
      Print(GetLogPrefix() + "next frame was not available");
    } else {
      Print(GetLogPrefix() + "next frame was " + m_GameHistory->m_PlayingBuffer[m_Offset].GetTypeName());
    }
    */
  } else {
    Print(GetLogPrefix() + "left the lobby");
  }
  SetLeftReasonGeneric("left voluntarily");
  SetDeleteMe(true);
}

void CAsyncObserver::EventProtocolError()
{
  if (!CloseConnection()) {
    return;
  }
  SetLeftReasonGeneric("disconnected due to protocol error");
  SetDeleteMe(true);
}

void CAsyncObserver::Send(const std::vector<uint8_t>& data)
{
  if (m_Socket && !m_Socket->HasError()) {
    m_Socket->PutBytes(data);
  }
}

void CAsyncObserver::SendOtherPlayersInfo()
{
  Send(m_GameHistory->m_PlayersBuffer);
}

void CAsyncObserver::SendChat(const string& message)
{
  if (m_StartedLoading && !m_FinishedLoading) {
    return;
  }
  if (!m_StartedLoading) {
    Send(GameProtocol::SEND_W3GS_CHAT_FROM_HOST_LOBBY(m_UID, CreateByteArray(m_UID), GameProtocol::Magic::ChatType::CHAT_LOBBY, message));
  } else {
    uint32_t targetCode = static_cast<uint32_t>(3u + m_Color);
    Send(GameProtocol::SEND_W3GS_CHAT_FROM_HOST_IN_GAME(m_UID, CreateByteArray(m_UID), GameProtocol::Magic::ChatType::CHAT_IN_GAME, targetCode, message));
  }
}

void CAsyncObserver::SendGameLoadedReport()
{
  int64_t ss, mm, hh;
  ss = (GetTicks() - m_GameHistory->GetStartedTicks()) / 1000;

  mm = ss / 60;
  ss = ss % 60;
  hh = mm / 60;
  mm = mm % 60;

  if (!m_Game.expired() && !m_Game.lock()->GetIsGameOver()) {
    SendChat("Running spectator mode (delay is " + ToDurationString(hh, mm, ss) + ")");
  } else {
    SendChat("Watching replay");
    SendChat("Game was played " + ToFormattedTimeStamp(hh, mm, ss) + " ago");
  }
  if (m_FrameRate > 1) {
    SendChat("Use !sync to watch at 1x, !ff to fast-forward");
  } else {
    SendChat("Use !ff to fast-forward, !sync to watch at 1x");
  }

  m_SentGameLoadedReport = true;
}

void CAsyncObserver::ResetClientFrameRate()
{
  m_CheckSumsTimeStamps.clear();
}

size_t CAsyncObserver::GetClientFrameClamped() const
{
  return min(m_SyncCounter, m_ActionFrameCounter);
}

double CAsyncObserver::GetClientFrameRate() const
{
  if (m_CheckSumsTimeStamps.size() < 2) return (double)m_FrameRate; // fallback to server frame rate
  int64_t deltaTicks = m_CheckSumsTimeStamps.back() - m_CheckSumsTimeStamps.front();
  return (double)((m_CheckSumsTimeStamps.size() - 1) * m_Latency * (int64_t)(TIMESTAMPS_SAMPLE_RATE)) / (double)(deltaTicks);
}

uint8_t CAsyncObserver::GetClientMissingLog() const
{
  constexpr double epsilon = numeric_limits<double>::epsilon();
  double missing = 1.0 - ((double)(GetClientFrameClamped()) / (double)m_GameHistory->GetNumActionFrames());
  if (missing < epsilon) return 0;
  double missingLog = clamp(-log2(missing), 0.0, 255.0);
  return static_cast<uint8_t>(missingLog);
}

void CAsyncObserver::SendProgressReport()
{
  constexpr double epsilon = numeric_limits<double>::epsilon();
  size_t clientFrame = GetClientFrameClamped();
  double clientFrameRate = GetClientFrameRate();
  double progress = (double)clientFrame / (double)m_GameHistory->GetNumActionFrames();

  double catchUpFrameRate = clientFrameRate;
  if (!GetIsGameOver()) catchUpFrameRate = max(0.0, catchUpFrameRate - 1.0);

  string rateFragment = ToFormattedString(PERCENT_FACTOR * progress) + "% - Fast-forwarding at " + to_string(static_cast<int64_t>(round(clientFrameRate))) + "x";
  if (catchUpFrameRate < epsilon) {
    SendChat(rateFragment);
  } else {
    // Estimate time for catching up with live (or finished) game, assuming that latency will be constant.
    double etaSeconds = (double)m_Latency * (double)(m_GameHistory->GetNumActionFrames() - clientFrame) / catchUpFrameRate / 1000.0;
    // Let it fit in chat log (F12)
    SendChat(rateFragment + " - ETA " + ToDurationString((int64_t)etaSeconds));
  }

  if (!m_CheckSumsTimeStamps.empty() || (clientFrameRate - 6.) < epsilon /* 6x or slower can be trusted */) {
    m_LastProgressReportTime = GetTime();
  }
}

string CAsyncObserver::GetLogPrefix() const
{
  if (!m_Game.expired()) return m_Game.lock()->GetLogPrefix() + "[SPECTATOR] [" + m_Name + "] ";
  return "[SPECTATOR] [" + m_Name + "] ";
}
