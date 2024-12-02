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

#ifndef ART_RUNTIME_VERIFIER_REG_TYPE_H_
#define ART_RUNTIME_VERIFIER_REG_TYPE_H_

#include <stdint.h>
#include <limits>
#include <set>
#include <string>
#include <string_view>

#include "base/arena_object.h"
#include "base/bit_vector.h"
#include "base/locks.h"
#include "base/logging.h"
#include "base/macros.h"
#include "dex/primitive.h"
#include "gc_root.h"
#include "handle.h"
#include "handle_scope.h"
#include "obj_ptr.h"

namespace art HIDDEN {
namespace mirror {
class Class;
class ClassLoader;
}  // namespace mirror

class ArenaAllocator;
class ArenaBitVector;

namespace verifier {

class MethodVerifier;
class RegTypeCache;

#define FOR_EACH_CONCRETE_REG_TYPE(V)                                         \
  V(Undefined)                                                                \
  V(Conflict)                                                                 \
  V(Boolean)                                                                  \
  V(Byte)                                                                     \
  V(Char)                                                                     \
  V(Short)                                                                    \
  V(Integer)                                                                  \
  V(LongLo)                                                                   \
  V(LongHi)                                                                   \
  V(Float)                                                                    \
  V(DoubleLo)                                                                 \
  V(DoubleHi)                                                                 \
  /* Category 1 groups of constant types are ordered by increasing range */   \
  /* within the non-negative and can-be-negative groups, so that merging */   \
  /* can simply use the type with the higher kind value. */                   \
  V(Zero)                                                                     \
  V(BooleanConstant)                                                          \
  V(PositiveByteConstant)                                                     \
  V(PositiveShortConstant)                                                    \
  V(CharConstant)                                                             \
  V(ByteConstant)                                                             \
  V(ShortConstant)                                                            \
  V(IntegerConstant)                                                          \
  V(ConstantLo)                                                               \
  V(ConstantHi)                                                               \
  V(Null)                                                                     \
  V(JavaLangObject)                                                           \
  V(UnresolvedReference)                                                      \
  V(UninitializedReference)                                                   \
  V(UninitializedThisReference)                                               \
  V(UnresolvedUninitializedReference)                                         \
  V(UnresolvedUninitializedThisReference)                                     \
  V(UnresolvedMergedReference)                                                \
  V(Reference)                                                                \

#define FORWARD_DECLARE_REG_TYPE(name) class name##Type;
FOR_EACH_CONCRETE_REG_TYPE(FORWARD_DECLARE_REG_TYPE)
#undef FORWARD_DECLARE_REG_TYPE

/*
 * RegType holds information about the "type" of data held in a register.
 */
class RegType {
 public:
  enum Kind : uint8_t {
#define DEFINE_REG_TYPE_ENUMERATOR(name) \
    k##name,
    FOR_EACH_CONCRETE_REG_TYPE(DEFINE_REG_TYPE_ENUMERATOR)
#undef DEFINE_REG_TYPE_ENUMERATOR
  };

  static constexpr size_t NumberOfKinds() {
#define ADD_ONE_FOR_CONCRETE_REG_TYPE(name) + 1
    return 0 FOR_EACH_CONCRETE_REG_TYPE(ADD_ONE_FOR_CONCRETE_REG_TYPE);
#undef ADD_ONE_FOR_CONCRETE_REG_TYPE
  }

  constexpr Kind GetKind() const { return kind_; }

#define DEFINE_IS_CONCRETE_REG_TYPE(name) \
  constexpr bool Is##name() const { return GetKind() == Kind::k##name; }
  FOR_EACH_CONCRETE_REG_TYPE(DEFINE_IS_CONCRETE_REG_TYPE)
#undef DEFINE_IS_CONCRETE_REG_TYPE

  constexpr bool IsConstantTypes() const {
    return IsConstant() || IsConstantLo() || IsConstantHi() || IsNull();
  }
  constexpr bool IsConstant() const {
    return IsZero() ||
           IsBooleanConstant() ||
           IsPositiveByteConstant() ||
           IsPositiveShortConstant() ||
           IsCharConstant() ||
           IsByteConstant() ||
           IsShortConstant() ||
           IsIntegerConstant();
  }

  constexpr bool IsNonZeroReferenceTypes() const;
  constexpr bool IsUninitializedTypes() const;
  constexpr bool IsUnresolvedTypes() const;

  static constexpr bool IsLowHalf(Kind kind) {
    return kind == Kind::kLongLo || kind == Kind::kDoubleLo || kind == Kind::kConstantLo;
  }
  static constexpr bool IsHighHalf(Kind kind) {
    return kind == Kind::kLongHi || kind == Kind::kDoubleHi || kind == Kind::kConstantHi;
  }

