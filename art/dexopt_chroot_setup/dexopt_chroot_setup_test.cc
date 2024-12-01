/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "dexopt_chroot_setup.h"

#include <unistd.h>

#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

#include "aidl/com/android/server/art/BnDexoptChrootSetup.h"
#include "android-base/file.h"
#include "android-base/properties.h"
#include "android-base/result-gmock.h"
#include "android-base/scopeguard.h"
#include "android/binder_auto_utils.h"
#include "base/common_art_test.h"
#include "base/macros.h"
#include "exec_utils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "tools/binder_utils.h"
#include "tools/cmdline_builder.h"

namespace art {
namespace dexopt_chroot_setup {
namespace {

using ::android::base::GetProperty;
using ::android::base::ScopeGuard;
using ::android::base::SetProperty;
using ::android::base::WaitForProperty;
using ::android::base::testing::HasError;
using ::android::base::testing::HasValue;
using ::android::base::testing::WithMessage;
using ::art::tools::CmdlineBuilder;

class DexoptChrootSetupTest : public CommonArtTest {
 protected:
  void SetUp() override {
    CommonArtTest::SetUp();
    dexopt_chroot_setup_ = ndk::SharedRefBase::make<DexoptChrootSetup>();

    // Note that if a real Pre-reboot Dexopt is kicked off after this check, the test will still
    // fail, but that should be very rare.
    if (std::filesystem::exists(DexoptChrootSetup::CHROOT_DIR)) {
      GTEST_SKIP() << "A real Pre-reboot Dexopt is running";
    }

    ASSERT_TRUE(
        WaitForProperty("dev.bootcomplete", "1", /*relative_timeout=*/std::chrono::minutes(3)));

    test_skipped_ = false;

    scratch_dir_ = std::make_unique<ScratchDir>();
    scratch_path_ = scratch_dir_->GetPath();
    // Remove the trailing '/';
    scratch_path_.resize(scratch_path_.length() - 1);

    partitions_sysprop_value_ = GetProperty(kAdditionalPartitionsSysprop, /*default_value=*/"");
    ASSERT_TRUE(SetProperty(kAdditionalPartitionsSysprop, "odm:/odm,system_dlkm:/system_dlkm"));
    partitions_sysprop_set_ = true;
  }

  void TearDown() override {
    if (test_skipped_) {
      return;
    }
    if (partitions_sysprop_set_ &&
        !SetProperty(kAdditionalPartitionsSysprop, partitions_sysprop_value_)) {
      LOG(ERROR) << ART_FORMAT("Failed to recover sysprop '{}'", kAdditionalPartitionsSysprop);
    }
    scratch_dir_.reset();
    dexopt_chroot_setup_->tearDown(/*in_allowConcurrent=*/false);
    CommonArtTest::TearDown();
  }

