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

#ifndef AURA_UNIFORM_SAMPLER_
#define AURA_UNIFORM_SAMPLER_

#include <deque>
#include <random>

#include "includes.h"

struct UniformSampler
{
  std::mt19937 rng;
  std::uniform_int_distribution<int> GetValue;

  UniformSampler(int sampleRate);
  ~UniformSampler();

  [[nodiscard]] bool GetBernoulli();
};

template <typename T>
struct UniformlySampledData
{
  size_t sampleRate;
  size_t capacity;
  UniformSampler sampler;
  std::deque<T> storage;

  UniformlySampledData(int nSampleRate, size_t nCapacity);
  virtual ~UniformlySampledData();

  size_t GetRate() const;
  void Add(T element);
  void TrySample(T element);
  void Reset();
  const std::deque<T>& GetData() const;
  bool GetIsEmpty() const;
};

struct UniformlySampledTimedData : UniformlySampledData<int64_t>
{
  std::optional<std::chrono::steady_clock::time_point> pendingStart;

  UniformlySampledTimedData(int nSampleRate, size_t nCapacity);
  ~UniformlySampledTimedData() final;

  UniformlySampledTimedData* TryStart();

  template <typename T>
  inline int64_t TryEnd();

  int64_t TryEndNano();
  int64_t TryEndMicro();
};

#endif // AURA_UNIFORM_SAMPLER_