  constexpr bool IsLowHalf() const { return IsLowHalf(GetKind()); }
  constexpr bool IsHighHalf() const { return IsHighHalf(GetKind()); }
  constexpr bool IsLongOrDoubleTypes() const { return IsLowHalf(); }

  static constexpr Kind ToHighHalf(Kind low) {
    static_assert(Kind::kConstantLo + 1 == Kind::kConstantHi);
    static_assert(Kind::kDoubleLo + 1 == Kind::kDoubleHi);
    static_assert(Kind::kLongLo + 1 == Kind::kLongHi);
    DCHECK(low == Kind::kConstantLo || low == Kind::kDoubleLo || low == Kind::kLongLo);
    return enum_cast<Kind>(low + 1);
  }

  // Check that `low` is the low half, and that `high` is its matching high-half.
  static inline bool CheckWidePair(Kind low, Kind high) {
    return (low == Kind::kConstantLo || low == Kind::kDoubleLo || low == Kind::kLongLo) &&
           high == ToHighHalf(low);
  }
  // Check this is the low half, and that type_h is its matching high-half.
  inline bool CheckWidePair(const RegType& type_h) const {
    return CheckWidePair(GetKind(), type_h.GetKind());
  }

  // The high half that corresponds to this low half
  const RegType& HighHalf(RegTypeCache* cache) const
      REQUIRES_SHARED(Locks::mutator_lock_);

  constexpr bool IsReferenceTypes() const {
    return IsNonZeroReferenceTypes() || IsZero() || IsNull();
  }
  constexpr bool IsZeroOrNull() const {
    return IsZero() || IsNull();
  }
  bool IsCategory1Types() const {
    return IsIntegralTypes() || IsFloat();
  }
  bool IsCategory2Types() const {
    return IsLowHalf();  // Don't expect explicit testing of high halves
  }
  static constexpr bool IsBooleanTypes(Kind kind) {
    return kind == Kind::kBoolean || kind == Kind::kZero || kind == Kind::kBooleanConstant;
  }
  constexpr bool IsBooleanTypes() const { return IsBooleanTypes(GetKind()); }
  constexpr bool IsByteTypes() const {
    return IsByte() || IsPositiveByteConstant() || IsByteConstant() || IsBooleanTypes();
  }
  constexpr bool IsShortTypes() const {
    return IsShort() || IsPositiveShortConstant() || IsShortConstant() || IsByteTypes();
  }
  constexpr bool IsCharTypes() const {
    return IsChar() ||
           IsCharConstant() ||
           IsPositiveShortConstant() ||
           IsPositiveByteConstant() ||
           IsBooleanTypes();
  }
  constexpr bool IsIntegralTypes() const {
    return IsInteger() || IsIntegerConstant() || IsChar() || IsCharConstant() || IsShortTypes();
  }
  // Give the constant value encoded, but this shouldn't be called in the
  // general case.
  bool IsArrayIndexTypes() const { return IsIntegralTypes(); }
  // Float type may be derived from any constant type
  constexpr bool IsFloatTypes() const { return IsFloat() || IsConstant(); }
  constexpr bool IsLongTypes() const { return IsLongLo() || IsConstantLo(); }
  constexpr bool IsLongHighTypes() const { return (IsLongHi() || IsConstantHi()); }
  constexpr bool IsDoubleTypes() const { return IsDoubleLo() || IsConstantLo(); }
  constexpr bool IsDoubleHighTypes() const { return (IsDoubleHi() || IsConstantHi()); }
  bool HasClass() const {
    // The only type with a class is `ReferenceType`. There is no class for
    // unresolved types and we do not record the class in uninitialized types.
    // We do not need the class for primitive types.
    return IsReference();
  }
  bool IsArrayTypes() const REQUIRES_SHARED(Locks::mutator_lock_);
  bool IsObjectArrayTypes() const REQUIRES_SHARED(Locks::mutator_lock_);
  Primitive::Type GetPrimitiveType() const;
  bool IsJavaLangObjectArray() const
      REQUIRES_SHARED(Locks::mutator_lock_);
  bool IsInstantiableTypes() const REQUIRES_SHARED(Locks::mutator_lock_);
  constexpr const std::string_view& GetDescriptor() const {
    DCHECK(IsJavaLangObject() ||
           IsReference() ||
           IsUninitializedTypes() ||
           (IsUnresolvedTypes() && !IsUnresolvedMergedReference()));
    return descriptor_;
  }
  ObjPtr<mirror::Class> GetClass() const REQUIRES_SHARED(Locks::mutator_lock_);
  Handle<mirror::Class> GetClassHandle() const REQUIRES_SHARED(Locks::mutator_lock_);
  uint16_t GetId() const { return cache_id_; }

