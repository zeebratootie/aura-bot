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

#ifndef AURA_GAMEUSER_H_
#define AURA_GAMEUSER_H_

#include "includes.h"
#include "command_history.h"
#include "connection.h"
#include "protocol/game_protocol.h"
#include "game_structs.h"
#include "rate_limiter.h"
#include "map.h"

//
// GameUser::CGameUser
//

namespace GameUser
{
  namespace KickReason
  {
    constexpr uint8_t NONE = 0u;
    constexpr uint8_t MAP_MISSING = 1u;
    constexpr uint8_t HIGH_PING = 2u;
    constexpr uint8_t SPOOFER = 4u;
    constexpr uint8_t ABUSER = 8u;
    constexpr uint8_t ANTISHARE = 16u;
  };

  class CGameUser final : public CConnection
  {
  public:
    std::reference_wrapper<CGame>    m_Game;
    std::shared_ptr<CGProxyServer>   m_GProxy;
    MapTransfer                      m_MapTransfer;
    CommandHistory                   m_CommandHistory;
    std::array<uint8_t, 4>           m_IPv4Internal;                 // the player's internal IP address as reported by the player when connecting
    std::vector<uint32_t>            m_RTTValues;                    // store the last few (10) pings received so we can take an average
    OptionalTimedUint32              m_MeasuredRTT;
    std::queue<uint32_t>             m_CheckSums;                    // the last few checksums the player has sent (for detecting desyncs)
    std::string                      m_LeftReason;                   // the reason the player left the game
    uint32_t                         m_RealmInternalId;
    std::string                      m_RealmHostName;                // the realm the player joined on (probable, can be spoofed)
    std::string                      m_Name;                         // the player's name
    size_t                           m_TotalPacketsSent;             // the total number of packets sent to the player
    uint32_t                         m_TotalPacketsReceived;         // the total number of packets received from the player
    uint32_t                         m_LeftCode;                     // the code to be sent in W3GS_PLAYERLEAVE_OTHERS for why this player left the game
    uint8_t                          m_Status;
    bool                             m_IsLeaver;
    uint8_t                          m_PingEqualizerOffset;          // how many frames are actions sent by this player offset by ping equalizer
    QueuedActionsFrameNode*          m_PingEqualizerFrameNode;
    std::queue<CIncomingAction>      m_OnHoldActionsQueue;                // if anti-share is enabled, holds actions sent by this player until all shared units settings are reverted
    size_t                           m_OnHoldActionsCount;
    std::bitset<MAX_SLOTS_MODERN>    m_ControllingUnitsFromSID;
    std::bitset<MAX_SLOTS_MODERN>    m_SharingUnitsWithSID;
    uint32_t                         m_PongCounter;
    size_t                           m_SyncCounterOffset;            // missed keepalive packets we are gonna ignore
    size_t                           m_SyncCounter;                  // the number of keepalive packets received from this player
    int64_t                          m_JoinTicks;                    // when the player joined the game (used to delay sending the /whois a few seconds to allow for some lag)
    int64_t                          m_FinishedLoadingTicks;         // when the player finished loading the game
    int64_t                          m_HandicapTicks;
    int64_t                          m_StartedLaggingTicks;          // when the player started laggin
    std::optional<int64_t>           m_KickByTicks;
    uint8_t                          m_SID;                          // the player's SID - this is well defined only after the game starts loading
    uint8_t                          m_UID;                          // the player's UID
    uint8_t                          m_OldUID;
    uint8_t                          m_PseudonymUID;
    bool                             m_GameVersionIsExact;
    Version                          m_GameVersion;
    bool                             m_Verified;                     // if the player has spoof checked or not
    bool                             m_Owner;                        // if the player has spoof checked or not
    bool                             m_Reserved;                     // if the player is reserved (VIP) or not
    bool                             m_Observer;                     // if the player is an observer
    bool                             m_PowerObserver;                // if the player is a referee - referees can be demoted to full observers
    bool                             m_WhoisShouldBeSent;            // if a battle.net /whois should be sent for this player or not
    bool                             m_WhoisSent;                    // if we've sent a battle.net /whois for this player yet (for spoof checking)
    bool                             m_MapChecked;                   // if we received any W3GS_MAPSIZE packet from the client
    bool                             m_MapReady;                     // if we received a valid W3GS_MAPSIZE packet from the client matching the map size
    bool                             m_InGameReady;
    std::optional<bool>              m_UserReady;
    bool                             m_Ready;
    std::optional<int64_t>           m_ReadyReminderLastTicks;
    uint8_t                          m_KickReason;                   // bitmask for all the reasons why this user is going to be kicked
    bool                             m_HasHighPing;                  // if last time we checked, the player had high ping
    bool                             m_DownloadAllowed;              // if we're allowed to download the map or not (used with permission based map downloads)
    bool                             m_FinishedLoading;              // if the player has finished loading or not
    bool                             m_Lagging;                      // if the player is lagging or not (on the lag screen)
    std::optional<bool>              m_DropVote;                     // if the player voted to drop the laggers or not (on the lag screen)
    std::optional<bool>              m_KickVote;                     // if the player voted to kick a player or not
    bool                             m_Muted;                        // if the player is muted or not
    bool                             m_ActionLocked;                 // if the player is not allowed to use commands, change their race/team/color/handicap or they are
    bool                             m_LeftMessageSent;              // if the playerleave message has been sent or not
    bool                             m_StatusMessageSent;            // if the message regarding player connection mode has been sent or not
    bool                             m_LatencySent;
    int64_t                          m_CheckStatusByTicks;
    int64_t                          m_MuteEndTicks;

