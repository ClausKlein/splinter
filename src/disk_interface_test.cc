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

#include <assert.h>
#include <stdio.h>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#endif

#include "disk_interface.h"
#include "graph.h"
#include "test.h"

namespace {

struct DiskInterfaceTest : public testing::Test {
  void SetUp() override final {
    // These tests do real disk accesses, so create a temp dir.
    temp_dir_.CreateAndEnter("Ninja-DiskInterfaceTest");
  }

  void TearDown() override final {
    temp_dir_.Cleanup();
  }

  bool Touch(const char* path) {
    FILE *f = fopen(path, "w");
    if (!f)
      return false;
    return fclose(f) == 0;
  }

  ScopedTempDir temp_dir_;
  RealDiskInterface disk_;
};

TEST_F(DiskInterfaceTest, StatMissingFile) {
  std::string err;
  EXPECT_EQ(TimeStamp::min(), disk_.Stat("nosuchfile", &err));
  EXPECT_EQ("", err);

  // On Windows, the errno for a file in a nonexistent directory
  // is different.
  EXPECT_EQ(TimeStamp::min(), disk_.Stat("nosuchdir/nosuchfile", &err));
  EXPECT_EQ("", err);

  // On POSIX systems, the errno is different if a component of the
  // path prefix is not a directory.
  ASSERT_TRUE(Touch("notadir"));
  EXPECT_EQ(TimeStamp::min(), disk_.Stat("notadir/nosuchfile", &err));
  EXPECT_EQ("", err);
}

TEST_F(DiskInterfaceTest, StatBadPath) {
  std::string err;
#ifdef _WIN32
  std::string bad_path("cc:\\foo");
  EXPECT_EQ(TimeStamp::max(), disk_.Stat(bad_path, &err));
  EXPECT_NE("", err);
#else
  std::string too_long_name(512, 'x');
  EXPECT_EQ(TimeStamp::max(), disk_.Stat(too_long_name, &err));
  EXPECT_NE("", err);
#endif
}

TEST_F(DiskInterfaceTest, StatExistingFile) {
  std::string err;
  ASSERT_TRUE(Touch("file"));
  EXPECT_GT(disk_.Stat("file", &err), TimeStamp(TimeStamp::duration(1)));
  EXPECT_EQ("", err);
}

TEST_F(DiskInterfaceTest, StatExistingDir) {
  std::string err;
  ASSERT_TRUE(disk_.MakeDir("subdir"));
  ASSERT_TRUE(disk_.MakeDir("subdir/subsubdir"));
  EXPECT_GT(disk_.Stat("..", &err), TimeStamp(TimeStamp::duration(1)));
  EXPECT_EQ("", err);
  EXPECT_GT(disk_.Stat(".", &err), TimeStamp(TimeStamp::duration(1)));
  EXPECT_EQ("", err);
  EXPECT_GT(disk_.Stat("subdir", &err), TimeStamp(TimeStamp::duration(1)));
  EXPECT_EQ("", err);
  EXPECT_GT(disk_.Stat("subdir/subsubdir", &err), TimeStamp(TimeStamp::duration(1)));
  EXPECT_EQ("", err);

  EXPECT_EQ(disk_.Stat("subdir", &err),
            disk_.Stat("subdir/.", &err));
  EXPECT_EQ(disk_.Stat("subdir", &err),
            disk_.Stat("subdir/subsubdir/..", &err));
  EXPECT_EQ(disk_.Stat("subdir/subsubdir", &err),
            disk_.Stat("subdir/subsubdir/.", &err));
}

#ifdef _WIN32
TEST_F(DiskInterfaceTest, StatCache) {
  std::string err;

  ASSERT_TRUE(Touch("file1"));
  ASSERT_TRUE(Touch("fiLE2"));
  ASSERT_TRUE(disk_.MakeDir("subdir"));
  ASSERT_TRUE(disk_.MakeDir("subdir/subsubdir"));
  ASSERT_TRUE(Touch("subdir\\subfile1"));
  ASSERT_TRUE(Touch("subdir\\SUBFILE2"));
  ASSERT_TRUE(Touch("subdir\\SUBFILE3"));

  disk_.AllowStatCache(false);
  TimeStamp parent_stat_uncached = disk_.Stat("..", &err);
  disk_.AllowStatCache(true);

  EXPECT_GT(disk_.Stat("FIle1", &err), TimeStamp(TimeStamp::duration(1)));
  EXPECT_EQ("", err);
  EXPECT_GT(disk_.Stat("file1", &err), TimeStamp(TimeStamp::duration(1)));
  EXPECT_EQ("", err);

  EXPECT_GT(disk_.Stat("subdir/subfile2", &err), TimeStamp(TimeStamp::duration(1)));
  EXPECT_EQ("", err);
  EXPECT_GT(disk_.Stat("sUbdir\\suBFile1", &err), TimeStamp(TimeStamp::duration(1)));
  EXPECT_EQ("", err);

  EXPECT_GT(disk_.Stat("..", &err), TimeStamp(TimeStamp::duration(1)));
  EXPECT_EQ("", err);
  EXPECT_GT(disk_.Stat(".", &err), TimeStamp(TimeStamp::duration(1)));
  EXPECT_EQ("", err);
  EXPECT_GT(disk_.Stat("subdir", &err), TimeStamp(TimeStamp::duration(1)));
  EXPECT_EQ("", err);
  EXPECT_GT(disk_.Stat("subdir/subsubdir", &err), TimeStamp(TimeStamp::duration(1)));
  EXPECT_EQ("", err);

#ifndef _MSC_VER // TODO: Investigate why. Also see https://github.com/ninja-build/ninja/pull/1423
  EXPECT_EQ(disk_.Stat("subdir", &err),
            disk_.Stat("subdir/.", &err));
  EXPECT_EQ("", err);
  EXPECT_EQ(disk_.Stat("subdir", &err),
            disk_.Stat("subdir/subsubdir/..", &err));
#endif
  EXPECT_EQ("", err);
  EXPECT_EQ(disk_.Stat("..", &err), parent_stat_uncached);
  EXPECT_EQ("", err);
  EXPECT_EQ(disk_.Stat("subdir/subsubdir", &err),
            disk_.Stat("subdir/subsubdir/.", &err));
  EXPECT_EQ("", err);

  // Test error cases.
  std::string bad_path("cc:\\foo");
  EXPECT_EQ(-1, disk_.Stat(bad_path, &err));
  EXPECT_NE("", err); err.clear();
  EXPECT_EQ(-1, disk_.Stat(bad_path, &err));
  EXPECT_NE("", err); err.clear();
  EXPECT_EQ(0, disk_.Stat("nosuchfile", &err));
  EXPECT_EQ("", err);
  EXPECT_EQ(0, disk_.Stat("nosuchdir/nosuchfile", &err));
  EXPECT_EQ("", err);
}
#endif

TEST_F(DiskInterfaceTest, ReadFile) {
  std::string err;
  std::string content;
  ASSERT_EQ(DiskInterface::NotFound,
            disk_.ReadFile("foobar", &content, &err));
  EXPECT_EQ("", content);
  EXPECT_NE("", err); // actual value is platform-specific
  err.clear();

  const char* kTestFile = "testfile";
  FILE* f = fopen(kTestFile, "wb");
  ASSERT_TRUE(f);
  const char* kTestContent = "test content\nok";
  fprintf(f, "%s", kTestContent);
  ASSERT_EQ(0, fclose(f));

  ASSERT_EQ(DiskInterface::Okay,
            disk_.ReadFile(kTestFile, &content, &err));
  EXPECT_EQ(kTestContent, content);
  EXPECT_EQ("", err);
}

TEST_F(DiskInterfaceTest, MakeDirs) {
  std::string path = "path/with/double//slash/";
  EXPECT_TRUE(disk_.MakeDirs(path.c_str()));
  FILE* f = fopen((path + "a_file").c_str(), "w");
  EXPECT_TRUE(f);
  EXPECT_EQ(0, fclose(f));
#ifdef _WIN32
  std::string path2 = "another\\with\\back\\\\slashes\\";
  EXPECT_TRUE(disk_.MakeDirs(path2.c_str()));
  FILE* f2 = fopen((path2 + "a_file").c_str(), "w");
  EXPECT_TRUE(f2);
  EXPECT_EQ(0, fclose(f2));
#endif
}

TEST_F(DiskInterfaceTest, RemoveFile) {
  const char* kFileName = "file-to-remove";
  ASSERT_TRUE(Touch(kFileName));
  EXPECT_EQ(0, disk_.RemoveFile(kFileName));
  EXPECT_EQ(1, disk_.RemoveFile(kFileName));
  EXPECT_EQ(1, disk_.RemoveFile("does not exist"));
}

struct StatTest : public StateTestWithBuiltinRules,
                  public DiskInterface {
  StatTest() : scan_(&state_, nullptr, nullptr, this, nullptr) {}

  // DiskInterface implementation.
  TimeStamp Stat(std::filesystem::path const& path, std::string* err) const override final;
  bool WriteFile(std::filesystem::path const& path, std::string_view contents) override final {
    assert(false);
    return true;
  }
  bool MakeDir(std::filesystem::path const& path) override final {
    assert(false);
    return false;
  }
  Status ReadFile(std::filesystem::path const& path, std::string* contents, std::string* err) override final {
    assert(false);
    return NotFound;
  }
  int RemoveFile(std::filesystem::path const& path) override final {
    assert(false);
    return 0;
  }

  DependencyScan scan_;
  std::map<std::string, TimeStamp> mtimes_;
  mutable std::vector<std::string> stats_;
};

TimeStamp StatTest::Stat(std::filesystem::path const& path, std::string* err) const {
  stats_.push_back(path);
  std::map<std::string, TimeStamp>::const_iterator i = mtimes_.find(path);
  if (i == mtimes_.end())
    return TimeStamp(TimeStamp::duration(456));  // File not found.
  return i->second;
}

TEST_F(StatTest, Simple) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build out: cat in\n"));