  std::string Dump() const REQUIRES_SHARED(Locks::mutator_lock_);

  enum class Assignability : uint8_t {
    kAssignable,
    kNotAssignable,
    kNarrowingConversion,
    kReference,
    kInvalid,
  };

  ALWAYS_INLINE static inline Assignability AssignabilityFrom(Kind lhs, Kind rhs);

  // Can this type be assigned by src?
  // Note: Object and interface types may always be assigned to one another, see
  // comment on
  // ClassJoin.
  bool IsAssignableFrom(const RegType& src, MethodVerifier* verifier) const
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Can this type be assigned by src? Variant of IsAssignableFrom that doesn't
  // allow assignment to
  // an interface from an Object.
  bool IsStrictlyAssignableFrom(const RegType& src, MethodVerifier* verifier) const
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Are these RegTypes the same?
  bool Equals(const RegType& other) const { return GetId() == other.GetId(); }

  // Compute the merge of this register from one edge (path) with incoming_type
  // from another.
  const RegType& Merge(const RegType& incoming_type,
                       RegTypeCache* reg_types,
                       MethodVerifier* verifier) const
      REQUIRES_SHARED(Locks::mutator_lock_);
  // Same as above, but also handles the case where incoming_type == this.
  const RegType& SafeMerge(const RegType& incoming_type,
                           RegTypeCache* reg_types,
                           MethodVerifier* verifier) const
      REQUIRES_SHARED(Locks::mutator_lock_) {
    if (Equals(incoming_type)) {
      return *this;
    }
    return Merge(incoming_type, reg_types, verifier);
  }

  constexpr ~RegType() {}

  static void* operator new(size_t size) noexcept {
    return ::operator new(size);
  }

  static void* operator new(size_t size, ArenaAllocator* allocator);
  static void* operator new(size_t size, ScopedArenaAllocator* allocator) = delete;

 protected:
  constexpr RegType(const std::string_view& descriptor, uint16_t cache_id, Kind kind)
      REQUIRES_SHARED(Locks::mutator_lock_)
      : descriptor_(descriptor),
        cache_id_(cache_id),
        kind_(kind) {}

  template <typename Class>
  constexpr void CheckConstructorInvariants([[maybe_unused]] Class* this_) const
      REQUIRES_SHARED(Locks::mutator_lock_);

  const std::string_view descriptor_;
  const uint16_t cache_id_;
  const Kind kind_;

  friend class RegTypeCache;

 private:
  static bool AssignableFrom(const RegType& lhs,
                             const RegType& rhs,
                             bool strict,
                             MethodVerifier* verifier)
      REQUIRES_SHARED(Locks::mutator_lock_);