    bool                             m_Disconnected;
    bool                             m_DisconnectNoticeSent;
    int64_t                          m_TotalDisconnectTicks;
    std::optional<int64_t>           m_LastDisconnectTicks;
    std::optional<int64_t>           m_LastDisconnectRepeatNoticeTicks;

    uint8_t                          m_TeamCaptain;

    std::string                      m_PinnedMessage;

    // Actions
    uint32_t                                    m_ActionCounter;
    std::array<uint32_t, 3>                     m_RecentActionCounter;
    uint8_t                                     m_AntiAbuseCounter;
    uint8_t                                     m_RemainingSaves;
    uint8_t                                     m_RemainingPauses;
    std::optional<uint8_t>                      m_SelfGameResult;
    std::optional<uint8_t>                      m_FinalGameResult;
    std::optional<TokenBucketRateLimiter>       m_APMQuota;
    std::optional<double>                       m_APMTrainer;

    CGameUser(std::shared_ptr<CGame> game, CConnection* connection, uint8_t nUID, const bool gameVersionIsExact, const Version& gameVersion, uint32_t nJoinedRealmInternalId, std::string nJoinedRealm, std::string nName, std::array<uint8_t, 4> nInternalIP, bool nReserved);
    ~CGameUser() final;

    [[nodiscard]] uint32_t GetOperationalRTT() const;
    [[nodiscard]] uint32_t GetDisplayRTT() const;
    [[nodiscard]] uint32_t GetRTT() const;
    [[nodiscard]] std::string GetConnectionErrorString() const;
    [[nodiscard]] inline bool                     GetIsReady() const { return m_Ready; }
    [[nodiscard]] inline uint8_t                  GetSID() const { return m_SID; }
    [[nodiscard]] inline uint8_t                  GetUID() const { return m_UID; }
    [[nodiscard]] inline uint8_t                  GetOldUID() const { return m_OldUID; }
    [[nodiscard]] inline uint8_t                  GetPseudonymUID() const { return m_PseudonymUID; }
    [[nodiscard]] inline bool                     GetGameVersionIsExact() const { return m_GameVersionIsExact; }
    [[nodiscard]] inline Version                  GetGameVersion() const { return m_GameVersion; }
    [[nodiscard]] std::string                     GetGameVersionString() const;
    [[nodiscard]] inline std::string              GetName() const { return m_Name; }
    [[nodiscard]] std::string                     GetLowerName() const;
    [[nodiscard]] std::string                     GetDisplayName() const;
    [[nodiscard]] std::shared_ptr<CGame>          GetGame();
    [[nodiscard]] std::shared_ptr<CGProxyServer>  GetGProxy() const;
    [[nodiscard]] inline MapTransfer&             GetMapTransfer() { return m_MapTransfer; }
    [[nodiscard]] inline const MapTransfer&       InspectMapTransfer() const { return m_MapTransfer; }
    [[nodiscard]] bool                            GetIsDownloading() const;
    [[nodiscard]] inline CommandHistory*          GetCommandHistory() { return &m_CommandHistory; }
    [[nodiscard]] inline const CommandHistory*    InspectCommandHistory() const { return &m_CommandHistory; }
    [[nodiscard]] inline std::array<uint8_t, 4>   GetIPv4Internal() const { return m_IPv4Internal; }
    [[nodiscard]] inline size_t                   GetStoredRTTCount() const { return m_RTTValues.size(); }
    [[nodiscard]] inline bool                     GetIsRTTMeasured() const { return m_MeasuredRTT.has_value() || !m_RTTValues.empty(); }
    [[nodiscard]] bool                            GetIsRTTMeasuredConsistent() const;
    [[nodiscard]] bool                            GetIsRTTMeasuredBadConsistent() const;
    [[nodiscard]] bool                            GetCanReconnect() const;
    [[nodiscard]] inline uint32_t                 GetPongCounter() const { return m_PongCounter; }
    [[nodiscard]] inline size_t                   GetNumCheckSums() const { return m_CheckSums.size(); }
    [[nodiscard]] inline std::queue<uint32_t>*    GetCheckSums() { return &m_CheckSums; }
    [[nodiscard]] inline bool                     HasCheckSums() const { return !m_CheckSums.empty(); }
    [[nodiscard]] inline bool                     HasLeftReason() const { return !m_LeftReason.empty(); }
    [[nodiscard]] inline std::string              GetLeftReason() const { return m_LeftReason; }
    [[nodiscard]] inline uint32_t                 GetLeftCode() const { return m_LeftCode; }
    [[nodiscard]] inline bool                     GetIsLeaver() const { return m_IsLeaver; }
    [[nodiscard]] inline bool                     GetIsInLoadingScreen() const { return m_Status == USERSTATUS_LOADING_SCREEN; }
    [[nodiscard]] inline bool                     GetIsEnding() const { return m_Status == USERSTATUS_ENDING; }
    [[nodiscard]] inline bool                     GetIsEnded() const { return m_Status == USERSTATUS_ENDED; }
    [[nodiscard]] inline bool                     GetIsEndingOrEnded() const { return m_Status == USERSTATUS_ENDING || m_Status == USERSTATUS_ENDED; }
    [[nodiscard]] inline bool                     GetIsLobbyOrPlaying() const { return m_Status == USERSTATUS_LOBBY || m_Status == USERSTATUS_PLAYING; }
    [[nodiscard]] inline uint8_t                  GetPingEqualizerOffset() const { return m_PingEqualizerOffset; }
    [[nodiscard]] uint32_t                        GetPingEqualizerDelay() const;
    [[nodiscard]] inline QueuedActionsFrameNode*  GetPingEqualizerFrameNode() const { return m_PingEqualizerFrameNode; }
    [[nodiscard]] CQueuedActionsFrame&            GetPingEqualizerFrame();