  std::shared_ptr<DexoptChrootSetup> dexopt_chroot_setup_;
  std::unique_ptr<ScratchDir> scratch_dir_;
  std::string scratch_path_;
  bool test_skipped_ = true;
  std::string partitions_sysprop_value_;
  bool partitions_sysprop_set_ = false;
};

TEST_F(DexoptChrootSetupTest, Run) {
  // We only test the Mainline update case here. There isn't an easy way to test the OTA update case
  // in such a unit test. The OTA update case is assumed to be covered by the E2E test.
  ASSERT_STATUS_OK(
      dexopt_chroot_setup_->setUp(/*in_otaSlot=*/std::nullopt, /*in_mapSnapshotsForOta=*/false));
  ASSERT_STATUS_OK(dexopt_chroot_setup_->init());

  std::string mounts;
  ASSERT_TRUE(android::base::ReadFileToString("/proc/mounts", &mounts, /*follow_symlinks=*/true));

  // Some important dirs that should be the same as outside.
  std::vector<const char*> same_dirs = {
      "/",
      "/system",
      "/system_ext",
      "/vendor",
      "/product",
      "/data",
      "/mnt/expand",
      "/dev",
      "/dev/cpuctl",
      "/dev/cpuset",
      "/proc",
      "/sys",
      "/sys/fs/cgroup",
      "/sys/fs/selinux",
      "/metadata",
      "/odm",
      "/system_dlkm",
  };

  for (const std::string& dir : same_dirs) {
    struct stat st_outside;
    ASSERT_EQ(stat(dir.c_str(), &st_outside), 0);
    struct stat st_inside;
    ASSERT_EQ(stat(PathInChroot(dir).c_str(), &st_inside), 0);
    EXPECT_EQ(std::make_pair(st_outside.st_dev, st_outside.st_ino),
              std::make_pair(st_inside.st_dev, st_inside.st_ino))
        << ART_FORMAT("Unexpected different directory in chroot: '{}'\n", dir) << mounts;
  }

  // Some important dirs that are expected to be writable.
  std::vector<const char*> writable_dirs = {
      "/data",
      "/mnt/expand",
  };

  for (const std::string& dir : writable_dirs) {
    EXPECT_EQ(access(PathInChroot(dir).c_str(), W_OK), 0);
  }

  // Some important dirs that are not the same as outside but should be prepared.
  std::vector<const char*> prepared_dirs = {
      "/apex/com.android.art",
      "/linkerconfig/com.android.art",
  };

  for (const std::string& dir : prepared_dirs) {
    EXPECT_FALSE(std::filesystem::is_empty(PathInChroot(dir)));
  }

  EXPECT_TRUE(std::filesystem::is_directory(PathInChroot("/mnt/artd_tmp")));

  // Check that the chroot environment is capable to run programs. `dex2oat` is arbitrarily picked
  // here. The test dex file and the scratch dir in /data are the same inside the chroot as outside.
  CmdlineBuilder args;
  args.Add(GetArtBinDir() + "/art_exec")
      .Add("--chroot=%s", DexoptChrootSetup::CHROOT_DIR)
      .Add("--")
      .Add(GetArtBinDir() + "/dex2oat" + (Is64BitInstructionSet(kRuntimeISA) ? "64" : "32"))
      .Add("--dex-file=%s", GetTestDexFileName("Main"))
      .Add("--oat-file=%s", scratch_path_ + "/output.odex")
      .Add("--output-vdex=%s", scratch_path_ + "/output.vdex")
      .Add("--compiler-filter=speed")
      .Add("--boot-image=/nonx/boot.art");
  std::string error_msg;
  EXPECT_TRUE(Exec(args.Get(), &error_msg)) << error_msg;

  // Check that `setUp` can be repeatedly called, to simulate the case where an instance of the
  // caller (typically system_server) called `setUp` and crashed later, and a new instance called
  // `setUp` again.
  ASSERT_STATUS_OK(
      dexopt_chroot_setup_->setUp(/*in_otaSlot=*/std::nullopt, /*in_mapSnapshotsForOta=*/false));
  ASSERT_STATUS_OK(dexopt_chroot_setup_->init());

  // Check that `init` cannot be repeatedly called.
  ndk::ScopedAStatus status = dexopt_chroot_setup_->init();
  EXPECT_FALSE(status.isOk());
  EXPECT_EQ(status.getExceptionCode(), EX_ILLEGAL_STATE);
  EXPECT_STREQ(status.getMessage(), "init must not be repeatedly called");

  ASSERT_STATUS_OK(dexopt_chroot_setup_->tearDown(/*in_allowConcurrent=*/false));

  EXPECT_FALSE(std::filesystem::exists(DexoptChrootSetup::CHROOT_DIR));

  // Check that `tearDown` can be repeatedly called too.
  ASSERT_STATUS_OK(dexopt_chroot_setup_->tearDown(/*in_allowConcurrent=*/false));

  // Check that `setUp` can be followed directly by a `tearDown`.
  ASSERT_STATUS_OK(
      dexopt_chroot_setup_->setUp(/*in_otaSlot=*/std::nullopt, /*in_mapSnapshotsForOta=*/false));
  ASSERT_STATUS_OK(dexopt_chroot_setup_->tearDown(/*in_allowConcurrent=*/false));
  EXPECT_FALSE(std::filesystem::exists(DexoptChrootSetup::CHROOT_DIR));
}

TEST(DexoptChrootSetupUnitTest, ConstructLinkerConfigCompatEnvSection) {
  std::string art_linker_config_content = R"(dir.com.android.art = /apex/com.android.art/bin
[com.android.art]
additional.namespaces = com_android_art,system
namespace.default.isolated = true
namespace.default.links = com_android_art,system
namespace.default.link.com_android_art.allow_all_shared_libs = true
namespace.default.link.system.shared_libs = libartpalette-system.so:libbinder_ndk.so:libc.so:libdl.so:libdl_android.so:liblog.so:libm.so
namespace.com_android_art.isolated = true
namespace.com_android_art.visible = true
namespace.com_android_art.search.paths = /apex/com.android.art/${LIB}
namespace.com_android_art.permitted.paths = /apex/com.android.art/${LIB}
namespace.com_android_art.permitted.paths += /system/${LIB}
namespace.com_android_art.permitted.paths += /system_ext/${LIB}
namespace.com_android_art.permitted.paths += /data
namespace.com_android_art.permitted.paths += /apex/com.android.art/javalib
namespace.com_android_art.links = system
namespace.com_android_art.link.system.shared_libs = libartpalette-system.so:libbinder_ndk.so:libc.so:libdl.so:libdl_android.so:liblog.so:libm.so
namespace.system.isolated = true
namespace.system.visible = true
namespace.system.search.paths = /system/${LIB}
namespace.system.search.paths += /system_ext/${LIB}
namespace.system.permitted.paths = /system/${LIB}/drm:/system/${LIB}/extractors:/system/${LIB}/hw
namespace.system.permitted.paths += /system_ext/${LIB}
namespace.system.permitted.paths += /system/framework
namespace.system.permitted.paths += /data
namespace.system.permitted.paths += /apex/com.android.runtime/${LIB}/bionic
namespace.system.permitted.paths += /system/${LIB}/bootstrap
namespace.system.links = com_android_art
namespace.system.link.com_android_art.shared_libs = libdexfile.so:libjdwp.so:libnativebridge.so:libnativehelper.so:libnativeloader.so:libsigchain.so
[some_other_section]
)";

