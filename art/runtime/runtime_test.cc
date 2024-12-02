/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "common_runtime_test.h"

#include <thread>

#include "android-base/logging.h"
#include "base/locks.h"
#include "base/mutex.h"
#include "oat/elf_file.h"
#include "runtime.h"
#include "thread-current-inl.h"

#ifdef ART_TARGET_ANDROID
#include "android-base/properties.h"
#endif

namespace art HIDDEN {

class RuntimeTest : public CommonRuntimeTest {};

// Ensure that abort works with ThreadList locks held.

TEST_F(RuntimeTest, AbortWithThreadListLockHeld) {
  // This assumes the test is run single-threaded: do not start the runtime to avoid daemon threads.

  constexpr const char* kDeathRegex = "Skipping all-threads dump as locks are held";
  ASSERT_DEATH({
    // The regex only works if we can ensure output goes to stderr.
    android::base::SetLogger(android::base::StderrLogger);

    MutexLock mu(Thread::Current(), *Locks::thread_list_lock_);
    Runtime::Abort("Attempt to abort");
  }, kDeathRegex);
}


TEST_F(RuntimeTest, AbortWithThreadSuspendCountLockHeld) {
  // This assumes the test is run single-threaded: do not start the runtime to avoid daemon threads.

  constexpr const char* kDeathRegex = "Skipping all-threads dump as locks are held";
  ASSERT_DEATH({
    // The regex only works if we can ensure output goes to stderr.
    android::base::SetLogger(android::base::StderrLogger);

    MutexLock mu(Thread::Current(), *Locks::thread_suspend_count_lock_);
    Runtime::Abort("Attempt to abort");
  }, kDeathRegex);
}

TEST_F(RuntimeTest, AbortFromUnattachedThread) {
  // This assumes the test is run single-threaded: do not start the runtime to avoid daemon threads.

  constexpr const char* kDeathRegex = "Going down";
  ASSERT_EXIT({
    // The regex only works if we can ensure output goes to stderr.
    android::base::SetLogger(android::base::StderrLogger);

    Thread::Current()->TransitionFromSuspendedToRunnable();
    runtime_->Start();

    std::thread t([]() {
      LOG(FATAL) << "Going down";
    });
    t.join();
  }, ::testing::KilledBySignal(SIGABRT), kDeathRegex);
}

// It is possible to run tests that validate an existing deployed on-device ART APEX ('standalone'
// tests). If these tests expect to load ELF files with a particular alignment, but those ELF files
// were created with a different alignment, there will be many difficult-to-debug failures. This
// test aims to identify this mismatch, related to whether or not the runtimes were built to be
// page-size agnostic.
TEST_F(RuntimeTest, ElfAlignmentMismatch) {
#ifdef ART_TARGET_ANDROID
  bool platform_pga = android::base::GetBoolProperty("ro.product.build.no_bionic_page_size_macro",
                                                     false);
  if (kPageSizeAgnostic != platform_pga) {
    LOG(WARNING) << "Test configured with kPageSizeAgnostic=" << kPageSizeAgnostic << ", but "
                 << "platform ro.product.build.no_bionic_page_size_macro=" << platform_pga << ".";
  }
#endif
  // Determine the alignment of the ART APEX by reading the alignment of boot.oat.
  std::string core_oat_location = GetSystemImageFilename(GetCoreOatLocation().c_str(),
                                                         kRuntimeQuickCodeISA);
  std::unique_ptr<File> core_oat_file(OS::OpenFileForReading(core_oat_location.c_str()));
  ASSERT_TRUE(core_oat_file.get() != nullptr) << core_oat_location;

  std::string error_msg;
  std::unique_ptr<ElfFile> elf_file(ElfFile::Open(core_oat_file.get(),
                                                  /*writable=*/false,
                                                  /*program_header_only=*/true,
                                                  /*low_4gb=*/false,
                                                  &error_msg));
  ASSERT_TRUE(elf_file != nullptr) << error_msg;
  EXPECT_EQ(kElfSegmentAlignment, elf_file->GetElfSegmentAlignmentFromFile());
}

class RuntimeInitMetricsDefaultTest : public CommonRuntimeTest {};

TEST_F(RuntimeInitMetricsDefaultTest, MetricsAreNotInitialized) {
  ASSERT_FALSE(runtime_->AreMetricsInitialized());
}

class RuntimeInitMetricsZygoteTest : public CommonRuntimeTest {
  void SetUpRuntimeOptions(RuntimeOptions* options) override {
    CommonRuntimeTest::SetUpRuntimeOptions(options);
    options->emplace_back(std::make_pair("-Xzygote", nullptr));
  }
};

TEST_F(RuntimeInitMetricsZygoteTest, MetricsAreInitialized) {
  ASSERT_TRUE(runtime_->AreMetricsInitialized());
}

class RuntimeInitMetricsForceEnableTest : public CommonRuntimeTest {
  void SetUpRuntimeOptions(RuntimeOptions* options) override {
    CommonRuntimeTest::SetUpRuntimeOptions(options);
    options->emplace_back(std::make_pair("-Xmetrics-force-enable:true", nullptr));
  }
};

TEST_F(RuntimeInitMetricsForceEnableTest, MetricsAreInitialized) {
  ASSERT_TRUE(runtime_->AreMetricsInitialized());
}

}  // namespace art