  DISALLOW_COPY_AND_ASSIGN(RegType);
};

std::ostream& operator<<(std::ostream& os, RegType::Kind kind);
std::ostream& operator<<(std::ostream& os, RegType::Assignability assignability);

// Bottom type.
class ConflictType final : public RegType {
 public:
  constexpr ConflictType(uint16_t cache_id)
      REQUIRES_SHARED(Locks::mutator_lock_);
};

// A variant of the bottom type used to specify an undefined value in the
// incoming registers.
// Merging with UndefinedType yields ConflictType which is the true bottom.
class UndefinedType final : public RegType {
 public:
  constexpr UndefinedType(uint16_t cache_id)
      REQUIRES_SHARED(Locks::mutator_lock_);
};

class PrimitiveType : public RegType {
 public:
  constexpr PrimitiveType(const std::string_view& descriptor, uint16_t cache_id, Kind kind)
      REQUIRES_SHARED(Locks::mutator_lock_)
      : RegType(descriptor, cache_id, kind) {
    DCHECK_EQ(descriptor.length(), 1u);
  }
};

class Cat1Type : public PrimitiveType {
 public:
  constexpr Cat1Type(const std::string_view& descriptor, uint16_t cache_id, Kind kind)
      REQUIRES_SHARED(Locks::mutator_lock_)
      : PrimitiveType(descriptor, cache_id, kind) {}
};

class IntegerType final : public Cat1Type {
 public:
  constexpr IntegerType(const std::string_view& descriptor, uint16_t cache_id)
      REQUIRES_SHARED(Locks::mutator_lock_);
};

class BooleanType final : public Cat1Type {
 public:
  constexpr BooleanType(const std::string_view& descriptor, uint16_t cache_id)
      REQUIRES_SHARED(Locks::mutator_lock_);
};

class ByteType final : public Cat1Type {
 public:
  constexpr ByteType(const std::string_view& descriptor, uint16_t cache_id)
      REQUIRES_SHARED(Locks::mutator_lock_);
};

class ShortType final : public Cat1Type {
 public:
  constexpr ShortType(const std::string_view& descriptor, uint16_t cache_id)
      REQUIRES_SHARED(Locks::mutator_lock_);
};

class CharType final : public Cat1Type {
 public:
  constexpr CharType(const std::string_view& descriptor, uint16_t cache_id)
      REQUIRES_SHARED(Locks::mutator_lock_);
};

class FloatType final : public Cat1Type {
 public:
  constexpr FloatType(const std::string_view& descriptor, uint16_t cache_id)
      REQUIRES_SHARED(Locks::mutator_lock_);
};

class Cat2Type : public PrimitiveType {
 public:
  constexpr Cat2Type(const std::string_view& descriptor, uint16_t cache_id, Kind kind)
      REQUIRES_SHARED(Locks::mutator_lock_)
      : PrimitiveType(descriptor, cache_id, kind) {}
};

class LongLoType final : public Cat2Type {
 public:
  constexpr LongLoType(const std::string_view& descriptor, uint16_t cache_id)
      REQUIRES_SHARED(Locks::mutator_lock_);
};

class LongHiType final : public Cat2Type {
 public:
  constexpr LongHiType(const std::string_view& descriptor, uint16_t cache_id)
      REQUIRES_SHARED(Locks::mutator_lock_);
};

class DoubleLoType final : public Cat2Type {
 public:
  constexpr DoubleLoType(const std::string_view& descriptor, uint16_t cache_id)
      REQUIRES_SHARED(Locks::mutator_lock_);
};

class DoubleHiType final : public Cat2Type {
 public:
  constexpr DoubleHiType(const std::string_view& descriptor, uint16_t cache_id)
      REQUIRES_SHARED(Locks::mutator_lock_);
};

class ConstantType : public RegType {
 public:
  constexpr ConstantType(uint16_t cache_id, Kind kind)
      REQUIRES_SHARED(Locks::mutator_lock_)
      : RegType("", cache_id, kind) {}
};

// Constant 0, or merged constants 0. Can be interpreted as `null`.
class ZeroType final : public ConstantType {
 public:
  constexpr explicit ZeroType(uint16_t cache_id) REQUIRES_SHARED(Locks::mutator_lock_);
};

// Constant 1, or merged constants 0 - 1.
class BooleanConstantType final : public ConstantType {
 public:
  constexpr explicit BooleanConstantType(uint16_t cache_id) REQUIRES_SHARED(Locks::mutator_lock_);
};

// Constants 2 - 0x7f, or merged constants 0 - 0x7f.
class PositiveByteConstantType final : public ConstantType {
 public:
  constexpr explicit PositiveByteConstantType(uint16_t cache_id)
      REQUIRES_SHARED(Locks::mutator_lock_);
};

// Constants 0x80 - 0x7fff, or merged constants 0 - 0x7fff.
class PositiveShortConstantType final : public ConstantType {
 public:
  constexpr explicit PositiveShortConstantType(uint16_t cache_id)
      REQUIRES_SHARED(Locks::mutator_lock_);
};

// Constants 0x8000 - 0xffff, or merged constants 0 - 0xffff.
class CharConstantType final : public ConstantType {
 public:
  constexpr explicit CharConstantType(uint16_t cache_id) REQUIRES_SHARED(Locks::mutator_lock_);
};

// Constants -0x80 - -1, or merged constants -x80 - 0x7f.
class ByteConstantType final : public ConstantType {
 public:
  constexpr explicit ByteConstantType(uint16_t cache_id) REQUIRES_SHARED(Locks::mutator_lock_);
};

// Constants -0x8000 - -0x81, or merged constants -x8000 - 0x7fff.
class ShortConstantType final : public ConstantType {
 public:
  constexpr explicit ShortConstantType(uint16_t cache_id) REQUIRES_SHARED(Locks::mutator_lock_);
};

// Constants -0x80000000 - -0x8001, or merged constants -0x80000000 - 0x7fffffff.
class IntegerConstantType final : public ConstantType {
 public:
  constexpr explicit IntegerConstantType(uint16_t cache_id) REQUIRES_SHARED(Locks::mutator_lock_);
};

class ConstantLoType final : public ConstantType {
 public:
  constexpr explicit ConstantLoType(uint16_t cache_id) REQUIRES_SHARED(Locks::mutator_lock_);
};

class ConstantHiType final : public ConstantType {
 public:
  constexpr explicit ConstantHiType(uint16_t cache_id) REQUIRES_SHARED(Locks::mutator_lock_);
};

// Special "null" type that captures the semantics of null / bottom.
class NullType final : public ConstantType {
 public:
  constexpr NullType(uint16_t cache_id)
      REQUIRES_SHARED(Locks::mutator_lock_);
};

// The reference type for `java.lang.Object` class is specialized to allow compile-time
// evaluation of merged types and assignablility. Note that we do not record the trivial
// assignability for `java.lang.Object` in the `VerifierDeps`.
class JavaLangObjectType final : public RegType {
 public:
  constexpr JavaLangObjectType(std::string_view descriptor,
                               uint16_t cache_id,
                               const UninitializedReferenceType* uninitialized_type)
      REQUIRES_SHARED(Locks::mutator_lock_);

