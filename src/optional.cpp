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

#include "optional.h"
#include "socket.h"
#include <filesystem>

#define INSTANTIATE_OPTIONAL_TEMPLATES(T)\
template class OptReader<T>;\
template class OptWriter<T>;\
template OptReader<T> ReadOpt(const optional<T>& opt);\
template OptWriter<T> WriteOpt(optional<T>& value);

using namespace std;

//
// OptReader
//

template<typename T>
OptReader<T>::OptReader(const optional<T>& nSource)
 : source(ref(nSource))
{
};

template<typename T>
OptReader<T>::~OptReader()
{
};

template<typename T>
void OptReader<T>::operator>>(T& target) const
{
  const optional<T>& optSource = source.get();
  if (!optSource.has_value()) return;
  target = *optSource;
}

template<typename T>
void OptReader<T>::operator>>(optional<T>& target) const
{
  const optional<T>& optSource = source.get();
  if (!optSource.has_value()) return;
  target = *optSource;
}

//
// OptWriter
//

template<typename T>
OptWriter<T>::OptWriter(optional<T>& nTarget)
 : target(ref(nTarget))
{
};

template<typename T>
OptWriter<T>::~OptWriter()
{
};

template<typename T>
void OptWriter<T>::operator<<(const T& source)
{
  optional<T>& optTarget = target.get();
  optTarget = source;
}

template<typename T>
void OptWriter<T>::operator<<(const optional<T>& source)
{
  if (!source.has_value()) return;
  optional<T>& optTarget = target.get();
  optTarget = *source;
}

template<typename T>
OptReader<T> ReadOpt(const optional<T>& opt) {
  return {opt};
}


template<typename T>
OptWriter<T> WriteOpt(optional<T>& value) {
  return {value};
}

INSTANTIATE_OPTIONAL_TEMPLATES(bool)
INSTANTIATE_OPTIONAL_TEMPLATES(uint8_t)
INSTANTIATE_OPTIONAL_TEMPLATES(uint16_t)
INSTANTIATE_OPTIONAL_TEMPLATES(uint32_t)
INSTANTIATE_OPTIONAL_TEMPLATES(int64_t)
INSTANTIATE_OPTIONAL_TEMPLATES(string)
INSTANTIATE_OPTIONAL_TEMPLATES(sockaddr_storage)
INSTANTIATE_OPTIONAL_TEMPLATES(Version)
INSTANTIATE_OPTIONAL_TEMPLATES(filesystem::path)
INSTANTIATE_OPTIONAL_TEMPLATES(vector<uint8_t>)

// enums
INSTANTIATE_OPTIONAL_TEMPLATES(CacheRevalidationMethod)
INSTANTIATE_OPTIONAL_TEMPLATES(CrossPlayMode)
INSTANTIATE_OPTIONAL_TEMPLATES(FakeUsersShareUnitsMode)
INSTANTIATE_OPTIONAL_TEMPLATES(GameLoadingTimeoutMode)
INSTANTIATE_OPTIONAL_TEMPLATES(GameObserversMode)
INSTANTIATE_OPTIONAL_TEMPLATES(GamePlayingTimeoutMode)
INSTANTIATE_OPTIONAL_TEMPLATES(GameResultSourceSelect)
INSTANTIATE_OPTIONAL_TEMPLATES(GameSpeed)
INSTANTIATE_OPTIONAL_TEMPLATES(GameVisibilityMode)
INSTANTIATE_OPTIONAL_TEMPLATES(HideIGNMode)
INSTANTIATE_OPTIONAL_TEMPLATES(LobbyOwnerTimeoutMode)
INSTANTIATE_OPTIONAL_TEMPLATES(LobbyTimeoutMode)
INSTANTIATE_OPTIONAL_TEMPLATES(LogLevel)
INSTANTIATE_OPTIONAL_TEMPLATES(MirrorSourceType)
INSTANTIATE_OPTIONAL_TEMPLATES(MirrorTimeoutMode)
INSTANTIATE_OPTIONAL_TEMPLATES(OnIPFloodHandler)
INSTANTIATE_OPTIONAL_TEMPLATES(OnPlayerLeaveHandler)
INSTANTIATE_OPTIONAL_TEMPLATES(OnRealmBroadcastErrorHandler)
INSTANTIATE_OPTIONAL_TEMPLATES(OnShareUnitsHandler)
INSTANTIATE_OPTIONAL_TEMPLATES(OnUnsafeNameHandler)
INSTANTIATE_OPTIONAL_TEMPLATES(PlayersReadyMode)
INSTANTIATE_OPTIONAL_TEMPLATES(UDPDiscoveryMode)
INSTANTIATE_OPTIONAL_TEMPLATES(W3ModLocale)

#undef INSTANTIATE_OPTIONAL_TEMPLATES
