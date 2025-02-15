// Copyright 2015 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "clparser.h"

#include <algorithm>
#include <assert.h>
#include <string.h>

#include "metrics.h"
#include "string_piece_util.h"

#ifdef _WIN32
#include "includes_normalize.h"
#include <string_view>
#else
#include "util.h"
#endif

namespace {

/// Return true if \a input ends with \a needle.
bool EndsWith(const std::string& input, const std::string& needle) {
  return (input.size() >= needle.size() &&
          input.substr(input.size() - needle.size()) == needle);
}

}  // anonymous namespace

// static
std::string CLParser::FilterShowIncludes(const std::string& line,
                                    const std::string& deps_prefix) {
  const std::string kDepsPrefixEnglish = "Note: including file: ";
  const char* in = line.c_str();
  const char* end = in + line.size();
  const std::string& prefix = deps_prefix.empty() ? kDepsPrefixEnglish : deps_prefix;
  if (end - in > (int)prefix.size() &&
      memcmp(in, prefix.c_str(), (int)prefix.size()) == 0) {
    in += prefix.size();
    while (*in == ' ')
      ++in;
    return line.substr(in - line.c_str());
  }
  return "";
}

// static
bool CLParser::IsSystemInclude(std::string path) {
  transform(path.begin(), path.end(), path.begin(), ToLowerASCII);
  // TODO: this is a heuristic, perhaps there's a better way?
  return (path.find("program files") != std::string::npos ||
          path.find("microsoft visual studio") != std::string::npos);
}

// static
bool CLParser::FilterInputFilename(std::string line) {
  transform(line.begin(), line.end(), line.begin(), ToLowerASCII);
  // TODO: other extensions, like .asm?
  return EndsWith(line, ".c") ||
      EndsWith(line, ".cc") ||
      EndsWith(line, ".cxx") ||
      EndsWith(line, ".cpp");
}

// static
bool CLParser::Parse(const std::string& output, const std::string& deps_prefix,
                     std::string* filtered_output, std::string* err) {
  METRIC_RECORD("CLParser::Parse");

  // Loop over all lines in the output to process them.
  assert(&output != filtered_output);
  size_t start = 0;
#ifdef _WIN32
  IncludesNormalize normalizer(".");
#endif

  while (start < output.size()) {
    size_t end = output.find_first_of("\r\n", start);
    if (end == std::string::npos)
      end = output.size();
    std::string line = output.substr(start, end - start);

    std::string include = FilterShowIncludes(line, deps_prefix);
    if (!include.empty()) {
      std::string normalized;
#ifdef _WIN32
      if (!normalizer.Normalize(include, &normalized, err))
        return false;
#else
      // TODO: should this make the path relative to cwd?
      normalized = include;
#endif
      if (!IsSystemInclude(normalized))
        includes_.insert(normalized);
    } else if (FilterInputFilename(line)) {
      // Drop it.
      // TODO: if we support compiling multiple output files in a single
      // cl.exe invocation, we should stash the filename.
    } else {
      filtered_output->append(line);
      filtered_output->append("\n");
    }

    if (end < output.size() && output[end] == '\r')
      ++end;
    if (end < output.size() && output[end] == '\n')
      ++end;
    start = end;
  }

  return true;
}
