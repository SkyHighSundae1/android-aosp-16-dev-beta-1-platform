/*
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "tools/tools.h"

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#include <filesystem>

#include "android-base/file.h"
#include "android-base/result.h"
#include "base/common_art_test.h"
#include "base/globals.h"
#include "base/pidfd.h"
#include "base/time_utils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "tools/testing.h"

namespace art {
namespace tools {
namespace {

using ::android::base::Result;
using ::android::base::WriteStringToFile;
using ::testing::UnorderedElementsAre;

void CreateFile(const std::string& filename, const std::string& content = "") {
  std::filesystem::path path(filename);
  std::filesystem::create_directories(path.parent_path());
  ASSERT_TRUE(WriteStringToFile(content, filename));
}

class ArtToolsTest : public CommonArtTest {
 protected:
  void SetUp() override {
    CommonArtTest::SetUp();
    scratch_dir_ = std::make_unique<ScratchDir>();
    scratch_path_ = scratch_dir_->GetPath();
    // Remove the trailing '/';
    scratch_path_.resize(scratch_path_.length() - 1);
  }

  void TearDown() override {
    scratch_dir_.reset();
    CommonArtTest::TearDown();
  }

  std::unique_ptr<ScratchDir> scratch_dir_;
  std::string scratch_path_;
};

TEST_F(ArtToolsTest, Glob) {
  CreateFile(scratch_path_ + "/abc/def/000.txt");
  CreateFile(scratch_path_ + "/abc/def/ghi/123.txt");
  CreateFile(scratch_path_ + "/abc/def/ghi/456.txt");
  CreateFile(scratch_path_ + "/abc/def/ghi/456.pdf");
  CreateFile(scratch_path_ + "/abc/def/ghi/jkl/456.txt");
  CreateFile(scratch_path_ + "/789.txt");
  CreateFile(scratch_path_ + "/abc/789.txt");
  CreateFile(scratch_path_ + "/abc/aaa/789.txt");
  CreateFile(scratch_path_ + "/abc/aaa/bbb/789.txt");
  CreateFile(scratch_path_ + "/abc/mno/123.txt");
  CreateFile(scratch_path_ + "/abc/aaa/mno/123.txt");
  CreateFile(scratch_path_ + "/abc/aaa/bbb/mno/123.txt");
  CreateFile(scratch_path_ + "/abc/aaa/bbb/mno/ccc/123.txt");
  CreateFile(scratch_path_ + "/pqr/123.txt");
  CreateFile(scratch_path_ + "/abc/pqr/123.txt");
  CreateFile(scratch_path_ + "/abc/aaa/pqr/123.txt");
  CreateFile(scratch_path_ + "/abc/aaa/bbb/pqr/123.txt");
  CreateFile(scratch_path_ + "/abc/aaa/bbb/pqr/ccc/123.txt");
  CreateFile(scratch_path_ + "/abc/aaa/bbb/pqr/ccc/ddd/123.txt");

  // This symlink will cause infinite recursion. It should not be followed.
  std::filesystem::create_directory_symlink(scratch_path_ + "/abc/aaa/bbb/pqr",
                                            scratch_path_ + "/abc/aaa/bbb/pqr/lnk");

  // This is a directory. It should not be included in the results.
  std::filesystem::create_directory(scratch_path_ + "/abc/def/ghi/000.txt");

  std::vector<std::string> patterns = {
      scratch_path_ + "/abc/def/000.txt",
      scratch_path_ + "/abc/def/ghi/*.txt",
      scratch_path_ + "/abc/**/789.txt",
      scratch_path_ + "/abc/**/mno/*.txt",
      scratch_path_ + "/abc/**/pqr/**",
  };

  EXPECT_THAT(Glob(patterns, scratch_path_),
              UnorderedElementsAre(scratch_path_ + "/abc/def/000.txt",
                                   scratch_path_ + "/abc/def/ghi/123.txt",
                                   scratch_path_ + "/abc/def/ghi/456.txt",
                                   scratch_path_ + "/abc/789.txt",
                                   scratch_path_ + "/abc/aaa/789.txt",
                                   scratch_path_ + "/abc/aaa/bbb/789.txt",
                                   scratch_path_ + "/abc/mno/123.txt",
                                   scratch_path_ + "/abc/aaa/mno/123.txt",
                                   scratch_path_ + "/abc/aaa/bbb/mno/123.txt",
                                   scratch_path_ + "/abc/pqr/123.txt",
                                   scratch_path_ + "/abc/aaa/pqr/123.txt",
                                   scratch_path_ + "/abc/aaa/bbb/pqr/123.txt",
                                   scratch_path_ + "/abc/aaa/bbb/pqr/ccc/123.txt",
                                   scratch_path_ + "/abc/aaa/bbb/pqr/ccc/ddd/123.txt"));
}

