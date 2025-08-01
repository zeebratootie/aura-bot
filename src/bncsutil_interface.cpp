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

#include "bncsutil_interface.h"

#include <bncsutil/bncsutil.h>

#include <cmath>

#include "util.h"

using namespace std;

//
// CBNCSUtilInterface
//

CBNCSUtilInterface::CBNCSUtilInterface(const string& userName, const string& userPassword)
 : m_ClientKey({}),
   m_M1({}),
   m_PvPGNPasswordHash({}),
   m_EXEVersion({}),
   m_EXEVersionHash({})
{
  m_NLS = new NLS(userName, userPassword);

  {
    // v1.22
    Version version = GAMEVER(1u, 22u);
    array<uint8_t, 4> patch = {184, 0, 22, 1};
    array<uint8_t, 4> hash = {219, 152, 153, 144};
    // Every war3 executable from v1.22 to v1.26 is 471 040 bytes
    m_DefaultVersionsData.emplace(version, VersionData("war3.exe 30/06/08 20:03:55 471040", patch, hash));
  }

  {
    // v1.23
    Version version = GAMEVER(1u, 23u);
    array<uint8_t, 4> patch = {208, 0, 23, 1};
    array<uint8_t, 4> hash = {5, 177, 217, 250};
    m_DefaultVersionsData.emplace(version, VersionData("war3.exe 21/03/09 20:03:55 471040", patch, hash));
  }

  {
    // v1.24
    Version version = GAMEVER(1u, 24u);
    array<uint8_t, 4> patch = {253, 4, 25, 1};
    array<uint8_t, 4> hash = {197, 67, 68, 222};
    m_DefaultVersionsData.emplace(version, VersionData("war3.exe 05/08/09 20:03:55 471040", patch, hash));
  }

  {
    // v1.25
    Version version = GAMEVER(1u, 25u);
    array<uint8_t, 4> patch = {253, 1, 25, 1};
    array<uint8_t, 4> hash = {76, 102, 168, 65};
    m_DefaultVersionsData.emplace(version, VersionData("war3.exe 09/03/11 20:03:55 471040", patch, hash));
  }

  {
    // v1.26
    Version version = GAMEVER(1u, 26u);
    array<uint8_t, 4> patch = {1, 0, 26, 1};
    array<uint8_t, 4> hash = {39, 240, 218, 47};
    m_DefaultVersionsData.emplace(version, VersionData("war3.exe 03/18/11 20:03:55 471040", patch, hash));
  }

  {
    // v1.27
    Version version = GAMEVER(1u, 27u);
    array<uint8_t, 4> patch = {173, 1, 27, 1};
    array<uint8_t, 4> hash = {72, 160, 171, 170};
    m_DefaultVersionsData.emplace(version, VersionData("war3.exe 15/03/16 00:00:00 515048", patch, hash));
  }

  {
    // v1.28
    Version version = GAMEVER(1u, 28u);
    array<uint8_t, 4> patch = {0, 5, 28, 1};
    array<uint8_t, 4> hash = {201, 63, 116, 96};
    m_DefaultVersionsData.emplace(version, VersionData("war3.exe 05/20/17 13:25:29 558568", patch, hash));
  }
}

CBNCSUtilInterface::~CBNCSUtilInterface()
{
  delete static_cast<NLS*>(m_NLS);
}

optional<VersionData> CBNCSUtilInterface::GetDefaultVersionData(const Version& version, const bool useFallback)
{
  auto match = m_DefaultVersionsData.find(version);
  if (match == m_DefaultVersionsData.end()) {
    if (!useFallback) return nullopt;
    match = m_DefaultVersionsData.find(GAMEVER(1u, 27u));
  }
  return optional<VersionData>(match->second);
}

void CBNCSUtilInterface::Reset(const string& userName, const string& userPassword)
{
  delete static_cast<NLS*>(m_NLS);
  m_NLS = new NLS(userName, userPassword);
}

