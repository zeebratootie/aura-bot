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

#include "os_util.h"
#include "util.h"

#include <cstdio>
#include <cstdlib>
#include <utf8/utf8.h>

using namespace std;

#ifdef _WIN32
optional<string> MaybeReadRegistryRaw(const wchar_t* mainKey, const wchar_t* subKey)
{
  optional<string> result;
  HKEY hKey;
  LPCWSTR registryPath = mainKey;
  LPCWSTR keyName = subKey;
  result.emplace();
  result->resize(2048, '\xFF');

  if (RegOpenKeyExW(HKEY_CURRENT_USER, registryPath, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
    DWORD bufferSize = result->size();
    DWORD valueType;
    // Query the value of the desired registry entry
    if (RegQueryValueExW(hKey, keyName, nullptr, &valueType, reinterpret_cast<BYTE*>(result->data()), &bufferSize) == ERROR_SUCCESS) {
      if (!(0 < bufferSize && bufferSize < 2048)) {
        result.reset();
        Print("[REGISTRY] error - value too long");
      }
    }
    // Close the key
    RegCloseKey(hKey);
  }
  return result;
}

optional<wstring> MaybeReadRegistry(const wchar_t* mainKey, const wchar_t* subKey)
{
  optional<wstring> result;
  HKEY hKey;
  LPCWSTR registryPath = mainKey;
  LPCWSTR keyName = subKey;
  WCHAR buffer[2048];

  if (RegOpenKeyExW(HKEY_CURRENT_USER, registryPath, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
    DWORD bufferSize = sizeof(buffer);
    DWORD valueType;
    // Query the value of the desired registry entry
    if (RegQueryValueExW(hKey, keyName, nullptr, &valueType, reinterpret_cast<BYTE*>(buffer), &bufferSize) == ERROR_SUCCESS) {
      if (valueType == REG_SZ && 0 < bufferSize && bufferSize < 2048) {
        buffer[bufferSize / sizeof(WCHAR)] = L'\0';
        result = wstring(buffer);
      } else {
        Print("[REGISTRY] error - value too long");
      }
    }
    // Close the key
    RegCloseKey(hKey);
  }
  return result;
}

optional<filesystem::path> MaybeReadRegistryPath(const wchar_t* mainKey, const wchar_t* subKey)
{
  optional<wstring> content = MaybeReadRegistry(mainKey, subKey);
  if (!content.has_value()) return nullopt;

  optional<filesystem::path> result;
  result = filesystem::path(content.value());
  return result;
}

bool DeleteUserRegistryKey(const wchar_t* subKey)
{
  return RegDeleteTree(HKEY_CURRENT_USER, subKey) == ERROR_SUCCESS;
}

bool SetUserRegistryKey(const wchar_t* subKey, const wchar_t* valueName, const wchar_t* value)
{
  HKEY hKey;
  LONG result = RegCreateKeyEx(HKEY_CURRENT_USER, subKey, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
  size_t cbDataSize = (wcslen(value) + 1) * sizeof(wchar_t);
  if (cbDataSize > 0xFFFF || result == ERROR_SUCCESS) {
    result = RegSetValueEx(hKey, valueName, 0, REG_SZ, (BYTE*)value, (DWORD)cbDataSize);
    RegCloseKey(hKey);
    return true;
  }
  return false;
}

#endif

optional<string> GetUserMultiPlayerName()
{
#ifdef _WIN32
  optional<wstring> localName = MaybeReadRegistry(L"SOFTWARE\\Blizzard Entertainment\\Warcraft III\\String", L"userlocal");
  if (!localName.has_value()) {
    // Fallback to Battle.net name
    localName = MaybeReadRegistry(L"SOFTWARE\\Blizzard Entertainment\\Warcraft III\\String", L"userbnet");
  }
  if (!localName.has_value()) {
    return nullopt;
  }

  optional<string> compatName;
  compatName.emplace();
  compatName->reserve(localName->size());
  for (wchar_t c : localName.value()) {
    if (c == 0) break;
    // Client transmits low-bytes only.
    compatName->push_back(static_cast<char>(c & 0xFF));
  }

  if (!IsArbitraryStringUTF8Safe(compatName.value())) {
    Print("[AURA] warning - Your Warcraft III username is not encoded as valid ANSI (it's probably Unicode instead).");
    Print("[AURA] warning - To ensure compatibility, paste your username through a tool or editor that converts text to ANSI.");
    Print("[AURA] warning - This operation can be done using Notepad++, or UTFizer, among other tools.");
    return nullopt;
  }

  return compatName;
#else
  return nullopt;
#endif
}

filesystem::path GetExePath()
{
  static filesystem::path Memoized;
  if (!Memoized.empty())
    return Memoized;

  size_t length = 0;
#ifdef _WIN32
  vector<wchar_t> buffer(2048);
#else
  vector<char> buffer(2048);
#endif

  do {
    buffer.resize(buffer.size() * 2);
#ifdef _WIN32
    length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
#else
    ssize_t lengthOrError = readlink("/proc/self/exe", buffer.data(), buffer.size());
    if (lengthOrError < 0) {
      length = 0;
      break;
    }
    length = signed_cast<size_t>(lengthOrError);
#endif
  } while ((buffer.size() <= 0xFFFF) && (buffer.size() - 1 <= length));

  if (length == 0) {
    Print("[AURA] Failed to retrieve Aura's directory.");
    return Memoized;
  }
  buffer.resize(length);

  Memoized = filesystem::path(buffer.data());
  return Memoized;
}

filesystem::path GetExeDirectory()
{
  filesystem::path executablePath = GetExePath();
  filesystem::path cwd;
  try {
    cwd = filesystem::current_path();
  } catch (...) {}
  if (!cwd.empty()) {
    NormalizeDirectory(cwd);
    cwd = cwd.parent_path();
  }

  bool cwdIsAncestor = cwd.empty();
  if (!cwdIsAncestor) {
    try {
      filesystem::path exeAncestor = executablePath;
      filesystem::path exeRoot = exeAncestor.root_path();
      while (exeAncestor != exeRoot) {
        exeAncestor = exeAncestor.parent_path();
        if (exeAncestor == cwd) {
          cwdIsAncestor = true;
          break;
        }
      }
    } catch (...) {}
  }

  filesystem::path exeDirectory;
  if (cwdIsAncestor) {    
    exeDirectory = executablePath.parent_path().lexically_relative(cwd);
  } else {
    exeDirectory = executablePath.parent_path();
  }

  NormalizeDirectory(exeDirectory);
  return exeDirectory;
} 

PLATFORM_STRING_TYPE GetEnvironmentVariable(const PLATFORM_STRING_TYPE& key)
{
  if (key.empty()) {
    return PLATFORM_STRING_TYPE();
  }
#ifdef _WIN32
  wchar_t* buffer = nullptr;
  size_t valueSize = 0;
  errno_t err = _wdupenv_s(&buffer, &valueSize, key.c_str());
  if (!err && buffer != nullptr) {
    PLATFORM_STRING_TYPE value(buffer);
    free(buffer);
    return value;
  }
#else
  const char* envValue = getenv(key.c_str());
  if (envValue != nullptr) {
    return PLATFORM_STRING_TYPE(envValue);
  }
#endif
  // Even though a POSIX envvar may be set but empty,
  // that's not the case for Windows.
  // 
  // So, we follow the lowest common denominator, and
  // treat empty environment variables as equivalent to non-existent.
  return PLATFORM_STRING_TYPE();
}

PLATFORM_STRING_TYPE GetEnvironmentVariableTrimmed(const PLATFORM_STRING_TYPE& key)
{
  if (key.empty()) {
    return PLATFORM_STRING_TYPE();
  }
  PLATFORM_STRING_TYPE value = GetEnvironmentVariable(key);
  return TrimPlatformString(value);
}

PLATFORM_STRING_TYPE ReadPersistentUserPathEnvironment()
{
#ifdef _WIN32
  optional<PLATFORM_STRING_TYPE> userPath = MaybeReadRegistry(L"Environment", L"PATH");
  if (userPath.has_value()) {
    return userPath.value();
  }
#else
  // Maybe ~/.bash_profile, maybe ~/.bash_rc, maybe...
  // This is a mess.
#endif
  return PLATFORM_STRING_TYPE();
}

#ifdef _WIN32
void SetPersistentUserPathEnvironment(const PLATFORM_STRING_TYPE& nUserPath)
#else
void SetPersistentUserPathEnvironment(const PLATFORM_STRING_TYPE& /*nUserPath*/)
#endif
{
#ifdef _WIN32
  SetUserRegistryKey(L"Environment", L"PATH", nUserPath.c_str());
#else
  // FIXME?: SetPersistentUserPathEnvironment() Linux case?
  // Maybe just rely on Makefile?
#endif
}

bool GetIsDirectoryInUserPath(const filesystem::path& nDirectory, PLATFORM_STRING_TYPE& nUserPath)
{
  nUserPath = ReadPersistentUserPathEnvironment();
  size_t startPos = 0;
  size_t endPos = nUserPath.find(PATH_ENVVAR_SEPARATOR, startPos);
  while (endPos != PLATFORM_STRING_TYPE::npos) {
    PLATFORM_STRING_TYPE currentDirectory = nUserPath.substr(startPos, endPos - startPos);
    if (currentDirectory == nDirectory.native()) {
      return true;
    }
    if (!nDirectory.empty() && !currentDirectory.empty() &&
      (currentDirectory.substr(0, currentDirectory.size() - 1) == nDirectory.native().substr(0, nDirectory.native().size() - 1))
    ) {
      return true;
    }
    startPos = endPos + 1;
    endPos = nUserPath.find(PATH_ENVVAR_SEPARATOR, startPos);
  }
  return false;
}

void AddDirectoryToUserPath(const filesystem::path& nDirectory, PLATFORM_STRING_TYPE& nUserPath)
{
  if (nDirectory.empty()) return;
  nUserPath = nDirectory.native() + PLATFORM_STRING_TYPE(PATH_ENVVAR_SEPARATOR) + nUserPath;
  SetPersistentUserPathEnvironment(nUserPath);
}

void EnsureDirectoryInUserPath(const filesystem::path& nDirectory)
{
  if (nDirectory.empty()) return;
  PLATFORM_STRING_TYPE userPath;
  if (!GetIsDirectoryInUserPath(nDirectory, userPath)) {
    AddDirectoryToUserPath(nDirectory, userPath);
    Print("[AURA] Installed to user PATH environment variable.");
  }
}

void SetWindowTitle(PLATFORM_STRING_TYPE nWindowTitle)
{
#ifdef _WIN32
  SetConsoleTitleW(nWindowTitle.c_str());
#else
  static std::optional<bool> supportsOSC;
  if (!supportsOSC.has_value()) {
    supportsOSC = isatty(fileno(stdout)) && getenv("TERM") && strcmp(getenv("TERM"), "dumb") != 0;
  }
  if (!supportsOSC.value()) {
    return;
  }
  cout << "\033]0;" << nWindowTitle.c_str() << "\007";
#endif
}

PLATFORM_STRING_TYPE GetDynamicLibraryName(PLATFORM_STRING_TYPE nName)
{
#ifdef _WIN32
  return nName + PLATFORM_STRING(".dll");
#else
#ifdef __APPLE__
  return nName + PLATFORM_STRING(".dylib");
#else
  return nName + PLATFORM_STRING(".so");
#endif
#endif
}

bool CheckDynamicLibrary(PLATFORM_STRING_TYPE libName, PLATFORM_STRING_TYPE serviceName)
{
  libName = GetDynamicLibraryName(libName);
#ifdef _WIN32
  HMODULE h = LoadLibraryW(libName.c_str());
  if (!h) {
    string serviceNameUTF8, libNameUTF8;
    utf8::utf16to8(serviceName.begin(), serviceName.end(), back_inserter(serviceNameUTF8));
    utf8::utf16to8(libName.begin(), libName.end(), back_inserter(libNameUTF8));
    Print("[AURA] Service " + serviceNameUTF8 + " requires shared library [" + libNameUTF8 + "] - not found.");
    return false;
  }
  FreeLibrary(h);
#else
  void* handle = dlopen(libName.c_str(), dlopen_flags);
  if (handle == nullptr) {
    Print("[AURA] Service " + serviceName + " requires shared library [" + libName + "] - not found.");
    return false;
  }
  dlclose(handle);
#endif
  return true;
}