    [[nodiscard]] inline std::queue<CIncomingAction>& GetOnHoldActions() { return m_OnHoldActionsQueue; }
    [[nodiscard]] inline bool                     GetOnHoldActionsAny() const { return !m_OnHoldActionsQueue.empty(); }
    inline void                                   AddOnHoldActionsCount(size_t count) { m_OnHoldActionsCount += count; }
    inline void                                   SubtractOnHoldActionsCount(size_t count) { m_OnHoldActionsCount -= count; }
    [[nodiscard]] size_t                          GetOnHoldActionsCount() const { return m_OnHoldActionsCount; }

    [[nodiscard]] std::shared_ptr<CRealm>         GetRealm(bool mustVerify) const;
    [[nodiscard]] std::string                     GetRealmDataBaseID(bool mustVerify) const;
    [[nodiscard]] inline uint32_t                 GetRealmInternalID() const { return m_RealmInternalId; }
    [[nodiscard]] inline std::string              GetRealmHostName() const { return m_RealmHostName; }
    [[nodiscard]] inline std::string              GetExtendedName() const {
      if (m_RealmHostName.empty()) {
        return m_Name + "@@@LAN/VPN";
      } else {
        return m_Name + "@" + m_RealmHostName;
      }
    }
    [[nodiscard]] inline bool                  GetIsRealmVerified() const { return m_Verified; }
    [[nodiscard]] inline size_t                GetSyncCounter() const { return m_SyncCounter; }
    [[nodiscard]] inline size_t                GetNormalSyncCounter() const { return m_SyncCounter + m_SyncCounterOffset; }
    [[nodiscard]] bool                         GetIsBehindFramesNormal(const uint32_t limit) const;
    [[nodiscard]] inline int64_t               GetJoinTicks() const { return m_JoinTicks; }
    [[nodiscard]] inline int64_t               GetFinishedLoadingTicks() const { return m_FinishedLoadingTicks; }
    [[nodiscard]] inline int64_t               GetHandicapTicks() const { return m_HandicapTicks; }