optional<Version> CBNCSUtilInterface::GetGameVersion(const filesystem::path& war3Path)
{
  optional<Version> version;
  const filesystem::path FileStormDLL = CaseInsensitiveFileExists(war3Path, "storm.dll");
  const filesystem::path FileGameDLL  = CaseInsensitiveFileExists(war3Path, "game.dll");
  const filesystem::path WarcraftIIIExe = CaseInsensitiveFileExists(war3Path, "Warcraft III.exe");
  const filesystem::path War3Exe = CaseInsensitiveFileExists(war3Path, "war3.exe");
  if (WarcraftIIIExe.empty() && War3Exe.empty()) {
    Print("[CONFIG] Game path corrupted or invalid (" + PathToString(war3Path) + "). Executable file not found.");
    Print("[CONFIG] Config required: <game.install_version>, <realm_N.auth_*>");
    return version;
  }
  if (FileStormDLL.empty() != FileGameDLL.empty()) {
    if (FileStormDLL.empty()) {
      Print("[CONFIG] Game.dll found, but Storm.dll missing at " + PathToString(war3Path) + ".");
    } else {
      Print("[CONFIG] Storm.dll found, but Game.dll missing at " + PathToString(war3Path) + ".");
    }
    Print("[CONFIG] Config required: <game.install_version>, <realm_N.auth_*>");
    return version;
  }
  if (FileStormDLL.empty()) {
    if (WarcraftIIIExe.empty()) {
      Print("[CONFIG] Game path corrupted or invalid (" + PathToString(war3Path) + "). No game files found.");
      Print("[CONFIG] Config required: <game.install_version>, <realm_N.auth_*>");
      return version;
    }
  }
  if (!War3Exe.empty()) {
    if (FileStormDLL.empty()) {
      Print("[CONFIG] Game path corrupted or invalid (" + PathToString(war3Path)  + "). Storm.dll is missing.");
      Print("[CONFIG] Config required: <game.install_version>, <realm_N.auth_*>");
      return version;
    }
  }

  uint8_t versionMode;
  if (!War3Exe.empty()) {
    versionMode = 27;
  } else if (!FileStormDLL.empty()) {
    versionMode = 28;
  } else {
    versionMode = 29;
  }

  const filesystem::path CheckExe = versionMode >= 28 ? WarcraftIIIExe : War3Exe;
  char     buf[1024];
  uint32_t EXEVersion = 0;
  getExeInfo(PathToString(CheckExe).c_str(), buf, 1024, &EXEVersion, BNCSUTIL_PLATFORM_X86);
  uint8_t readVersion = static_cast<uint8_t>(EXEVersion >> 16);

  if (readVersion == 0) {
    Print("[CONFIG] Game path corrupted or invalid (" + PathToString(war3Path)  + ").");
    Print("[CONFIG] Game path has files from v1." + to_string(versionMode));
    Print("[CONFIG] " + PathToString(CheckExe) + " cannot read version");
    Print("[CONFIG] Config required: <game.install_version>, <realm_N.auth_*>");
    return version;
  }
  if ((versionMode == 28) != (readVersion == 28) || (versionMode < 28 && readVersion > 28) || (versionMode > 28 && readVersion < 28)) {
    Print("[CONFIG] Game path corrupted or invalid (" + PathToString(war3Path)  + ").");
    Print("[CONFIG] Game path has files from v1." + to_string(versionMode));
    Print("[CONFIG] " + PathToString(CheckExe) + " is v1." + to_string(readVersion));
    Print("[CONFIG] Config required: <game.install_version>, <realm_N.auth_*>");
    return version;
  }

  version = GAMEVER(1, readVersion);
  return version;
}

bool CBNCSUtilInterface::ExtractEXEFeatures(const Version& war3DataVersion, const filesystem::path& war3Path, const string& valueStringFormula, const string& mpqFileName)
{
  if (war3Path.empty()) {
    return false;
  }

  const filesystem::path FileWar3EXE = [&]() {
    if (war3DataVersion >= GAMEVER(1u, 28u))
      return CaseInsensitiveFileExists(war3Path, "Warcraft III.exe");
    else
      return CaseInsensitiveFileExists(war3Path, "war3.exe");
  }();
  const filesystem::path FileStormDLL = CaseInsensitiveFileExists(war3Path, "storm.dll");
  const filesystem::path FileGameDLL  = CaseInsensitiveFileExists(war3Path, "game.dll");

  if (!FileWar3EXE.empty() && (war3DataVersion >= GAMEVER(1u, 29u) || (!FileStormDLL.empty() && !FileGameDLL.empty())))
  {
    size_t bufferSize = 512;
    size_t requiredSize = 0;
    vector<char> buffer(bufferSize);

    uint32_t EXEVersion = 0;
    unsigned long EXEVersionHash = 0;

    do {
      bufferSize *= 2;
      buffer.resize(bufferSize);
      requiredSize = (size_t)getExeInfo(PathToString(FileWar3EXE).c_str(), buffer.data(), bufferSize, &EXEVersion, BNCSUTIL_PLATFORM_X86);
    } while (0 < requiredSize && bufferSize < requiredSize);

    if (requiredSize == 0) {
      return false;
    }

    if (war3DataVersion >= GAMEVER(1u, 29u))
    {
      const char* filesArray[] = {PathToString(FileWar3EXE).c_str()};
      checkRevision(valueStringFormula.c_str(), filesArray, 1, extractMPQNumber(mpqFileName.c_str()), &EXEVersionHash);
    }
    else 
      checkRevisionFlat(valueStringFormula.c_str(), PathToString(FileWar3EXE).c_str(), PathToString(FileStormDLL).c_str(), PathToString(FileGameDLL).c_str(), extractMPQNumber(mpqFileName.c_str()), &EXEVersionHash);

    buffer.resize(requiredSize);
    m_EXEInfo        = buffer.data();
    m_EXEVersion     = CreateFixedByteArrayLE(EXEVersion);
    m_EXEVersionHash = CreateFixedByteArrayLossy<Endianness::kLittle>(static_cast<int64_t>(EXEVersionHash)); // Only uses 4 bytes

    return true;
  }
  else
  {
    if (FileWar3EXE.empty())
      Print("[BNCS] unable to open War3EXE [" + PathToString(FileWar3EXE) + "]");

    if (FileStormDLL.empty() && war3DataVersion < GAMEVER(1u, 29u))
      Print("[BNCS] unable to open StormDLL [" + PathToString(FileStormDLL) + "]");
    if (FileGameDLL.empty() && war3DataVersion < GAMEVER(1u, 29u))
      Print("[BNCS] unable to open GameDLL [" + PathToString(FileGameDLL) + "]");
  }

  return false;
}