  const UninitializedReferenceType* GetUninitializedType() const {
    return uninitialized_type_;
  }

 private:
  const UninitializedReferenceType* const uninitialized_type_;
};

// Common parent of all uninitialized types. Uninitialized types are created by
// "new" dex
// instructions and must be passed to a constructor.
class UninitializedType : public RegType {
 public:
  constexpr UninitializedType(const std::string_view& descriptor, uint16_t cache_id, Kind kind)
      REQUIRES_SHARED(Locks::mutator_lock_)
      : RegType(descriptor, cache_id, kind) {}
};

// A type of register holding a reference to an Object of type GetClass or a
// sub-class.
class ReferenceType final : public RegType {
 public:
  ReferenceType(Handle<mirror::Class> klass,
                const std::string_view& descriptor,
                uint16_t cache_id) REQUIRES_SHARED(Locks::mutator_lock_)
      : RegType(descriptor, cache_id, Kind::kReference),
        klass_(klass),
        uninitialized_type_(nullptr) {
    CheckConstructorInvariants(this);
  }

  ObjPtr<mirror::Class> GetClassImpl() const REQUIRES_SHARED(Locks::mutator_lock_) {
    DCHECK(!klass_.IsNull());
    return klass_.Get();
  }

  Handle<mirror::Class> GetClassHandleImpl() const REQUIRES_SHARED(Locks::mutator_lock_) {
    DCHECK(!klass_.IsNull());
    return klass_;
  }

  const UninitializedReferenceType* GetUninitializedType() const {
    return uninitialized_type_;
  }

  void SetUninitializedType(const UninitializedReferenceType* uninitialized_type) const {
    uninitialized_type_ = uninitialized_type;
  }

  void CheckClassDescriptor() const REQUIRES_SHARED(Locks::mutator_lock_);

 private:
  const Handle<mirror::Class> klass_;

  // The corresponding uninitialized type created from this type for a `new-instance` instruction.
  // This member is mutable because it's a part of the type cache, not part of the type itself.
  mutable const UninitializedReferenceType* uninitialized_type_;
};

// Similar to ReferenceType but not yet having been passed to a constructor.
class UninitializedReferenceType final : public UninitializedType {
 public:
  constexpr UninitializedReferenceType(uint16_t cache_id, const RegType* initialized_type)
      REQUIRES_SHARED(Locks::mutator_lock_);

  const RegType* GetInitializedType() const {
    return initialized_type_;
  }

 private:
  // The corresponding initialized type to transition to after a constructor call.
  const RegType* const initialized_type_;
};

// Similar to UninitializedReferenceType but special case for the this argument
// of a constructor.
class UninitializedThisReferenceType final : public UninitializedType {
 public:
  UninitializedThisReferenceType(uint16_t cache_id, const ReferenceType* initialized_type)
      REQUIRES_SHARED(Locks::mutator_lock_)
      : UninitializedType(initialized_type->GetDescriptor(),
                          cache_id,
                          Kind::kUninitializedThisReference),
        initialized_type_(initialized_type) {
    CheckConstructorInvariants(this);
  }

  const ReferenceType* GetInitializedType() const {
    return initialized_type_;
  }

 private:
  // The corresponding initialized type to transition to after a constructor call.
  const ReferenceType* initialized_type_;
};

// Common parent of unresolved types.
class UnresolvedType : public RegType {
 public:
  UnresolvedType(const std::string_view& descriptor, uint16_t cache_id, Kind kind)
      REQUIRES_SHARED(Locks::mutator_lock_)
      : RegType(descriptor, cache_id, kind) {}
};

// Similar to ReferenceType except the Class couldn't be loaded. Assignability
// and other tests made
// of this type must be conservative.
class UnresolvedReferenceType final : public UnresolvedType {
 public:
  UnresolvedReferenceType(const std::string_view& descriptor, uint16_t cache_id)
      REQUIRES_SHARED(Locks::mutator_lock_)
      : UnresolvedType(descriptor, cache_id, Kind::kUnresolvedReference),
        uninitialized_type_(nullptr) {
    CheckConstructorInvariants(this);
  }

