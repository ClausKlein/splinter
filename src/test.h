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

#ifndef NINJA_TEST_H_
#define NINJA_TEST_H_

#include "disk_interface.h"
#include "manifest_parser.h"
#include "state.h"
#include "util.h"

// A tiny testing framework inspired by googletest, but much simpler and
// faster to compile. It supports most things commonly used from googltest. The
// most noticeable things missing: EXPECT_* and ASSERT_* don't support
// streaming notes to them with operator<<, and for failing tests the lhs and
// rhs are not printed. That's so that this header does not have to include
// sstream, which slows down building ninja_test almost 20%.
namespace testing {
class Test {
  bool failed_ = false;
  int assertion_failures_ = 0;
 public:
  Test() = default;
  virtual ~Test() = default;
  virtual void SetUp() {}
  virtual void TearDown() {}
  virtual void Run() = 0;

  bool Failed() const { return failed_; }
  int AssertionFailures() const { return assertion_failures_; }
  void AddAssertionFailure() { assertion_failures_++; }
  bool Check(bool condition, const char* file, int line, const char* error);
};
}

void RegisterTest(testing::Test* (*)(), const char*);

extern testing::Test* g_current_test;
#define TEST_F_(x, y, name)                                           \
  struct y final : public x {                                         \
    static testing::Test* Create() { return g_current_test = new y; } \
    void Run() override final;                                        \
  };                                                                  \
  struct Register##y final {                                          \
    Register##y() { RegisterTest(y::Create, name); }                  \
  };                                                                  \
  Register##y g_register_##y;                                         \
  void y::Run()

#define TEST_F(x, y) TEST_F_(x, x##y, #x "." #y)
#define TEST(x, y) TEST_F_(testing::Test, x##y, #x "." #y)

#define EXPECT_EQ(a, b) \
  g_current_test->Check(a == b, __FILE__, __LINE__, #a " == " #b)
#define EXPECT_NE(a, b) \
  g_current_test->Check(a != b, __FILE__, __LINE__, #a " != " #b)
#define EXPECT_GT(a, b) \
  g_current_test->Check(a > b, __FILE__, __LINE__, #a " > " #b)
#define EXPECT_LT(a, b) \
  g_current_test->Check(a < b, __FILE__, __LINE__, #a " < " #b)
#define EXPECT_GE(a, b) \
  g_current_test->Check(a >= b, __FILE__, __LINE__, #a " >= " #b)
#define EXPECT_LE(a, b) \
  g_current_test->Check(a <= b, __FILE__, __LINE__, #a " <= " #b)
#define EXPECT_TRUE(a) \
  g_current_test->Check(static_cast<bool>(a), __FILE__, __LINE__, #a)
#define EXPECT_FALSE(a) \
  g_current_test->Check(!static_cast<bool>(a), __FILE__, __LINE__, #a)

#define ASSERT_EQ(a, b) \
  if (!EXPECT_EQ(a, b)) { g_current_test->AddAssertionFailure(); return; }
#define ASSERT_NE(a, b) \
  if (!EXPECT_NE(a, b)) { g_current_test->AddAssertionFailure(); return; }
#define ASSERT_GT(a, b) \
  if (!EXPECT_GT(a, b)) { g_current_test->AddAssertionFailure(); return; }
#define ASSERT_LT(a, b) \
  if (!EXPECT_LT(a, b)) { g_current_test->AddAssertionFailure(); return; }
#define ASSERT_GE(a, b) \
  if (!EXPECT_GE(a, b)) { g_current_test->AddAssertionFailure(); return; }
#define ASSERT_LE(a, b) \
  if (!EXPECT_LE(a, b)) { g_current_test->AddAssertionFailure(); return; }
#define ASSERT_TRUE(a)  \
  if (!EXPECT_TRUE(a))  { g_current_test->AddAssertionFailure(); return; }
#define ASSERT_FALSE(a) \
  if (!EXPECT_FALSE(a)) { g_current_test->AddAssertionFailure(); return; }
#define ASSERT_NO_FATAL_FAILURE(a)                           \
  {                                                          \
    int fail_count = g_current_test->AssertionFailures();    \
    a;                                                       \
    if (fail_count != g_current_test->AssertionFailures()) { \
      g_current_test->AddAssertionFailure();                 \
      return;                                                \
    }                                                        \
  }

// Support utilities for tests.

struct Node;

/// A base test fixture that includes a State object with a
/// builtin "cat" rule.
struct StateTestWithBuiltinRules : public testing::Test {
  StateTestWithBuiltinRules();

  /// Add a "cat" rule to \a state.  Used by some tests; it's
  /// otherwise done by the ctor to state_.
  void AddCatRule(State* state);

  /// Short way to get a Node by its path from state_.
  Node* GetNode(const std::string& path);

  State state_;
};

void AssertParse(State* state, const char* input,
                 ManifestParserOptions = ManifestParserOptions());
void AssertHash(const char* expected, uint64_t actual);
void VerifyGraph(const State& state);

/// An implementation of DiskInterface that uses an in-memory representation
/// of disk state.  It also logs file accesses and directory creations
/// so it can be used by tests to verify disk access patterns.
struct VirtualFileSystem final : public DiskInterface {
  VirtualFileSystem() = default;

  /// "Create" a file with contents.
  void Create(std::filesystem::path const& path,
              std::string_view contents);

  /// Tick "time" forwards; subsequent file operations will be newer than
  /// previous ones.
  TimeStamp Tick() {
    // Can use ++ in C++20.
    now_ += TimeStamp::duration(1);
    return now_;
  }

  // DiskInterface
  TimeStamp Stat(std::filesystem::path const& path, std::string* err) const override final;
  bool WriteFile(std::filesystem::path const& path, std::string_view contents) override final;
  bool MakeDir(std::filesystem::path const& path) override final;
  Status ReadFile(std::filesystem::path const& path, std::string* contents, std::string* err) override final;
  int RemoveFile(std::filesystem::path const& path) override final;

  /// An entry for a single in-memory file.
  struct Entry final {
    TimeStamp mtime;
    std::string stat_error;  // If mtime is -1.
    std::string contents;
  };

  std::vector<std::string> directories_made_;
  std::vector<std::string> files_read_;
  typedef std::map<std::string, Entry, std::less<>> FileMap;
  FileMap files_;
  std::set<std::string> files_removed_;
  std::set<std::string> files_created_;

  /// A simple fake timestamp for file operations.
  TimeStamp now_ = TimeStamp::min();
};

struct ScopedTempDir final {
  /// Create a temporary directory and chdir into it.
  void CreateAndEnter(const std::string& name);

  /// Clean up the temporary directory.
  void Cleanup();

  /// The temp directory containing our dir.
  std::string start_dir_;
  /// The subdirectory name for our dir, or empty if it hasn't been set up.
  std::string temp_dir_name_;
};

#endif // NINJA_TEST_H_
