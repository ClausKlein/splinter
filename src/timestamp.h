// Copyright 2011 Google Inc. All Rights Reserved.
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

#ifndef NINJA_TIMESTAMP_H_
#define NINJA_TIMESTAMP_H_

#include <filesystem>

// When considering file modification times we only care to compare
// them against one another -- we never convert them to an absolute
// real time.
using TimeStamp = std::filesystem::file_time_type;

#endif  // NINJA_TIMESTAMP_H_
