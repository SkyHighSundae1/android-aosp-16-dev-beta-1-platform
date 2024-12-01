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

#include "interpreter/interpreter_switch_impl-inl.h"

#include "aot_class_linker.h"
#include "transaction.h"

namespace art HIDDEN {
namespace interpreter {

class ActiveTransactionChecker {
 public:
  static inline bool WriteConstraint(Thread* self, ObjPtr<mirror::Object> obj)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    return GetClassLinker()->TransactionWriteConstraint(self, obj);
  }

  static inline bool WriteValueConstraint(Thread* self, ObjPtr<mirror::Object> value)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    return GetClassLinker()->TransactionWriteValueConstraint(self, value);
  }

  static inline bool ReadConstraint(Thread* self, ObjPtr<mirror::Object> obj)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    return GetClassLinker()->TransactionReadConstraint(self, obj);
  }

  static inline bool AllocationConstraint(Thread* self, ObjPtr<mirror::Class> klass)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    return GetClassLinker()->TransactionAllocationConstraint(self, klass);
  }

  static inline bool IsTransactionAborted() {
    return GetClassLinker()->IsTransactionAborted();
  }

  static void RecordArrayElementsInTransaction(ObjPtr<mirror::Object> array, int32_t count)
      REQUIRES_SHARED(Locks::mutator_lock_);

  static void RecordNewObject(ObjPtr<mirror::Object> new_object)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    GetClassLinker()->GetTransaction()->RecordNewObject(new_object);
  }

  static void RecordNewArray(ObjPtr<mirror::Array> new_object)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    GetClassLinker()->GetTransaction()->RecordNewArray(new_object);
  }

 private:
  static AotClassLinker* GetClassLinker() {
    return down_cast<AotClassLinker*>(Runtime::Current()->GetClassLinker());
  }
};

// TODO: Use ObjPtr here.
template<typename T>
static void RecordArrayElementsInTransactionImpl(Transaction* transaction,
                                                 ObjPtr<mirror::PrimitiveArray<T>> array,
                                                 int32_t count)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  for (int32_t i = 0; i < count; ++i) {
    transaction->RecordWriteArray(array.Ptr(), i, array->GetWithoutChecks(i));
  }
}

void ActiveTransactionChecker::RecordArrayElementsInTransaction(ObjPtr<mirror::Object> array,
                                                                int32_t count) {
  DCHECK(Runtime::Current()->IsActiveTransaction());
  if (array == nullptr) {
    return;  // The interpreter shall throw NPE.
  }
  DCHECK(array->IsArrayInstance());
  DCHECK_LE(count, array->AsArray()->GetLength());
  Transaction* transaction = GetClassLinker()->GetTransaction();
  if (!transaction->ArrayNeedsTransactionRecords(array->AsArray())) {
    return;
  }
  // No read barrier is needed for reading a chain of constant references
  // for reading a constant primitive value, see `ReadBarrierOption`.
  Primitive::Type primitive_component_type =
      array->GetClass<kDefaultVerifyFlags, kWithoutReadBarrier>()
          ->GetComponentType<kDefaultVerifyFlags, kWithoutReadBarrier>()->GetPrimitiveType();
  switch (primitive_component_type) {
    case Primitive::kPrimBoolean:
      RecordArrayElementsInTransactionImpl(transaction, array->AsBooleanArray(), count);
      break;
    case Primitive::kPrimByte:
      RecordArrayElementsInTransactionImpl(transaction, array->AsByteArray(), count);
      break;
    case Primitive::kPrimChar:
      RecordArrayElementsInTransactionImpl(transaction, array->AsCharArray(), count);
      break;
    case Primitive::kPrimShort:
      RecordArrayElementsInTransactionImpl(transaction, array->AsShortArray(), count);
      break;
    case Primitive::kPrimInt:
      RecordArrayElementsInTransactionImpl(transaction, array->AsIntArray(), count);
      break;
    case Primitive::kPrimFloat:
      RecordArrayElementsInTransactionImpl(transaction, array->AsFloatArray(), count);
      break;
    case Primitive::kPrimLong:
      RecordArrayElementsInTransactionImpl(transaction, array->AsLongArray(), count);
      break;
    case Primitive::kPrimDouble:
      RecordArrayElementsInTransactionImpl(transaction, array->AsDoubleArray(), count);
      break;
    default:
      LOG(FATAL) << "Unsupported primitive type " << primitive_component_type
                 << " in fill-array-data";
      UNREACHABLE();
  }
}

