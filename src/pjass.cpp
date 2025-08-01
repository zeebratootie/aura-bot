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

#include "pjass.h"

using namespace std;

#ifndef DISABLE_PJASS
pair<bool, string> ParseJASSFiles(const vector<filesystem::path>& filePaths, const bitset<11> flags)
{
  const static vector<string> pjassAvailableFlags = {
    // pjass error types
    "+nosyntaxerror", "+nosemanticerror", "+noruntimeerror",

    // jass language features
    "+rb", "+nomodulooperator", "+shadow", "+checklongnames",

    // linter
    "+filter", "+checkglobalsinit", "+checkstringhash", "+checknumberliterals"
  };

  bool result = false;
  string details;
  char buffer[1024];
  int maxOutSize = sizeof(buffer);
  int outSize = 0;
  int fileCount = signed_cast_lossy<int>(filePaths.size());
  if (fileCount <= 0) {
    details = "Invalid invocation";
    return make_pair(result, details);
  }

  vector<string> filePathsInner;
  vector<const char*> filePtrs;

  for (size_t i = 0; i < flags.size(); ++i) {
    if (flags.test(i)) {
      filePtrs.push_back(pjassAvailableFlags[i].c_str());
      fileCount++;
    }
  }

  for (const auto& filePath : filePaths) {
    filePathsInner.push_back(filePath.string());
    filePtrs.push_back(filePathsInner.back().c_str());
  }

  result = parse_jass_custom_r(buffer, maxOutSize, &outSize, fileCount, filePtrs.data()) == 0;
  if (result) {
    return make_pair(result, details);
  }
  if (outSize <= 0) {
    details = "Generic parser error";
  } else {
    details = string(buffer, outSize);
  }
  return make_pair(result, details);
}

pair<bool, string> ParseJASSFiles(const vector<filesystem::path>& filePaths, const bitset<11> baseFlags, const Version& version)
{
  bitset<11> flags = baseFlags;
  if (version < GAMEVER(1u, 24u)) {
    // https://jass.sourceforge.net/doc/retbug.shtml
    // Return bug may be used for old patches
    // Intentionally not implemented <maps.validators.jass.features.return_bug>
    flags.set(PJASS_OPTIONS_RB);
  } else {
    flags.reset(PJASS_OPTIONS_RB);
  }

  if (version < GAMEVER(1u, 29u)) {
    // Modulo operator (%) does not exist for old patches
    // Intentionally not implemented <maps.validators.jass.features.modulo> 
    flags.set(PJASS_OPTIONS_NOMODULOOPERATOR);
  } else {
    flags.reset(PJASS_OPTIONS_NOMODULOOPERATOR);
  }

  if (version >= GAMEVER(1u, 31u)) {
    // Intentionally not implemented <maps.validators.jass.features.shadowing>
    flags.set(PJASS_OPTIONS_SHADOW);
  } else {
    // Local variables may be defined with the same name as global variables for old patches
    flags.reset(PJASS_OPTIONS_SHADOW);
  }

  if (version < GAMEVER(1u, 31u)) {
    // Names should be at most 3958 characters long for old patches
    // Intentionally not implemented <maps.validators.jass.features.long_names>
    flags.set(PJASS_OPTIONS_CHECKLONGNAMES);
  } else {
    flags.reset(PJASS_OPTIONS_CHECKLONGNAMES);
  }

  return ParseJASSFiles(filePaths, flags);
}

pair<bool, string> ParseJASS(string commonJ, string blizzardJ, string war3mapJ, const bitset<11> flags)
{
  const static vector<string> pjassAvailableFlags = {
    // pjass error types
    "+nosyntaxerror", "+nosemanticerror", "+noruntimeerror",

    // jass language features
    "+rb", "+nomodulooperator", "+shadow", "+checklongnames",

    // linter
    "+filter", "+checkglobalsinit", "+checkstringhash", "+checknumberliterals"
  };

  bool result = false;
  string details;
  char buffer[1024];
  int maxOutSize = sizeof(buffer);
  int outSize = 0;

  string flagString;
  for (size_t i = 0; i < flags.size(); ++i) {
    if (flags.test(i)) {
      if (flagString.empty()) {
        flagString = pjassAvailableFlags[i];
      } else {
        flagString.append(" " + pjassAvailableFlags[i]);
      }
    }
  }

  size_t commonSize = commonJ.size();
  size_t blizzardSize = blizzardJ.size();
  size_t war3mapSize = war3mapJ.size();

  if (commonSize > INT32_MAX || blizzardSize > INT32_MAX || war3mapSize > INT32_MAX) {
    details = "Invalid invocation";
    return make_pair(result, details);
  }

  const int bufferSizes[] = {signed_cast_lossy<int>(commonSize), signed_cast_lossy<int>(blizzardSize), signed_cast_lossy<int>(war3mapSize)};
  char* targets[] = {commonJ.data(), blizzardJ.data(), war3mapJ.data()};
  char* fixedFlags[] = {flagString.data(), flagString.data(), flagString.data()};

  result = parse_jass_custom(buffer, maxOutSize, &outSize, 3, bufferSizes, targets, fixedFlags) == 0;
  //result = parse_jass_custom(buffer, maxOutSize, &outSize, 1, bufferSizes + 2, targets + 2, fixedFlags + 2) == 0;
  //result = parse_jass_triad(buffer, maxOutSize, &outSize, commonJ.data(), commonJ.size(), blizzardJ.data(), blizzardJ.size(), war3mapJ.data(), war3mapJ.size()) == 0;
  //result = parse_jass(buffer, maxOutSize, &outSize, war3mapJ.data(), war3mapJ.size()) == 0;
  if (result) {
    return make_pair(result, details);
  }
  if (outSize <= 0) {
    details = "Generic parser error";
  } else {
    details = string(buffer, outSize);
  }
  return make_pair(result, details);
}

pair<bool, string> ParseJASS(const string& commonJ, const string& blizzardJ, const string& war3mapJ, const bitset<11> baseFlags, const Version& version)
{
  bitset<11> flags = baseFlags;
  if (version < GAMEVER(1u, 24u)) {
    // https://jass.sourceforge.net/doc/retbug.shtml
    // Return bug may be used for old patches
    // Intentionally not implemented <maps.validators.jass.features.return_bug>
    flags.set(PJASS_OPTIONS_RB);
  } else {
    flags.reset(PJASS_OPTIONS_RB);
  }

  if (version < GAMEVER(1u, 29u)) {
    // Modulo operator (%) does not exist for old patches
    // Intentionally not implemented <maps.validators.jass.features.modulo> 
    flags.set(PJASS_OPTIONS_NOMODULOOPERATOR);
  } else {
    flags.reset(PJASS_OPTIONS_NOMODULOOPERATOR);
  }

  if (version >= GAMEVER(1u, 31u)) {
    // Intentionally not implemented <maps.validators.jass.features.shadowing>
    flags.set(PJASS_OPTIONS_SHADOW);
  } else {
    // Local variables may be defined with the same name as global variables for old patches
    flags.reset(PJASS_OPTIONS_SHADOW);
  }

  if (version < GAMEVER(1u, 31u)) {
    // Names should be at most 3958 characters long for old patches
    // Intentionally not implemented <maps.validators.jass.features.long_names>
    flags.set(PJASS_OPTIONS_CHECKLONGNAMES);
  } else {
    flags.reset(PJASS_OPTIONS_CHECKLONGNAMES);
  }

  return ParseJASS(commonJ, blizzardJ, war3mapJ, baseFlags);
}

#endif
