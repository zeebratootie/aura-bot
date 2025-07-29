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

#ifndef AURA_ACTION_H_
#define AURA_ACTION_H_

#include "includes.h"

enum class CommandAuth : uint8_t
{
  kAuto = 0u,
  kSpoofed = 1u,
  kVerified = 2u,
  kAdmin = 3u,
  kRootAdmin = 4u,
  kSudo = 5u,
};

struct AppAction
{
  AppActionType type;
  AppActionMode mode;
  uint32_t value_1;
  uint32_t value_2;
  int64_t queuedTicks;

  AppAction(AppActionType nType, AppActionMode nMode = AppActionMode::kNone, uint32_t nValue1 = 0, uint32_t nValue2 = 0)
   : type(nType),
     mode(nMode),
     value_1(nValue1),
     value_2(nValue2),
     queuedTicks(GetTicks())
  {
  };

  ~AppAction() = default;
};

struct LazyCommandContext
{
  bool broadcast;
  bool online;
  int64_t queuedTicks;
  std::string command;
  std::string target;
  std::string identityName;
  std::string identityLoc;
  CommandAuth auth;

  LazyCommandContext(bool nBroadcast, bool nOnline, std::string_view nCommand, std::string_view nTarget, const std::string& nIdentityName, const std::string& nIdentityLoc, const CommandAuth nAuth)
   : broadcast(nBroadcast),
     online(nOnline),
     queuedTicks(GetTicks()),
     command(nCommand),
     target(nTarget),
     identityName(nIdentityName),
     identityLoc(nIdentityLoc),
     auth(nAuth)
  {
  };

  ~LazyCommandContext() = default;
};

#endif // AURA_ACTION_H_
