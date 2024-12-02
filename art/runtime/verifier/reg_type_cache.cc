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

#include "reg_type_cache-inl.h"

#include <type_traits>

#include "base/aborting.h"
#include "base/arena_bit_vector.h"
#include "base/bit_vector-inl.h"
#include "base/casts.h"
#include "base/scoped_arena_allocator.h"
#include "base/stl_util.h"
#include "class_linker-inl.h"
#include "class_root-inl.h"
#include "dex/descriptors_names.h"
#include "dex/dex_file-inl.h"
#include "mirror/class-inl.h"
#include "mirror/object-inl.h"
#include "reg_type-inl.h"

namespace art HIDDEN {
namespace verifier {

void RegTypeCache::FillPrimitiveAndConstantTypes() {
  entries_.resize(kNumberOfFixedCacheIds);
  ArrayRef<const RegType*> entries(entries_);

  static constexpr UndefinedType constUndefinedType(kUndefinedCacheId);
  entries[kUndefinedCacheId] = &constUndefinedType;
  static constexpr ConflictType constConflictType(kConflictCacheId);
  entries[kConflictCacheId] = &constConflictType;

#define CREATE_PRIMITIVE_TYPE(name, descriptor) \
  static constexpr name##Type const##name(descriptor, k##name##CacheId); \
  entries[k##name##CacheId] = &const##name;
  CREATE_PRIMITIVE_TYPE(Boolean, "Z");
  CREATE_PRIMITIVE_TYPE(Byte, "B");
  CREATE_PRIMITIVE_TYPE(Short, "S");
  CREATE_PRIMITIVE_TYPE(Char, "C");
  CREATE_PRIMITIVE_TYPE(Integer, "I");
  CREATE_PRIMITIVE_TYPE(LongLo, "J");
  CREATE_PRIMITIVE_TYPE(LongHi, "J");
  CREATE_PRIMITIVE_TYPE(Float, "F");
  CREATE_PRIMITIVE_TYPE(DoubleLo, "D");
  CREATE_PRIMITIVE_TYPE(DoubleHi, "D");
#undef CREATE_PRIMITIVE_TYPE

#define CREATE_CONSTANT_TYPE(name) \
  static constexpr name##Type const##name(k##name##CacheId); \
  entries[k##name##CacheId] = &const##name;
  CREATE_CONSTANT_TYPE(Zero);
  CREATE_CONSTANT_TYPE(BooleanConstant);
  CREATE_CONSTANT_TYPE(PositiveByteConstant);
  CREATE_CONSTANT_TYPE(PositiveShortConstant);
  CREATE_CONSTANT_TYPE(CharConstant);
  CREATE_CONSTANT_TYPE(ByteConstant);
  CREATE_CONSTANT_TYPE(ShortConstant);
  CREATE_CONSTANT_TYPE(IntegerConstant);
  CREATE_CONSTANT_TYPE(ConstantLo);
  CREATE_CONSTANT_TYPE(ConstantHi);
  CREATE_CONSTANT_TYPE(Null);
#undef CREATE_CONSTANT_TYPE

  // `JavaLangObjectType` must be initialized together with its uninitialized type.
  struct JavaLangObjectPair {
    constexpr JavaLangObjectPair()
        : initialized("Ljava/lang/Object;", kJavaLangObjectCacheId, &uninitialized),
          uninitialized(kUninitializedJavaLangObjectCacheId, &initialized) {}
    JavaLangObjectType initialized;
    UninitializedReferenceType uninitialized;
  };
  static constexpr JavaLangObjectPair constJavaLangObject;
  entries[kJavaLangObjectCacheId] = &constJavaLangObject.initialized;
  entries[kUninitializedJavaLangObjectCacheId] = &constJavaLangObject.uninitialized;
}

const RegType& RegTypeCache::FromDescriptor(const char* descriptor) {
  if (descriptor[1] == '\0') {
    switch (descriptor[0]) {
      case 'Z':
        return Boolean();
      case 'B':
        return Byte();
      case 'S':
        return Short();
      case 'C':
        return Char();
      case 'I':
        return Integer();
      case 'J':
        return LongLo();
      case 'F':
        return Float();
      case 'D':
        return DoubleLo();
      case 'V':  // For void types, conflict types.
      default:
        return Conflict();
    }
  } else if (descriptor[0] == 'L' || descriptor[0] == '[') {
    return From(descriptor);
  } else {
    return Conflict();
  }
}

const RegType& RegTypeCache::FromTypeIndexUncached(dex::TypeIndex type_index) {
  DCHECK_EQ(ids_for_type_index_[type_index.index_], kNoIdForTypeIndex);
  const char* descriptor = dex_file_->GetTypeDescriptor(type_index);
  const RegType& reg_type = FromDescriptor(descriptor);
  DCHECK_NE(reg_type.GetId(), kNoIdForTypeIndex);
  ids_for_type_index_[type_index.index_] = reg_type.GetId();
  return reg_type;
}

const RegType& RegTypeCache::RegTypeFromPrimitiveType(Primitive::Type prim_type) const {
  switch (prim_type) {
    case Primitive::kPrimBoolean:
      return Boolean();
    case Primitive::kPrimByte:
      return Byte();
    case Primitive::kPrimShort:
      return Short();
    case Primitive::kPrimChar:
      return Char();
    case Primitive::kPrimInt:
      return Integer();
    case Primitive::kPrimLong:
      return LongLo();
    case Primitive::kPrimFloat:
      return Float();
    case Primitive::kPrimDouble:
      return DoubleLo();
    case Primitive::kPrimVoid:
    default:
      return *entries_[kConflictCacheId];
  }
}

bool RegTypeCache::MatchDescriptor(size_t idx, const std::string_view& descriptor) {
  const RegType* entry = entries_[idx];
  if (descriptor != entry->descriptor_) {
    return false;
  }
  DCHECK(entry->IsJavaLangObject() || entry->IsReference() || entry->IsUnresolvedReference());
  return true;
}

ObjPtr<mirror::Class> RegTypeCache::ResolveClass(const char* descriptor, size_t descriptor_length) {
  // Class was not found, must create new type.
  // Try resolving class
  Thread* self = Thread::Current();
  ObjPtr<mirror::Class> klass = nullptr;
  if (can_load_classes_) {
    klass = class_linker_->FindClass(self, descriptor, descriptor_length, class_loader_);
  } else {
    std::string_view sv_descriptor(descriptor, descriptor_length);
    klass = class_linker_->LookupClass(self, sv_descriptor, class_loader_.Get());
    if (klass != nullptr && !klass->IsResolved()) {
      // We found the class but without it being loaded its not safe for use.
      klass = nullptr;
    }
  }
  return klass;
}

std::string_view RegTypeCache::AddString(const std::string_view& str) {
  char* ptr = allocator_.AllocArray<char>(str.length());
  memcpy(ptr, str.data(), str.length());
  return std::string_view(ptr, str.length());
}

const RegType& RegTypeCache::From(const char* descriptor) {
  // TODO: Avoid the implicit `strlen()` call for ASCII descriptors from the dex file.
  std::string_view sv_descriptor(descriptor);
  // Try looking up the class in the cache first. We use a std::string_view to avoid
  // repeated strlen operations on the descriptor.
  if (MatchDescriptor(kJavaLangObjectCacheId, sv_descriptor)) {
    return *(entries_[kJavaLangObjectCacheId]);
  }
  for (size_t i = kNumberOfFixedCacheIds; i < entries_.size(); i++) {
    if (MatchDescriptor(i, sv_descriptor)) {
      return *(entries_[i]);
    }
  }
  // Class not found in the cache, will create a new type for that.
  // Try resolving class.
  ObjPtr<mirror::Class> klass = ResolveClass(descriptor, sv_descriptor.length());
  // TODO: Avoid copying the `descriptor` with `AddString()` below if the `descriptor`
  // comes from the dex file, for example through `FromTypeIndex()`.
  if (klass != nullptr) {
    DCHECK(!klass->IsPrimitive());
    if (klass->IsObjectClass()) {
      return JavaLangObject();
    }
    const RegType* entry = new (&allocator_) ReferenceType(
        handles_.NewHandle(klass), AddString(sv_descriptor), entries_.size());
    return AddEntry(entry);
  } else {  // Class not resolved.
    // We tried loading the class and failed, this might get an exception raised
    // so we want to clear it before we go on.
    if (can_load_classes_) {
      DCHECK(Thread::Current()->IsExceptionPending());
      Thread::Current()->ClearException();
    } else {
      DCHECK(!Thread::Current()->IsExceptionPending());
    }
    if (IsValidDescriptor(descriptor)) {
      return AddEntry(new (&allocator_) UnresolvedReferenceType(AddString(sv_descriptor),
                                                                entries_.size()));
    } else {
      // The descriptor is broken return the unknown type as there's nothing sensible that
      // could be done at runtime
      return Conflict();
    }
  }
}

const RegType& RegTypeCache::MakeUnresolvedReference() {
  // The descriptor is intentionally invalid so nothing else will match this type.
  return AddEntry(new (&allocator_) UnresolvedReferenceType(AddString("a"), entries_.size()));
}

const RegType& RegTypeCache::FromClass(ObjPtr<mirror::Class> klass) {
  DCHECK(klass != nullptr);
  DCHECK(!klass->IsProxyClass());

  if (klass->IsPrimitive()) {
    return RegTypeFromPrimitiveType(klass->GetPrimitiveType());
  }
  if (klass->IsObjectClass()) {
    return JavaLangObject();
  }
  if (!klass->IsArrayClass() && &klass->GetDexFile() == dex_file_) {
    // Go through the `TypeIndex`-based cache. If the entry is not there yet, we shall
    // fill it in now to make sure it's available for subsequent lookups.
    std::optional<StackHandleScope<1u>> hs(std::nullopt);
    if (kIsDebugBuild) {
      hs.emplace(Thread::Current());
    }
    Handle<mirror::Class> h_class =
        kIsDebugBuild ? hs->NewHandle(klass) : Handle<mirror::Class>();
    const RegType& reg_type = FromTypeIndex(klass->GetDexTypeIndex());
    DCHECK(reg_type.IsReference());
    DCHECK(reg_type.GetClass() == h_class.Get());
    return reg_type;
  }
  for (auto& pair : klass_entries_) {
    const Handle<mirror::Class> entry_klass = pair.first;
    const RegType* entry_reg_type = pair.second;
    if (entry_klass.Get() == klass) {
      return *entry_reg_type;
    }
  }

  // No reference to the class was found, create new reference.
  std::string_view descriptor;
  if (klass->IsArrayClass()) {
    std::string temp;
    descriptor = AddString(std::string_view(klass->GetDescriptor(&temp)));
  } else {
    // Point `descriptor` to the string data in the dex file that defines the `klass`.
    // That dex file cannot be unloaded while we hold a `Handle<>` to that `klass`.
    descriptor = klass->GetDescriptorView();
  }
  Handle<mirror::Class> h_klass = handles_.NewHandle(klass);
  const RegType* reg_type = new (&allocator_) ReferenceType(h_klass, descriptor, entries_.size());
  return AddEntry(reg_type);
}

RegTypeCache::RegTypeCache(Thread* self,
                           ClassLinker* class_linker,
                           ArenaPool* arena_pool,
                           Handle<mirror::ClassLoader> class_loader,
                           const DexFile* dex_file,
                           bool can_load_classes,
                           bool can_suspend)
    : allocator_(arena_pool),
      entries_(allocator_.Adapter(kArenaAllocVerifier)),
      klass_entries_(allocator_.Adapter(kArenaAllocVerifier)),
      handles_(self),
      class_linker_(class_linker),
      class_loader_(class_loader),
      dex_file_(dex_file),
      ids_for_type_index_(allocator_.AllocArray<uint16_t>(dex_file->NumTypeIds())),
      last_uninitialized_this_type_(nullptr),
      can_load_classes_(can_load_classes),
      can_suspend_(can_suspend) {
  DCHECK(can_suspend || !can_load_classes) << "Cannot load classes if suspension is disabled!";
  if (kIsDebugBuild && can_suspend) {
    Thread::Current()->AssertThreadSuspensionIsAllowable(gAborting == 0);
  }
  // `ArenaAllocator` guarantees zero-initialization.
  static_assert(kNoIdForTypeIndex == 0u);
  DCHECK(std::all_of(ids_for_type_index_,
                     ids_for_type_index_ + dex_file->NumTypeIds(),
                     [](uint16_t id) { return id == kNoIdForTypeIndex; }));
  // The klass_entries_ array does not have primitives or constants.
  static constexpr size_t kNumReserveEntries = 32;
  klass_entries_.reserve(kNumReserveEntries);
  // We want to have room for additional entries after inserting primitives and constants.
  entries_.reserve(kNumReserveEntries + kNumberOfFixedCacheIds);
  FillPrimitiveAndConstantTypes();
}

const RegType& RegTypeCache::FromUnresolvedMerge(const RegType& left,
                                                 const RegType& right,
                                                 MethodVerifier* verifier) {
  ArenaBitVector types(&allocator_,
                       kDefaultArenaBitVectorBytes * kBitsPerByte,  // Allocate at least 8 bytes.
                       true);                                       // Is expandable.
  const RegType* left_resolved;
  bool left_unresolved_is_array;
  if (left.IsUnresolvedMergedReference()) {
    const UnresolvedMergedReferenceType& left_merge =
        *down_cast<const UnresolvedMergedReferenceType*>(&left);

    types.Copy(&left_merge.GetUnresolvedTypes());
    left_resolved = &left_merge.GetResolvedPart();
    left_unresolved_is_array = left.IsArrayTypes();
  } else if (left.IsUnresolvedTypes()) {
    types.SetBit(left.GetId());
    left_resolved = &Zero();
    left_unresolved_is_array = left.IsArrayTypes();
  } else {
    left_resolved = &left;
    left_unresolved_is_array = false;
  }

  const RegType* right_resolved;
  bool right_unresolved_is_array;
  if (right.IsUnresolvedMergedReference()) {
    const UnresolvedMergedReferenceType& right_merge =
        *down_cast<const UnresolvedMergedReferenceType*>(&right);

    types.Union(&right_merge.GetUnresolvedTypes());
    right_resolved = &right_merge.GetResolvedPart();
    right_unresolved_is_array = right.IsArrayTypes();
  } else if (right.IsUnresolvedTypes()) {
    types.SetBit(right.GetId());
    right_resolved = &Zero();
    right_unresolved_is_array = right.IsArrayTypes();
  } else {
    right_resolved = &right;
    right_unresolved_is_array = false;
  }

  // Merge the resolved parts. Left and right might be equal, so use SafeMerge.
  const RegType& resolved_parts_merged = left_resolved->SafeMerge(*right_resolved, this, verifier);
  // If we get a conflict here, the merge result is a conflict, not an unresolved merge type.
  if (resolved_parts_merged.IsConflict()) {
    return Conflict();
  }
  if (resolved_parts_merged.IsJavaLangObject()) {
    return resolved_parts_merged;
  }

  bool resolved_merged_is_array = resolved_parts_merged.IsArrayTypes();
  if (left_unresolved_is_array || right_unresolved_is_array || resolved_merged_is_array) {
    // Arrays involved, see if we need to merge to Object.

    // Is the resolved part a primitive array?
    if (resolved_merged_is_array && !resolved_parts_merged.IsObjectArrayTypes()) {
      return JavaLangObject();
    }

    // Is any part not an array (but exists)?
    if ((!left_unresolved_is_array && left_resolved != &left) ||
        (!right_unresolved_is_array && right_resolved != &right) ||
        !resolved_merged_is_array) {
      return JavaLangObject();
    }
  }

  // Check if entry already exists.
  for (size_t i = kNumberOfFixedCacheIds; i < entries_.size(); i++) {
    const RegType* cur_entry = entries_[i];
    if (cur_entry->IsUnresolvedMergedReference()) {
      const UnresolvedMergedReferenceType* cmp_type =
          down_cast<const UnresolvedMergedReferenceType*>(cur_entry);
      const RegType& resolved_part = cmp_type->GetResolvedPart();
      const BitVector& unresolved_part = cmp_type->GetUnresolvedTypes();
      // Use SameBitsSet. "types" is expandable to allow merging in the components, but the
      // BitVector in the final RegType will be made non-expandable.
      if (&resolved_part == &resolved_parts_merged && types.SameBitsSet(&unresolved_part)) {
        return *cur_entry;
      }
    }
  }
  return AddEntry(new (&allocator_) UnresolvedMergedReferenceType(resolved_parts_merged,
                                                                  types,
                                                                  this,
                                                                  entries_.size()));
}

const UninitializedType& RegTypeCache::Uninitialized(const RegType& type) {
  auto get_or_create_uninitialized_type =
    [&](auto& ref_type) REQUIRES_SHARED(Locks::mutator_lock_) {
      using RefType = std::remove_const_t<std::remove_reference_t<decltype(ref_type)>>;
      static_assert(std::is_same_v<RefType, ReferenceType> ||
                    std::is_same_v<RefType, UnresolvedReferenceType>);
      using UninitRefType =
          std::remove_const_t<std::remove_pointer_t<decltype(ref_type.GetUninitializedType())>>;
      static_assert(std::is_same_v<RefType, ReferenceType>
          ? std::is_same_v<UninitRefType, UninitializedReferenceType>
          : std::is_same_v<UninitRefType, UnresolvedUninitializedReferenceType>);
      const UninitRefType* uninit_ref_type = ref_type.GetUninitializedType();
      if (uninit_ref_type == nullptr) {
        uninit_ref_type = new (&allocator_) UninitRefType(entries_.size(), &ref_type);
        AddEntry(uninit_ref_type);
        ref_type.SetUninitializedType(uninit_ref_type);
      }
      return uninit_ref_type;
    };

  if (type.IsReference()) {
    return *get_or_create_uninitialized_type(down_cast<const ReferenceType&>(type));
  } else if (type.IsUnresolvedReference()) {
    return *get_or_create_uninitialized_type(down_cast<const UnresolvedReferenceType&>(type));
  } else {
    DCHECK(type.IsJavaLangObject());
    return *down_cast<const JavaLangObjectType&>(type).GetUninitializedType();
  }
}

const RegType& RegTypeCache::FromUninitialized(const RegType& uninit_type) {
  if (uninit_type.IsUninitializedReference()) {
    return *down_cast<const UninitializedReferenceType&>(uninit_type).GetInitializedType();
  } else if (uninit_type.IsUnresolvedUninitializedReference()) {
    return *down_cast<const UnresolvedUninitializedReferenceType&>(
        uninit_type).GetInitializedType();
  } else if (uninit_type.IsUninitializedThisReference()) {
    return *down_cast<const UninitializedThisReferenceType&>(uninit_type).GetInitializedType();
  } else {
    DCHECK(uninit_type.IsUnresolvedUninitializedThisReference()) << uninit_type;
    return *down_cast<const UnresolvedUninitializedThisReferenceType&>(
        uninit_type).GetInitializedType();
  }
}

const UninitializedType& RegTypeCache::UninitializedThisArgument(const RegType& type) {
  if (last_uninitialized_this_type_ != nullptr && last_uninitialized_this_type_->Equals(type)) {
    return *last_uninitialized_this_type_;
  }

  UninitializedType* entry;
  const std::string_view& descriptor(type.GetDescriptor());
  if (type.IsUnresolvedReference()) {
    for (size_t i = kNumberOfFixedCacheIds; i < entries_.size(); i++) {
      const RegType* cur_entry = entries_[i];
      if (cur_entry->IsUnresolvedUninitializedThisReference() &&
          down_cast<const UnresolvedUninitializedThisReferenceType*>(cur_entry)
              ->GetInitializedType() == &type) {
        DCHECK_EQ(cur_entry->GetDescriptor(), type.GetDescriptor());
        return *down_cast<const UninitializedType*>(cur_entry);
      }
    }
    entry = new (&allocator_) UnresolvedUninitializedThisReferenceType(
        entries_.size(), down_cast<const UnresolvedReferenceType*>(&type));
  } else {
    DCHECK(type.IsJavaLangObject() || type.IsReference());
    for (size_t i = kNumberOfFixedCacheIds; i < entries_.size(); i++) {
      const RegType* cur_entry = entries_[i];
      if (cur_entry->IsUninitializedThisReference() &&
          down_cast<const UninitializedThisReferenceType*>(cur_entry)
              ->GetInitializedType() == &type) {
        DCHECK_EQ(cur_entry->GetDescriptor(), type.GetDescriptor());
        return *down_cast<const UninitializedType*>(cur_entry);
      }
    }
    entry = new (&allocator_) UninitializedThisReferenceType(
        entries_.size(), down_cast<const ReferenceType*>(&type));
  }
  last_uninitialized_this_type_ = entry;
  return AddEntry(entry);
}

const RegType& RegTypeCache::GetComponentType(const RegType& array) {
  if (!array.IsArrayTypes()) {
    return Conflict();
  } else if (array.IsUnresolvedTypes()) {
    DCHECK(!array.IsUnresolvedMergedReference());  // Caller must make sure not to ask for this.
    const std::string descriptor(array.GetDescriptor());
    return FromDescriptor(descriptor.c_str() + 1);
  } else {
    ObjPtr<mirror::Class> klass = array.GetClass()->GetComponentType();
    if (klass->IsErroneous()) {
      // Arrays may have erroneous component types, use unresolved in that case.
      // We assume that the primitive classes are not erroneous, so we know it is a
      // reference type.
      std::string temp;
      const char* descriptor = klass->GetDescriptor(&temp);
      return FromDescriptor(descriptor);
    } else {
      return FromClass(klass);
    }
  }
}

void RegTypeCache::Dump(std::ostream& os) {
  for (size_t i = 0; i < entries_.size(); i++) {
    const RegType* cur_entry = entries_[i];
    if (cur_entry != nullptr) {
      os << i << ": " << cur_entry->Dump() << "\n";
    }
  }
}

}  // namespace verifier
}  // namespace art
