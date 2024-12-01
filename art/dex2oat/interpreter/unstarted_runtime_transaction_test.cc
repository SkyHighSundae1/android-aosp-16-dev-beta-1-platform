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

#include "interpreter/unstarted_runtime_test.h"

#include "class_root-inl.h"
#include "common_transaction_test.h"
#include "dex/descriptors_names.h"
#include "interpreter/interpreter_common.h"
#include "handle.h"
#include "handle_scope-inl.h"
#include "mirror/class-inl.h"

namespace art HIDDEN {
namespace interpreter {

class UnstartedRuntimeTransactionTest : public CommonTransactionTestBase<UnstartedRuntimeTestBase> {
 protected:
  // Prepare for aborts. Aborts assume that the exception class is already resolved, as the
  // loading code doesn't work under transactions.
  void PrepareForAborts() REQUIRES_SHARED(Locks::mutator_lock_) {
    ObjPtr<mirror::Object> result =
        FindClass(kTransactionAbortErrorDescriptor, ScopedNullHandle<mirror::ClassLoader>());
    CHECK(result != nullptr);
  }
};

TEST_F(UnstartedRuntimeTransactionTest, ToLowerUpper) {
  Thread* self = Thread::Current();
  ScopedObjectAccess soa(self);
  UniqueDeoptShadowFramePtr tmp = CreateShadowFrame(10, nullptr, 0);

  PrepareForAborts();

  for (uint32_t i = 128; i < 256; ++i) {
    {
      JValue result;
      tmp->SetVReg(0, static_cast<int32_t>(i));
      EnterTransactionMode();
      UnstartedCharacterToLowerCase(self, tmp.get(), &result, 0);
      ASSERT_TRUE(IsTransactionAborted());
      ExitTransactionMode();
      ASSERT_TRUE(self->IsExceptionPending());
    }
    {
      JValue result;
      tmp->SetVReg(0, static_cast<int32_t>(i));
      EnterTransactionMode();
      UnstartedCharacterToUpperCase(self, tmp.get(), &result, 0);
      ASSERT_TRUE(IsTransactionAborted());
      ExitTransactionMode();
      ASSERT_TRUE(self->IsExceptionPending());
    }
  }
  for (uint64_t i = 256; i <= std::numeric_limits<uint32_t>::max(); i <<= 1) {
    {
      JValue result;
      tmp->SetVReg(0, static_cast<int32_t>(i));
      EnterTransactionMode();
      UnstartedCharacterToLowerCase(self, tmp.get(), &result, 0);
      ASSERT_TRUE(IsTransactionAborted());
      ExitTransactionMode();
      ASSERT_TRUE(self->IsExceptionPending());
    }
    {
      JValue result;
      tmp->SetVReg(0, static_cast<int32_t>(i));
      EnterTransactionMode();
      UnstartedCharacterToUpperCase(self, tmp.get(), &result, 0);
      ASSERT_TRUE(IsTransactionAborted());
      ExitTransactionMode();
      ASSERT_TRUE(self->IsExceptionPending());
    }
  }
}

TEST_F(UnstartedRuntimeTransactionTest, ThreadLocalGet) {
  Thread* self = Thread::Current();
  ScopedObjectAccess soa(self);
  UniqueDeoptShadowFramePtr shadow_frame = CreateShadowFrame(10, nullptr, 0);

  // Negative test.
  PrepareForAborts();

  // Just use a method in Class.
  ObjPtr<mirror::Class> class_class = GetClassRoot<mirror::Class>();
  ArtMethod* caller_method =
      &*class_class->GetDeclaredMethods(class_linker_->GetImagePointerSize()).begin();
  UniqueDeoptShadowFramePtr caller_frame = CreateShadowFrame(10, caller_method, 0);
  shadow_frame->SetLink(caller_frame.get());

  JValue result;
  EnterTransactionMode();
  UnstartedThreadLocalGet(self, shadow_frame.get(), &result, 0);
  ASSERT_TRUE(IsTransactionAborted());
  ExitTransactionMode();
  ASSERT_TRUE(self->IsExceptionPending());
  self->ClearException();

  shadow_frame->ClearLink();
}

TEST_F(UnstartedRuntimeTransactionTest, ThreadCurrentThread) {
  Thread* self = Thread::Current();
  ScopedObjectAccess soa(self);
  UniqueDeoptShadowFramePtr shadow_frame = CreateShadowFrame(10, nullptr, 0);

  // Negative test. In general, currentThread should fail (as we should not leak a peer that will
  // be recreated at runtime).
  PrepareForAborts();

  JValue result;
  EnterTransactionMode();
  UnstartedThreadCurrentThread(self, shadow_frame.get(), &result, 0);
  ASSERT_TRUE(IsTransactionAborted());
  ExitTransactionMode();
  ASSERT_TRUE(self->IsExceptionPending());
  self->ClearException();
}

class UnstartedClassForNameTransactionTest : public UnstartedRuntimeTransactionTest {
 public:
  template <typename T>
  void RunTest(T&& runner, bool should_succeed) {
    Thread* self = Thread::Current();
    ScopedObjectAccess soa(self);

    // Ensure that Class is initialized.
    CHECK(GetClassRoot<mirror::Class>()->IsInitialized());

    // A selection of classes from different core classpath components.
    constexpr const char* kTestCases[] = {
        "java.net.CookieManager",  // From libcore.
        "dalvik.system.ClassExt",  // From libart.
    };

    // For transaction mode, we cannot load any classes, as the pre-fence initialization of
    // classes isn't transactional. Load them ahead of time.
    for (const char* name : kTestCases) {
      FindClass(DotToDescriptor(name).c_str(), ScopedNullHandle<mirror::ClassLoader>());
      CHECK(!self->IsExceptionPending()) << self->GetException()->Dump();
    }

    if (!should_succeed) {
      // Negative test. In general, currentThread should fail (as we should not leak a peer that will
      // be recreated at runtime).
      PrepareForAborts();
    }

    JValue result;
    UniqueDeoptShadowFramePtr shadow_frame = CreateShadowFrame(10, nullptr, 0);

    for (const char* name : kTestCases) {
      EnterTransactionMode();

      ObjPtr<mirror::String> name_string = mirror::String::AllocFromModifiedUtf8(self, name);
      CHECK(name_string != nullptr);
      CHECK(!self->IsExceptionPending());

      runner(self, shadow_frame.get(), name_string, &result);

      if (should_succeed) {
        CHECK(!self->IsExceptionPending()) << name << " " << self->GetException()->Dump();
        CHECK(result.GetL() != nullptr) << name;
      } else {
        CHECK(self->IsExceptionPending()) << name;
        ASSERT_TRUE(IsTransactionAborted());
        self->ClearException();
      }

      ExitTransactionMode();
    }
  }
};

TEST_F(UnstartedClassForNameTransactionTest, ClassForNameLongWithClassLoader) {
  Thread* self = Thread::Current();
  ScopedObjectAccess soa(self);

  StackHandleScope<1> hs(self);
  Handle<mirror::ClassLoader> boot_cp = hs.NewHandle(GetBootClassLoader());

  auto runner = [&](Thread* th,
                    ShadowFrame* shadow_frame,
                    ObjPtr<mirror::String> name,
                    JValue* result)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    shadow_frame->SetVRegReference(0, name);
    shadow_frame->SetVReg(1, 0);
    shadow_frame->SetVRegReference(2, boot_cp.Get());
    UnstartedClassForNameLong(th, shadow_frame, result, 0);
  };
  RunTest(runner, /*should_succeed=*/ true);
}

TEST_F(UnstartedClassForNameTransactionTest, ClassForNameLongWithClassLoaderFail) {
  Thread* self = Thread::Current();
  ScopedObjectAccess soa(self);

  StackHandleScope<2> hs(self);
  jobject path_jobj = class_linker_->CreatePathClassLoader(self, {});
  ASSERT_TRUE(path_jobj != nullptr);
  Handle<mirror::ClassLoader> path_cp = hs.NewHandle<mirror::ClassLoader>(
      self->DecodeJObject(path_jobj)->AsClassLoader());

  auto runner = [&](Thread* th,
                    ShadowFrame* shadow_frame,
                    ObjPtr<mirror::String> name,
                    JValue* result) REQUIRES_SHARED(Locks::mutator_lock_) {
    shadow_frame->SetVRegReference(0, name);
    shadow_frame->SetVReg(1, 0);
    shadow_frame->SetVRegReference(2, path_cp.Get());
    UnstartedClassForNameLong(th, shadow_frame, result, 0);
  };
  RunTest(runner, /*should_succeed=*/ false);
}

}  // namespace interpreter
}  // namespace art
