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

#include "locations.h"
#include "game.h"
#include "game_user.h"
#include "async_observer.h"

using namespace std;

//
// ServiceUser
//

ServiceUser::ServiceUser()
 : serviceType(ServiceType::kNone),
   subLocation(nullptr)
{
};

ServiceUser::ServiceUser(const ServiceUser& otherService)
 : serviceType(otherService.serviceType),
   servicePtr(otherService.servicePtr.lock()),
   userName(otherService.userName),
   subLocation(nullptr)
{
};

ServiceUser::ServiceUser(ServiceType nServiceType, string_view nUserName)
 : serviceType(nServiceType),
   userName(string(nUserName)),
   subLocation(nullptr)
{
};

ServiceUser::ServiceUser(ServiceType nServiceType, int64_t nUserIdentifier, string_view nUserName)
 : serviceType(nServiceType),
   userIdentifier(nUserIdentifier),
   userName(string(nUserName)),
   subLocation(nullptr)
{
};

ServiceUser::ServiceUser(ServiceType nServiceType, string_view nUserName, shared_ptr<void> nServicePtr)
 : serviceType(nServiceType),
   servicePtr(nServicePtr),
   userName(string(nUserName)),
   subLocation(nullptr)
{
};

void ServiceUser::Reset()
{
  serviceType = ServiceType::kNone;
  servicePtr.reset();
  userIdentifier.reset();
  userName.clear();
}

ServiceUser::~ServiceUser()
{
  delete subLocation;
  subLocation = nullptr;
}

bool ServiceUser::operator==(const ServiceUser& other) const
{
  return (
    (serviceType == other.serviceType) &&
    (servicePtr.lock() == other.servicePtr.lock()) &&
    (userIdentifier.has_value() == other.userIdentifier.has_value()) &&
    (!userIdentifier.has_value() || (*userIdentifier == *other.userIdentifier)) &&
    (userName == other.userName) &&
    (subLocation == other.subLocation)
  );
}

ServiceUser& ServiceUser::operator=(const ServiceUser& other)
{
  if (this != &other) {
    serviceType = other.serviceType;
    servicePtr = other.servicePtr.lock();
    if (other.userIdentifier.has_value()) {
      userIdentifier = other.userIdentifier.value();
    } else {
      userIdentifier.reset();
    }
    userName = other.userName;
    subLocation = other.subLocation;
  }
  return *this;
}

template <typename T>
shared_ptr<T> ServiceUser::GetService() const
{
  return static_pointer_cast<T>(servicePtr.lock());
}

template shared_ptr<CGame> ServiceUser::GetService() const;
template shared_ptr<CRealm> ServiceUser::GetService() const;
template shared_ptr<const CGame> ServiceUser::GetService() const;
template shared_ptr<const CRealm> ServiceUser::GetService() const;

//
// GameSource
//


GameSource::GameSource()
 : userType(GameCommandSource::kNone),
   user(nullptr)
{
}

GameSource::GameSource(const GameSource& otherSource)
 : userType(otherSource.userType),
   game(otherSource.GetGame()),
   user(otherSource.user)
{
}

GameSource::GameSource(GameUser::CGameUser* nUser)
 : userType(GameCommandSource::kUser),
   game(nUser->GetGame()),
   user(nUser)
{
}

GameSource::GameSource(CAsyncObserver* nSpectator)
 : userType(nSpectator->GetGame() ? GameCommandSource::kSpectator : GameCommandSource::kReplay),
   game(nSpectator->GetGame()),
   spectator(nSpectator)
{
}

GameSource::~GameSource()
{
}

CConnection* GameSource::GetUserOrSpectator() const
{
  return static_cast<CConnection*>(user);
}

void GameSource::Expire()
{
  switch (userType) {
    case GameCommandSource::kUser:
      Reset();
      break;
    case GameCommandSource::kSpectator:
      userType = GameCommandSource::kReplay;
      game.reset();
      break;
    case GameCommandSource::kReplay:
    case GameCommandSource::kNone:
      break;
    IGNORE_ENUM_LAST(GameCommandSource)
  }
}

void GameSource::Reset()
{
  userType = GameCommandSource::kNone;
  game.reset();
  user = nullptr;
}

bool GameSource::operator==(const GameSource& other) const
{
  return (
    (userType == other.userType) &&
    (game.lock() == other.game.lock()) &&
    reinterpret_cast<const void*>(user) == reinterpret_cast<const void*>(other.user)
  );
}

GameSource& GameSource::operator=(const GameSource& other)
{
  if (this != &other) {
    userType = other.userType;
    game = other.game.lock();
    user = reinterpret_cast<GameUser::CGameUser*>(other.user);
  }
  return *this;
}
