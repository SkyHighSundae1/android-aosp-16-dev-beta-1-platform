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

#include "reg_type-inl.h"

#include "android-base/stringprintf.h"

#include "base/arena_bit_vector.h"
#include "base/bit_vector-inl.h"
#include "base/casts.h"
#include "class_linker-inl.h"
#include "dex/descriptors_names.h"
#include "dex/dex_file-inl.h"
#include "method_verifier.h"
#include "mirror/class-inl.h"
#include "mirror/class.h"
#include "mirror/object-inl.h"
#include "mirror/object_array-inl.h"
#include "reg_type_cache-inl.h"
#include "scoped_thread_state_change-inl.h"

#include <limits>
#include <sstream>

namespace art HIDDEN {
namespace verifier {

using android::base::StringPrintf;

std::ostream& operator<<(std::ostream& os, RegType::Kind kind) {
  const char* kind_name;
  switch (kind) {
#define DEFINE_REG_TYPE_CASE(name) \
    case RegType::Kind::k##name:   \
      kind_name = #name;           \
      break;
    FOR_EACH_CONCRETE_REG_TYPE(DEFINE_REG_TYPE_CASE)
#undef DEFINE_REG_TYPE_CASE
    default:
      return os << "Corrupted RegType::Kind: " << static_cast<uint32_t>(kind);
  }
  return os << kind_name;
}

std::ostream& operator<<(std::ostream& os, RegType::Assignability assignability) {
  const char* assignability_name;
  switch (assignability) {
    case RegType::Assignability::kAssignable:
      assignability_name = "Assignable";
      break;
    case RegType::Assignability::kNotAssignable:
      assignability_name = "NotAssignable";
      break;
    case RegType::Assignability::kNarrowingConversion:
      assignability_name = "NarrowingConversion";
      break;
    case RegType::Assignability::kReference:
      assignability_name = "Reference";
      break;
    case RegType::Assignability::kInvalid:
      assignability_name = "Invalid";
      break;
    default:
      return os << "Corrupted RegType::Assignability: " << static_cast<uint32_t>(assignability);
  }
  return os << assignability_name;
}

std::string RegType::Dump() const {
  std::string_view reference_tag;
  switch (GetKind()) {
    case Kind::kUndefined: return "Undefined";
    case Kind::kConflict: return "Conflict";
    case Kind::kBoolean: return "Boolean";
    case Kind::kByte: return "Byte";
    case Kind::kShort: return "Short";
    case Kind::kChar: return "Char";
    case Kind::kInteger: return "Integer";
    case Kind::kLongLo: return "Long (Low Half)";
    case Kind::kLongHi: return "Long (High Half)";
    case Kind::kFloat: return "Float";
    case Kind::kDoubleLo: return "Double (Low Half)";
    case Kind::kDoubleHi: return "Double (High Half)";
    case Kind::kZero: return "Zero/null";
    case Kind::kBooleanConstant: return "BooleanConstant";
    case Kind::kPositiveByteConstant: return "PositiveByteConstant";
    case Kind::kPositiveShortConstant: return "PositiveShortConstant";
    case Kind::kCharConstant: return "CharConstant";
    case Kind::kByteConstant: return "ByteConstant";
    case Kind::kShortConstant: return "ShortConstant";
    case Kind::kIntegerConstant: return "IntegerConstant";
    case Kind::kConstantLo: return "Low-half Constant";
    case Kind::kConstantHi: return "High-half Constant";
    case Kind::kNull: return "null";
    case Kind::kJavaLangObject: return "Reference java.lang.Object";

    case Kind::kUnresolvedReference:
      reference_tag = "Unresolved Reference: ";
      break;
    case Kind::kUninitializedReference:
      reference_tag = "Uninitialized Reference: ";
      break;
    case Kind::kUninitializedThisReference:
      reference_tag = "Uninitialized This Reference: ";
      break;
    case Kind::kUnresolvedUninitializedReference:
      reference_tag = "Unresolved And Uninitialized Reference: ";
      break;
    case Kind::kUnresolvedUninitializedThisReference:
      reference_tag = "Unresolved And Uninitialized This Reference: ";
      break;
    case Kind::kReference:
      reference_tag = "Reference: ";
      break;

    case Kind::kUnresolvedMergedReference:
      return down_cast<const UnresolvedMergedReferenceType&>(*this).DumpImpl();
  }
  DCHECK(!reference_tag.empty());
  std::string result(reference_tag);
  AppendPrettyDescriptor(std::string(GetDescriptor()).c_str(), &result);
  return result;
}

std::string UnresolvedMergedReferenceType::DumpImpl() const {
  std::stringstream result;
  result << "UnresolvedMergedReferences(" << GetResolvedPart().Dump() << " | ";
  const BitVector& types = GetUnresolvedTypes();

  bool first = true;
  for (uint32_t idx : types.Indexes()) {
    if (!first) {
      result << ", ";
    } else {
      first = false;
    }
    result << reg_type_cache_->GetFromId(idx).Dump();
  }
  result << ")";
  return result.str();
}

const RegType& RegType::HighHalf(RegTypeCache* cache) const {
  DCHECK(IsLowHalf());
  if (IsLongLo()) {
    return cache->LongHi();
  } else if (IsDoubleLo()) {
    return cache->DoubleHi();
  } else {
    DCHECK(IsConstantLo());
    return cache->ConstantHi();
  }
}

Primitive::Type RegType::GetPrimitiveType() const {
  if (IsNonZeroReferenceTypes()) {
    return Primitive::kPrimNot;
  } else if (IsBooleanTypes()) {
    return Primitive::kPrimBoolean;
  } else if (IsByteTypes()) {
    return Primitive::kPrimByte;
  } else if (IsShortTypes()) {
    return Primitive::kPrimShort;
  } else if (IsCharTypes()) {
    return Primitive::kPrimChar;
  } else if (IsFloat()) {
    return Primitive::kPrimFloat;
  } else if (IsIntegralTypes()) {
    return Primitive::kPrimInt;
  } else if (IsDoubleLo()) {
    return Primitive::kPrimDouble;
  } else {
    DCHECK(IsLongTypes());
    return Primitive::kPrimLong;
  }
}

bool RegType::IsObjectArrayTypes() const {
  if (IsUnresolvedMergedReference()) {
    return down_cast<const UnresolvedMergedReferenceType&>(*this).IsObjectArrayTypesImpl();
  } else if (IsUnresolvedTypes()) {
    // Primitive arrays will always resolve.
    DCHECK(descriptor_[1] == 'L' || descriptor_[1] == '[');
    return descriptor_[0] == '[';
  } else if (HasClass()) {
    ObjPtr<mirror::Class> type = GetClass();
    return type->IsArrayClass() && !type->GetComponentType()->IsPrimitive();
  } else {
    return false;
  }
}

bool RegType::IsArrayTypes() const {
  if (IsUnresolvedMergedReference()) {
    return down_cast<const UnresolvedMergedReferenceType&>(*this).IsArrayTypesImpl();
  } else if (IsUnresolvedTypes()) {
    return descriptor_[0] == '[';
  } else if (HasClass()) {
    return GetClass()->IsArrayClass();
  } else {
    return false;
  }
}

bool RegType::IsJavaLangObjectArray() const {
  if (HasClass()) {
    ObjPtr<mirror::Class> type = GetClass();
    return type->IsArrayClass() && type->GetComponentType()->IsObjectClass();
  }
  return false;
}

bool RegType::IsInstantiableTypes() const {
  DCHECK(IsJavaLangObject() || IsReference() || IsUnresolvedReference()) << *this;
  return !IsReference() || GetClass()->IsInstantiable();
}

static constexpr const RegType& SelectNonConstant(const RegType& a, const RegType& b) {
  return a.IsConstantTypes() ? b : a;
}

static const RegType& SelectNonConstant2(const RegType& a, const RegType& b) {
  return a.IsConstantTypes() ? (b.IsZero() ? a : b) : a;
}

namespace {

ObjPtr<mirror::Class> ArrayClassJoin(ObjPtr<mirror::Class> s,
                                     ObjPtr<mirror::Class> t,
                                     ClassLinker* class_linker)
    REQUIRES_SHARED(Locks::mutator_lock_);

ObjPtr<mirror::Class> InterfaceClassJoin(ObjPtr<mirror::Class> s, ObjPtr<mirror::Class> t)
    REQUIRES_SHARED(Locks::mutator_lock_);

/*
 * A basic Join operation on classes. For a pair of types S and T the Join, written S v T = J, is
 * S <: J, T <: J and for-all U such that S <: U, T <: U then J <: U. That is J is the parent of
 * S and T such that there isn't a parent of both S and T that isn't also the parent of J (ie J
 * is the deepest (lowest upper bound) parent of S and T).
 *
 * This operation applies for regular classes and arrays, however, for interface types there
 * needn't be a partial ordering on the types. We could solve the problem of a lack of a partial
 * order by introducing sets of types, however, the only operation permissible on an interface is
 * invoke-interface. In the tradition of Java verifiers [1] we defer the verification of interface
 * types until an invoke-interface call on the interface typed reference at runtime and allow
 * the perversion of Object being assignable to an interface type (note, however, that we don't
 * allow assignment of Object or Interface to any concrete class and are therefore type safe).
 *
 * Note: This may return null in case of internal errors, e.g., OOME when a new class would have
 *       to be created but there is no heap space. The exception will stay pending, and it is
 *       the job of the caller to handle it.
 *
 * [1] Java bytecode verification: algorithms and formalizations, Xavier Leroy
 */
ObjPtr<mirror::Class> ClassJoin(ObjPtr<mirror::Class> s,
                                ObjPtr<mirror::Class> t,
                                ClassLinker* class_linker)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  DCHECK(!s->IsPrimitive()) << s->PrettyClass();
  DCHECK(!t->IsPrimitive()) << t->PrettyClass();
  if (s == t) {
    return s;
  } else if (s->IsAssignableFrom(t)) {
    return s;
  } else if (t->IsAssignableFrom(s)) {
    return t;
  } else if (s->IsArrayClass() && t->IsArrayClass()) {
    return ArrayClassJoin(s, t, class_linker);
  } else if (s->IsInterface() || t->IsInterface()) {
    return InterfaceClassJoin(s, t);
  } else {
    size_t s_depth = s->Depth();
    size_t t_depth = t->Depth();
    // Get s and t to the same depth in the hierarchy
    if (s_depth > t_depth) {
      while (s_depth > t_depth) {
        s = s->GetSuperClass();
        s_depth--;
      }
    } else {
      while (t_depth > s_depth) {
        t = t->GetSuperClass();
        t_depth--;
      }
    }
    // Go up the hierarchy until we get to the common parent
    while (s != t) {
      s = s->GetSuperClass();
      t = t->GetSuperClass();
    }
    return s;
  }
}

ObjPtr<mirror::Class> ArrayClassJoin(ObjPtr<mirror::Class> s,
                                     ObjPtr<mirror::Class> t,
                                     ClassLinker* class_linker) {
  ObjPtr<mirror::Class> s_ct = s->GetComponentType();
  ObjPtr<mirror::Class> t_ct = t->GetComponentType();
  if (s_ct->IsPrimitive() || t_ct->IsPrimitive()) {
    // Given the types aren't the same, if either array is of primitive types then the only
    // common parent is java.lang.Object
    ObjPtr<mirror::Class> result = s->GetSuperClass();  // short-cut to java.lang.Object
    DCHECK(result->IsObjectClass());
    return result;
  }
  Thread* self = Thread::Current();
  ObjPtr<mirror::Class> common_elem = ClassJoin(s_ct, t_ct, class_linker);
  if (UNLIKELY(common_elem == nullptr)) {
    self->AssertPendingException();
    return nullptr;
  }
  // Note: The following lookup invalidates existing ObjPtr<>s.
  ObjPtr<mirror::Class> array_class = class_linker->FindArrayClass(self, common_elem);
  if (UNLIKELY(array_class == nullptr)) {
    self->AssertPendingException();
    return nullptr;
  }
  return array_class;
}

ObjPtr<mirror::Class> InterfaceClassJoin(ObjPtr<mirror::Class> s, ObjPtr<mirror::Class> t) {
  // This is expensive, as we do not have good data structures to do this even halfway
  // efficiently.
  //
  // We're not following JVMS for interface verification (not everything is assignable to an
  // interface, we trade this for IMT dispatch). We also don't have set types to make up for
  // it. So we choose one arbitrary common ancestor interface by walking the interface tables
  // backwards.
  //
  // For comparison, runtimes following the JVMS will punt all interface type checking to
  // runtime.
  ObjPtr<mirror::IfTable> s_if = s->GetIfTable();
  int32_t s_if_count = s->GetIfTableCount();
  ObjPtr<mirror::IfTable> t_if = t->GetIfTable();
  int32_t t_if_count = t->GetIfTableCount();

  // Note: we'll be using index == count to stand for the argument itself.
  for (int32_t s_it = s_if_count; s_it >= 0; --s_it) {
    ObjPtr<mirror::Class> s_cl = s_it == s_if_count ? s : s_if->GetInterface(s_it);
    if (!s_cl->IsInterface()) {
      continue;
    }

    for (int32_t t_it = t_if_count; t_it >= 0; --t_it) {
      ObjPtr<mirror::Class> t_cl = t_it == t_if_count ? t : t_if->GetInterface(t_it);
      if (!t_cl->IsInterface()) {
        continue;
      }

      if (s_cl == t_cl) {
        // Found something arbitrary in common.
        return s_cl;
      }
    }
  }

  // Return java.lang.Object.
  ObjPtr<mirror::Class> obj_class = s->IsInterface() ? s->GetSuperClass() : t->GetSuperClass();
  DCHECK(obj_class->IsObjectClass());
  return obj_class;
}

class RegTypeMergeImpl final : public RegType {
 public:
  explicit constexpr RegTypeMergeImpl(RegType::Kind kind)
      : RegType("", /* unused cache id */ 0, kind) {}

