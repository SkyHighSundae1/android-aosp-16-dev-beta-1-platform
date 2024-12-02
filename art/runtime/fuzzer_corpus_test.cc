/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <cstdint>
#include <filesystem>
#include <unordered_set>

#include "android-base/file.h"
#include "android-base/macros.h"
#include "common_runtime_test.h"
#include "dex/class_accessor-inl.h"
#include "dex/dex_file_verifier.h"
#include "dex/standard_dex_file.h"
#include "gtest/gtest.h"
#include "handle_scope-inl.h"
#include "jni/java_vm_ext.h"
#include "verifier/class_verifier.h"
#include "ziparchive/zip_archive.h"

namespace art {
// Global variable to count how many DEX files passed DEX file verification and they were
// registered, since these are the cases for which we would be running the GC.
int skipped_gc_iterations = 0;
// Global variable to call the GC once every maximum number of iterations.
// TODO: These values were obtained from local experimenting. They can be changed after
// further investigation.
static constexpr int kMaxSkipGCIterations = 100;

// A class to be friends with ClassLinker and access the internal FindDexCacheDataLocked method.
// TODO: Deduplicate this since it is the same with tools/fuzzer/libart_verify_classes_fuzzer.cc.
class VerifyClassesFuzzerCorpusTestHelper {
 public:
  static const ClassLinker::DexCacheData* GetDexCacheData(Runtime* runtime, const DexFile* dex_file)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    Thread* self = Thread::Current();
    ReaderMutexLock mu(self, *Locks::dex_lock_);
    ClassLinker* class_linker = runtime->GetClassLinker();
    const ClassLinker::DexCacheData* cached_data = class_linker->FindDexCacheDataLocked(*dex_file);
    return cached_data;
  }
};

// Manages the ZipArchiveHandle liveness.
class ZipArchiveHandleScope {
 public:
  explicit ZipArchiveHandleScope(ZipArchiveHandle* handle) : handle_(handle) {}
  ~ZipArchiveHandleScope() { CloseArchive(*(handle_.release())); }

 private:
  std::unique_ptr<ZipArchiveHandle> handle_;
};

class FuzzerCorpusTest : public CommonRuntimeTest {
 public:
  static void DexFileVerification(const uint8_t* data,
                                  size_t size,
                                  const std::string& name,
                                  bool expected_success) {
    // Do not verify the checksum as we only care about the DEX file contents,
    // and know that the checksum would probably be erroneous (i.e. random).
    constexpr bool kVerify = false;

    auto container = std::make_shared<MemoryDexFileContainer>(data, size);
    StandardDexFile dex_file(data,
                             /*location=*/name,
                             /*location_checksum=*/0,
                             /*oat_dex_file=*/nullptr,
                             container);

    std::string error_msg;
    bool is_valid_dex_file =
        dex::Verify(&dex_file, dex_file.GetLocation().c_str(), kVerify, &error_msg);
    ASSERT_EQ(is_valid_dex_file, expected_success) << " Failed for " << name;
  }

  static void ClassVerification(const uint8_t* data,
                                size_t size,
                                const std::string& name,
                                bool expected_success) {
    // Do not verify the checksum as we only care about the DEX file contents,
    // and know that the checksum would probably be erroneous (i.e. random)
    constexpr bool kVerify = false;
    bool passed_class_verification = true;

    auto container = std::make_shared<MemoryDexFileContainer>(data, size);
    StandardDexFile dex_file(data,
                             /*location=*/name,
                             /*location_checksum=*/0,
                             /*oat_dex_file=*/nullptr,
                             container);

    std::string error_msg;
    const bool success_dex =
        dex::Verify(&dex_file, dex_file.GetLocation().c_str(), kVerify, &error_msg);
    ASSERT_EQ(success_dex, true) << " Failed for " << name;

    Runtime* runtime = Runtime::Current();
    CHECK(runtime != nullptr);

    ScopedObjectAccess soa(Thread::Current());
    ClassLinker* class_linker = runtime->GetClassLinker();
    jobject class_loader = RegisterDexFileAndGetClassLoader(runtime, &dex_file);

    // Scope for the handles
    {
      art::StackHandleScope<3> scope(soa.Self());
      art::Handle<art::mirror::ClassLoader> h_loader =
          scope.NewHandle(soa.Decode<art::mirror::ClassLoader>(class_loader));
      art::MutableHandle<art::mirror::Class> h_klass(scope.NewHandle<art::mirror::Class>(nullptr));
      art::MutableHandle<art::mirror::DexCache> h_dex_cache(
          scope.NewHandle<art::mirror::DexCache>(nullptr));

      for (art::ClassAccessor accessor : dex_file.GetClasses()) {
        h_klass.Assign(
            class_linker->FindClass(soa.Self(), dex_file, accessor.GetClassIdx(), h_loader));
        // Ignore classes that couldn't be loaded since we are looking for crashes during
        // class/method verification.
        if (h_klass == nullptr || h_klass->IsErroneous()) {
          // Treat as failure to pass verification
          passed_class_verification = false;
          soa.Self()->ClearException();
          continue;
        }
        h_dex_cache.Assign(h_klass->GetDexCache());
        verifier::FailureKind failure =
            verifier::ClassVerifier::VerifyClass(soa.Self(),
                                                 /* verifier_deps= */ nullptr,
                                                 h_dex_cache->GetDexFile(),
                                                 h_klass,
                                                 h_dex_cache,
                                                 h_loader,
                                                 *h_klass->GetClassDef(),
                                                 runtime->GetCompilerCallbacks(),
                                                 verifier::HardFailLogMode::kLogWarning,
                                                 /* api_level= */ 0,
                                                 &error_msg);
        if (failure != verifier::FailureKind::kNoFailure) {
          passed_class_verification = false;
        }
      }
    }
    skipped_gc_iterations++;

    // Delete weak root to the DexCache before removing a DEX file from the cache. This is usually
    // handled by the GC, but since we are not calling it every iteration, we need to delete them
    // manually.
    const ClassLinker::DexCacheData* dex_cache_data =
        VerifyClassesFuzzerCorpusTestHelper::GetDexCacheData(runtime, &dex_file);
    soa.Env()->GetVm()->DeleteWeakGlobalRef(soa.Self(), dex_cache_data->weak_root);

    class_linker->RemoveDexFromCaches(dex_file);

    // Delete global ref and unload class loader to free RAM.
    soa.Env()->GetVm()->DeleteGlobalRef(soa.Self(), class_loader);

    if (skipped_gc_iterations == kMaxSkipGCIterations) {
      runtime->GetHeap()->CollectGarbage(/* clear_soft_references */ true);
      skipped_gc_iterations = 0;
    }

    ASSERT_EQ(passed_class_verification, expected_success) << " Failed for " << name;
  }

