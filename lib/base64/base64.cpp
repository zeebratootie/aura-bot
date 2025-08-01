/*
   base64.cpp and base64.h

   This library has been modified for inclusion in Aura.
*/

/*
   base64.cpp and base64.h

   base64 encoding and decoding with C++.
   More information at
     https://renenyffenegger.ch/notes/development/Base64/Encoding-and-decoding-base-64-with-cpp

   Version: 2.rc.09 (release candidate)

   Copyright (C) 2004-2017, 2020-2022 René Nyffenegger

   This source code is provided 'as-is', without any express or implied
   warranty. In no event will the author be held liable for any damages
   arising from the use of this software.

   Permission is granted to anyone to use this software for any purpose,
   including commercial applications, and to alter it and redistribute it
   freely, subject to the following restrictions:

   1. The origin of this source code must not be misrepresented; you must not
      claim that you wrote the original source code. If you use this source code
      in a product, an acknowledgment in the product documentation would be
      appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and must not be
      misrepresented as being the original source code.

   3. This notice may not be removed or altered from any source distribution.

   René Nyffenegger rene.nyffenegger@adp-gmbh.ch

*/

#include "base64.h"

#include <algorithm>
#include <stdexcept>

using namespace std;

constexpr unsigned int UpperCaseA = static_cast<unsigned int>(static_cast<unsigned char>('A'));
constexpr unsigned int UpperCaseZ = static_cast<unsigned int>(static_cast<unsigned char>('Z'));
constexpr unsigned int AlphabetSize = UpperCaseZ - UpperCaseA + 1;
constexpr unsigned int POS_OF_UPPER_CASE_OFFSET_MINUS = UpperCaseA;
constexpr unsigned int POS_OF_LOWER_CASE_OFFSET_MINUS = static_cast<unsigned int>(static_cast<unsigned char>('a')) - AlphabetSize;
constexpr unsigned int POS_OF_DIGITS_OFFSET = 2 * AlphabetSize - static_cast<unsigned int>(static_cast<unsigned char>('0'));

namespace Base64
{
   //
   // Depending on the url parameter in Charset, one of
   // two sets of base64 characters needs to be chosen.
   // They differ in their last two characters.
   //

  static const char* Charset[2] = {
               "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
               "abcdefghijklmnopqrstuvwxyz"
               "0123456789"
               "+/",

               "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
               "abcdefghijklmnopqrstuvwxyz"
               "0123456789"
               "-_"};

  static unsigned int GetPosOfChar(const unsigned char chr) {
   //
   // Return the position of chr within Encode()
   //

      if      (chr >= 'A' && chr <= 'Z') return static_cast<unsigned int>(chr) - POS_OF_UPPER_CASE_OFFSET_MINUS;
      else if (chr >= 'a' && chr <= 'z') return static_cast<unsigned int>(chr) - POS_OF_LOWER_CASE_OFFSET_MINUS;
      else if (chr >= '0' && chr <= '9') return static_cast<unsigned int>(chr) + POS_OF_DIGITS_OFFSET;
      else if (chr == '+' || chr == '-') return 62; // Be liberal with input and accept both url ('-') and non-url ('+') base 64 characters (
      else if (chr == '/' || chr == '_') return 63; // Ditto for '/' and '_'
      else
   //
   // 2020-10-23: Throw exception rather than const char*
   //(Pablo Martin-Gomez, https://github.com/Bouska)
   //
      throw runtime_error("Input is not valid base64-encoded data.");
  }

  static string insert_linebreaks(string str, size_t distance) {
   //
   // Provided by https://github.com/JomaCorpFX, adapted by me.
   //
      if (!str.length()) {
          return "";
      }

      size_t pos = distance;

      while (pos < str.size()) {
          str.insert(pos, "\n");
          pos += distance + 1;
      }

      return str;
  }

  template <typename String, unsigned int line_length>
  static string encode_with_line_breaks(String s) {
    return insert_linebreaks(Encode(s, false), line_length);
  }

  template <typename String>
  static string encode_PEM(String s) {
    return encode_with_line_breaks<String, 64>(s);
  }

  template <typename String>
  static string encode_MIME(String s) {
    return encode_with_line_breaks<String, 76>(s);
  }

  template <typename String>
  static string encode(String s, bool url) {
    return Encode(reinterpret_cast<const unsigned char*>(s.data()), s.length(), url);
  }