  std::string expected_compat_env_section = R"([com.android.art.compat]
additional.namespaces = com_android_art,system
namespace.default.isolated = true
namespace.default.links = com_android_art,system
namespace.default.link.com_android_art.allow_all_shared_libs = true
namespace.default.link.system.shared_libs = libartpalette-system.so:libbinder_ndk.so:libc.so:libdl.so:libdl_android.so:liblog.so:libm.so
namespace.com_android_art.isolated = true
namespace.com_android_art.visible = true
namespace.com_android_art.search.paths = /apex/com.android.art/${LIB}
namespace.com_android_art.permitted.paths = /apex/com.android.art/${LIB}
namespace.com_android_art.permitted.paths += /mnt/compat_env/system/${LIB}
namespace.com_android_art.permitted.paths += /mnt/compat_env/system_ext/${LIB}
namespace.com_android_art.permitted.paths += /data
namespace.com_android_art.permitted.paths += /apex/com.android.art/javalib
namespace.com_android_art.links = system
namespace.com_android_art.link.system.shared_libs = libartpalette-system.so:libbinder_ndk.so:libc.so:libdl.so:libdl_android.so:liblog.so:libm.so
namespace.system.isolated = true
namespace.system.visible = true
namespace.system.search.paths = /mnt/compat_env/system/${LIB}
namespace.system.search.paths += /mnt/compat_env/system_ext/${LIB}
namespace.system.permitted.paths = /mnt/compat_env/system/${LIB}/drm:/mnt/compat_env/system/${LIB}/extractors:/mnt/compat_env/system/${LIB}/hw
namespace.system.permitted.paths += /mnt/compat_env/system_ext/${LIB}
namespace.system.permitted.paths += /system/framework
namespace.system.permitted.paths += /data
namespace.system.permitted.paths += /apex/com.android.runtime/${LIB}/bionic
namespace.system.permitted.paths += /mnt/compat_env/system/${LIB}/bootstrap
namespace.system.links = com_android_art
namespace.system.link.com_android_art.shared_libs = libdexfile.so:libjdwp.so:libnativebridge.so:libnativehelper.so:libnativeloader.so:libsigchain.so
)";

  EXPECT_THAT(ConstructLinkerConfigCompatEnvSection(art_linker_config_content),
              HasValue(expected_compat_env_section));
}

TEST(DexoptChrootSetupUnitTest, ConstructLinkerConfigCompatEnvSectionNoMatch) {
  std::string art_linker_config_content = R"(dir.com.android.art = /apex/com.android.art/bin
[com.android.art]
additional.namespaces = com_android_art,system
namespace.default.isolated = true
namespace.default.links = com_android_art,system
namespace.default.link.com_android_art.allow_all_shared_libs = true
namespace.default.link.system.shared_libs = libartpalette-system.so:libbinder_ndk.so:libc.so:libdl.so:libdl_android.so:liblog.so:libm.so
namespace.com_android_art.isolated = true
namespace.com_android_art.visible = true
namespace.com_android_art.search.paths = /apex/com.android.art/${LIB}
namespace.com_android_art.permitted.paths = /apex/com.android.art/${LIB}
namespace.com_android_art.permitted.paths += /foo/${LIB}
namespace.com_android_art.permitted.paths += /foo_ext/${LIB}
namespace.com_android_art.permitted.paths += /data
namespace.com_android_art.permitted.paths += /apex/com.android.art/javalib
namespace.com_android_art.links = system
namespace.com_android_art.link.system.shared_libs = libartpalette-system.so:libbinder_ndk.so:libc.so:libdl.so:libdl_android.so:liblog.so:libm.so
namespace.system.isolated = true
namespace.system.visible = true
namespace.system.search.paths = /foo/${LIB}
namespace.system.search.paths += /foo_ext/${LIB}
namespace.system.permitted.paths = /foo/${LIB}/drm:/foo/${LIB}/extractors:/foo/${LIB}/hw
namespace.system.permitted.paths += /foo_ext/${LIB}
namespace.system.permitted.paths += /system/framework
namespace.system.permitted.paths += /data
namespace.system.permitted.paths += /apex/com.android.runtime/${LIB}/bionic
namespace.system.permitted.paths += /foo/${LIB}/bootstrap
namespace.system.links = com_android_art
namespace.system.link.com_android_art.shared_libs = libdexfile.so:libjdwp.so:libnativebridge.so:libnativehelper.so:libnativeloader.so:libsigchain.so
[some_other_section]
)";

  EXPECT_THAT(ConstructLinkerConfigCompatEnvSection(art_linker_config_content),
              HasError(WithMessage("No matching lines to patch in ART linker config")));
}

}  // namespace
}  // namespace dexopt_chroot_setup
}  // namespace art
