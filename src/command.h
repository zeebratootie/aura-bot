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

#ifndef AURA_COMMAND_H_
#define AURA_COMMAND_H_

#include "includes.h"
#include "locations.h"

#include <ostream>

//
// CommandTokens
//

struct CommandTokensView
{
  CommandTokensMatchType matchType;
  bool padding;
  std::string_view token;
  std::string_view cmd;
  std::string_view target;

  CommandTokensView()
  : matchType(CommandTokensMatchType::kNone),
    padding(false)
  {
  }

  CommandTokensView(CommandTokensMatchType nType, std::string_view nToken, std::string_view nPadding, std::string_view nCmd, std::string_view nTarget)
  : matchType(nType),
    padding(false),
    token(nToken),
    cmd(nCmd),
    target(nTarget)
  {
  }

  ~CommandTokensView()
  {
  }

  void Reset()
  {
    matchType = CommandTokensMatchType::kNone;
    token = {};
    padding = false;
    cmd = {};
    target = {};
  }

  void ResetInner()
  {
    cmd = {};
    target = {};
  }
};

//
// CCommandContext
//

class CCommandContext : public std::enable_shared_from_this<CCommandContext>
{
public:
  CAura*                        m_Aura;
  CCommandConfig*               m_Config;
  std::weak_ptr<CRealm>         m_TargetRealm;
  std::weak_ptr<CGame>          m_TargetGame;

protected:
  GameSource                    m_GameSource;
  ServiceUser                   m_ServiceSource;
  void*                         m_InteractionSource;
  bool                          m_FromWhisper;
  bool                          m_IsBroadcast;
  char                          m_Token;           // command token (e.g. !)
  uint16_t                      m_Permissions;     // bitmask

  std::string                   m_ServerName;
  std::string                   m_ReverseHostName; // user hostname, reversed from their IP (received from IRC chat)
  std::string                   m_ActionMessage;

  std::ostream*                 m_Output;

  std::optional<bool>           m_OverrideVerified;
  std::optional<uint16_t>       m_OverridePermissions;

  bool                          m_PartiallyDestroyed;

public:
  // Game
  CCommandContext(ServiceType serviceType, CAura* nAura, CCommandConfig* config, std::shared_ptr<CGame> game, GameUser::CGameUser* user, const bool& nIsBroadcast, std::ostream* outputStream);
  CCommandContext(ServiceType serviceType, CAura* nAura, CCommandConfig* config, std::shared_ptr<CGame> game, CAsyncObserver* spectator, const bool& nIsBroadcast, std::ostream* outputStream);

  // Realm, Realm->Game
  CCommandContext(ServiceType serviceType, CAura* nAura, CCommandConfig* config, std::shared_ptr<CGame> targetGame, std::shared_ptr<CRealm> fromRealm, std::string_view fromName, const bool& isWhisper, const bool& nIsBroadcast, std::ostream* outputStream);
  CCommandContext(ServiceType serviceType, CAura* nAura, CCommandConfig* config, std::shared_ptr<CRealm> fromRealm, std::string_view fromName, const bool& isWhisper, const bool& nIsBroadcast, std::ostream* outputStream);

  // IRC, IRC->Game
  CCommandContext(ServiceType serviceType, CAura* nAura, CCommandConfig* config, std::string_view channelName, std::string_view userName, const bool& isWhisper, std::string_view reverseHostName, const bool& nIsBroadcast, std::ostream* outputStream);
  CCommandContext(ServiceType serviceType, CAura* nAura, CCommandConfig* config, std::shared_ptr<CGame> targetGame, std::string_view channelName, std::string_view userName, const bool& isWhisper, std::string_view reverseHostName, const bool& nIsBroadcast, std::ostream* outputStream);

#ifndef DISABLE_DPP
  // Discord, Discord->Game
  CCommandContext(ServiceType serviceType, CAura* nAura, CCommandConfig* config, dpp::slashcommand_t* discordAPI, std::ostream* outputStream);
  CCommandContext(ServiceType serviceType, CAura* nAura, CCommandConfig* config, std::shared_ptr<CGame> targetGame, dpp::slashcommand_t* discordAPI, std::ostream* outputStream);
#endif

  // Arbitrary, Arbitrary->Game
  CCommandContext(ServiceType serviceType, CAura* nAura, std::string_view nFromName, const bool& nIsBroadcast, std::ostream* outputStream);
  CCommandContext(ServiceType serviceType, CAura* nAura, CCommandConfig* config, std::shared_ptr<CGame> targetGame, std::string_view nFromName, const bool& nIsBroadcast, std::ostream* outputStream);

  [[nodiscard]] inline bool GetWritesToStdout() const { return m_ServiceSource.GetServiceType() == ServiceType::kCLI; }

  [[nodiscard]] std::string GetUserAttribution();
  [[nodiscard]] std::string GetUserAttributionPreffix();
  [[nodiscard]] std::ostream* GetOutputStream() { return m_Output; }  

  [[nodiscard]] inline bool GetIsWhisper() const { return m_FromWhisper; }

  [[nodiscard]] inline GameSource& GetGameSource() { return m_GameSource; }
  [[nodiscard]] inline const GameSource& InspectGameSource() const { return m_GameSource; }
  [[nodiscard]] inline GameCommandSource GetGameSourceUserType() const { return m_GameSource.GetType(); }
  inline void ResetGameSource() { m_GameSource.Reset(); }
  inline void ExpireGameSource() { m_GameSource.Expire(); }

  [[nodiscard]] std::shared_ptr<CGame> GetSourceGame() const;
  [[nodiscard]] inline bool GetIsGameUser() const { return m_GameSource.GetIsUser(); }
  [[nodiscard]] GameUser::CGameUser* GetGameUser() const;
  [[nodiscard]] CConnection* GetGameUserOrSpectator() const;

