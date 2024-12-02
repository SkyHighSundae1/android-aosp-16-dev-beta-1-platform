/*
 * Copyright (C) 2012 The Android Open Source Project
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

#include "arch/context.h"
#include "art_method-inl.h"
#include "callee_save_frame.h"
#include "dex/code_item_accessors-inl.h"
#include "dex/dex_instruction-inl.h"
#include "common_throws.h"
#include "mirror/object-inl.h"
#include "nth_caller_visitor.h"
#include "thread.h"
#include "well_known_classes.h"

namespace art HIDDEN {

// Deliver an exception that's pending on thread helping set up a callee save frame on the way.
extern "C" Context* artDeliverPendingExceptionFromCode(Thread* self)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  ScopedQuickEntrypointChecks sqec(self);
  std::unique_ptr<Context> context = self->QuickDeliverException();
  DCHECK(context != nullptr);
  return context.release();
}

extern "C" Context* artInvokeObsoleteMethod(ArtMethod* method, Thread* self)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  DCHECK(method->IsObsolete());
  ScopedQuickEntrypointChecks sqec(self);
  ThrowInternalError("Attempting to invoke obsolete version of '%s'.",
                     method->PrettyMethod().c_str());
  std::unique_ptr<Context> context = self->QuickDeliverException();
  DCHECK(context != nullptr);
  return context.release();
}

// Called by generated code to throw an exception.
extern "C" Context* artDeliverExceptionFromCode(mirror::Throwable* exception, Thread* self)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  /*
   * exception may be null, in which case this routine should
   * throw NPE.  NOTE: this is a convenience for generated code,
   * which previously did the null check inline and constructed
   * and threw a NPE if null.  This routine responsible for setting
   * exception_ in thread and delivering the exception.
   */
  ScopedQuickEntrypointChecks sqec(self);
  if (exception == nullptr) {
    self->ThrowNewException("Ljava/lang/NullPointerException;", nullptr);
  } else {
    self->SetException(exception);
  }
  std::unique_ptr<Context> context = self->QuickDeliverException();
  DCHECK(context != nullptr);
  return context.release();
}

// Called by generated code to throw a NPE exception.
extern "C" Context* artThrowNullPointerExceptionFromCode(Thread* self)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  ScopedQuickEntrypointChecks sqec(self);
  // We come from an explicit check in the generated code. This path is triggered
  // only if the object is indeed null.
  ThrowNullPointerExceptionFromDexPC(/* check_address= */ false, 0U);
  std::unique_ptr<Context> context = self->QuickDeliverException();
  DCHECK(context != nullptr);
  return context.release();
}

// Installed by a signal handler to throw a NPE exception.
extern "C" Context* artThrowNullPointerExceptionFromSignal(uintptr_t addr, Thread* self)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  ScopedQuickEntrypointChecks sqec(self);
  ThrowNullPointerExceptionFromDexPC(/* check_address= */ true, addr);
  std::unique_ptr<Context> context = self->QuickDeliverException();
  DCHECK(context != nullptr);
  return context.release();
}

// Called by generated code to throw an arithmetic divide by zero exception.
extern "C" Context* artThrowDivZeroFromCode(Thread* self)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  ScopedQuickEntrypointChecks sqec(self);
  ThrowArithmeticExceptionDivideByZero();
  std::unique_ptr<Context> context = self->QuickDeliverException();
  DCHECK(context != nullptr);
  return context.release();
}

// Called by generated code to throw an array index out of bounds exception.
extern "C" Context* artThrowArrayBoundsFromCode(int index, int length, Thread* self)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  ScopedQuickEntrypointChecks sqec(self);
  ThrowArrayIndexOutOfBoundsException(index, length);
  std::unique_ptr<Context> context = self->QuickDeliverException();
  DCHECK(context != nullptr);
  return context.release();
}

// Called by generated code to throw a string index out of bounds exception.
extern "C" Context* artThrowStringBoundsFromCode(int index, int length, Thread* self)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  ScopedQuickEntrypointChecks sqec(self);
  ThrowStringIndexOutOfBoundsException(index, length);
  std::unique_ptr<Context> context = self->QuickDeliverException();
  DCHECK(context != nullptr);
  return context.release();
}

extern "C" Context* artThrowStackOverflowFromCode(Thread* self)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  ScopedQuickEntrypointChecks sqec(self);
  // Throw a stack overflow error for the quick stack. This is needed to throw stack overflow
  // errors on the simulated stack, which is used for quick code when building for the simulator.
  // See kQuickStackType for more details.
  ThrowStackOverflowError<kQuickStackType>(self);
  std::unique_ptr<Context> context = self->QuickDeliverException();
  DCHECK(context != nullptr);
  return context.release();
}

extern "C" Context* artThrowClassCastException(mirror::Class* dest_type,
                                               mirror::Class* src_type,
                                               Thread* self)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  ScopedQuickEntrypointChecks sqec(self);
  if (dest_type == nullptr) {
    // Find the target class for check cast using the bitstring check (dest_type == null).
    NthCallerVisitor visitor(self, 0u);
    visitor.WalkStack();
    DCHECK(visitor.caller != nullptr);
    uint32_t dex_pc = visitor.GetDexPc();
    CodeItemDataAccessor accessor(*visitor.caller->GetDexFile(), visitor.caller->GetCodeItem());
    const Instruction& check_cast = accessor.InstructionAt(dex_pc);
    DCHECK_EQ(check_cast.Opcode(), Instruction::CHECK_CAST);
    dex::TypeIndex type_index(check_cast.VRegB_21c());
    ClassLinker* linker = Runtime::Current()->GetClassLinker();
    dest_type = linker->LookupResolvedType(type_index, visitor.caller).Ptr();
    CHECK(dest_type != nullptr) << "Target class should have been previously resolved: "
        << visitor.caller->GetDexFile()->PrettyType(type_index);
    CHECK(!dest_type->IsAssignableFrom(src_type))
        << " " << std::hex << dest_type->PrettyDescriptor() << ";" << dest_type->Depth()
        << "/" << dest_type->GetField32(mirror::Class::StatusOffset())
        << " <: " << src_type->PrettyDescriptor() << ";" << src_type->Depth()
        << "/" << src_type->GetField32(mirror::Class::StatusOffset());
  }
  DCHECK(!dest_type->IsAssignableFrom(src_type));
  ThrowClassCastException(dest_type, src_type);
  std::unique_ptr<Context> context = self->QuickDeliverException();
  DCHECK(context != nullptr);
  return context.release();
}

extern "C" Context* artThrowClassCastExceptionForObject(mirror::Object* obj,
                                                        mirror::Class* dest_type,
                                                        Thread* self)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  DCHECK(obj != nullptr);
  return artThrowClassCastException(dest_type, obj->GetClass(), self);
}

extern "C" Context* artThrowArrayStoreException(mirror::Object* array,
                                                mirror::Object* value,
                                                Thread* self)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  ScopedQuickEntrypointChecks sqec(self);
  ThrowArrayStoreException(value->GetClass(), array->GetClass());
  std::unique_ptr<Context> context = self->QuickDeliverException();
  DCHECK(context != nullptr);
  return context.release();
}

}  // namespace art
