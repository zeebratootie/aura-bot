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

#ifndef AURA_LOCATIONS_H_
#define AURA_LOCATIONS_H_

#include "includes.h"

//
// GameControllerSearchResult
//

struct GameControllerSearchResult
{
  bool success;
  uint8_t SID;
  GameUser::CGameUser* user;

  GameControllerSearchResult()
   : success(false),
     SID(0),
     user(nullptr)
  {}

  GameControllerSearchResult(uint8_t nSID, GameUser::CGameUser* nUser)
   : success(true),
     SID(nSID),
     user(nUser)
  {}

  ~GameControllerSearchResult() = default;

  [[nodiscard]] inline bool GetSuccess() const { return success; }
};

//
// RealmUserSearchResult
//

struct RealmUserSearchResult
{
  bool success;
  bool fullyQualified;
  std::string userName;
  std::string hostName;
  std::weak_ptr<CRealm> realm;

  RealmUserSearchResult()
   : success(false),
     fullyQualified(false)
  {}

  RealmUserSearchResult(const bool nFullyQualified, const std::string& nUserName, const std::string& nHostName)
   : success(false),
     fullyQualified(nFullyQualified),
     userName(nUserName),
     hostName(nHostName)
  {}

  RealmUserSearchResult(const bool nFullyQualified, const std::string& nUserName, const std::string& nHostName, std::shared_ptr<CRealm> nRealm)
   : success(true),
     fullyQualified(nFullyQualified),
     userName(nUserName),
     hostName(nHostName),
     realm(nRealm)
  {}

  ~RealmUserSearchResult() = default;

  [[nodiscard]] inline bool GetSuccess() const { return success; }
  [[nodiscard]] inline bool GetIsFullyQualified() const { return fullyQualified; }
  [[nodiscard]] inline const std::string& GetUser() const { return userName; }
  [[nodiscard]] inline const std::string& GetHostName() const { return hostName; }
  [[nodiscard]] inline std::shared_ptr<CRealm> GetRealm() const { return realm.lock(); }
};

//
// SimpleNestedLocation
//

struct SimpleNestedLocation
{
  uint8_t order;
  std::string name;
  std::optional<uint64_t> id;
  SimpleNestedLocation* subLocation;

  SimpleNestedLocation(uint8_t nOrder, std::string_view locationName)
   : order(nOrder),
     name(std::string(locationName)),
     subLocation(nullptr)
  {
  }

  SimpleNestedLocation(uint8_t nOrder, std::string_view locationName, const uint64_t locationIdentifier)
   : order(nOrder),
     name(std::string(locationName)),
     id(locationIdentifier),
     subLocation(nullptr)
  {
  }

  ~SimpleNestedLocation() {
    delete subLocation;
    subLocation = nullptr;
  }

  inline uint8_t GetOrder() const { return order; }
  inline std::string_view GetName() const { return name; }
  inline SimpleNestedLocation* GetSubLocation() const { return subLocation; }
  inline const SimpleNestedLocation* InspectSubLocation() const { return subLocation; }

  SimpleNestedLocation* AddNested(std::string_view locationName)
  {
    subLocation = new SimpleNestedLocation(order + 1, locationName);
    return subLocation;
  }

  SimpleNestedLocation* AddNested(std::string_view locationName, const uint64_t locationIdentifier)
  {
    subLocation = new SimpleNestedLocation(order + 1, locationName, locationIdentifier);
    return subLocation;
  }
};

//
// ServiceUser
//

struct ServiceUser
{
  ServiceType serviceType;
  std::weak_ptr<void> servicePtr;
  std::optional<int64_t> userIdentifier;
  std::string userName;
  SimpleNestedLocation* subLocation;

  ServiceUser();
  ServiceUser(const ServiceUser& otherService);
  ServiceUser(ServiceType serviceType, std::string_view nUserName);
  ServiceUser(ServiceType serviceType, int64_t nUserIdentifier, std::string_view nUserName);
  ServiceUser(ServiceType serviceType, std::string_view nUserName, std::shared_ptr<void> nServicePtr);
  ~ServiceUser();

  template <typename T>
  [[nodiscard]] std::shared_ptr<T> GetService() const;

  inline bool GetIsEmpty() const { return serviceType == ServiceType::kNone; }
  inline bool GetIsAnonymous() const { return userName.empty(); }
  inline bool GetIsExpired() const { return servicePtr.expired(); }
  inline ServiceType GetServiceType() const { return serviceType; }
  inline int64_t GetUserIdentifier() const { return userIdentifier.value(); }
  inline std::string_view GetUser() const { return userName; }
  inline std::string GetUserOrAnon() const { return userName.empty() ? "[Anonymous]" : userName; }
  void Reset();

  inline void SetServiceType(ServiceType nServiceType) { serviceType = nServiceType; }
  inline void SetName(std::string_view nUserName) { userName = std::string(nUserName); }

  inline uint8_t GetOrder() const { return 0; }
  inline SimpleNestedLocation* GetSubLocation() const { return subLocation; }
  inline const SimpleNestedLocation* InspectSubLocation() const { return subLocation; }

  SimpleNestedLocation* AddNested(std::string_view locationName)
  {
    subLocation = new SimpleNestedLocation(1, locationName);
    return subLocation;
  }

  SimpleNestedLocation* AddNested(std::string_view locationName, const uint64_t locationIdentifier)
  {
    subLocation = new SimpleNestedLocation(1, locationName, locationIdentifier);
    return subLocation;
  }

  ServiceUser& operator=(const ServiceUser& other);
  bool operator==(const ServiceUser& other) const;
};

//
// GameSource
//

struct GameSource
{
  GameCommandSource userType;
  std::weak_ptr<CGame> game;
  union {
    GameUser::CGameUser* user;
    CAsyncObserver* spectator;
  };

  GameSource();
  GameSource(const GameSource& otherSource);
  GameSource(GameUser::CGameUser* user);
  GameSource(CAsyncObserver* spectator);
  ~GameSource();

  inline GameCommandSource GetType() const { return userType; }
  inline bool GetIsEmpty() const { return userType == GameCommandSource::kNone; }
  inline bool GetIsUser() const { return userType == GameCommandSource::kUser; }
  inline bool GetIsSpectator() const { return userType == GameCommandSource::kSpectator || userType == GameCommandSource::kReplay; }
  inline bool GetIsReplay() const { return userType == GameCommandSource::kReplay; }
  inline bool GetIsGameExpired() const { return game.expired(); }

  /* In reality, these 3 functions are the same */
  inline GameUser::CGameUser* GetUser() const { return user; }
  inline CAsyncObserver* GetSpectator() const { return spectator; }
  CConnection* GetUserOrSpectator() const;

  inline std::shared_ptr<CGame> GetGame() const { return game.lock(); }

  void Expire();
  void Reset();

  GameSource& operator=(const GameSource& other);
  bool operator==(const GameSource& other) const;
};

#endif
