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

#ifndef AURA_FILEUTIL_H_
#define AURA_FILEUTIL_H_

#include "includes.h"
#include <fstream>
#include <filesystem>
#include <system_error>
#include <cstdio>

#define __STORMLIB_SELF__
#include <StormLib.h>

#ifdef _WIN32
#pragma once
#include <windows.h>
#define stat _stat
#else
#include <sys/stat.h>
#include <dirent.h>
#include <cstring>
#include <unistd.h>
#include <limits.h>
#endif

struct FileChunkCached
{
  size_t fileSize;
  size_t start;
  size_t end;
  WeakByteArray bytes;

  FileChunkCached();
  FileChunkCached(size_t nFileSize, size_t nStart, size_t nEnd, SharedByteArray nBytes);
  ~FileChunkCached() = default;

  uint8_t* GetDataAtCursor(size_t cursor) const
  {
    if (bytes.expired()) return nullptr;
    return bytes.lock()->data() + cursor - start;
  };

  size_t GetSizeFromCursor(size_t cursor) const
  {
    if (bytes.expired()) return 0;
    return start + bytes.lock()->size() - cursor;
  };
};

struct FileChunkTransient
{
  size_t start;
  SharedByteArray bytes;

  FileChunkTransient();
  FileChunkTransient(size_t nStart, SharedByteArray nBytes);
  FileChunkTransient(const FileChunkCached& cached);
  ~FileChunkTransient() = default;

  uint8_t* GetDataAtCursor(size_t cursor) const
  {
    return bytes->data() + cursor - start;
  };

  size_t GetSizeFromCursor(size_t cursor) const
  {
    return start + bytes->size() - cursor;
  };
};

[[nodiscard]] bool FileExists(const std::filesystem::path& file);
[[nodiscard]] PLATFORM_STRING_TYPE GetFileName(const PLATFORM_STRING_TYPE& inputPath);
[[nodiscard]] PLATFORM_STRING_TYPE GetFileExtension(const PLATFORM_STRING_TYPE& inputPath);
[[nodiscard]] std::string PathToString(const std::filesystem::path& file);
[[nodiscard]] std::string PathToAbsoluteString(const std::filesystem::path& file);
[[nodiscard]] std::string_view SanitizeUTF8Path(const std::filesystem::path& filePath, std::string_view = {});
[[nodiscard]] std::string SanitizeWrapUTF8Path(const std::filesystem::path& filePath, std::string_view = "REDACTED");
[[nodiscard]] std::vector<std::filesystem::path> FilesMatch(const std::filesystem::path& path, const std::vector<PLATFORM_STRING_TYPE>& extensionList);

[[nodiscard]] std::uintmax_t FileSize(const std::filesystem::path& filePath) noexcept;

template <typename Container>
[[nodiscard]] bool FileRead(const std::filesystem::path& filePath, Container& container, const size_t maxSize) noexcept;

template <typename Container>
[[nodiscard]] bool FileReadPartial(const std::filesystem::path& filePath, Container& container, const size_t start, size_t maxReadSize, size_t* fileSize, size_t* actualReadSize) noexcept;

bool FileWrite(const std::filesystem::path& file, const uint8_t* data, size_t length);
bool FileAppend(const std::filesystem::path& file, const uint8_t* data, size_t length);
bool FileDelete(const std::filesystem::path& file);
[[nodiscard]] std::optional<int64_t> GetMaybeModifiedTime(const std::filesystem::path& file);
[[nodiscard]] std::filesystem::path CaseInsensitiveFileExists(const std::filesystem::path& path, const std::string& file);
[[nodiscard]] std::vector<std::pair<std::string, int>> FuzzySearchFiles(const std::filesystem::path& directory, const std::vector<PLATFORM_STRING_TYPE>& baseExtensions, const std::string& rawPattern);

[[nodiscard]] bool OpenMPQArchive(void** MPQ, const std::filesystem::path& filePath);
void CloseMPQArchive(void* MPQ);
std::optional<uint32_t> GetMPQFileSize(void* MPQ, const char* packedFileName, const uint16_t locale);

template <typename Container>
bool ReadMPQFile(void* MPQ, const char* packedFileName, Container& container, const uint16_t locale);

bool ExtractMPQFile(void* MPQ, const char* packedFileName, const std::filesystem::path& outPath, const uint16_t locale = 0);

#endif // AURA_FILEUTIL_H_
