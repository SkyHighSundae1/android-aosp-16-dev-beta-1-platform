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

#ifndef ART_COMPILER_OPTIMIZING_REFERENCE_TYPE_INFO_H_
#define ART_COMPILER_OPTIMIZING_REFERENCE_TYPE_INFO_H_

#include <iosfwd>

#include "base/globals.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/value_object.h"
#include "handle.h"
#include "mirror/class-inl.h"

namespace art HIDDEN {

class ReferenceTypeInfo : ValueObject {
 public:
  using TypeHandle = Handle<mirror::Class>;

  static ReferenceTypeInfo Create(TypeHandle type_handle, bool is_exact) {
    if (kIsDebugBuild) {
      DCheckValidTypeInfo(type_handle, is_exact);
    }
    return ReferenceTypeInfo(type_handle, is_exact);
  }

  static ReferenceTypeInfo Create(TypeHandle type_handle) REQUIRES_SHARED(Locks::mutator_lock_) {
    return Create(type_handle, type_handle->CannotBeAssignedFromOtherTypes());
  }

  static ReferenceTypeInfo CreateUnchecked(TypeHandle type_handle, bool is_exact) {
    return ReferenceTypeInfo(type_handle, is_exact);
  }

  static ReferenceTypeInfo CreateInvalid() { return ReferenceTypeInfo(); }

  static bool IsValidHandle(TypeHandle handle) {
    return handle.GetReference() != nullptr;
  }

  bool IsValid() const {
    return IsValidHandle(type_handle_);
  }

  bool IsExact() const { return is_exact_; }

  bool IsObjectClass() const REQUIRES_SHARED(Locks::mutator_lock_) {
    DCHECK(IsValid());
    return GetTypeHandle()->IsObjectClass();
  }

  bool IsStringClass() const REQUIRES_SHARED(Locks::mutator_lock_) {
    DCHECK(IsValid());
    return GetTypeHandle()->IsStringClass();
  }

  bool IsObjectArray() const REQUIRES_SHARED(Locks::mutator_lock_) {
    DCHECK(IsValid());
    return IsArrayClass() && GetTypeHandle()->GetComponentType()->IsObjectClass();
  }

  bool IsInterface() const REQUIRES_SHARED(Locks::mutator_lock_) {
    DCHECK(IsValid());
    return GetTypeHandle()->IsInterface();
  }

  bool IsArrayClass() const REQUIRES_SHARED(Locks::mutator_lock_) {
    DCHECK(IsValid());
    return GetTypeHandle()->IsArrayClass();
  }

  bool IsPrimitiveArrayClass() const REQUIRES_SHARED(Locks::mutator_lock_) {
    DCHECK(IsValid());
    return GetTypeHandle()->IsPrimitiveArray();
  }

  bool IsNonPrimitiveArrayClass() const REQUIRES_SHARED(Locks::mutator_lock_) {
    DCHECK(IsValid());
    return IsArrayClass() && !GetTypeHandle()->IsPrimitiveArray();
  }

  bool CanArrayHold(ReferenceTypeInfo rti)  const REQUIRES_SHARED(Locks::mutator_lock_) {
    DCHECK(IsValid());
    if (!IsExact()) return false;
    if (!IsArrayClass()) return false;
    return GetTypeHandle()->GetComponentType()->IsAssignableFrom(rti.GetTypeHandle().Get());
  }

  bool CanArrayHoldValuesOf(ReferenceTypeInfo rti)  const REQUIRES_SHARED(Locks::mutator_lock_) {
    DCHECK(IsValid());
    if (!IsExact()) return false;
    if (!IsArrayClass()) return false;
    if (!rti.IsArrayClass()) return false;
    return GetTypeHandle()->GetComponentType()->IsAssignableFrom(
        rti.GetTypeHandle()->GetComponentType());
  }

  Handle<mirror::Class> GetTypeHandle() const { return type_handle_; }

  bool IsSupertypeOf(ReferenceTypeInfo rti) const REQUIRES_SHARED(Locks::mutator_lock_) {
    DCHECK(IsValid());
    DCHECK(rti.IsValid());
    return GetTypeHandle()->IsAssignableFrom(rti.GetTypeHandle().Get());
  }

  // Returns true if the type information provide the same amount of details.
  // Note that it does not mean that the instructions have the same actual type
  // (because the type can be the result of a merge).
  bool IsEqual(ReferenceTypeInfo rti) const REQUIRES_SHARED(Locks::mutator_lock_) {
    if (!IsValid() && !rti.IsValid()) {
      // Invalid types are equal.
      return true;
    }
    if (!IsValid() || !rti.IsValid()) {
      // One is valid, the other not.
      return false;
    }
    return IsExact() == rti.IsExact() && GetTypeHandle().Get() == rti.GetTypeHandle().Get();
  }

 private:
  ReferenceTypeInfo() : type_handle_(TypeHandle()), is_exact_(false) {}
  ReferenceTypeInfo(TypeHandle type_handle, bool is_exact)
      : type_handle_(type_handle), is_exact_(is_exact) { }

  static void DCheckValidTypeInfo(TypeHandle type_handle, bool is_exact);

  // The class of the object.
  TypeHandle type_handle_;
  // Whether or not the type is exact or a superclass of the actual type.
  // Whether or not we have any information about this type.
  bool is_exact_;
};

std::ostream& operator<<(std::ostream& os, const ReferenceTypeInfo& rhs);

}  // namespace art

#endif  // ART_COMPILER_OPTIMIZING_REFERENCE_TYPE_INFO_H_
