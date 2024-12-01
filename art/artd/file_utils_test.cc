/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "file_utils.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <memory>
#include <string>

#include "aidl/com/android/server/art/FsPermission.h"
#include "android-base/errors.h"
#include "android-base/file.h"
#include "android-base/result-gmock.h"
#include "android-base/result.h"
#include "base/common_art_test.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace art {
namespace artd {
namespace {

using ::aidl::com::android::server::art::FsPermission;
using ::android::base::Error;
using ::android::base::ReadFileToString;
using ::android::base::Result;
using ::android::base::WriteStringToFd;
using ::android::base::WriteStringToFile;
using ::android::base::testing::HasError;
using ::android::base::testing::HasValue;
using ::android::base::testing::Ok;
using ::android::base::testing::WithMessage;
using ::testing::ContainsRegex;
using ::testing::IsEmpty;
using ::testing::NotNull;

void CheckContent(const std::string& path, const std::string& expected_content) {
  std::string actual_content;
  ASSERT_TRUE(ReadFileToString(path, &actual_content));
  EXPECT_EQ(actual_content, expected_content);
}

// A file that will always fail on `Keep`.
class UnkeepableFile : public NewFile {
 public:
  static Result<std::unique_ptr<UnkeepableFile>> Create(const std::string& path,
                                                        const FsPermission& fs_permission) {
    std::unique_ptr<NewFile> new_file = OR_RETURN(NewFile::Create(path, fs_permission));
    return std::unique_ptr<UnkeepableFile>(new UnkeepableFile(std::move(*new_file)));
  }

  Result<void> Keep() override { return Error() << "Unkeepable file"; }

 private:
  explicit UnkeepableFile(NewFile&& other) : NewFile(std::move(other)) {}
};

class FileUtilsTest : public CommonArtTest {
 protected:
  void SetUp() override {
    CommonArtTest::SetUp();
    scratch_dir_ = std::make_unique<ScratchDir>();
    struct stat st;
    ASSERT_EQ(stat(scratch_dir_->GetPath().c_str(), &st), 0);
    fs_permission_ = FsPermission{.uid = static_cast<int32_t>(st.st_uid),
                                  .gid = static_cast<int32_t>(st.st_gid)};
  }

  void TearDown() override {
    scratch_dir_.reset();
    CommonArtTest::TearDown();
  }