  Node* out = GetNode("out");
  std::string err;
  EXPECT_TRUE(out->Stat(this, &err));
  EXPECT_EQ("", err);
  ASSERT_EQ(1u, stats_.size());
  scan_.RecomputeDirty(out, nullptr);
  ASSERT_EQ(2u, stats_.size());
  ASSERT_EQ("out", stats_[0]);
  ASSERT_EQ("in",  stats_[1]);
}

TEST_F(StatTest, TwoStep) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build out: cat mid\n"
"build mid: cat in\n"));

  Node* out = GetNode("out");
  std::string err;
  EXPECT_TRUE(out->Stat(this, &err));
  EXPECT_EQ("", err);
  ASSERT_EQ(1u, stats_.size());
  scan_.RecomputeDirty(out, nullptr);
  ASSERT_EQ(3u, stats_.size());
  ASSERT_EQ("out", stats_[0]);
  ASSERT_TRUE(GetNode("out")->dirty());
  ASSERT_EQ("mid",  stats_[1]);
  ASSERT_TRUE(GetNode("mid")->dirty());
  ASSERT_EQ("in",  stats_[2]);
}

TEST_F(StatTest, Tree) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build out: cat mid1 mid2\n"
"build mid1: cat in11 in12\n"
"build mid2: cat in21 in22\n"));

  Node* out = GetNode("out");
  std::string err;
  EXPECT_TRUE(out->Stat(this, &err));
  EXPECT_EQ("", err);
  ASSERT_EQ(1u, stats_.size());
  scan_.RecomputeDirty(out, nullptr);
  ASSERT_EQ(1u + 6u, stats_.size());
  ASSERT_EQ("mid1", stats_[1]);
  ASSERT_TRUE(GetNode("mid1")->dirty());
  ASSERT_EQ("in11", stats_[2]);
}

TEST_F(StatTest, Middle) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build out: cat mid\n"
"build mid: cat in\n"));

  mtimes_["in"] = TimeStamp(TimeStamp::duration(1));
  mtimes_["mid"] = TimeStamp::min();  // missing
  mtimes_["out"] = TimeStamp(TimeStamp::duration(1));

  Node* out = GetNode("out");
  std::string err;
  EXPECT_TRUE(out->Stat(this, &err));
  EXPECT_EQ("", err);
  ASSERT_EQ(1u, stats_.size());
  scan_.RecomputeDirty(out, nullptr);
  ASSERT_FALSE(GetNode("in")->dirty());
  ASSERT_TRUE(GetNode("mid")->dirty());
  ASSERT_TRUE(GetNode("out")->dirty());
}

}  // namespace