    [[nodiscard]] inline int64_t               GetStartedLaggingTicks() const { return m_StartedLaggingTicks; }

    [[nodiscard]] inline bool                  GetDisconnected() const { return m_Disconnected; }
    bool                                       GetDisconnectedUnrecoverably() const;
    [[nodiscard]] int64_t                      GetTotalDisconnectTicks() const;
    [[nodiscard]] inline bool                  GetDisconnectNoticeSent() const { return m_DisconnectNoticeSent; }
    inline void                                SetDisconnectNoticeSent(bool nDisconnectNoticeSent = true) { m_DisconnectNoticeSent = nDisconnectNoticeSent; }
    [[nodiscard]] std::string                  GetReconnectionText() const;
    [[nodiscard]] std::string                  GetDelayText(bool displaySync) const;
    [[nodiscard]] std::string                  GetSyncText() const;
    
    [[nodiscard]] inline bool                  GetIsReserved() const { return m_Reserved; }
    [[nodiscard]] inline bool                  GetIsObserver() const { return m_Observer; }
    [[nodiscard]] inline bool                  GetIsPowerObserver() const { return m_PowerObserver; }
    [[nodiscard]] bool                         GetIsNativeReferee() const;
    [[nodiscard]] bool                         GetCanUsePublicChat() const;
    bool                                       Mute(const int64_t seconds);
    bool                                       UnMute();
    [[nodiscard]] inline bool                  GetWhoisShouldBeSent() const { return m_WhoisShouldBeSent; }
    [[nodiscard]] inline bool                  GetWhoisSent() const { return m_WhoisSent; }
    [[nodiscard]] inline bool                  GetDownloadAllowed() const { return m_DownloadAllowed; }
    [[nodiscard]] inline bool                  GetFinishedLoading() const { return m_FinishedLoading; }
    [[nodiscard]] inline bool                  GetMapChecked() const { return m_MapChecked; }
    [[nodiscard]] inline bool                  GetMapReady() const { return m_MapReady; }
    [[nodiscard]] inline bool                  GetInGameReady() const { return m_InGameReady; }
    [[nodiscard]] inline bool                  GetMapKicked() const { return (m_KickReason & GameUser::KickReason::MAP_MISSING) != GameUser::KickReason::NONE; }
    [[nodiscard]] inline bool                  GetPingKicked() const { return (m_KickReason & GameUser::KickReason::HIGH_PING) != GameUser::KickReason::NONE; }
    [[nodiscard]] inline bool                  GetSpoofKicked() const { return (m_KickReason & GameUser::KickReason::SPOOFER) != GameUser::KickReason::NONE; }
    [[nodiscard]] inline bool                  GetAbuseKicked() const { return (m_KickReason & GameUser::KickReason::ABUSER) != GameUser::KickReason::NONE; }
    [[nodiscard]] inline bool                  GetAntiShareKicked() const { return (m_KickReason & GameUser::KickReason::ANTISHARE) != GameUser::KickReason::NONE; }
    [[nodiscard]] inline bool                  GetAnyKicked() const { return m_KickReason != GameUser::KickReason::NONE; }
    [[nodiscard]] inline bool                  GetHasHighPing() const { return m_HasHighPing; }
    [[nodiscard]] inline bool                  GetKickQueued() const { return m_KickByTicks.has_value(); }
    [[nodiscard]] inline bool                  GetIsLagging() const { return m_Lagging; }
    [[nodiscard]] inline std::optional<bool>   GetDropVote() const { return m_DropVote; }
    [[nodiscard]] inline std::optional<bool>   GetKickVote() const { return m_KickVote; }
    [[nodiscard]] inline bool                  GetIsMuted() const { return m_Muted; }
    [[nodiscard]] inline int64_t               GetMuteEndTicks() const { return m_MuteEndTicks; }
    [[nodiscard]] inline bool                  GetIsActionLocked() const { return m_ActionLocked; }
    [[nodiscard]] inline bool                  GetStatusMessageSent() const { return m_StatusMessageSent; }
    [[nodiscard]] inline bool                  GetLatencySent() const { return m_LatencySent; }
    [[nodiscard]] inline bool                  GetLeftMessageSent() const { return m_LeftMessageSent; }
    [[nodiscard]] bool                         UpdateReady();
    [[nodiscard]] bool                         GetIsOwner(std::optional<bool> nAssumeVerified) const;
    [[nodiscard]] inline bool                  GetIsDraftCaptain() const { return m_TeamCaptain != 0; }
    [[nodiscard]] inline bool                  GetIsDraftCaptainOf(const uint8_t nTeam) const { return m_TeamCaptain == nTeam + 1; }
    [[nodiscard]] inline bool                  GetCanPause() const { return m_RemainingPauses > 0; }
    [[nodiscard]] inline bool                  GetCanSave() const { return m_RemainingSaves > 0; }