  FsPermission fs_permission_;
  std::unique_ptr<ScratchDir> scratch_dir_;
};

TEST_F(FileUtilsTest, NewFileCreate) {
  std::string path = scratch_dir_->GetPath() + "/file.tmp";

  Result<std::unique_ptr<NewFile>> new_file = NewFile::Create(path, fs_permission_);
  ASSERT_THAT(new_file, HasValue(NotNull()));
  EXPECT_GE((*new_file)->Fd(), 0);
  EXPECT_EQ((*new_file)->FinalPath(), path);
  EXPECT_THAT((*new_file)->TempPath(), Not(IsEmpty()));
  EXPECT_THAT((*new_file)->TempId(), Not(IsEmpty()));

  EXPECT_FALSE(std::filesystem::exists((*new_file)->FinalPath()));
  EXPECT_TRUE(std::filesystem::exists((*new_file)->TempPath()));
}

TEST_F(FileUtilsTest, NewFileCreateNonExistentDir) {
  std::string path = scratch_dir_->GetPath() + "/non_existent_dir/file.tmp";

  EXPECT_THAT(NewFile::Create(path, fs_permission_),
              HasError(WithMessage(
                  ContainsRegex("Failed to create temp file for .*/non_existent_dir/file.tmp"))));
}

TEST_F(FileUtilsTest, NewFileExplicitCleanup) {
  std::string path = scratch_dir_->GetPath() + "/file.tmp";
  std::unique_ptr<NewFile> new_file = OR_FATAL(NewFile::Create(path, fs_permission_));
  new_file->Cleanup();

  EXPECT_FALSE(std::filesystem::exists(path));
  EXPECT_FALSE(std::filesystem::exists(new_file->TempPath()));
}

TEST_F(FileUtilsTest, NewFileImplicitCleanup) {
  std::string path = scratch_dir_->GetPath() + "/file.tmp";
  std::string temp_path;

  // Cleanup on object destruction.
  {
    std::unique_ptr<NewFile> new_file = OR_FATAL(NewFile::Create(path, fs_permission_));
    temp_path = new_file->TempPath();
  }

  EXPECT_FALSE(std::filesystem::exists(path));
  EXPECT_FALSE(std::filesystem::exists(temp_path));
}

TEST_F(FileUtilsTest, NewFileCommit) {
  std::string path = scratch_dir_->GetPath() + "/file.tmp";
  std::string temp_path;

  {
    std::unique_ptr<NewFile> new_file = OR_FATAL(NewFile::Create(path, fs_permission_));
    temp_path = new_file->TempPath();
    new_file->CommitOrAbandon();
  }

  EXPECT_TRUE(std::filesystem::exists(path));
  EXPECT_FALSE(std::filesystem::exists(temp_path));
}

TEST_F(FileUtilsTest, NewFileCommitAllNoOldFile) {
  std::string file_1_path = scratch_dir_->GetPath() + "/file_1";
  std::string file_2_path = scratch_dir_->GetPath() + "/file_2";

  std::unique_ptr<NewFile> new_file_1 = OR_FATAL(NewFile::Create(file_1_path, fs_permission_));
  std::unique_ptr<NewFile> new_file_2 = OR_FATAL(NewFile::Create(file_2_path, fs_permission_));

  ASSERT_TRUE(WriteStringToFd("new_file_1", new_file_1->Fd()));
  ASSERT_TRUE(WriteStringToFd("new_file_2", new_file_2->Fd()));

  EXPECT_THAT(NewFile::CommitAllOrAbandon({new_file_1.get(), new_file_2.get()}), Ok());

  // New files are committed.
  CheckContent(file_1_path, "new_file_1");
  CheckContent(file_2_path, "new_file_2");

  // New files are no longer at the temporary paths.
  EXPECT_FALSE(std::filesystem::exists(new_file_1->TempPath()));
  EXPECT_FALSE(std::filesystem::exists(new_file_2->TempPath()));
}

TEST_F(FileUtilsTest, NewFileCommitAllReplacesOldFiles) {
  std::string file_1_path = scratch_dir_->GetPath() + "/file_1";
  std::string file_2_path = scratch_dir_->GetPath() + "/file_2";

  ASSERT_TRUE(WriteStringToFile("old_file_1", file_1_path));
  ASSERT_TRUE(WriteStringToFile("old_file_2", file_2_path));

  std::unique_ptr<NewFile> new_file_1 = OR_FATAL(NewFile::Create(file_1_path, fs_permission_));
  std::unique_ptr<NewFile> new_file_2 = OR_FATAL(NewFile::Create(file_2_path, fs_permission_));

  ASSERT_TRUE(WriteStringToFd("new_file_1", new_file_1->Fd()));
  ASSERT_TRUE(WriteStringToFd("new_file_2", new_file_2->Fd()));

  EXPECT_THAT(NewFile::CommitAllOrAbandon({new_file_1.get(), new_file_2.get()}), Ok());

  // New files are committed.
  CheckContent(file_1_path, "new_file_1");
  CheckContent(file_2_path, "new_file_2");

  // New files are no longer at the temporary paths.
  EXPECT_FALSE(std::filesystem::exists(new_file_1->TempPath()));
  EXPECT_FALSE(std::filesystem::exists(new_file_2->TempPath()));
}

TEST_F(FileUtilsTest, NewFileCommitAllReplacesLessOldFiles) {
  std::string file_1_path = scratch_dir_->GetPath() + "/file_1";
  std::string file_2_path = scratch_dir_->GetPath() + "/file_2";

  ASSERT_TRUE(WriteStringToFile("old_file_1", file_1_path));  // No old_file_2.

  std::unique_ptr<NewFile> new_file_1 = OR_FATAL(NewFile::Create(file_1_path, fs_permission_));
  std::unique_ptr<NewFile> new_file_2 = OR_FATAL(NewFile::Create(file_2_path, fs_permission_));

  ASSERT_TRUE(WriteStringToFd("new_file_1", new_file_1->Fd()));
  ASSERT_TRUE(WriteStringToFd("new_file_2", new_file_2->Fd()));

  EXPECT_THAT(NewFile::CommitAllOrAbandon({new_file_1.get(), new_file_2.get()}), Ok());

  // New files are committed.
  CheckContent(file_1_path, "new_file_1");
  CheckContent(file_2_path, "new_file_2");

  // New files are no longer at the temporary paths.
  EXPECT_FALSE(std::filesystem::exists(new_file_1->TempPath()));
  EXPECT_FALSE(std::filesystem::exists(new_file_2->TempPath()));
}

TEST_F(FileUtilsTest, NewFileCommitAllReplacesMoreOldFiles) {
  std::string file_1_path = scratch_dir_->GetPath() + "/file_1";
  std::string file_2_path = scratch_dir_->GetPath() + "/file_2";
  std::string file_3_path = scratch_dir_->GetPath() + "/file_3";

  ASSERT_TRUE(WriteStringToFile("old_file_1", file_1_path));
  ASSERT_TRUE(WriteStringToFile("old_file_2", file_2_path));
  ASSERT_TRUE(WriteStringToFile("old_file_3", file_3_path));  // Extra file.

  std::unique_ptr<NewFile> new_file_1 = OR_FATAL(NewFile::Create(file_1_path, fs_permission_));
  std::unique_ptr<NewFile> new_file_2 = OR_FATAL(NewFile::Create(file_2_path, fs_permission_));

  ASSERT_TRUE(WriteStringToFd("new_file_1", new_file_1->Fd()));
  ASSERT_TRUE(WriteStringToFd("new_file_2", new_file_2->Fd()));

  EXPECT_THAT(NewFile::CommitAllOrAbandon({new_file_1.get(), new_file_2.get()}, {file_3_path}),
              Ok());

  // New files are committed.
  CheckContent(file_1_path, "new_file_1");
  CheckContent(file_2_path, "new_file_2");
  EXPECT_FALSE(std::filesystem::exists(file_3_path));  // Extra file removed.

  // New files are no longer at the temporary paths.
  EXPECT_FALSE(std::filesystem::exists(new_file_1->TempPath()));
  EXPECT_FALSE(std::filesystem::exists(new_file_2->TempPath()));
}

TEST_F(FileUtilsTest, NewFileCommitAllFailedToKeep) {
  std::string file_1_path = scratch_dir_->GetPath() + "/file_1";
  std::string file_2_path = scratch_dir_->GetPath() + "/file_2";
  std::string file_3_path = scratch_dir_->GetPath() + "/file_3";

  ASSERT_TRUE(WriteStringToFile("old_file_1", file_1_path));
  ASSERT_TRUE(WriteStringToFile("old_file_2", file_2_path));
  ASSERT_TRUE(WriteStringToFile("old_file_3", file_3_path));  // Extra file.

  std::unique_ptr<NewFile> new_file_1 = OR_FATAL(NewFile::Create(file_1_path, fs_permission_));
  // Unkeepable file.
  std::unique_ptr<NewFile> new_file_2 =
      OR_FATAL(UnkeepableFile::Create(file_2_path, fs_permission_));

  ASSERT_TRUE(WriteStringToFd("new_file_1", new_file_1->Fd()));
  ASSERT_TRUE(WriteStringToFd("new_file_2", new_file_2->Fd()));

  EXPECT_THAT(NewFile::CommitAllOrAbandon({new_file_1.get(), new_file_2.get()}, {file_3_path}),
              HasError(WithMessage("Unkeepable file")));

  // Old files are fine.
  CheckContent(file_1_path, "old_file_1");
  CheckContent(file_2_path, "old_file_2");
  CheckContent(file_3_path, "old_file_3");

  // New files are abandoned.
  EXPECT_FALSE(std::filesystem::exists(new_file_1->TempPath()));
  EXPECT_FALSE(std::filesystem::exists(new_file_2->TempPath()));
}

TEST_F(FileUtilsTest, NewFileCommitAllFailedToCommit) {
  std::string dir_1_path = scratch_dir_->GetPath() + "/dir_1";
  std::string dir_2_path = scratch_dir_->GetPath() + "/dir_2";

  ASSERT_TRUE(std::filesystem::create_directory(dir_1_path));
  ASSERT_TRUE(std::filesystem::create_directory(dir_2_path));

  std::string file_1_path = dir_1_path + "/file_1";
  std::string file_2_path = dir_2_path + "/file_2";

  std::unique_ptr<NewFile> new_file_1 = OR_FATAL(NewFile::Create(file_1_path, fs_permission_));
  std::unique_ptr<NewFile> new_file_2 = OR_FATAL(NewFile::Create(file_2_path, fs_permission_));

  ASSERT_TRUE(WriteStringToFd("new_file_1", new_file_1->Fd()));
  ASSERT_TRUE(WriteStringToFd("new_file_2", new_file_2->Fd()));

  {
    // Make `new_file_2` fail to commit.
    auto scoped_inaccessible = ScopedInaccessible(dir_2_path);
    std::filesystem::permissions(
        dir_2_path, std::filesystem::perms::owner_exec, std::filesystem::perm_options::add);
    auto scoped_unroot = ScopedUnroot();

    EXPECT_THAT(NewFile::CommitAllOrAbandon({new_file_1.get(), new_file_2.get()}),
                HasError(WithMessage(ContainsRegex("Failed to move file .*file_2.*"))));
  }

  // Files are abandoned at best effort. File 1 is abandoned, but file 2 cannot be abandoned due to
  // permission denied.
  EXPECT_FALSE(std::filesystem::exists(new_file_1->TempPath()));
  EXPECT_FALSE(std::filesystem::exists(new_file_1->FinalPath()));
  EXPECT_TRUE(std::filesystem::exists(new_file_2->TempPath()));
  EXPECT_FALSE(std::filesystem::exists(new_file_2->FinalPath()));
}

TEST_F(FileUtilsTest, NewFileCommitAllFailedToMoveOldFile) {
  std::string file_1_path = scratch_dir_->GetPath() + "/file_1";
  std::string file_2_path = scratch_dir_->GetPath() + "/file_2";
  std::filesystem::create_directory(file_2_path);
  std::string file_3_path = scratch_dir_->GetPath() + "/file_3";

  ASSERT_TRUE(WriteStringToFile("old_file_1", file_1_path));
  ASSERT_TRUE(WriteStringToFile("old_file_3", file_3_path));  // Extra file.

  std::unique_ptr<NewFile> new_file_1 = OR_FATAL(NewFile::Create(file_1_path, fs_permission_));
  std::unique_ptr<NewFile> new_file_2 = OR_FATAL(NewFile::Create(file_2_path, fs_permission_));

  ASSERT_TRUE(WriteStringToFd("new_file_1", new_file_1->Fd()));
  ASSERT_TRUE(WriteStringToFd("new_file_2", new_file_2->Fd()));

  // file_2 is not movable because it is a directory.
  EXPECT_THAT(NewFile::CommitAllOrAbandon({new_file_1.get(), new_file_2.get()}, {file_3_path}),
              HasError(WithMessage(ContainsRegex("Old file '.*/file_2' is a directory"))));

  // Old files are fine.
  CheckContent(file_1_path, "old_file_1");
  EXPECT_TRUE(std::filesystem::is_directory(file_2_path));
  CheckContent(file_3_path, "old_file_3");

  // New files are abandoned.
  EXPECT_FALSE(std::filesystem::exists(new_file_1->TempPath()));
  EXPECT_FALSE(std::filesystem::exists(new_file_2->TempPath()));
}

TEST_F(FileUtilsTest, BuildTempPath) {
  EXPECT_EQ(NewFile::BuildTempPath("/a/b/original_path", "123456"),
            "/a/b/original_path.123456.tmp");
}

TEST_F(FileUtilsTest, OpenFileForReading) {
  std::string path = scratch_dir_->GetPath() + "/foo";
  ASSERT_TRUE(WriteStringToFile("foo", path));

  EXPECT_THAT(OpenFileForReading(path), HasValue(NotNull()));
}

TEST_F(FileUtilsTest, OpenFileForReadingFailed) {
  std::string path = scratch_dir_->GetPath() + "/foo";

  EXPECT_THAT(OpenFileForReading(path),
              HasError(WithMessage(ContainsRegex("Failed to open file .*/foo"))));
}

TEST_F(FileUtilsTest, FileFsPermissionToMode) {
  EXPECT_EQ(FileFsPermissionToMode(FsPermission{}), S_IRUSR | S_IWUSR | S_IRGRP);
  EXPECT_EQ(FileFsPermissionToMode(FsPermission{.isOtherReadable = true}),
            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
  EXPECT_EQ(FileFsPermissionToMode(FsPermission{.isOtherExecutable = true}),
            S_IRUSR | S_IWUSR | S_IRGRP | S_IXOTH);
  EXPECT_EQ(
      FileFsPermissionToMode(FsPermission{.isOtherReadable = true, .isOtherExecutable = true}),
      S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH | S_IXOTH);
}

TEST_F(FileUtilsTest, DirFsPermissionToMode) {
  EXPECT_EQ(DirFsPermissionToMode(FsPermission{}), S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP);
  EXPECT_EQ(DirFsPermissionToMode(FsPermission{.isOtherReadable = true}),
            S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH);
  EXPECT_EQ(DirFsPermissionToMode(FsPermission{.isOtherExecutable = true}),
            S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IXOTH);
  EXPECT_EQ(DirFsPermissionToMode(FsPermission{.isOtherReadable = true, .isOtherExecutable = true}),
            S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
}

}  // namespace
}  // namespace artd
}  // namespace art
