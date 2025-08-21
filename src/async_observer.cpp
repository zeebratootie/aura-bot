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
#include "sampler.h"
#include "socket.h"
#include "protocol/game_protocol.h"
#include "protocol/gps_protocol.h"
#include "protocol/vlan_protocol.h"

using namespace std;

//
// CAsyncObserver
//

CAsyncObserver::CAsyncObserver(shared_ptr<CGame> nGame, CConnection* nConnection, uint8_t nUID, const bool gameVersionIsExact, const Version& gameVersion, shared_ptr<CRealm> nFromRealm, string_view nName)
  : CConnection(*nConnection),
    m_Game(nGame),
    m_GameHistory(nGame->GetGameHistory()),
    m_FromRealm(nFromRealm),
    m_IsObserver(false),
    m_MapChecked(false),
    m_MapReady(false),
    m_StateSynchronized(true),
    m_TimeSynchronized(false),
    m_TimeLiveSynchronized(false),
    m_Offset(0),
    m_Goal(AsyncObserverGoal::kSpectator),
    m_UID(nUID),
    m_SID(nGame->GetSIDFromUID(nUID)),
    m_Color(nGame->GetColorFromUID(nUID)),
    m_GameVersionIsExact(gameVersionIsExact),
    m_GameVersion(gameVersion),
    m_MissingLog(0),
    m_MaxFrameRate(2),
    m_FrameRate(2),
    m_MaxSafeClientFrameRate(1),
    m_Latency(nGame->GetGameHistory()->GetDefaultLatency()),
    m_SyncCounter(0),
    m_ActionFrameCounter(0),
    m_CheckSumsTimeStamps(UniformlySampledData<int64_t>(TIMESTAMPS_SAMPLE_RATE, MAXIMUM_TIMESTAMPS_COUNT)),
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
    m_Name(nName)
{
  m_IsObserver = m_Color == nGame->GetMap()->GetVersionMaxSlots();
  m_Socket->SetLogErrors(true);
}

