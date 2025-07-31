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

#include "sampler.h"

//
// UniformSampler
//

UniformSampler::UniformSampler(int sampleRate)
: rng(std::random_device{}()),
  GetValue(std::uniform_int_distribution<int>(1, sampleRate))
{
}

UniformSampler::~UniformSampler()
{
}

bool UniformSampler::GetBernoulli() {
  return GetValue(rng) == 1;
}

//
// UniformlySampledData
//

template <typename T>
UniformlySampledData<T>::UniformlySampledData(int nSampleRate, size_t nCapacity)
: sampleRate(static_cast<size_t>(nSampleRate)),
  capacity(nCapacity),
  sampler(UniformSampler(nSampleRate))
{
  assert((capacity > 0) && "Capacity must be a positive value.");
}

template <typename T>
UniformlySampledData<T>::~UniformlySampledData()
{
}

template <typename T>
size_t UniformlySampledData<T>::GetRate() const {
  return sampleRate;
}

template <typename T>
void UniformlySampledData<T>::Add(T element) {
  if (storage.size() >= capacity) {
    storage.pop_front();
  }
  storage.push_back(element);
}

template <typename T>
void UniformlySampledData<T>::TrySample(T element) {
  if (sampler.GetBernoulli()) {
    Add(element);
  }
}

template <typename T>
void UniformlySampledData<T>::Reset() {
  storage.clear();
}

template <typename T>
const std::deque<T>& UniformlySampledData<T>::GetData() const {
  return storage;
}

template <typename T>
bool UniformlySampledData<T>::GetIsEmpty() const {
  return storage.empty();
}

template struct UniformlySampledData<int64_t>;

//
// UniformlySampledTimedData
//

UniformlySampledTimedData::UniformlySampledTimedData(int nSampleRate, size_t nCapacity)
: UniformlySampledData(nSampleRate, nCapacity)
{
}

UniformlySampledTimedData::~UniformlySampledTimedData()
{
}

UniformlySampledTimedData* UniformlySampledTimedData::TryStart()
{
  if (sampler.GetBernoulli()) {
    pendingStart = std::chrono::steady_clock::now();
  }
  return this;
}

template <typename T>
int64_t UniformlySampledTimedData::TryEnd()
{
  if (!pendingStart.has_value()) return 0;
  int64_t delta = std::chrono::duration_cast<T>(std::chrono::steady_clock::now() - *pendingStart).count();
  Add(delta);
  pendingStart.reset();
  return delta;
}

template int64_t UniformlySampledTimedData::TryEnd<std::chrono::nanoseconds>();
template int64_t UniformlySampledTimedData::TryEnd<std::chrono::microseconds>();

int64_t UniformlySampledTimedData::TryEndNano()
{
  return TryEnd<std::chrono::nanoseconds>();
}

int64_t UniformlySampledTimedData::TryEndMicro()
{
  return TryEnd<std::chrono::microseconds>();
}