bool CBNCSUtilInterface::HELP_SID_AUTH_CHECK(const filesystem::path& war3Path, const optional<Version>& war3DataVersion, const bool realmIsExpansion, const Version& realmAuthGameVersion, const CRealmConfig* realmConfig, const string& valueStringFormula, const string& mpqFileName, const uint32_t clientToken, const uint32_t serverToken)
{
  m_KeyInfoROC = CreateKeyInfo(realmConfig->m_CDKeyROC, clientToken, serverToken);
  m_KeyInfoTFT = CreateKeyInfo(realmConfig->m_CDKeyTFT, clientToken, serverToken);

  if (m_KeyInfoROC.size() != 36)
    Print("[BNCS] unable to create ROC key info - invalid ROC key");

  if (realmIsExpansion && m_KeyInfoTFT.size() != 36)
    Print("[BNCS] unable to create TFT key info - invalid TFT key");

  if (realmConfig->m_ExeAuthUseCustomVersionData) {
    if (realmConfig->m_ExeAuthVersionDetails.has_value()) {
      copy_n(realmConfig->m_ExeAuthVersionDetails.value().begin(), 4, m_EXEVersion.begin());
    }
    if (realmConfig->m_ExeAuthVersionHash.has_value()) {
      copy_n(realmConfig->m_ExeAuthVersionHash.value().begin(), 4, m_EXEVersionHash.begin());
    }
    if (!realmConfig->m_ExeAuthInfo.empty()) {
      m_EXEInfo = realmConfig->m_ExeAuthInfo;
    }
    return true;
  }

  if (
    !war3DataVersion.has_value() || war3DataVersion.value() != realmAuthGameVersion ||
    !ExtractEXEFeatures(realmAuthGameVersion, war3Path, valueStringFormula, mpqFileName)
  ) {
    optional<VersionData> versionData = GetDefaultVersionData(realmAuthGameVersion);
    if (!versionData.has_value()) {
      return false;
    }
    m_EXEInfo = versionData->info;
    m_EXEVersion = versionData->patch;
    m_EXEVersionHash = versionData->hash;
  }

  if (m_EXEInfo.empty()) {
    // Just in case
    m_EXEInfo = GetDefaultVersionData(GAMEVER(1u, 27u))->info;
  }

  return true;
}

bool CBNCSUtilInterface::HELP_SID_AUTH_ACCOUNTLOGON()
{
  // set m_ClientKey

  char buf[32];
  (static_cast<NLS*>(m_NLS))->getPublicKey(buf);
  copy_n(buf, 32, m_ClientKey.begin());
  return true;
}

bool CBNCSUtilInterface::HELP_SID_AUTH_ACCOUNTLOGONPROOF(const std::array<uint8_t, 32>& salt, const std::array<uint8_t, 32>& serverKey)
{
  // set m_M1

  char buf[20];
  (static_cast<NLS*>(m_NLS))->getClientSessionKey(buf, string(begin(salt), end(salt)).c_str(), string(begin(serverKey), end(serverKey)).c_str());
  copy_n(buf, 20, m_M1.begin());
  return true;
}

bool CBNCSUtilInterface::HELP_PvPGNPasswordHash(const string& userPassword)
{
  // set m_PvPGNPasswordHash

  char buf[20];
  hashPassword(userPassword.c_str(), buf);
  copy_n(buf, 20, m_PvPGNPasswordHash.begin());
  return true;
}

std::vector<uint8_t> CBNCSUtilInterface::CreateKeyInfo(const string& key, uint32_t clientToken, uint32_t serverToken)
{
  std::vector<uint8_t> KeyInfo;
  CDKeyDecoder Decoder(key.c_str(), key.size());

  if (Decoder.isKeyValid())
  {
    const uint8_t Zeros[] = {0, 0, 0, 0};
    AppendNumberLE(KeyInfo, (uint32_t)key.size()); // 4 bytes
    AppendContainer(KeyInfo, CreateByteArrayLE(Decoder.getProduct())); // 4 bytes
    AppendContainer(KeyInfo, CreateByteArrayLE(Decoder.getVal1())); // 4 bytes
    AppendBytes(KeyInfo, Zeros, 4);
    size_t Length = Decoder.calculateHash(clientToken, serverToken);
    auto   buf    = new char[Length];
    Length        = Decoder.getHash(buf);
    AppendBytes(KeyInfo, reinterpret_cast<uint8_t*>(buf), Length);
    delete[] buf;
  }

  return KeyInfo;
}