    inline void SetSID(const uint8_t nSID) { m_SID = nSID; }
    inline void SetLeftReason(const std::string& nLeftReason) { m_LeftReason = nLeftReason; }
    inline void AddLeftReason(const std::string& nLeftReason) {
      if (m_LeftReason.empty()) SetLeftReason(nLeftReason);
      else m_LeftReason.append(" - " + nLeftReason);
    }
    inline void SetLeftCode(uint32_t nLeftCode) { m_LeftCode = nLeftCode; }
    inline void SetIsLeaver(bool nIsLeaver) { m_IsLeaver = nIsLeaver; }
    inline void SetStatus(uint8_t nStatus) { m_Status = nStatus; }
    inline void TrySetEnding() {
      if (m_Status != USERSTATUS_ENDED) {
        m_Status = USERSTATUS_ENDING;
      }
    }
    inline void SetPingEqualizerOffset(uint8_t nOffset) { m_PingEqualizerOffset = nOffset; }
    void AdvanceActiveGameFrame();
    bool AddDelayPingEqualizerFrame();
    bool SubDelayPingEqualizerFrame();
    inline void SetPingEqualizerFrameNode(QueuedActionsFrameNode* nFrame) { m_PingEqualizerFrameNode = nFrame; }
    void ReleaseOnHoldActionsCount(size_t count);
    void ReleaseOnHoldActions();
    void UpdateAPMQuota();
    bool GetShouldHoldActionInner();
    bool GetShouldHoldAction(uint16_t count);
    void CheckReleaseOnHoldActions();
    bool CheckMuted();

    void AddActionCounters();
    void ShiftRecentActionCounters();
    inline void AddAbuseCounter() { ++m_AntiAbuseCounter; }
    inline int64_t GetAntiAbuseTimeout() const { return 5000 / (1 << m_AntiAbuseCounter); }

    inline bool GetIsSharingUnitsWithAnyAllies() const { return m_SharingUnitsWithSID.any(); }
    inline bool GetIsSharingUnitsWithSlot(const uint8_t SID) const { return m_SharingUnitsWithSID.test(SID); }
    inline void SetIsSharingUnitsWithSlot(const uint8_t SID, const bool wantsShare) {
      if (wantsShare) m_SharingUnitsWithSID.set(SID);
      else m_SharingUnitsWithSID.reset(SID);
    }

    inline bool GetHasControlOverAnyAlliedUnits() const { return m_ControllingUnitsFromSID.any(); }
    inline bool GetHasControlOverUnitsFromSlot(const uint8_t SID) const { return m_ControllingUnitsFromSID.test(SID); }
    inline void SetHasControlOverUnitsFromSlot(const uint8_t SID, const bool wantsShare) {
      if (wantsShare) m_ControllingUnitsFromSID.set(SID);
      else m_ControllingUnitsFromSID.reset(SID);
    }