  [[nodiscard]] inline bool GetHasCommandHistory() const { return !m_GameSource.GetIsEmpty(); }
  [[nodiscard]] CommandHistory* GetCommandHistory() const;

  [[nodiscard]] inline ServiceUser& GetServiceSource() { return m_ServiceSource; }
  [[nodiscard]] inline const ServiceUser& InspectServiceSource() const { return m_ServiceSource; }
  [[nodiscard]] inline ServiceType GetServiceSourceType() const { return m_ServiceSource.GetServiceType(); }
  inline void ResetServiceSource() { m_ServiceSource.Reset(); }
  [[nodiscard]] inline bool GetIsAnonymous() const { return m_ServiceSource.GetIsAnonymous(); }

  [[nodiscard]] inline std::string GetSender() const { return std::string(m_ServiceSource.GetUser()); }
  [[nodiscard]] std::string_view GetChannelName() const;
  [[nodiscard]] std::shared_ptr<CRealm> GetSourceRealm() const;

  [[nodiscard]] inline std::shared_ptr<CRealm> GetTargetRealm() const { return m_TargetRealm.lock(); }
  [[nodiscard]] inline std::shared_ptr<CGame> GetTargetGame() const { return m_TargetGame.lock(); }
  inline void ResetTargetRealm() { m_TargetRealm.reset(); }
  inline void ResetTargetGame() { m_TargetGame.reset(); }

  inline void* GetInteraction() const { return m_InteractionSource; }
#ifndef DISABLE_DPP
  inline dpp::slashcommand_t* GetDiscordInteraction() const { return static_cast<dpp::slashcommand_t*>(m_InteractionSource); }
#endif

  void SetIdentity(const std::string& userName);
  void SetAuthenticated(const bool& nAuthenticated);
  void SetPermissions(const uint16_t nPermissions);
  void UpdatePermissions();
  void ClearActionMessage() { m_ActionMessage.clear(); }

  void CheckServiceType(ServiceType serviceType);
  [[nodiscard]] std::optional<bool> CheckPermissions(const uint8_t nPermissionsRequired) const;
  [[nodiscard]] bool CheckPermissions(const uint8_t nPermissionsRequired, const uint8_t nAutoPermissions) const;
  [[nodiscard]] std::optional<std::pair<std::string, std::string>> CheckSudo(const std::string& message);
  [[nodiscard]] bool GetIsSudo() const;
  [[nodiscard]] bool CheckActionMessage(std::string_view nMessage) { return m_ActionMessage == nMessage; }
  [[nodiscard]] bool CheckConfirmation(const std::string& cmdToken, const std::string& cmd, const std::string& target, const std::string& errorMessage);

  [[nodiscard]] std::vector<std::string> JoinReplyListCompact(const std::vector<std::string>& stringList) const;

  void SendPrivateReply(const std::string& message, const uint8_t ctxFlags = 0);
  void SendReplyCustomFlags(const std::string& message, const uint8_t ctxFlags);
  void SendReply(const std::string& message, const uint8_t ctxFlags = 0);
  void InfoReply(const std::string& message, const uint8_t ctxFlags = 0);
  void DoneReply(const std::string& message, const uint8_t ctxFlags = 0);
  void ErrorReply(const std::string& message, const uint8_t ctxFlags = 0);
  void SendAll(const std::string& message);
  void InfoAll(const std::string& message);
  void DoneAll(const std::string& message);
  void ErrorAll(const std::string& message);
  void SendAllUnlessHidden(const std::string& message);
  [[nodiscard]] GameUser::CGameUser* GetTargetUser(const std::string& target);
  [[nodiscard]] GameUser::CGameUser* RunTargetUser(const std::string& target);
  [[nodiscard]] GameUser::CGameUser* GetTargetUserOrSelf(const std::string& target);
  [[nodiscard]] GameUser::CGameUser* RunTargetUserOrSelf(const std::string& target);
  [[nodiscard]] GameControllerSearchResult GetParseController(const std::string& target);
  [[nodiscard]] GameControllerSearchResult RunParseController(const std::string& target);
  [[nodiscard]] std::optional<uint8_t> GetParseNonPlayerSlot(const std::string& target);
  [[nodiscard]] std::optional<uint8_t> RunParseNonPlayerSlot(const std::string& target);
  [[nodiscard]] std::shared_ptr<CRealm> GetTargetRealmOrCurrent(const std::string& target);
  [[nodiscard]] RealmUserSearchResult GetParseTargetRealmUser(const std::string& target, bool allowNoRealm = false, bool searchHistory = false);
  [[nodiscard]] ServiceType GetParseTargetServiceUser(const std::string& target, std::string& nameFragment, std::string& locationFragment, void*& location);
  [[nodiscard]] std::shared_ptr<CGame> GetTargetGame(const std::string& target);
  void UseImplicitReplaceable();
  void UseImplicitHostedGame();
  void Run(const std::string& token, const std::string& command, const std::string& target);
  void SetPartiallyDestroyed() { m_PartiallyDestroyed = true; }
  bool GetPartiallyDestroyed() const { return m_PartiallyDestroyed; }

  [[nodiscard]] static AppActionStatus TryDeferred(CAura* nAura, const LazyCommandContext& lazyCtx);
  
  ~CCommandContext();
};

[[nodiscard]] std::string_view GetTokenName(std::string_view token);
[[nodiscard]] std::string_view HelpMissingComma(std::string_view target);
[[nodiscard]] bool ExtractMessageTokens(std::string_view message, std::string_view token, CommandTokensView& output);
void ExtractMessageTokensAny(std::string_view message, std::string_view privateToken, std::string_view broadcastToken, CommandTokensView& output);

#endif