CAsyncObserver::~CAsyncObserver()
{
  m_GameHistory.reset();

  if (HasLeftReason()) {
    Print(Concat(GetLogPrefix(), "destroyed - ", GetLeftReason()));
  } else {
    Print(Concat(GetLogPrefix(), "destroyed"));
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
  m_TimeoutTicks = m_Aura->GetLoopTicks() + delta;
}

void CAsyncObserver::SetTimeoutAtLatest(const int64_t atLatestTicks)
{
  if (!m_TimeoutTicks.has_value() || atLatestTicks < m_TimeoutTicks.value()) {
    m_TimeoutTicks = atLatestTicks;
  }
}

void CAsyncObserver::SetFrameRate(int64_t nFrameRate)
{
  m_FrameRate = nFrameRate;
  if ((int64_t)m_MaxFrameRate < m_FrameRate) {
    // m_FrameRate restricted to 1x-64x, so uint8_t m_MaxFrameRate is enough
    m_MaxFrameRate = (uint8_t)m_FrameRate;
  }
}

bool CAsyncObserver::CloseConnection(bool /*recoverable*/)
{
  if (!m_Socket->GetConnected()) return false;
  m_Socket->Close();
  return true;
}

AsyncObserverStatus CAsyncObserver::Update(fd_set* fd, fd_set* send_fd, int64_t timeout)
{
  if (!m_Socket || m_Socket->HasError()) {
    return AsyncObserverStatus::kDestroy;
  }

  if (m_DeleteMe) {
    m_Socket->ClearRecvBuffer(); // in case there are pending bytes from a previous recv
    m_Socket->Discard(fd);
    return AsyncObserverStatus::kDestroy;
  }

  if (m_TimeoutTicks.has_value() && m_Aura->GetTicksIsAfter(m_TimeoutTicks.value())) {
    SetLeftReasonGeneric("observer timeout");
    return AsyncObserverStatus::kDestroy;
  }

  AsyncObserverStatus result = AsyncObserverStatus::kOk;
  bool Abort = false;
  if (m_Type == IncomingConnectionType::kKickedPlayer) {
    m_Socket->Discard(fd);
  } else if (m_Socket->DoRecv(fd)) {
    CStreamIOSocket* socket = m_Socket;
    string_view data = socket->GetRecvBufferView();

    // a packet is at least 4 bytes so loop as long as the buffer contains 4 bytes

    while (data.size() >= 4) {
      // bytes 2 and 3 contain the length of the packet
      const uint16_t packetSize = ByteArrayToUInt16LE(data, 2);
      if (packetSize < 4) {
        EventProtocolError();
        Abort = true;
        break;
      }
      if (data.size() < packetSize) {
        // we don't have the complete packet yet
        break;
      }
      string_view packet = data.substr(0, packetSize);
      const uint8_t packetFamily = GetByteAt(packet, 0);
      const uint8_t packetType = GetByteAt(packet, 1);

      switch (packetFamily) {
        case GameProtocol::Magic::W3GS_HEADER: {
          switch (packetType) {
            case GameProtocol::Magic::LEAVEGAME: {
              if (ValidateLength(packet) && packet.size() >= 8) {
                const uint32_t reason = ByteArrayToUInt32LE(packet, 4);
                EventLeft(reason);
                //m_Socket->SetLogErrors(false);
              } else {
                EventProtocolError();
              }
              Abort = true;
              break;
            }

            case GameProtocol::Magic::GAMELOADED_SELF: {
              if (GameProtocol::RECEIVE_W3GS_GAMELOADED_SELF(packet)) {
                if (m_StartedLoading && !m_FinishedLoading) {
                  m_FinishedLoading      = true;
                  m_FinishedLoadingTicks = m_Aura->GetLoopTicks();
                  m_LastFrameTicks = m_FinishedLoadingTicks;
                  EventGameLoaded();
                }
              }

              break;
            }

            case GameProtocol::Magic::OUTGOING_ACTION: {
              // Ignore all actions performed by observers,
              // and let's see how this turns out.
              if (packetSize < 9) {
                EventProtocolError();
                Abort = true;
                break;
              }
              bool skipActions = false;
              uint8_t actionType = GetByteAt(packet, 8);
              switch (actionType) {
                case ACTION_SCENARIO_TRIGGER: // seen in WarChasers, WormWar
                  skipActions = (packetSize % GameProtocol::GetActionSize(actionType) == 8);
                  break;
                case ACTION_MINIMAPSIGNAL:
                case ACTION_MODAL_BTN_CLICK:
                case ACTION_MODAL_BTN:
                case ACTION_PAUSE:
                case ACTION_RESUME:
                case ACTION_SAVE:
                  skipActions = (packetSize == 8 + GameProtocol::GetActionSize(actionType));
                  break;
                case ACTION_SAVE_ENDED:
                  skipActions = true;
                  break;
              }
              if (!skipActions) {
                Print(Concat(GetLogPrefix(), "got action <", ByteArrayToHexString((uint8_t*)(packet.data() + 8), (size_t)(packetSize - 8)), ">"));
              }
              break;
            }

            case GameProtocol::Magic::OUTGOING_KEEPALIVE: {
              EventClientGameState(GameProtocol::RECEIVE_W3GS_OUTGOING_KEEPALIVE(packet));

              if (!m_Socket->GetConnected()) {
                Abort = true;
              }
              break;
            }

            case GameProtocol::Magic::CHAT_TO_HOST: {
              CIncomingMessageOrSettingsView incomingChatMessage = GameProtocol::RECEIVE_W3GS_CHAT_TO_HOST(packet);

              if (incomingChatMessage.GetIsValid()) {
                EventChatOrPlayerSettings(incomingChatMessage);
              } else {
                // empty chat, not UTF8 or contains control characters: ignore it
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

              CIncomingMapFileSize incomingMapSize = GameProtocol::RECEIVE_W3GS_MAPSIZE(packet);

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
          if (/*game && game->GetIsProxyReconnectable() && */packetType == GPSProtocol::Magic::INIT) {
            Print(Concat(GetLogPrefix(), "client started GProxy handshake "));
          }
          break;
        }

        default: {
          Abort = true;
        }
      }

      data.remove_prefix(packetSize);

      if (Abort) {
        // Process no more packets
        data.remove_prefix(data.size());
        break;
      }
    }

    if (data.size() != socket->GetRecvBufferSize()) {
      socket->UpdateRecvBuffer(data);
    }
  } else if (m_Aura->GetTicksIsAfterDelay(m_Socket->GetLastRecv(), timeout)) {
    SetLeftReasonGeneric("connection timed out");
    return AsyncObserverStatus::kDestroy;
  }

  if (Abort) {
    m_DeleteMe = true;
  }

  if (m_DeleteMe || !m_Socket->GetConnected() || m_Socket->HasError() || m_Socket->HasFin()) {
    SetLeftReasonGeneric("observer decomissioned");
    return AsyncObserverStatus::kDestroy;
  }

  if (!m_StartedLoading) {
    CheckStartLoading();
  } else if (m_FinishedLoading && !m_PlaybackEnded) {
    // If we don't wait a few seconds, the message gets lost to the F12 Chat Log ??
    const bool canSendChat =  m_Aura->GetTicksIsAfterDelay(m_FinishedLoadingTicks, 3000);
    if (!m_SentGameLoadedReport && canSendChat) {
      // Grace period so that chat messages are visible
      SendGameLoadedReport();
    }
    //const size_t beforeCounter = m_ActionFrameCounter;
    if (PushGameFrames()) {
      /*
      const size_t delta = SubtractClampZero(m_ActionFrameCounter, beforeCounter);
      if (beforeCounter <= 50 || delta > 1) Print(Concat(GetLogPrefix(), "pushed ", to_string(delta), " action frames"));
      //*/
      if (m_FrameRate > 1) {
        // High watermark
        if (
          (m_MaxSafeClientFrameRate < m_FrameRate) &&
          GetClientIsBehindFrames((size_t)(3000 * m_FrameRate / m_Latency))
        ) {
          ResetClientFrameRate();
          ResetFrameRateToClientSafe();
          SendChat(Concat("Your playback speed is limited to ", to_string(m_FrameRate), "x"));
        }
        if (canSendChat) {
          if (m_Aura->GetTimeIsAfterDelay(m_LastProgressReportTime, m_Aura->GetTicksIsAfterDelay(m_FinishedLoadingTicks, 120000) ? 75 : 30)) {
            SendProgressReport();
            m_MissingLog = GetClientMissingLog();
          } else if (m_Aura->GetTimeIsAfterDelay(m_LastProgressReportTime, 5)) {
            uint8_t missingLog = GetClientMissingLog();
            if (m_MissingLog < missingLog) {
              // Ensure progress reports around 75% 87.5% 91.25% ...
              SendProgressReport();
              m_MissingLog = missingLog;
            }
          }
        }
      }
    }
    CheckPlayBackOver();
  }

  if (m_Aura->GetTicksIsAfterDelay(m_LastPingTicks, 5000)) {
    Send(GameProtocol::SEND_W3GS_PING_FROM_HOST(m_Aura->GetLoopTicks()));
    m_LastPingTicks = m_Aura->GetLoopTicks();
  }

  m_Socket->DoSend(send_fd);
  return result;
}

uint8_t CAsyncObserver::GetChatChannel(bool forcePrivate) const
{
  if (!m_StartedLoading) return 0;
  if (m_IsObserver && !forcePrivate) return CHAT_RECV_OBS;
  uint32_t channel = integer_cast<uint32_t>(CHAT_RECV_PRIVATE_OFFSET);
  return integer_cast_lossy<uint8_t>(channel + integer_cast<uint32_t>(m_Color));
}

bool CAsyncObserver::GetIsGameOver() const
{
  return m_GameHistory->GetIsFinished();
}

void CAsyncObserver::CheckPlayBackOver()
{
  if (!GetIsGameOver()) return;
  FlushGameFrames();
  if (m_GameHistory->m_PlayingBuffer.size() <= m_Offset) {
    m_PlaybackEnded = true;
    Print(Concat(GetLogPrefix(), "playback ended"));
    SendChat("Playback ended. Game will exit automatically in 10 seconds.");

    // Kick after 10 seconds
    SetTimeoutAtLatest(m_Aura->GetLoopTicks() + 10000);
  }
}

int64_t CAsyncObserver::GetNextTimedActionByTicks() const
{
  if (GetGoalActionFrames() <= m_ActionFrameCounter) {
    return APP_MAX_TICKS;
  }
  return m_LastFrameTicks + m_Latency / m_FrameRate;
}

bool CAsyncObserver::GetClientIsBehindFrames(const size_t limit) const
{
  return GetClientFramesBehind() >= limit;
}

size_t CAsyncObserver::GetClientFramesBehind() const
{
  if (m_ActionFrameCounter < m_SyncCounter) return 0;
  return m_ActionFrameCounter - m_SyncCounter;
}

bool CAsyncObserver::PushGameFrames(bool isFlush)
{
  const int64_t hiResTicks = GetTicks();
  // Note: Actually, each frame may have its own custom latency.
  int64_t gameDurationWanted = m_FrameRate * (hiResTicks - m_LastFrameTicks);
  if (gameDurationWanted < m_Latency) {
    // Fast path for the common case (there will never be a GAME_FRAME_TYPE_LATENCY hanging)
    return false;
  }
  if (!isFlush && m_ActionFrameCounter >= GetGoalActionFrames()) {
    if (!m_TimeSynchronized) {
      if (m_FrameRate > 1) m_FrameRate = 1;
      m_TimeSynchronized = true;
      m_TimeLiveSynchronized = GetClientFrameClamped() >= m_GameHistory->GetNumActionFrames();
      if (m_TimeLiveSynchronized) {
        SendChat("You are now synchronized with the live game.");
      } else {
        SendChat("You are now synchronized with the live stream.");
      }
    }
    return false;
  }

  bool success = false;
  auto it = begin(m_GameHistory->m_PlayingBuffer) + signed_cast<ptrdiff_t>(m_Offset);
  auto itEnd = begin(m_GameHistory->m_PlayingBuffer) + signed_cast<ptrdiff_t>(m_GameHistory->GetSpectatorOffset());
  while (it != itEnd && (m_Latency <= gameDurationWanted || it->GetType() == GAME_FRAME_TYPE_LATENCY)) {
    //Print(Concat(GetLogPrefix(), "sending ", it->GetTypeName(), " frame"));
    switch (it->GetType()) {
      case GAME_FRAME_TYPE_GPROXY:
        // if stored, GAME_FRAME_TYPE_GPROXY always precedes GAME_FRAME_TYPE_ACTIONS
        Send(GameProtocol::SEND_W3GS_EMPTY_ACTIONS(m_GameHistory->GetGProxyEmptyActions()));
        break;
      case GAME_FRAME_TYPE_LATENCY:
        // it stored, GAME_FRAME_TYPE_LATENCY always goes after GAME_FRAME_TYPE_ACTIONS
        m_Latency = ByteArrayToUInt16LE(it->GetBytes(), 0);
        ResetClientFrameRate();
        break;
      case GAME_FRAME_TYPE_ACTIONS:
        gameDurationWanted -= m_Latency;
        m_GameTicks += m_Latency;
        // falls through
      case GAME_FRAME_TYPE_PAUSED:
        success = true;
        m_LastFrameTicks = hiResTicks;
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

  m_CheckSumsTimeStamps.TrySample(m_Aura->GetLoopTicks());
}

bool CAsyncObserver::UpdateClientGameState(const uint32_t checkSum)
{
  if (!m_StateSynchronized) return false;

  if (!m_Game.expired() && m_Game.lock()->GetSyncCounter() < m_SyncCounter) {
    string text = Concat(GetLogPrefix(), "incorrectly ahead of sync");
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
  constexpr static size_t fixedOffset = (
    2 /* W3GS type headers */ +
    2 /* W3GS packet byte size */ +
    2 /* EncodeSlotInfo() byte size */ +
    1 /* number of slots */ +
    1 /* download status offset in CGameSlot::GetProtocolArray() */
  );
  size_t progressionIndex = 9u * integer_cast<size_t>(m_SID) + fixedOffset;
  slotInfo[progressionIndex] = downloadProgression;
  Send(slotInfo);
}

MapTransferStatus CAsyncObserver::NextSendMap()
{
  if (m_Game.expired()) return MapTransferStatus::kNone;
  return m_Game.lock()->NextSendMap(this, GetUID(), GetMapTransfer()); 
}

void CAsyncObserver::EventDesync()
{
  while (!m_CheckSums.empty()) {
    m_CheckSums.pop();
  }
  string text = Concat(GetLogPrefix(), "desynchronized on ", ToOrdinalName(m_SyncCounter), " checksum - sent ", to_string(m_Offset), " total frames (", to_string(m_ActionFrameCounter), " actions)");
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

  if (!CheckStartLoading()) {
    if (auto game = m_Game.lock()) {
      int64_t remainingSeconds = (int64_t)game->m_Config.m_SpectatorDelay - m_GameHistory->m_Duration / 1000;
      if (remainingSeconds > 0) {
        SendChat(Concat("Please wait for spectator delay (", to_string(remainingSeconds), " seconds...)"));
      }
    }
  }
}

bool CAsyncObserver::CheckStartLoading()
{
  if (!m_MapReady || m_StartedLoading) return false;
  if (GetGoalActionFrames() == 0) {
    return false;
  }
  StartLoading();
  return true;
}

void CAsyncObserver::StartLoading()
{
  Print(Concat(GetLogPrefix(), "started loading"));
  Send(GameProtocol::SEND_W3GS_COUNTDOWN_START());
  Send(GameProtocol::SEND_W3GS_COUNTDOWN_END());
  m_StartedLoading = true;
}

void CAsyncObserver::EventGameLoaded()
{
  Print(Concat(GetLogPrefix(), "finished loading"));
  Send(m_GameHistory->m_LoadingRealBuffer);
  Send(m_GameHistory->m_LoadingVirtualBuffer);

  if (auto game = m_Game.lock()) {
    game->SendSpectatorChat(this, string_view(), Concat(GetName(), " joined as an spectator."));
  }
}

void CAsyncObserver::EventChat(const CIncomingMessageOrSettingsView& incomingChatMessage)
{
  const bool isLobbyChat = incomingChatMessage.GetType() == GameProtocol::ChatToHostType::CTH_MESSAGE_LOBBY;
  if (isLobbyChat == m_StartedLoading) {
    // Racing condition
    PRINT_IF(LogLevel::kDebug, Concat("Chat message from [", GetName(), "] ignored (game stage mismatch)"));
    return;
  }

  string_view textContent = incomingChatMessage.GetMessage();
  assert((!textContent.empty()) && "Chat message cannot be empty");
  bool shouldRelay = !isLobbyChat; // relay the chat message to other users
  const uint8_t targetType = incomingChatMessage.GetInGameChannel();

  if (!isLobbyChat && m_Aura->m_Config.m_LogGameChat == LOG_GAME_CHAT_ALWAYS) {
    Print(Concat(GetLogPrefix(), "[", GetName(), "] ", textContent));
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
      CommandTokensView commandTokens;
      ExtractMessageTokensAny(textContent, gameConfig->m_PrivateCmdToken, gameConfig->m_BroadcastCmdToken, commandTokens);
      isCommand = commandTokens.matchType != CommandTokensMatchType::kNone;
      if (isCommand) {
        string cmdToken(commandTokens.token);
        string command = ToLowerCase(commandTokens.cmd);
        string target(commandTokens.target);
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
      } else if (textContent == "?trigger") {
        if (shouldRelay) {
          //SendChat(incomingChatMessage);
          shouldRelay = false;
        }
        //TODO:SendCommandsHelp()
        //game->SendCommandsHelp(gameConfig->m_BroadcastCmdToken.empty() ? gameConfig->m_PrivateCmdToken : gameConfig->m_BroadcastCmdToken, this, false);
      } else if (textContent == "/p" || textContent == "/ping" || textContent == "/game") {
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
          string cmdToken(gameConfig->m_PrivateCmdToken);
          string command(textContent.substr(1));
          string target;
          ctx->Run(cmdToken, command, target);
        }
      } else if (isLobbyChat && !cmdHistory->GetUsedAnyCommands()) {
        if (shouldRelay) {
          //SendChat(incomingChatMessage);
          shouldRelay = false;
        }
        /*
        // TODO: CAsyncObserver smart commands
        if (!game->CheckSmartCommands(this, textContent, activeSmartCommand, commandCFG) && !GetCommandHistory()->GetSentAutoCommandsHelp()) {
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
    }
    bool relaySuccess = false;
    if (shouldRelay && game) {
      string prefix = Concat("[", ToFormattedTimeStamp(m_GameTicks / 1000), "] [", m_Name, "]: ");
      relaySuccess = game->SendSpectatorChat(this, prefix, textContent);
    }
    if (shouldRelay && m_IsObserver) {
      if (relaySuccess && targetType != CHAT_RECV_OBS) {
        SendChat("[All] Chat is DISABLED. You are in spectator mode, and may only chat with other spectators.");
      } else if (!relaySuccess) {
        SendChat("You are in spectator mode, and may only chat with other spectators. No other spectators found.");
      }
    }
    if (shouldRelay && relaySuccess) {
      shouldRelay = false;
    }
    if (shouldRelay) {
      //SendChat(incomingChatMessage);
      shouldRelay = false;
    }
  }
}

void CAsyncObserver::EventChatOrPlayerSettings(const CIncomingMessageOrSettingsView& incomingChatMessage)
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
    string reason;
    if (clientReason == PLAYERLEAVE_GPROXY) {
      reason = Concat(" (", GameProtocol::LeftCodeToString(clientReason), ")");
    }
    Print(Concat(GetLogPrefix(), "left the game at [", ToFormattedTimeStamp(m_GameTicks / 1000), "]", reason));
    if (m_FinishedLoading) {
      if (auto game = m_Game.lock()) {
        game->SendSpectatorChat(this, string_view(), Concat(GetName(), " left spectator mode."));
      }
    }
    /*
    if (m_GameHistory->m_PlayingBuffer.size() <= m_Offset) {
      Print(Concat(GetLogPrefix(), "next frame was not available"));
    } else {
      Print(Concat(GetLogPrefix(), "next frame was ", m_GameHistory->m_PlayingBuffer[m_Offset].GetTypeName()));
    }
    */
  } else {
    Print(Concat(GetLogPrefix(), "left the lobby"));
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

void CAsyncObserver::Send(const GameProtocol::PacketWrapper& data)
{
  if (m_Socket && !m_Socket->HasError()) {
    m_Socket->PutBytes(data.data);
  }
}

void CAsyncObserver::SendOtherPlayersInfo()
{
  Send(m_GameHistory->m_PlayersBuffer);
}

void CAsyncObserver::SendChat(string_view message)
{
  if (m_StartedLoading && !m_FinishedLoading) {
    return;
  }
  if (!m_StartedLoading) {
    Send(GameProtocol::SEND_W3GS_CHAT_FROM_HOST_LOBBY(m_UID, CreateByteArray(m_UID), GameProtocol::Magic::ChatType::CHAT_LOBBY, message));
  } else {
    Send(GameProtocol::SEND_W3GS_CHAT_FROM_HOST_IN_GAME(m_UID, CreateByteArray(m_UID), GameProtocol::Magic::ChatType::CHAT_IN_GAME, GetChatChannel(true), message));
  }
}

void CAsyncObserver::SendGameLoadedReport()
{
  shared_ptr<CGame> game = GetGame();
  size_t numSpectators = game ? game->GetNumSpectators() : 1;
  string otherSpectators;
  if (numSpectators > 1) {
    otherSpectators = Concat(" with ", to_string(numSpectators - 1), " other user(s)");
  }
  if (m_GameHistory->GetIsFinished()) {
    int64_t playedAgo = (m_Aura->GetLoopTicks() - m_GameHistory->GetFinishedTicks()) / 1000;
    string playedAgoFragment;
    if (playedAgo > 0) {
      playedAgoFragment = Concat(ToDurationString(playedAgo), " ago");
    } else {
      playedAgoFragment = "just now";
    }
    SendChat("Watching replay");
    SendChat(Concat("Game was played ", playedAgoFragment, ". Duration: ", ToFormattedTimeStamp(m_GameHistory->GetDuration() / 1000)));
  } else {
    int64_t delay = m_GameHistory->GetSpectatorDelay();
    string delayHint;
    if (delay > 0) {
      delayHint = Concat(" (delay is ", ToDurationString(delay / 1000), ")");
    }
    SendChat(Concat("Watching game", otherSpectators + delayHint));
  }
  if (m_FrameRate > 1) {
    SendChat("Use !sync to watch at 1x, !ff to fast-forward");
  } else {
    SendChat("Use !ff to fast-forward, !sync to watch at 1x");
  }

  m_SentGameLoadedReport = true;
}

void CAsyncObserver::SampleMaxSafeFrameRate()
{
  optional<double> maybeClientFrameRate = GetClientFrameRate();
  if (!maybeClientFrameRate.has_value()) return;
  // intentionally flooring
  uint64_t clientFrameRate = DoubleToUnsigned(maybeClientFrameRate.value());
  if ((uint64_t)m_MaxFrameRate < clientFrameRate) {
    // Restrict to 1x-64x range
    clientFrameRate = (uint64_t)m_MaxFrameRate;
  }
  if (m_MaxSafeClientFrameRate < (int64_t)clientFrameRate) {
    m_MaxSafeClientFrameRate = (int64_t)clientFrameRate;
  }
}

void CAsyncObserver::ResetClientFrameRate()
{
  SampleMaxSafeFrameRate();
  m_CheckSumsTimeStamps.Reset();
}

size_t CAsyncObserver::GetClientFrameClamped() const
{
  return min(m_SyncCounter, m_ActionFrameCounter);
}

optional<double> CAsyncObserver::GetClientFrameRate() const
{
  auto timeStamps = m_CheckSumsTimeStamps.GetData();
  if (timeStamps.size() < 2) return nullopt;
  int64_t deltaTicks = timeStamps.back() - timeStamps.front();
  if (deltaTicks < 0) return nullopt;
  return (double)((int64_t)(timeStamps.size() - 1) * m_Latency * (int64_t)(m_CheckSumsTimeStamps.GetRate())) / (double)(deltaTicks);
}

uint8_t CAsyncObserver::GetClientMissingLog() const
{
  constexpr double epsilon = numeric_limits<double>::epsilon();
  double missing = 1.0 - ((double)(GetClientFrameClamped()) / (double)m_GameHistory->GetNumActionFrames());
  if (missing < epsilon) return 0;
  double missingLog = clamp(-log2(missing), 0.0, 255.0);
  return static_cast<uint8_t>(missingLog);
}

size_t CAsyncObserver::GetGoalActionFrames() const
{
  return m_Goal == AsyncObserverGoal::kSpectator ? m_GameHistory->GetNumSpectatorActionFrames() : m_GameHistory->GetNumActionFrames();
}

void CAsyncObserver::SendProgressReport()
{
  constexpr double epsilon = numeric_limits<double>::epsilon();
  size_t clientFrame = GetClientFrameClamped();
  double clientFrameRate = GetClientFrameRate().value_or((double)m_FrameRate);
  double progress = (double)clientFrame / (double)GetGoalActionFrames();

  double catchUpFrameRate = clientFrameRate;
  if (!GetIsGameOver()) catchUpFrameRate = max(0.0, catchUpFrameRate - 1.0);

  bool isFastForward = round(clientFrameRate) > 1;

  string message = Concat(ToFormattedString(PERCENT_FACTOR * progress), "%");
  if (isFastForward) {
    message.append(Concat(" - Fast-forwarding at ", to_string(static_cast<int64_t>(round(clientFrameRate))), "x"));
  }
  if (catchUpFrameRate > epsilon) {
    // Estimate time for catching up with live (or finished) game,
    // assuming that latency will be constant.
    uint64_t etaSeconds = DoubleToUnsigned((double)m_Latency * (double)((GetGoalActionFrames() - clientFrame)) / catchUpFrameRate / (double)1000.0);
    // Let it fit in chat log (F12)
    message.append(Concat(" - ETA ", ToDurationString(etaSeconds)));
  }
  SendChat(message);

  if (!m_CheckSumsTimeStamps.GetIsEmpty() || (clientFrameRate - 6.) <= epsilon /* 6x or slower can be trusted */) {
    m_LastProgressReportTime = m_Aura->GetLoopTime();
  }
}

string CAsyncObserver::GetLogPrefix() const
{
  if (!m_Game.expired()) return Concat(m_Game.lock()->GetLogPrefix(), "[SPECTATOR] [", m_Name, "] ");
  return Concat("[SPECTATOR] [", m_Name, "] ");
}