TEST_F(ArtToolsTest, EscapeGlob) {
  CreateFile(scratch_path_ + "/**");
  CreateFile(scratch_path_ + "/*.txt");
  CreateFile(scratch_path_ + "/?.txt");
  CreateFile(scratch_path_ + "/[a-z].txt");
  CreateFile(scratch_path_ + "/**.txt");
  CreateFile(scratch_path_ + "/??.txt");
  CreateFile(scratch_path_ + "/[a-z[a-z]][a-z].txt");

  // Paths that shouldn't be matched if the paths above are escaped.
  CreateFile(scratch_path_ + "/abc/b.txt");
  CreateFile(scratch_path_ + "/b.txt");
  CreateFile(scratch_path_ + "/*b.txt");
  CreateFile(scratch_path_ + "/?b.txt");
  CreateFile(scratch_path_ + "/[a-zb]b.txt");

  // Verifies that the escaped path only matches the given path.
  auto verify_escape = [this](const std::string& file) {
    EXPECT_THAT(Glob({EscapeGlob(file)}, scratch_path_), UnorderedElementsAre(file));
  };

  verify_escape(scratch_path_ + "/**");
  verify_escape(scratch_path_ + "/*.txt");
  verify_escape(scratch_path_ + "/?.txt");
  verify_escape(scratch_path_ + "/[a-z].txt");
  verify_escape(scratch_path_ + "/**.txt");
  verify_escape(scratch_path_ + "/**");
  verify_escape(scratch_path_ + "/??.txt");
  verify_escape(scratch_path_ + "/[a-z[a-z]][a-z].txt");
}

TEST_F(ArtToolsTest, PathStartsWith) {
  EXPECT_TRUE(PathStartsWith("/a/b", "/a"));
  EXPECT_TRUE(PathStartsWith("/a/b", "/a/"));

  EXPECT_FALSE(PathStartsWith("/a/c", "/a/b"));
  EXPECT_FALSE(PathStartsWith("/ab", "/a"));

  EXPECT_TRUE(PathStartsWith("/a", "/a"));
  EXPECT_TRUE(PathStartsWith("/a/", "/a"));
  EXPECT_TRUE(PathStartsWith("/a", "/a/"));

  EXPECT_TRUE(PathStartsWith("/a", "/"));
  EXPECT_TRUE(PathStartsWith("/", "/"));
  EXPECT_FALSE(PathStartsWith("/", "/a"));
}

class ArtToolsEnsureNoProcessInDirTest : public ArtToolsTest {
 protected:
  void SetUp() override {
    ArtToolsTest::SetUp();

    if (!PidfdOpen(getpid(), /*flags=*/0).ok() && errno == ENOSYS) {
      GTEST_SKIP() << "'pidfd_open' is not available";
    }

    related_dir_ = scratch_path_ + "/related";
    unrelated_dir_ = scratch_path_ + "/unrelated";

    std::string sleep_bin = GetSleepBin();
    if (sleep_bin.empty()) {
      GTEST_SKIP() << "'sleep' is not available";
    }

    std::filesystem::create_directories(related_dir_);
    std::filesystem::create_directories(unrelated_dir_);
    std::filesystem::copy(sleep_bin, related_dir_ + "/sleep");
    std::filesystem::copy(sleep_bin, unrelated_dir_ + "/sleep");
  }

  std::string related_dir_;
  std::string unrelated_dir_;

