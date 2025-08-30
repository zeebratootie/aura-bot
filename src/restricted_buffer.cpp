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

#include "restricted_buffer.h"
#include "protocol/game_protocol.h"

using namespace std;

template struct RestrictedBuffer<GameProtocol::PacketWrapper>;

template <typename T>
vector<T> RestrictedBuffer<T>::GetNewEntries() const
{
  vector<T> newEntries;
  newEntries.reserve(m_NewEntries.size());
  newEntries.insert(newEntries.end(), m_NewEntries.begin() + m_NewStartIndex, m_NewEntries.end());
  newEntries.insert(newEntries.end(), m_NewEntries.begin(), m_NewEntries.begin() + m_NewStartIndex);
  return newEntries;
}

template <typename T>
void RestrictedBuffer<T>::Clear()
{
  m_OldEntries = vector<T>();
  m_NewEntries = vector<T>();
  m_DeletedSize = 0;
  m_NewStartIndex = 0;
}

template <typename T>
void RestrictedBuffer<T>::Push(const T& entry)
{
  if (m_OldEntries.size() < m_MaxOldSize) {
    m_OldEntries.push_back(entry);
  } else if (m_NewEntries.size() < m_MaxNewSize) {
    m_NewEntries.push_back(entry);
  } else {
    m_NewEntries[m_NewStartIndex] = entry;
    m_DeletedSize++;
    m_NewStartIndex = (m_NewStartIndex + 1) % m_MaxNewSize;
  }
}

template <typename T>
void RestrictedBuffer<T>::Push(T&& entry)
{
  if (m_OldEntries.size() < m_MaxOldSize) {
    m_OldEntries.push_back(entry);
  } else if (m_NewEntries.size() < m_MaxNewSize) {
    m_NewEntries.push_back(entry);
  } else {
    m_NewEntries[m_NewStartIndex] = move(entry);
    m_DeletedSize++;
    m_NewStartIndex = (m_NewStartIndex + 1) % m_MaxNewSize;
  }
}