    inline void SetSyncCounter(const size_t nSyncCounter) { m_SyncCounter = nSyncCounter; }
    inline void AddSyncCounterOffset(const size_t nOffset) { m_SyncCounterOffset += nOffset; }
    inline void ResetSyncCounterOffset() { m_SyncCounterOffset = 0; }
    inline void SetHandicapTicks(uint64_t nHandicapTicks) { m_HandicapTicks = nHandicapTicks; }
    inline void SetStartedLaggingTicks(uint64_t nStartedLaggingTicks) { m_StartedLaggingTicks = nStartedLaggingTicks; }
    inline void SetRealmVerified(bool nVerified) { m_Verified = nVerified; }
    inline void SetOwner(bool nOwner) { m_Owner = nOwner; }
    inline void SetReserved(bool nReserved) { m_Reserved = nReserved; }
    inline void SetIsObserver(bool nObserver) { m_Observer = nObserver; }
    inline void SetPseudonymUID(uint8_t nUID) { m_PseudonymUID = nUID; }
    inline void SetPowerObserver(bool nPowerObserver) { m_PowerObserver = nPowerObserver; }
    inline void SetWhoisShouldBeSent(bool nWhoisShouldBeSent) { m_WhoisShouldBeSent = nWhoisShouldBeSent; }
    inline void SetDownloadAllowed(bool nDownloadAllowed) { m_DownloadAllowed = nDownloadAllowed; }
    inline void SetMapChecked(bool nChecked) { m_MapChecked = nChecked; }
    inline void SetMapReady(bool nHasMap) { m_MapReady = nHasMap; }
    inline void SetMapReady() { m_MapReady = true; }
    inline void SetInGameReady(bool nInGameReady) { m_InGameReady = nInGameReady; }
    inline void SetInGameReady() { m_InGameReady = true; }
    inline void SetHasHighPing(bool nHasHighPing) { m_HasHighPing = nHasHighPing; }
    inline void SetLagging(bool nLagging) { m_Lagging = nLagging; }
    inline void SetDropVote(bool nDropVote) { m_DropVote = nDropVote; }
    inline void SetKickVote(bool nKickVote) { m_KickVote = nKickVote; }
    inline void SetMuteEndTicks(int64_t nTicks) { m_MuteEndTicks = nTicks; }
    inline void SetActionLocked(bool nActionLocked) { m_ActionLocked = nActionLocked; }
    inline void SetStatusMessageSent(bool nStatusMessageSent) { m_StatusMessageSent = nStatusMessageSent; }
    inline void SetLatencySent(bool nLatencySent) { m_LatencySent = nLatencySent; }
    inline void SetLeftMessageSent(bool nLeftMessageSent) { m_LeftMessageSent = nLeftMessageSent; }
    void DisableReconnect();
    inline void SetKickByTicks(int64_t nKickByTicks) { m_KickByTicks = nKickByTicks; }
    inline void ClearKickByTicks() { m_KickByTicks = std::nullopt; }
    inline void AddKickReason(const uint8_t nKickReason) { m_KickReason |= nKickReason; }
    inline void RemoveKickReason(const uint8_t nKickReason) { m_KickReason &= ~nKickReason; }
    inline void ResetKickReason() { m_KickReason = GameUser::KickReason::NONE; }
    inline void KickAtLatest(int64_t nKickByTicks) {
      if (!m_KickByTicks.has_value() || nKickByTicks < m_KickByTicks.value()) {
        m_KickByTicks = nKickByTicks;
      }
    }
    inline void CheckStillKicked() {
      if (!GetAnyKicked() && GetKickQueued()) {
        ClearKickByTicks();
      }
    }
    inline void ResetLeftReason() { m_LeftReason.clear(); }
    inline void SetUserReady(bool nReady) { m_UserReady = nReady; }
    inline void ClearUserReady() { m_UserReady = std::nullopt; }

    [[nodiscard]] bool GetReadyReminderIsDue() const;
    void SetReadyReminded();

    inline void SetDraftCaptain(const uint8_t nTeamNumber) { m_TeamCaptain = nTeamNumber; }
    inline void DropRemainingSaves() { --m_RemainingSaves; }
    inline void SetRemainingSaves(uint8_t nCount) { m_RemainingSaves = nCount; }
    inline void SetCannotSave() { m_RemainingSaves = 0; }

    inline void DropRemainingPauses() { --m_RemainingPauses; }
    inline void SetCannotPause() { m_RemainingPauses = 0; }
    void ClearStalePings();