  constexpr RegType::Kind MergeKind(RegType::Kind incoming_kind) const;
};

constexpr RegType::Kind RegTypeMergeImpl::MergeKind(RegType::Kind incoming_kind) const {
  const RegTypeMergeImpl incoming_type(incoming_kind);
  if (IsUndefined() || incoming_type.IsUndefined()) {
    // There is a difference between undefined and conflict. Conflicts may be copied around, but
    // not used. Undefined registers must not be copied. So any merge with undefined should return
    // undefined.
    return Kind::kUndefined;
  } else if (IsConflict() || incoming_type.IsConflict()) {
    return Kind::kConflict;  // (Conflict MERGE *) or (* MERGE Conflict) => Conflict
  } else if (IsConstant() && incoming_type.IsConstant()) {
    // Kind enumerator values within the non-negative and can-be-negative constant groups are
    // ordered by increasing range, so the type with the higher kind can be used within these
    // groups as the merged kind and merging across the groups is also quite simple.
    static_assert(Kind::kZero < Kind::kBooleanConstant);
    static_assert(Kind::kBooleanConstant < Kind::kPositiveByteConstant);
    static_assert(Kind::kPositiveByteConstant < Kind::kPositiveShortConstant);
    static_assert(Kind::kPositiveShortConstant < Kind::kCharConstant);
    static_assert(Kind::kByteConstant < Kind::kShortConstant);
    static_assert(Kind::kShortConstant < Kind::kIntegerConstant);

    auto is_non_negative = [](const RegType& reg_type) constexpr {
      bool result = reg_type.IsZero() ||
                    reg_type.IsBooleanConstant() ||
                    reg_type.IsPositiveByteConstant() ||
                    reg_type.IsPositiveShortConstant() ||
                    reg_type.IsCharConstant();
      DCHECK_NE(
          result,
          reg_type.IsByteConstant() || reg_type.IsShortConstant() || reg_type.IsIntegerConstant());
      return result;
    };

    bool is_non_negative_this = is_non_negative(*this);
    if (is_non_negative_this == is_non_negative(incoming_type)) {
      return GetKind() >= incoming_type.GetKind() ? GetKind() : incoming_type.GetKind();
    }
    Kind non_negative_kind = is_non_negative_this ? GetKind() : incoming_type.GetKind();
    Kind can_be_negative_kind = is_non_negative_this ? incoming_type.GetKind() : GetKind();
    if (can_be_negative_kind == Kind::kByteConstant &&
        non_negative_kind <= Kind::kPositiveByteConstant) {
      return Kind::kByteConstant;
    } else if (can_be_negative_kind <= Kind::kShortConstant &&
               non_negative_kind <= Kind::kPositiveShortConstant) {
      return Kind::kShortConstant;
    } else {
      return Kind::kIntegerConstant;
    }
  } else if ((IsConstantLo() && incoming_type.IsConstantLo()) ||
             (IsConstantHi() && incoming_type.IsConstantHi())) {
    return GetKind();
  } else if (IsIntegralTypes() && incoming_type.IsIntegralTypes()) {
    if (IsBooleanTypes() && incoming_type.IsBooleanTypes()) {
      return Kind::kBoolean;  // boolean MERGE boolean => boolean
    }
    if (IsByteTypes() && incoming_type.IsByteTypes()) {
      return Kind::kByte;  // byte MERGE byte => byte
    }
    if (IsShortTypes() && incoming_type.IsShortTypes()) {
      return Kind::kShort;  // short MERGE short => short
    }
    if (IsCharTypes() && incoming_type.IsCharTypes()) {
      return Kind::kChar;  // char MERGE char => char
    }
    return Kind::kInteger;  // int MERGE * => int
  } else if ((IsFloatTypes() && incoming_type.IsFloatTypes()) ||
             (IsLongTypes() && incoming_type.IsLongTypes()) ||
             (IsLongHighTypes() && incoming_type.IsLongHighTypes()) ||
             (IsDoubleTypes() && incoming_type.IsDoubleTypes()) ||
             (IsDoubleHighTypes() && incoming_type.IsDoubleHighTypes())) {
    // check constant case was handled prior to entry
    DCHECK_IMPLIES(IsConstant(), !incoming_type.IsConstant());
    // float/long/double MERGE float/long/double_constant => float/long/double
    return SelectNonConstant(*this, incoming_type).GetKind();
  } else if (IsReferenceTypes() && incoming_type.IsReferenceTypes()) {
    if (IsUninitializedTypes() || incoming_type.IsUninitializedTypes()) {
      // Something that is uninitialized hasn't had its constructor called. Unitialized types are
      // special. They may only ever be merged with themselves (must be taken care of by the
      // caller of Merge(), see the DCHECK on entry). So mark any other merge as conflicting here.
      return Kind::kConflict;
    } else if (IsJavaLangObject() || incoming_type.IsJavaLangObject()) {
      return Kind::kJavaLangObject;
    } else {
      // Use `UnresolvedMergedReference` to tell the caller to process a reference merge.
      // This does not mean that the actual merged kind must be `UnresolvedMergedReference`.
      return Kind::kUnresolvedMergedReference;
    }
  } else {
    return Kind::kConflict;  // Unexpected types => Conflict
  }
}

}  // namespace

const RegType& RegType::Merge(const RegType& incoming_type,
                              RegTypeCache* reg_types,
                              MethodVerifier* verifier) const {
  DCHECK(!Equals(incoming_type));  // Trivial equality handled by caller

  static constexpr size_t kNumKinds = NumberOfKinds();
  using MergeTable = std::array<std::array<Kind, kNumKinds>, kNumKinds>;
  static constexpr MergeTable kMergeTable = []() constexpr {
    MergeTable result;
    for (size_t lhs = 0u; lhs != kNumKinds; ++lhs) {
      for (size_t rhs = 0u; rhs != kNumKinds; ++rhs) {
        RegTypeMergeImpl lhs_impl(enum_cast<RegType::Kind>(lhs));
        result[lhs][rhs] = lhs_impl.MergeKind(enum_cast<RegType::Kind>(rhs));
      }
    }
    return result;
  }();

  Kind merge_kind = kMergeTable[GetKind()][incoming_type.GetKind()];
  if (merge_kind != Kind::kUnresolvedMergedReference) {
    return reg_types->GetFromRegKind(merge_kind);
  } else {
    // The `UnresolvedMergedReference` tells us to do non-trivial reference merging which
    // requires more information than the two kinds used for the lookup in `kMergeTable`.
    DCHECK(IsReferenceTypes()) << *this;
    DCHECK(incoming_type.IsReferenceTypes()) << incoming_type;
    DCHECK(!IsUninitializedTypes()) << *this;
    DCHECK(!incoming_type.IsUninitializedTypes());
    DCHECK(!IsJavaLangObject());
    DCHECK(!incoming_type.IsJavaLangObject());
    if (IsZeroOrNull() || incoming_type.IsZeroOrNull()) {
      return SelectNonConstant2(*this, incoming_type);  // 0 MERGE ref => ref
    } else if (IsUnresolvedTypes() || incoming_type.IsUnresolvedTypes()) {
      // We know how to merge an unresolved type with itself, 0 or Object. In this case we
      // have two sub-classes and don't know how to merge. Create a new string-based unresolved
      // type that reflects our lack of knowledge and that allows the rest of the unresolved
      // mechanics to continue.
      return reg_types->FromUnresolvedMerge(*this, incoming_type, verifier);
    } else {  // Two reference types, compute Join
      // Do not cache the classes as ClassJoin() can suspend and invalidate ObjPtr<>s.
      DCHECK(GetClass() != nullptr && !GetClass()->IsPrimitive());
      DCHECK(incoming_type.GetClass() != nullptr && !incoming_type.GetClass()->IsPrimitive());
      ObjPtr<mirror::Class> join_class = ClassJoin(GetClass(),
                                                   incoming_type.GetClass(),
                                                   reg_types->GetClassLinker());
      if (UNLIKELY(join_class == nullptr)) {
        // Internal error joining the classes (e.g., OOME). Report an unresolved reference type.
        // We cannot report an unresolved merge type, as that will attempt to merge the resolved
        // components, leaving us in an infinite loop.
        // We do not want to report the originating exception, as that would require a fast path
        // out all the way to VerifyClass. Instead attempt to continue on without a detailed type.
        Thread* self = Thread::Current();
        self->AssertPendingException();
        self->ClearException();

        // When compiling on the host, we rather want to abort to ensure determinism for preopting.
        // (In that case, it is likely a misconfiguration of dex2oat.)
        if (!kIsTargetBuild && (verifier != nullptr && verifier->IsAotMode())) {
          LOG(FATAL) << "Could not create class join of "
                     << GetClass()->PrettyClass()
                     << " & "
                     << incoming_type.GetClass()->PrettyClass();
          UNREACHABLE();
        }

        return reg_types->MakeUnresolvedReference();
      }

      // Record the dependency that both `GetClass()` and `incoming_type.GetClass()`
      // are assignable to `join_class`. The `verifier` is null during unit tests.
      if (verifier != nullptr) {
        VerifierDeps::MaybeRecordAssignability(verifier->GetVerifierDeps(),
                                               verifier->GetDexFile(),
                                               verifier->GetClassDef(),
                                               join_class,
                                               GetClass());
        VerifierDeps::MaybeRecordAssignability(verifier->GetVerifierDeps(),
                                               verifier->GetDexFile(),
                                               verifier->GetClassDef(),
                                               join_class,
                                               incoming_type.GetClass());
      }
      if (GetClass() == join_class) {
        return *this;
      } else if (incoming_type.GetClass() == join_class) {
        return incoming_type;
      } else {
        return reg_types->FromClass(join_class);
      }
    }
  }
}

void ReferenceType::CheckClassDescriptor() const {
  CHECK(IsReference());
  CHECK(!klass_.IsNull());
  CHECK(!descriptor_.empty()) << *this;
  std::string temp;
  CHECK_EQ(descriptor_, klass_->GetDescriptor(&temp)) << *this;
}

UnresolvedMergedReferenceType::UnresolvedMergedReferenceType(const RegType& resolved,
                                                             const BitVector& unresolved,
                                                             const RegTypeCache* reg_type_cache,
                                                             uint16_t cache_id)
    : UnresolvedType("", cache_id, Kind::kUnresolvedMergedReference),
      reg_type_cache_(reg_type_cache),
      resolved_part_(resolved),
      unresolved_types_(unresolved, false, unresolved.GetAllocator()) {
  CheckConstructorInvariants(this);
  if (kIsDebugBuild) {
    CheckInvariants();
  }
}

void UnresolvedMergedReferenceType::CheckInvariants() const {
  CHECK(reg_type_cache_ != nullptr);

  // Unresolved merged types: merged types should be defined.
  CHECK(descriptor_.empty()) << *this;
  CHECK(!HasClass()) << *this;

  CHECK(!resolved_part_.IsConflict());
  CHECK(resolved_part_.IsReferenceTypes());
  CHECK(!resolved_part_.IsUnresolvedTypes());

  CHECK(resolved_part_.IsZero() ||
        !(resolved_part_.IsArrayTypes() && !resolved_part_.IsObjectArrayTypes()));

  CHECK_GT(unresolved_types_.NumSetBits(), 0U);
  bool unresolved_is_array =
      reg_type_cache_->GetFromId(unresolved_types_.GetHighestBitSet()).IsArrayTypes();
  for (uint32_t idx : unresolved_types_.Indexes()) {
    const RegType& t = reg_type_cache_->GetFromId(idx);
    CHECK_EQ(unresolved_is_array, t.IsArrayTypes());
  }

  if (!resolved_part_.IsZero()) {
    CHECK_EQ(resolved_part_.IsArrayTypes(), unresolved_is_array);
  }
}

bool UnresolvedMergedReferenceType::IsArrayTypesImpl() const {
  // For a merge to be an array, both the resolved and the unresolved part need to be object
  // arrays.
  // (Note: we encode a missing resolved part [which doesn't need to be an array] as zero.)

  if (!resolved_part_.IsZero() && !resolved_part_.IsArrayTypes()) {
    return false;
  }

  // It is enough to check just one of the merged types. Otherwise the merge should have been
  // collapsed (checked in CheckInvariants on construction).
  uint32_t idx = unresolved_types_.GetHighestBitSet();
  const RegType& unresolved = reg_type_cache_->GetFromId(idx);
  return unresolved.IsArrayTypes();
}
bool UnresolvedMergedReferenceType::IsObjectArrayTypesImpl() const {
  // Same as IsArrayTypes, as primitive arrays are always resolved.
  return IsArrayTypes();
}

std::ostream& operator<<(std::ostream& os, const RegType& rhs) {
  os << rhs.Dump();
  return os;
}

}  // namespace verifier
}  // namespace art
