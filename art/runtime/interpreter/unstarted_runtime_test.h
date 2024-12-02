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

#ifndef ART_RUNTIME_INTERPRETER_UNSTARTED_RUNTIME_TEST_H_
#define ART_RUNTIME_INTERPRETER_UNSTARTED_RUNTIME_TEST_H_

#include "unstarted_runtime.h"

#include <memory>

#include "class_linker-inl.h"
#include "common_runtime_test.h"
#include "handle.h"
#include "handle_scope-inl.h"
#include "interpreter_common.h"
#include "mirror/object_array-alloc-inl.h"
#include "mirror/object_array-inl.h"
#include "shadow_frame-inl.h"

namespace art HIDDEN {
namespace interpreter {

// Deleter to be used with ShadowFrame::CreateDeoptimizedFrame objects.
struct DeoptShadowFrameDelete {
  // NOTE: Deleting a const object is valid but free() takes a non-const pointer.
  void operator()(ShadowFrame* ptr) const {
    ShadowFrame::DeleteDeoptimizedFrame(ptr);
  }
};
// Alias for std::unique_ptr<> that uses the above deleter.
using UniqueDeoptShadowFramePtr = std::unique_ptr<ShadowFrame, DeoptShadowFrameDelete>;

class UnstartedRuntimeTestBase : public CommonRuntimeTest {
 protected:
  // Re-expose all UnstartedRuntime implementations so we don't need to declare a million
  // test friends.

  // Methods that intercept available libcore implementations.
#define UNSTARTED_DIRECT(Name, DescriptorIgnored, NameIgnored, SignatureIgnored)              \
  static void Unstarted ## Name(Thread* self,                                                 \
                                ShadowFrame* shadow_frame,                                    \
                                JValue* result,                                               \
                                size_t arg_offset)                                            \
      REQUIRES_SHARED(Locks::mutator_lock_) {                                                 \
    interpreter::UnstartedRuntime::Unstarted ## Name(self, shadow_frame, result, arg_offset); \
  }
  UNSTARTED_RUNTIME_DIRECT_LIST(UNSTARTED_DIRECT)
#undef UNSTARTED_DIRECT

  // Methods that are native.
#define UNSTARTED_JNI(Name, DescriptorIgnored, NameIgnored, SignatureIgnored)                  \
  static void UnstartedJNI ## Name(Thread* self,                                               \
                                   ArtMethod* method,                                          \
                                   mirror::Object* receiver,                                   \
                                   uint32_t* args,                                             \
                                   JValue* result)                                             \
      REQUIRES_SHARED(Locks::mutator_lock_) {                                                  \
    interpreter::UnstartedRuntime::UnstartedJNI ## Name(self, method, receiver, args, result); \
  }
  UNSTARTED_RUNTIME_JNI_LIST(UNSTARTED_JNI)
#undef UNSTARTED_JNI

  UniqueDeoptShadowFramePtr CreateShadowFrame(uint32_t num_vregs,
                                              ArtMethod* method,
                                              uint32_t dex_pc) {
    return UniqueDeoptShadowFramePtr(
        ShadowFrame::CreateDeoptimizedFrame(num_vregs, method, dex_pc));
  }

  mirror::ClassLoader* GetBootClassLoader() REQUIRES_SHARED(Locks::mutator_lock_) {
    Thread* self = Thread::Current();
    StackHandleScope<2> hs(self);

    // Create the fake boot classloader. Any instance is fine, they are technically interchangeable.
    Handle<mirror::Class> boot_cp_class = hs.NewHandle(
        FindClass("Ljava/lang/BootClassLoader;", ScopedNullHandle<mirror::ClassLoader>()));
    CHECK(boot_cp_class != nullptr);
    CHECK(class_linker_->EnsureInitialized(
        self, boot_cp_class, /*can_init_fields=*/ true, /*can_init_parents=*/ true));

    Handle<mirror::ClassLoader> boot_cp =
        hs.NewHandle(boot_cp_class->AllocObject(self)->AsClassLoader());
    CHECK(boot_cp != nullptr);

    ArtMethod* boot_cp_init =
        boot_cp_class->FindConstructor("()V", class_linker_->GetImagePointerSize());
    CHECK(boot_cp_init != nullptr);

    JValue result;
    UniqueDeoptShadowFramePtr shadow_frame = CreateShadowFrame(10, boot_cp_init, 0);
    shadow_frame->SetVRegReference(0, boot_cp.Get());

    // create instruction data for invoke-direct {v0} of method with fake index
    uint16_t inst_data[3] = { 0x1070, 0x0000, 0x0010 };

    interpreter::DoCall<false>(boot_cp_init,
                               self,
                               *shadow_frame,
                               Instruction::At(inst_data),
                               inst_data[0],
                               /* string_init= */ false,
                               &result);
    CHECK(!self->IsExceptionPending());

    return boot_cp.Get();
  }
};

}  // namespace interpreter
}  // namespace art

#endif  // ART_RUNTIME_INTERPRETER_UNSTARTED_RUNTIME_TEST_H_