  void TestFuzzerHelper(
      const std::string& archive_filename,
      const std::unordered_set<std::string>& valid_dex_files,
      std::function<void(const uint8_t*, size_t, const std::string&, bool)> verify_file) {
    // Consistency checks.
    const std::string folder = android::base::GetExecutableDirectory();
    ASSERT_TRUE(std::filesystem::is_directory(folder)) << folder << " is not a folder";
    ASSERT_FALSE(std::filesystem::is_empty(folder)) << " No files found for directory " << folder;
    const std::string filename = folder + "/" + archive_filename;

    // Iterate using ZipArchiveHandle. We have to be careful about managing the pointers with
    // CloseArchive, StartIteration, and EndIteration.
    std::string error_msg;
    ZipArchiveHandle handle;
    ZipArchiveHandleScope scope(&handle);
    int32_t error = OpenArchive(filename.c_str(), &handle);
    ASSERT_TRUE(error == 0) << "Error: " << error;

    void* cookie;
    error = StartIteration(handle, &cookie);
    ASSERT_TRUE(error == 0) << "couldn't iterate " << filename << " : " << ErrorCodeString(error);

    ZipEntry64 entry;
    std::string name;
    std::vector<char> data;
    while ((error = Next(cookie, &entry, &name)) >= 0) {
      if (!name.ends_with(".dex")) {
        // Skip non-DEX files.
        LOG(WARNING) << "Found a non-dex file: " << name;
        continue;
      }
      data.resize(entry.uncompressed_length);
      error = ExtractToMemory(handle, &entry, reinterpret_cast<uint8_t*>(data.data()), data.size());
      ASSERT_TRUE(error == 0) << "failed to extract entry: " << name << " from " << filename << ""
                              << ErrorCodeString(error);

      const uint8_t* file_data = reinterpret_cast<const uint8_t*>(data.data());
      // Special case for empty dex file. Set a fake data since the size is 0 anyway.
      if (file_data == nullptr) {
        ASSERT_EQ(data.size(), 0);
        file_data = reinterpret_cast<const uint8_t*>(&name);
      }

      const bool is_valid_dex_file = valid_dex_files.find(name) != valid_dex_files.end();
      verify_file(file_data, data.size(), name, is_valid_dex_file);
    }

    ASSERT_TRUE(error >= -1) << "failed iterating " << filename << " : " << ErrorCodeString(error);
    EndIteration(cookie);
  }

 private:
  static jobject RegisterDexFileAndGetClassLoader(Runtime* runtime, StandardDexFile* dex_file)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    Thread* self = Thread::Current();
    ClassLinker* class_linker = runtime->GetClassLinker();
    const std::vector<const DexFile*> dex_files = {dex_file};
    jobject class_loader = class_linker->CreatePathClassLoader(self, dex_files);
    ObjPtr<mirror::ClassLoader> cl = self->DecodeJObject(class_loader)->AsClassLoader();
    class_linker->RegisterDexFile(*dex_file, cl);
    return class_loader;
  }
};

// Tests that we can verify dex files without crashing.
TEST_F(FuzzerCorpusTest, VerifyCorpusDexFiles) {
  // These dex files are expected to pass verification. The others are regressions tests.
  const std::unordered_set<std::string> valid_dex_files = {"Main.dex", "hello_world.dex"};
  const std::string archive_filename = "dex_verification_fuzzer_corpus.zip";

  TestFuzzerHelper(archive_filename, valid_dex_files, DexFileVerification);
}

// Tests that we can verify classes from dex files without crashing.
TEST_F(FuzzerCorpusTest, VerifyCorpusClassDexFiles) {
  // These dex files are expected to pass verification. The others are regressions tests.
  const std::unordered_set<std::string> valid_dex_files = {"Main.dex", "hello_world.dex"};
  const std::string archive_filename = "class_verification_fuzzer_corpus.zip";

  TestFuzzerHelper(archive_filename, valid_dex_files, ClassVerification);
}

}  // namespace art