class InactiveInstrumentationHandler {
 public:
  ALWAYS_INLINE WARN_UNUSED
  static bool HasFieldReadListeners(const instrumentation::Instrumentation* instrumentation)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    DCHECK(!instrumentation->HasFieldReadListeners());
    return false;
  }

  ALWAYS_INLINE WARN_UNUSED
  static bool HasFieldWriteListeners(const instrumentation::Instrumentation* instrumentation)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    DCHECK(!instrumentation->HasFieldWriteListeners());
    return false;
  }

  ALWAYS_INLINE WARN_UNUSED
  static bool HasBranchListeners(const instrumentation::Instrumentation* instrumentation)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    DCHECK(!instrumentation->HasBranchListeners());
    return false;
  }

  ALWAYS_INLINE WARN_UNUSED
  static bool NeedsDexPcEvents(ShadowFrame& shadow_frame)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    DCHECK(!shadow_frame.GetNotifyDexPcMoveEvents());
    DCHECK(!Runtime::Current()->GetInstrumentation()->HasDexPcListeners());
    return false;
  }

  ALWAYS_INLINE WARN_UNUSED
  static bool NeedsMethodExitEvent(const instrumentation::Instrumentation* instrumentation)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    DCHECK(!interpreter::NeedsMethodExitEvent(instrumentation));
    return false;
  }

  ALWAYS_INLINE WARN_UNUSED
  static bool GetForcePopFrame(ShadowFrame& shadow_frame) {
    DCHECK(!shadow_frame.GetForcePopFrame());
    DCHECK(!Runtime::Current()->AreNonStandardExitsEnabled());
    return false;
  }

  NO_RETURN
  static void Branch([[maybe_unused]] Thread* self,
                     [[maybe_unused]] ArtMethod* method,
                     [[maybe_unused]] uint32_t dex_pc,
                     [[maybe_unused]] int32_t dex_pc_offset,
                     [[maybe_unused]] const instrumentation::Instrumentation* instrumentation)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    LOG(FATAL) << "UNREACHABLE";
    UNREACHABLE();
  }

  static bool ExceptionHandledEvent(
      [[maybe_unused]] Thread* self,
      [[maybe_unused]] bool is_move_exception,
      [[maybe_unused]] const instrumentation::Instrumentation* instrumentation)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    LOG(FATAL) << "UNREACHABLE";
    UNREACHABLE();
  }

  static bool DoDexPcMoveEvent(
      [[maybe_unused]] Thread* self,
      [[maybe_unused]] const CodeItemDataAccessor& accessor,
      [[maybe_unused]] const ShadowFrame& shadow_frame,
      [[maybe_unused]] uint32_t dex_pc,
      [[maybe_unused]] const instrumentation::Instrumentation* instrumentation,
      [[maybe_unused]] JValue* save_ref)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    LOG(FATAL) << "UNREACHABLE";
    UNREACHABLE();
  }

  template <typename T>
  static bool SendMethodExitEvents(
      [[maybe_unused]] Thread* self,
      [[maybe_unused]] const instrumentation::Instrumentation* instrumentation,
      [[maybe_unused]] ShadowFrame& frame,
      [[maybe_unused]] ArtMethod* method,
      [[maybe_unused]] T& result) REQUIRES_SHARED(Locks::mutator_lock_) {
    LOG(FATAL) << "UNREACHABLE";
    UNREACHABLE();
  }
};

// Explicit definition of ExecuteSwitchImplCpp.
template
void ExecuteSwitchImplCpp<true>(SwitchImplContext* ctx);

}  // namespace interpreter
}  // namespace art
