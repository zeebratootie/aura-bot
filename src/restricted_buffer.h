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

#ifndef AURA_RESTRICTED_BUFFER_H
#define AURA_RESTRICTED_BUFFER_H

#include "includes.h"

template <typename T>
struct RestrictedBuffer
{
private:
  std::vector<T> m_OldEntries;
  std::vector<T> m_NewEntries;
  size_t m_MaxOldSize;
  size_t m_MaxNewSize;

public:
  size_t m_DeletedSize;

private:
  size_t m_NewStartIndex;

public:
  RestrictedBuffer(size_t maxOldSize, size_t maxNewSize)
  : m_MaxOldSize(maxOldSize),
    m_MaxNewSize(maxNewSize),
    m_DeletedSize(0),
    m_NewStartIndex(0)
  {
  }

  ~RestrictedBuffer() = default;

  RestrictedBuffer(const RestrictedBuffer& other)
  : m_OldEntries(other.m_OldEntries),
    m_NewEntries(other.m_NewEntries),
    m_MaxOldSize(other.m_MaxOldSize),
    m_MaxNewSize(other.m_MaxNewSize),
    m_DeletedSize(other.m_DeletedSize),
    m_NewStartIndex(other.m_NewStartIndex)
  {
    m_OldEntries = other.m_OldEntries;
    m_NewEntries = other.m_NewEntries;
  }

  RestrictedBuffer& operator=(const RestrictedBuffer& other)
  {
    if (this != &other) {
      m_OldEntries = other.m_OldEntries;
      m_NewEntries = other.m_NewEntries;
      m_MaxOldSize = other.m_MaxOldSize;
      m_MaxNewSize = other.m_MaxNewSize;
      m_DeletedSize = other.m_DeletedSize;
      m_NewStartIndex = other.m_NewStartIndex;
    }
    return *this;
  }

  RestrictedBuffer(RestrictedBuffer&& other) noexcept
  : m_OldEntries(move(other.m_OldEntries)),
    m_NewEntries(move(other.m_NewEntries)),
    m_MaxOldSize(other.m_MaxOldSize),
    m_MaxNewSize(other.m_MaxNewSize),
    m_DeletedSize(other.m_DeletedSize),
    m_NewStartIndex(other.m_NewStartIndex)
  {
  }

  RestrictedBuffer& operator=(RestrictedBuffer&& other) noexcept {
    if (this != &other) {
      m_OldEntries = move(other.m_OldEntries);
      m_NewEntries = move(other.m_NewEntries);
      m_MaxOldSize = other.m_MaxOldSize;
      m_MaxNewSize = other.m_MaxNewSize;
      m_DeletedSize = other.m_DeletedSize;
      m_NewStartIndex = other.m_NewStartIndex;
    }
    return *this;
  }

  inline bool GetIsEmpty() const { return m_OldEntries.empty() && m_NewEntries.empty(); }
  inline size_t GetDeletedSize() const { return m_DeletedSize; }
  inline const std::vector<T>& GetOldEntries() const { return m_OldEntries; }
  inline const std::vector<T>& GetFastNewEntries() const { return m_NewEntries; }
  std::vector<T> GetNewEntries() const;
  void Clear();
  void Push(const T& entry);
  void Push(T&& entry);
};

#endif // AURA_RESTRICTED_BUFFER_H
