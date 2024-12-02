/*
 * Copyright (C) 2018 The Android Open Source Project
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

// The interpreter function takes considerable time to compile and link.
// We compile the explicit definitions separately to speed up the build.

#include "interpreter_switch_impl-inl.h"

namespace art HIDDEN {
namespace interpreter {

// Define the helper class that does not do any transaction checks.
class InactiveTransactionChecker {
 public:
  ALWAYS_INLINE static bool WriteConstraint([[maybe_unused]] Thread* self,
                                            [[maybe_unused]] ObjPtr<mirror::Object> obj)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    return false;
  }

  ALWAYS_INLINE static bool WriteValueConstraint([[maybe_unused]] Thread* self,
                                                 [[maybe_unused]] ObjPtr<mirror::Object> value)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    return false;
  }

  ALWAYS_INLINE static bool ReadConstraint([[maybe_unused]] Thread* self,
                                           [[maybe_unused]] ObjPtr<mirror::Object> value)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    return false;
  }

  ALWAYS_INLINE static bool AllocationConstraint([[maybe_unused]] Thread* self,
                                                 [[maybe_unused]] ObjPtr<mirror::Class> klass)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    return false;
  }

  ALWAYS_INLINE static bool IsTransactionAborted() {
    return false;
  }

  static void RecordArrayElementsInTransaction([[maybe_unused]] ObjPtr<mirror::Object> array,
                                               [[maybe_unused]] int32_t count)
      REQUIRES_SHARED(Locks::mutator_lock_) {}

  ALWAYS_INLINE static void RecordNewObject([[maybe_unused]] ObjPtr<mirror::Object> new_object)
      REQUIRES_SHARED(Locks::mutator_lock_) {}

  ALWAYS_INLINE static void RecordNewArray([[maybe_unused]] ObjPtr<mirror::Array> new_array)
      REQUIRES_SHARED(Locks::mutator_lock_) {}
};

class ActiveInstrumentationHandler {
 public:
  ALWAYS_INLINE WARN_UNUSED
  static bool HasFieldReadListeners(const instrumentation::Instrumentation* instrumentation)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    return instrumentation->HasFieldReadListeners();
  }

  ALWAYS_INLINE WARN_UNUSED
  static bool HasFieldWriteListeners(const instrumentation::Instrumentation* instrumentation)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    return instrumentation->HasFieldWriteListeners();
  }

  ALWAYS_INLINE WARN_UNUSED
  static bool HasBranchListeners(const instrumentation::Instrumentation* instrumentation)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    return instrumentation->HasBranchListeners();
  }

  ALWAYS_INLINE WARN_UNUSED
  static bool NeedsDexPcEvents(ShadowFrame& shadow_frame)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    DCHECK_IMPLIES(shadow_frame.GetNotifyDexPcMoveEvents(),
                   Runtime::Current()->GetInstrumentation()->HasDexPcListeners());
    return shadow_frame.GetNotifyDexPcMoveEvents();
  }

  ALWAYS_INLINE WARN_UNUSED
  static bool NeedsMethodExitEvent(const instrumentation::Instrumentation* instrumentation)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    return interpreter::NeedsMethodExitEvent(instrumentation);
  }

  ALWAYS_INLINE WARN_UNUSED
  static bool GetForcePopFrame(ShadowFrame& shadow_frame) {
    DCHECK_IMPLIES(shadow_frame.GetForcePopFrame(),
                   Runtime::Current()->AreNonStandardExitsEnabled());
    return shadow_frame.GetForcePopFrame();
  }

  ALWAYS_INLINE
  static void Branch(Thread* self,
                     ArtMethod* method,
                     uint32_t dex_pc,
                     int32_t dex_pc_offset,
                     const instrumentation::Instrumentation* instrumentation)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    instrumentation->Branch(self, method, dex_pc, dex_pc_offset);
  }

  static bool ExceptionHandledEvent(Thread* self,
                                    bool is_move_exception,
                                    const instrumentation::Instrumentation* instrumentation)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    StackHandleScope<1> hs(self);
    Handle<mirror::Throwable> exception(hs.NewHandle(self->GetException()));
    // Clear any exception while reporting the ExceptionHandled event. We should not run the handler
    // with an exception set.
    self->ClearException();
    instrumentation->ExceptionHandledEvent(self, exception.Get());
    // If there is an exception then that is the exception thrown by the exception handled event
    // and we should just handle the new exception. The earlier exception if any is ignored.
    if (self->IsExceptionPending()) {
      return false;  // Pending exception.
    }

    // Restore the original exception if the instruction we are going to execute is a move exception
    // instruction.
    if (is_move_exception) {
      self->SetException(exception.Get());
    }
    return true;
  }

  // Unlike most other events the DexPcMovedEvent can be sent when there is a pending exception (if
  // the next instruction is MOVE_EXCEPTION). This means it needs to be handled carefully to be able
  // to detect exceptions thrown by the DexPcMovedEvent itself. These exceptions could be thrown by
  // jvmti-agents while handling breakpoint or single step events. We had to move this into its own
  // function because it was making ExecuteSwitchImpl have too large a stack.
  NO_INLINE static bool DoDexPcMoveEvent(Thread* self,
                                         const CodeItemDataAccessor& accessor,
                                         const ShadowFrame& shadow_frame,
                                         uint32_t dex_pc,
                                         const instrumentation::Instrumentation* instrumentation,
                                         JValue* save_ref)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    DCHECK(instrumentation->HasDexPcListeners());
    StackHandleScope<2> hs(self);
    Handle<mirror::Throwable> thr(hs.NewHandle(self->GetException()));
    mirror::Object* null_obj = nullptr;
    HandleWrapper<mirror::Object> h(
        hs.NewHandleWrapper(LIKELY(save_ref == nullptr) ? &null_obj : save_ref->GetGCRoot()));
    self->ClearException();
    instrumentation->DexPcMovedEvent(self,
                                     shadow_frame.GetThisObject(accessor.InsSize()),
                                     shadow_frame.GetMethod(),
                                     dex_pc);
    if (UNLIKELY(self->IsExceptionPending())) {
      // We got a new exception in the dex-pc-moved event.
      // We just let this exception replace the old one.
      // TODO It would be good to add the old exception to the
      // suppressed exceptions of the new one if possible.
      return false;  // Pending exception.
    }
    if (UNLIKELY(!thr.IsNull())) {
      self->SetException(thr.Get());
    }
    return true;
  }

  template <typename T>
  ALWAYS_INLINE WARN_UNUSED
  static bool SendMethodExitEvents(
      Thread* self,
      const instrumentation::Instrumentation* instrumentation,
      ShadowFrame& frame,
      ArtMethod* method,
      T& result) REQUIRES_SHARED(Locks::mutator_lock_) {
    return interpreter::SendMethodExitEvents(self, instrumentation, frame, method, result);
  }
};

// Explicit definition of ExecuteSwitchImplCpp.
template HOT_ATTR
void ExecuteSwitchImplCpp<false>(SwitchImplContext* ctx);

}  // namespace interpreter
}  // namespace art