    [[nodiscard]] inline const std::string& GetPinnedMessage() { return  m_PinnedMessage; }
    [[nodiscard]] inline bool GetHasPinnedMessage() { return !m_PinnedMessage.empty(); }
    inline void SetPinnedMessage(const std::string nPinnedMessage) { m_PinnedMessage = nPinnedMessage; }
    inline void ClearPinnedMessage() { m_PinnedMessage.clear(); }

    [[nodiscard]] bool GetIsSyncCounterStartLagging() const;
    [[nodiscard]] bool GetIsSyncCounterStopLag() const;

    void RefreshUID();
    inline void SetSelfReportedGameResult(const uint8_t result) { m_SelfGameResult = result; }
    inline void SetFinalGameResult(const uint8_t result) { m_FinalGameResult = result; }

    double GetAPM() const;
    double GetRecentAPM() const;
    double GetMostRecentAPM() const;
    inline bool GetHasAPMQuota() const { return m_APMQuota.has_value(); }
    inline const TokenBucketRateLimiter& InspectAPMQuota() { return m_APMQuota.value(); }
    inline TokenBucketRateLimiter& GetAPMQuota() { return m_APMQuota.value(); }
    inline bool GetHasAPMTrainer() const { return m_APMTrainer.has_value(); }
    inline double GetAPMTrainerTarget() const { return m_APMTrainer.value(); }
    inline void SetAPMTrainer(double apm) { m_APMTrainer = apm; }
    inline void DisableAPMTrainer() { m_APMTrainer.reset(); }
    void RestrictAPM(double apm, double burstActions);

    // processing functions

    [[nodiscard]] bool Update(fd_set* fd, int64_t timeout);
    [[nodiscard]] uint8_t NextSendMap();

    // other functions

    void Send(const std::vector<uint8_t>& data) final;


    void EventGProxyClientInit(const uint32_t version);
    void EventGProxyExtendedClientInit(const std::vector<uint8_t>& data);
    void EventGProxyChangeKey(const uint32_t key);
    void EventGProxyAck(const size_t lastPacket);
    void EventGProxyReconnect(CConnection* connection, const uint32_t LastPacket);
    void EventGProxyReconnectInvalid();

    bool CloseConnection(bool fromOpen = false);
    void UnrefConnection(bool deferred = false);
  };

  [[nodiscard]] inline std::string ToNameListSentence(ImmutableUserList userList, bool useRealNames = false) {
    if (userList.empty()) return std::string();
    std::vector<std::string> userNames;
    for (const auto& user : userList) {
      if (useRealNames) {
        userNames.push_back("[" + user->GetName() + "]");
      } else {
        userNames.push_back("[" + user->GetDisplayName() + "]");
      }
    }
    return JoinStrings(userNames, ", ", false);
  }

  [[nodiscard]] inline std::string ToNameListSentence(UserList userList, bool useRealNames = false) {
    if (userList.empty()) return std::string();
    std::vector<std::string> userNames;
    for (const auto& user : userList) {
      if (useRealNames) {
        userNames.push_back("[" + user->GetName() + "]");
      } else {
        userNames.push_back("[" + user->GetDisplayName() + "]");
      }
    }
    return JoinStrings(userNames, ", ", false);
  }

  [[nodiscard]] inline bool SortUsersByDownloadProgressAscending(const GameUser::CGameUser* a, const GameUser::CGameUser* b) {
    return a->InspectMapTransfer().GetLastSentOffsetEnd() < b->InspectMapTransfer().GetLastSentOffsetEnd();
  }

  [[nodiscard]] inline bool SortUsersByKeepAlivesAscending(const GameUser::CGameUser* a, const GameUser::CGameUser* b) {
    return a->GetNormalSyncCounter() < b->GetNormalSyncCounter();
  }

  [[nodiscard]] inline bool SortUsersByLatencyDescending(const GameUser::CGameUser* a, const GameUser::CGameUser* b) {
    return a->GetOperationalRTT() > b->GetOperationalRTT();
  }

  [[nodiscard]] inline bool SortUsersByPairedUint32Descending(const std::pair<GameUser::CGameUser*, uint32_t> a, const std::pair<GameUser::CGameUser*, uint32_t> b) {
    return a.second > b.second;
  };
};

#endif // AURA_GAMEUSER_H_
