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

#ifndef DISABLE_TESTS
#include "runner.h"
#include "../util.h"

using namespace std;

bool TestRunner::CheckStatStrings()
{
  bool success = true;
  filesystem::path rawFolder = "test/fixtures/protocol/stat-strings/raw";
  filesystem::path encFolder = "test/fixtures/protocol/stat-strings/encoded";

  try {
    for (const auto& entry : filesystem::directory_iterator(rawFolder)) {
      filesystem::path fromPath = entry.path();
      filesystem::path fileName = fromPath.filename();
      filesystem::path outPath = encFolder / fileName;
      if (filesystem::is_regular_file(fromPath)) {
        vector<uint8_t> fromContents, actual, expected, reversed;
        if (!FileRead(fromPath, fromContents, 0xFFFF)) {
          throw runtime_error("Failed to read file [" + PathToString(fromPath) + "]");
        }
        actual = EncodeStatString(fromContents);
        if (filesystem::exists(outPath)) {
          if (!FileRead(outPath, expected, 0xFFFF)) {
            throw runtime_error("Failed to read file [" + PathToString(outPath) + "]");
          }
          if (expected != actual) {
            Print("[TEST] ERR - EncodeStatString [" + PathToString(fromPath) + "] Expected output <" + ByteArrayToHexString(expected) + "> but got <" + ByteArrayToHexString(actual) + ">");
            success = false;
          }
        } else {
          filesystem::path pendingName = filesystem::path(PLATFORM_STRING_TYPE(fileName.native().c_str()) + PLATFORM_STRING("-new"));
          filesystem::path pendingPath = encFolder / pendingName;
          if (!FileWrite(pendingPath, reinterpret_cast<const uint8_t*>(actual.data()), actual.size())) {
            throw runtime_error("Failed to write file [" + PathToString(pendingPath) + "]");
          }
          Print("[TEST] Pending - EncodeStatString [" + PathToString(fromPath) + "] Generated output snapshot");
        }
        reversed = DecodeStatString(actual);
        if (reversed != fromContents) {
          Print("[TEST] ERR - DecodeStatString failed to reverse EncodeStatString [" + PathToString(fromPath) + "]");
          success = false;
        }
      }
    }
  } catch (const exception& e) {
    success = false;
    Print("[TEST] ERR - " + string(e.what()));
  }

  return success;
}

uint16_t TestRunner::Run()
{
  if (!CheckStatStrings()) return 1;
  return 0;
}
#endif