 private:
  std::string GetSleepBin() {
    if constexpr (kIsTargetAndroid) {
      return GetBin("sleep");
    }
    if (access("/usr/bin/sleep", X_OK) == 0) {
      return "/usr/bin/sleep";
    }
    return "";
  }
};

TEST_F(ArtToolsEnsureNoProcessInDirTest, WaitsProcesses) {
  std::vector<std::string> args_1{related_dir_ + "/sleep", "0.3"};
  auto [pid_1, scope_guard_1] = ScopedExec(args_1, /*wait=*/false);
  std::vector<std::string> args_2{unrelated_dir_ + "/sleep", "2"};
  auto [pid_2, scope_guard_2] = ScopedExec(args_2, /*wait=*/false);
  NanoSleep(100'000'000);  // Wait for child processes to exec.

  ASSERT_RESULT_OK(EnsureNoProcessInDir(related_dir_, /*timeout_ms=*/5000, /*try_kill=*/false));

  // Check the current status of the process with `WNOHANG`. The related process should have exited,
  // so `si_signo` should be `SIGCHLD`.
  siginfo_t info;
  ASSERT_EQ(TEMP_FAILURE_RETRY(waitid(P_PID, pid_1, &info, WEXITED | WNOWAIT | WNOHANG)), 0);
  EXPECT_EQ(info.si_signo, SIGCHLD);
  EXPECT_EQ(info.si_code, CLD_EXITED);
  EXPECT_EQ(info.si_status, 0);

  // The method should not wait on unrelated processes. The unrelated process should not have
  // exited, so `si_signo` should be 0.
  ASSERT_EQ(TEMP_FAILURE_RETRY(waitid(P_PID, pid_2, &info, WEXITED | WNOWAIT | WNOHANG)), 0);
  EXPECT_EQ(info.si_signo, 0);
}

TEST_F(ArtToolsEnsureNoProcessInDirTest, TimesOut) {
  std::vector<std::string> args{related_dir_ + "/sleep", "5"};
  auto [pid, scope_guard] = ScopedExec(args, /*wait=*/false);
  NanoSleep(100'000'000);  // Wait for child processes to exec.

  Result<void> result = EnsureNoProcessInDir(related_dir_, /*timeout_ms=*/200, /*try_kill=*/false);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.error().message(), "Some process(es) are still running after 200ms");

  // The process should not have exited.
  siginfo_t info;
  ASSERT_EQ(TEMP_FAILURE_RETRY(waitid(P_PID, pid, &info, WEXITED | WNOWAIT | WNOHANG)), 0);
  EXPECT_EQ(info.si_signo, 0);
}

TEST_F(ArtToolsEnsureNoProcessInDirTest, KillsProcesses) {
  std::vector<std::string> args_1{related_dir_ + "/sleep", "5"};
  auto [pid_1, scope_guard_1] = ScopedExec(args_1, /*wait=*/false);
  std::vector<std::string> args_2{unrelated_dir_ + "/sleep", "5"};
  auto [pid_2, scope_guard_2] = ScopedExec(args_2, /*wait=*/false);
  NanoSleep(100'000'000);  // Wait for child processes to exec.

  ASSERT_RESULT_OK(EnsureNoProcessInDir(related_dir_, /*timeout_ms=*/200, /*try_kill=*/true));

  // The related process should have been killed.
  siginfo_t info;
  ASSERT_EQ(TEMP_FAILURE_RETRY(waitid(P_PID, pid_1, &info, WEXITED | WNOWAIT | WNOHANG)), 0);
  EXPECT_EQ(info.si_signo, SIGCHLD);
  EXPECT_EQ(info.si_code, CLD_KILLED);
  EXPECT_EQ(info.si_status, SIGKILL);

  // The unrelated process should still be running.
  ASSERT_EQ(TEMP_FAILURE_RETRY(waitid(P_PID, pid_2, &info, WEXITED | WNOWAIT | WNOHANG)), 0);
  EXPECT_EQ(info.si_signo, 0);
}

}  // namespace
}  // namespace tools
}  // namespace art