  string Encode(unsigned char const* bytes_to_encode, size_t in_len, bool url) {

    size_t len_encoded = (in_len +2) / 3 * 4;

    unsigned char trailing_char = url ? '.' : '=';

    //
    // Choose set of base64 characters. They differ
    // for the last two positions, depending on the url
    // parameter.
    // A bool (as is the parameter url) is guaranteed
    // to evaluate to either 0 or 1 in C++ therefore,
    // the correct character set is chosen by subscripting
    // Charset with url.
    //

    const char* Charset_ = Charset[url];

    string ret;
    ret.reserve(len_encoded);

    unsigned int pos = 0;

    while (pos < in_len) {
      ret.push_back(Charset_[(bytes_to_encode[pos + 0] & 0xfc) >> 2]);

      if (pos+1 < in_len) {
        ret.push_back(Charset_[((bytes_to_encode[pos + 0] & 0x03) << 4) + ((bytes_to_encode[pos + 1] & 0xf0) >> 4)]);

        if (pos+2 < in_len) {
         ret.push_back(Charset_[((bytes_to_encode[pos + 1] & 0x0f) << 2) + ((bytes_to_encode[pos + 2] & 0xc0) >> 6)]);
         ret.push_back(Charset_[  bytes_to_encode[pos + 2] & 0x3f]);
        }
        else {
         ret.push_back(Charset_[(bytes_to_encode[pos + 1] & 0x0f) << 2]);
         ret.push_back(static_cast<char>(trailing_char));
        }
      }
      else {
        ret.push_back(Charset_[(bytes_to_encode[pos + 0] & 0x03) << 4]);
        ret.push_back(static_cast<char>(trailing_char));
        ret.push_back(static_cast<char>(trailing_char));
      }

      pos += 3;
    }


    return ret;
  }

  template <typename String>
  static string decode(String const& encoded_string, bool remove_linebreaks) {
     //
     // decode(…) is templated so that it can be used with String = const string&
     // or string_view (requires at least C++17)
     //

    if (encoded_string.empty()) return string();

    if (remove_linebreaks) {

      string copy(encoded_string);

      copy.erase(remove(copy.begin(), copy.end(), '\n'), copy.end());

      return Decode(copy, false);
    }

    size_t length_of_string = encoded_string.length();
    size_t pos = 0;

    //
    // The approximate length (bytes) of the decoded string might be one or
    // two bytes smaller, depending on the amount of trailing equal signs
    // in the encoded string. This approximation is needed to reserve
    // enough space in the string to be returned.
    //

    size_t approx_length_of_decoded_string = length_of_string / 4 * 3;
    string ret;
    ret.reserve(approx_length_of_decoded_string);

    while (pos < length_of_string) {
    //
    // Iterate over encoded input string in chunks. The size of all
    // chunks except the last one is 4 bytes.
    //
    // The last chunk might be padded with equal signs or dots
    // in order to make it 4 bytes in size as well, but this
    // is not required as per RFC 2045.
    //
    // All chunks except the last one produce three output bytes.
    //
    // The last chunk produces at least one and up to three bytes.
    //

      size_t pos_of_char_1 = GetPosOfChar((unsigned char)encoded_string.at(pos+1) );

      //
      // Emit the first output byte that is produced in each chunk:
      //
      ret.push_back(static_cast<string::value_type>( ( (GetPosOfChar((unsigned char)encoded_string.at(pos+0)) ) << 2 ) + ( (pos_of_char_1 & 0x30 ) >> 4)));

      if ( ( pos + 2 < length_of_string  )       &&  // Check for data that is not padded with equal signs (which is allowed by RFC 2045)
            encoded_string.at(pos+2) != '='     &&
            encoded_string.at(pos+2) != '.'         // accept URL-safe base 64 strings, too, so check for '.' also.
      ) {
        //
        // Emit a chunk's second byte (which might not be produced in the last chunk).
        //
        unsigned int pos_of_char_2 = GetPosOfChar((unsigned char)encoded_string.at(pos+2) );
        ret.push_back(static_cast<string::value_type>( (( pos_of_char_1 & 0x0f) << 4) + (( pos_of_char_2 & 0x3c) >> 2)));

        if ( ( pos + 3 < length_of_string )     &&
               encoded_string.at(pos+3) != '='  &&
               encoded_string.at(pos+3) != '.'
        ) {
          //
          // Emit a chunk's third byte (which might not be produced in the last chunk).
          //
          ret.push_back(static_cast<string::value_type>( ( (pos_of_char_2 & 0x03 ) << 6 ) + GetPosOfChar((unsigned char)encoded_string.at(pos+3))   ));
        }
      }

      pos += 4;
    }

    return ret;
  }

  string Decode(string const& s, bool remove_linebreaks) {
     return decode(s, remove_linebreaks);
  }

  string Encode(string const& s, bool url) {
     return encode(s, url);
  }

  string Encode_PEM (string const& s) {
     return encode_PEM(s);
  }

  string Encode_MIME(string const& s) {
     return encode_MIME(s);
  }

#if __cplusplus >= 201703L
  //
  // Interface with string_view rather than const string&
  // Requires C++17
  // Provided by Yannic Bonenberger (https://github.com/Yannic)
  //

  string Encode(string_view s, bool url) {
     return encode(s, url);
  }

  string Encode_PEM(string_view s) {
     return encode_PEM(s);
  }

  string Encode_MIME(string_view s) {
     return encode_MIME(s);
  }

  string Decode(string_view s, bool remove_linebreaks) {
     return decode(s, remove_linebreaks);
  }
#endif  // __cplusplus >= 201703L
};