  const UnresolvedUninitializedReferenceType* GetUninitializedType() const {
    return uninitialized_type_;
  }

  void SetUninitializedType(const UnresolvedUninitializedReferenceType* uninitialized_type) const {
    uninitialized_type_ = uninitialized_type;
  }

 private:
  // The corresponding uninitialized type created from this type for a `new-instance` instruction.
  // This member is mutable because it's a part of the type cache, not part of the type itself.
  mutable const UnresolvedUninitializedReferenceType* uninitialized_type_;
};

// Similar to UnresolvedReferenceType but not yet having been passed to a
// constructor.
class UnresolvedUninitializedReferenceType final : public UninitializedType {
 public:
  UnresolvedUninitializedReferenceType(uint16_t cache_id,
                                       const UnresolvedReferenceType* initialized_type)
      REQUIRES_SHARED(Locks::mutator_lock_)
      : UninitializedType(initialized_type->GetDescriptor(),
                          cache_id,
                          Kind::kUnresolvedUninitializedReference),
        initialized_type_(initialized_type) {
    CheckConstructorInvariants(this);
  }

  const UnresolvedReferenceType* GetInitializedType() const {
    return initialized_type_;
  }

 private:
  // The corresponding initialized type to transition to after a constructor call.
  const UnresolvedReferenceType* const initialized_type_;
};

class UnresolvedUninitializedThisReferenceType final : public UninitializedType {
 public:
  UnresolvedUninitializedThisReferenceType(uint16_t cache_id,
                                           const UnresolvedReferenceType* initialized_type)
      REQUIRES_SHARED(Locks::mutator_lock_)
      : UninitializedType(initialized_type->GetDescriptor(),
                          cache_id,
                          Kind::kUnresolvedUninitializedThisReference),
        initialized_type_(initialized_type) {
    CheckConstructorInvariants(this);
  }

  const UnresolvedReferenceType* GetInitializedType() const {
    return initialized_type_;
  }

 private:
  // The corresponding initialized type to transition to after a constructor call.
  const UnresolvedReferenceType* initialized_type_;
};

// A merge of unresolved (and resolved) types. If the types were resolved this may be
// Conflict or another known ReferenceType.
class UnresolvedMergedReferenceType final : public UnresolvedType {
 public:
  // Note: the constructor will copy the unresolved BitVector, not use it directly.
  UnresolvedMergedReferenceType(const RegType& resolved,
                                const BitVector& unresolved,
                                const RegTypeCache* reg_type_cache,
                                uint16_t cache_id)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // The resolved part. See description below.
  const RegType& GetResolvedPart() const {
    return resolved_part_;
  }
  // The unresolved part.
  const BitVector& GetUnresolvedTypes() const {
    return unresolved_types_;
  }

  bool IsArrayTypesImpl() const REQUIRES_SHARED(Locks::mutator_lock_);
  bool IsObjectArrayTypesImpl() const REQUIRES_SHARED(Locks::mutator_lock_);
  std::string DumpImpl() const REQUIRES_SHARED(Locks::mutator_lock_);

  const RegTypeCache* GetRegTypeCache() const { return reg_type_cache_; }

 private:
  void CheckInvariants() const REQUIRES_SHARED(Locks::mutator_lock_);

  const RegTypeCache* const reg_type_cache_;

  // The original implementation of merged types was a binary tree. Collection of the flattened
  // types ("leaves") can be expensive, so we store the expanded list now, as two components:
  // 1) A resolved component. We use Zero when there is no resolved component, as that will be
  //    an identity merge.
  // 2) A bitvector of the unresolved reference types. A bitvector was chosen with the assumption
  //    that there should not be too many types in flight in practice. (We also bias the index
  //    against the index of Zero, which is one of the later default entries in any cache.)
  const RegType& resolved_part_;
  const BitVector unresolved_types_;
};

std::ostream& operator<<(std::ostream& os, const RegType& rhs)
    REQUIRES_SHARED(Locks::mutator_lock_);

namespace detail {

template <class ConcreteRegType>
struct RegTypeToKind { /* No `kind` defined in unspecialized template. */ };

#define DEFINE_REG_TYPE_TO_KIND(name) \
  template<> struct RegTypeToKind<name##Type> { \
    static constexpr RegType::Kind kind = RegType::Kind::k##name; \
  };
FOR_EACH_CONCRETE_REG_TYPE(DEFINE_REG_TYPE_TO_KIND);
#undef DEFINE_REG_TYPE_TO_KIND

template <template <class ConcreteRegType> class Traits>
constexpr bool EvaluateTrait(RegType::Kind kind) {
  switch (kind) {
#define DEFINE_EVALUATE_TRAIT_CASE(name) \
    case RegType::Kind::k##name:         \
      return Traits<name##Type>::value;
    FOR_EACH_CONCRETE_REG_TYPE(DEFINE_EVALUATE_TRAIT_CASE);
#undef DEFINE_EVALUATE_TRAIT_CASE
  }
}

template <class ConcreteRegType>
struct IsUninitializedTypes
    : std::bool_constant<std::is_base_of_v<UninitializedType, ConcreteRegType>> {};

template <class ConcreteRegType>
struct IsUnresolvedTypes : std::bool_constant<
    std::is_base_of_v<UnresolvedType, ConcreteRegType> ||
    // Unresolved uninitialized types do not inherit `UnresolvedType`.
    // (We're using single-inheritance and they inherit `UninitializedType`.)
    std::is_same_v<UnresolvedUninitializedReferenceType, ConcreteRegType> ||
    std::is_same_v<UnresolvedUninitializedThisReferenceType, ConcreteRegType>> {};

template <class ConcreteRegType>
struct IsNonZeroReferenceTypes
    : std::bool_constant<std::is_same_v<JavaLangObjectType, ConcreteRegType> ||
                         std::is_same_v<ReferenceType, ConcreteRegType> ||
                         std::is_base_of_v<UnresolvedType, ConcreteRegType> ||
                         std::is_base_of_v<UninitializedType, ConcreteRegType>> {};

}  // namespace detail

template <typename Class>
inline constexpr void RegType::CheckConstructorInvariants([[maybe_unused]] Class* this_) const {
  static_assert(std::is_final<Class>::value, "Class must be final.");
  DCHECK_EQ(GetKind(), detail::RegTypeToKind<Class>::kind);
  if constexpr (std::is_same_v<Class, UndefinedType> ||
                std::is_same_v<Class, ConflictType> ||
                std::is_same_v<Class, NullType> ||
                std::is_base_of_v<ConstantType, Class>) {
    DCHECK(descriptor_.empty()) << *this;
  } else if constexpr (std::is_base_of_v<PrimitiveType, Class>) {
    DCHECK_EQ(descriptor_.length(), 1u) << *this;
  } else if constexpr (std::is_same_v<Class, JavaLangObjectType>) {
    DCHECK(!descriptor_.empty()) << *this;
  } else if constexpr (std::is_same_v<Class, UnresolvedMergedReferenceType>) {
    // `UnresolvedMergedReferenceType` is an unresolved type but it has an empty descriptor.
    DCHECK(descriptor_.empty()) << *this;
  } else if constexpr (detail::IsUnresolvedTypes<Class>::value ||  // NOLINT
                       std::is_base_of_v<UninitializedType, Class>) {
    DCHECK(!descriptor_.empty()) << *this;
  } else if (kIsDebugBuild) {
    CHECK(IsReference());
    down_cast<const ReferenceType&>(*this).CheckClassDescriptor();
  }
}

constexpr UndefinedType::UndefinedType(uint16_t cache_id)
    : RegType("", cache_id, Kind::kUndefined) {
  CheckConstructorInvariants(this);
}

constexpr ConflictType::ConflictType(uint16_t cache_id)
    : RegType("", cache_id, Kind::kConflict) {
  CheckConstructorInvariants(this);
}

constexpr IntegerType::IntegerType(const std::string_view& descriptor, uint16_t cache_id)
    : Cat1Type(descriptor, cache_id, Kind::kInteger) {
  CheckConstructorInvariants(this);
}

constexpr BooleanType::BooleanType(const std::string_view& descriptor, uint16_t cache_id)
    : Cat1Type(descriptor, cache_id, Kind::kBoolean) {
  CheckConstructorInvariants(this);
}

constexpr ByteType::ByteType(const std::string_view& descriptor, uint16_t cache_id)
    : Cat1Type(descriptor, cache_id, Kind::kByte) {
  CheckConstructorInvariants(this);
}

constexpr ShortType::ShortType(const std::string_view& descriptor, uint16_t cache_id)
    : Cat1Type(descriptor, cache_id, Kind::kShort) {
  CheckConstructorInvariants(this);
}

constexpr CharType::CharType(const std::string_view& descriptor, uint16_t cache_id)
    : Cat1Type(descriptor, cache_id, Kind::kChar) {
  CheckConstructorInvariants(this);
}

constexpr FloatType::FloatType(const std::string_view& descriptor, uint16_t cache_id)
    : Cat1Type(descriptor, cache_id, Kind::kFloat) {
  CheckConstructorInvariants(this);
}

constexpr LongLoType::LongLoType(const std::string_view& descriptor, uint16_t cache_id)
    : Cat2Type(descriptor, cache_id, Kind::kLongLo) {
  CheckConstructorInvariants(this);
}

constexpr LongHiType::LongHiType(const std::string_view& descriptor, uint16_t cache_id)
    : Cat2Type(descriptor, cache_id, Kind::kLongHi) {
  CheckConstructorInvariants(this);
}

constexpr DoubleLoType::DoubleLoType(const std::string_view& descriptor, uint16_t cache_id)
    : Cat2Type(descriptor, cache_id, Kind::kDoubleLo) {
  CheckConstructorInvariants(this);
}

constexpr DoubleHiType::DoubleHiType(const std::string_view& descriptor, uint16_t cache_id)
    : Cat2Type(descriptor, cache_id, Kind::kDoubleHi) {
  CheckConstructorInvariants(this);
}

constexpr ZeroType::ZeroType(uint16_t cache_id)
    : ConstantType(cache_id, Kind::kZero) {
  CheckConstructorInvariants(this);
}

constexpr BooleanConstantType::BooleanConstantType(uint16_t cache_id)
    : ConstantType(cache_id, Kind::kBooleanConstant) {
  CheckConstructorInvariants(this);
}

constexpr PositiveByteConstantType::PositiveByteConstantType(uint16_t cache_id)
    : ConstantType(cache_id, Kind::kPositiveByteConstant) {
  CheckConstructorInvariants(this);
}

constexpr PositiveShortConstantType::PositiveShortConstantType(uint16_t cache_id)
    : ConstantType(cache_id, Kind::kPositiveShortConstant) {
  CheckConstructorInvariants(this);
}

constexpr CharConstantType::CharConstantType(uint16_t cache_id)
    : ConstantType(cache_id, Kind::kCharConstant) {
  CheckConstructorInvariants(this);
}

constexpr ByteConstantType::ByteConstantType(uint16_t cache_id)
    : ConstantType(cache_id, Kind::kByteConstant) {
  CheckConstructorInvariants(this);
}

constexpr ShortConstantType::ShortConstantType(uint16_t cache_id)
    : ConstantType(cache_id, Kind::kShortConstant) {
  CheckConstructorInvariants(this);
}

constexpr IntegerConstantType::IntegerConstantType(uint16_t cache_id)
    : ConstantType(cache_id, Kind::kIntegerConstant) {
  CheckConstructorInvariants(this);
}

constexpr ConstantLoType::ConstantLoType(uint16_t cache_id)
    : ConstantType(cache_id, Kind::kConstantLo) {
  CheckConstructorInvariants(this);
}

constexpr ConstantHiType::ConstantHiType(uint16_t cache_id)
    : ConstantType(cache_id, Kind::kConstantHi) {
  CheckConstructorInvariants(this);
}

constexpr NullType::NullType(uint16_t cache_id)
    : ConstantType(cache_id, Kind::kNull) {
  CheckConstructorInvariants(this);
}

constexpr JavaLangObjectType::JavaLangObjectType(
    std::string_view descriptor,
    uint16_t cache_id,
    const UninitializedReferenceType* uninitialized_type)
    : RegType(descriptor, cache_id, Kind::kJavaLangObject),
      uninitialized_type_(uninitialized_type) {
  CheckConstructorInvariants(this);
}

constexpr UninitializedReferenceType::UninitializedReferenceType(uint16_t cache_id,
                                                                 const RegType* initialized_type)
    : UninitializedType(initialized_type->GetDescriptor(),
                        cache_id,
                        Kind::kUninitializedReference),
      initialized_type_(initialized_type) {
  CheckConstructorInvariants(this);
}

constexpr bool RegType::IsNonZeroReferenceTypes() const {
  return detail::EvaluateTrait<detail::IsNonZeroReferenceTypes>(GetKind());
}

constexpr bool RegType::IsUninitializedTypes() const {
  return detail::EvaluateTrait<detail::IsUninitializedTypes>(GetKind());
}

constexpr bool RegType::IsUnresolvedTypes() const {
  return detail::EvaluateTrait<detail::IsUnresolvedTypes>(GetKind());
}

}  // namespace verifier
}  // namespace art

#endif  // ART_RUNTIME_VERIFIER_REG_TYPE_H_
