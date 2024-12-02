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

#ifndef ART_RUNTIME_VERIFIER_REG_TYPE_CACHE_INL_H_
#define ART_RUNTIME_VERIFIER_REG_TYPE_CACHE_INL_H_

#include "base/bit_vector-inl.h"
#include "class_root-inl.h"
#include "dex/dex_file.h"
#include "mirror/class-inl.h"
#include "mirror/method_handle_impl.h"
#include "mirror/method_type.h"
#include "mirror/string.h"
#include "mirror/throwable.h"
#include "reg_type.h"
#include "reg_type_cache.h"

namespace art HIDDEN {
namespace verifier {

inline const art::verifier::RegType& RegTypeCache::GetFromId(uint16_t id) const {
  DCHECK_LT(id, entries_.size());
  const RegType* result = entries_[id];
  DCHECK(result != nullptr);
  return *result;
}

namespace detail {

struct RegKindToCacheId : RegTypeCache {
  // Inherit fixed cache ids from `RegTypeCache` and add fake non-fixed cache ids so that
  // we can use `FOR_EACH_CONCRETE_REG_TYPE` to check the fixed cache ids.
#define DEFINE_FAKE_CACHE_ID(name)                                    \
  static_assert(RegType::Kind::k##name >= kNumberOfRegKindCacheIds);  \
  static constexpr uint32_t k##name##CacheId = RegType::Kind::k##name;
  DEFINE_FAKE_CACHE_ID(UnresolvedReference)
  DEFINE_FAKE_CACHE_ID(UninitializedReference)
  DEFINE_FAKE_CACHE_ID(UninitializedThisReference)
  DEFINE_FAKE_CACHE_ID(UnresolvedUninitializedReference)
  DEFINE_FAKE_CACHE_ID(UnresolvedUninitializedThisReference)
  DEFINE_FAKE_CACHE_ID(UnresolvedMergedReference)
  DEFINE_FAKE_CACHE_ID(Reference)
#undef DEFINE_FAKE_CACHE_ID

#define ASSERT_CACHE_ID_EQUALS_KIND(name) \
  static_assert(k##name##CacheId == RegType::Kind::k##name);
  FOR_EACH_CONCRETE_REG_TYPE(ASSERT_CACHE_ID_EQUALS_KIND);
#undef ASSERT_CACHE_ID_EQUALS_KIND

  static constexpr uint16_t Translate(RegType::Kind kind) {
    DCHECK_LT(kind, kNumberOfRegKindCacheIds);
    return kind;
  }
};

}  // namespace detail

constexpr uint16_t RegTypeCache::IdForRegKind(RegType::Kind kind) {
  return detail::RegKindToCacheId::Translate(kind);
}

constexpr RegType::Kind RegTypeCache::RegKindForId(uint16_t id) {
  DCHECK_LT(id, NumberOfRegKindCacheIds());
  RegType::Kind kind = enum_cast<RegType::Kind>(id);
  DCHECK_EQ(id, IdForRegKind(kind));
  return kind;
}

inline const RegType& RegTypeCache::GetFromRegKind(RegType::Kind kind) const {
  return GetFromId(IdForRegKind(kind));
}

inline const RegType& RegTypeCache::FromTypeIndex(dex::TypeIndex type_index) {
  DCHECK_LT(type_index.index_, dex_file_->NumTypeIds());
  if (ids_for_type_index_[type_index.index_] != kNoIdForTypeIndex) {
    return GetFromId(ids_for_type_index_[type_index.index_]);
  }
  return FromTypeIndexUncached(type_index);
}

inline const BooleanType& RegTypeCache::Boolean() const {
  return *down_cast<const BooleanType*>(entries_[kBooleanCacheId]);
}

inline const ByteType& RegTypeCache::Byte() const {
  return *down_cast<const ByteType*>(entries_[kByteCacheId]);
}

inline const CharType& RegTypeCache::Char() const {
  return *down_cast<const CharType*>(entries_[kCharCacheId]);
}

inline const ShortType& RegTypeCache::Short() const {
  return *down_cast<const ShortType*>(entries_[kShortCacheId]);
}

inline const IntegerType& RegTypeCache::Integer() const {
  return *down_cast<const IntegerType*>(entries_[kIntegerCacheId]);
}

inline const FloatType& RegTypeCache::Float() const {
  return *down_cast<const FloatType*>(entries_[kFloatCacheId]);
}

inline const LongLoType& RegTypeCache::LongLo() const {
  return *down_cast<const LongLoType*>(entries_[kLongLoCacheId]);
}

inline const LongHiType& RegTypeCache::LongHi() const {
  return *down_cast<const LongHiType*>(entries_[kLongHiCacheId]);
}

inline const DoubleLoType& RegTypeCache::DoubleLo() const {
  return *down_cast<const DoubleLoType*>(entries_[kDoubleLoCacheId]);
}

inline const DoubleHiType& RegTypeCache::DoubleHi() const {
  return *down_cast<const DoubleHiType*>(entries_[kDoubleHiCacheId]);
}

inline const UndefinedType& RegTypeCache::Undefined() const {
  return *down_cast<const UndefinedType*>(entries_[kUndefinedCacheId]);
}

inline const ConflictType& RegTypeCache::Conflict() const {
  return *down_cast<const ConflictType*>(entries_[kConflictCacheId]);
}

inline const NullType& RegTypeCache::Null() const {
  return *down_cast<const NullType*>(entries_[kNullCacheId]);
}

inline const ZeroType& RegTypeCache::Zero() const {
  return *down_cast<const ZeroType*>(entries_[kZeroCacheId]);
}

inline const BooleanConstantType& RegTypeCache::BooleanConstant() const {
  return *down_cast<const BooleanConstantType*>(entries_[kBooleanConstantCacheId]);
}

inline const ByteConstantType& RegTypeCache::ByteConstant() const {
  return *down_cast<const ByteConstantType*>(entries_[kByteConstantCacheId]);
}

inline const CharConstantType& RegTypeCache::CharConstant() const {
  return *down_cast<const CharConstantType*>(entries_[kCharConstantCacheId]);
}

inline const ShortConstantType& RegTypeCache::ShortConstant() const {
  return *down_cast<const ShortConstantType*>(entries_[kShortConstantCacheId]);
}

inline const IntegerConstantType& RegTypeCache::IntegerConstant() const {
  return *down_cast<const IntegerConstantType*>(entries_[kIntegerConstantCacheId]);
}

inline const PositiveByteConstantType& RegTypeCache::PositiveByteConstant() const {
  return *down_cast<const PositiveByteConstantType*>(entries_[kPositiveByteConstantCacheId]);
}

inline const PositiveShortConstantType& RegTypeCache::PositiveShortConstant() const {
  return *down_cast<const PositiveShortConstantType*>(entries_[kPositiveShortConstantCacheId]);
}

inline const ConstantLoType& RegTypeCache::ConstantLo() const {
  return *down_cast<const ConstantLoType*>(entries_[kConstantLoCacheId]);
}

inline const ConstantHiType& RegTypeCache::ConstantHi() const {
  return *down_cast<const ConstantHiType*>(entries_[kConstantHiCacheId]);
}

inline const ReferenceType& RegTypeCache::JavaLangClass() {
  const RegType* result = &FromClass(GetClassRoot<mirror::Class>());
  DCHECK(result->GetClass()->DescriptorEquals("Ljava/lang/Class;"));
  DCHECK(result->IsReference());
  return *down_cast<const ReferenceType*>(result);
}

inline const ReferenceType& RegTypeCache::JavaLangString() {
  const RegType* result = &FromClass(GetClassRoot<mirror::String>());
  DCHECK(result->GetClass()->DescriptorEquals("Ljava/lang/String;"));
  DCHECK(result->IsReference());
  return *down_cast<const ReferenceType*>(result);
}

inline const ReferenceType& RegTypeCache::JavaLangInvokeMethodHandle() {
  const RegType* result = &FromClass(GetClassRoot<mirror::MethodHandle>());
  DCHECK(result->GetClass()->DescriptorEquals("Ljava/lang/invoke/MethodHandle;"));
  DCHECK(result->IsReference());
  return *down_cast<const ReferenceType*>(result);
}

inline const ReferenceType& RegTypeCache::JavaLangInvokeMethodType() {
  const RegType* result = &FromClass(GetClassRoot<mirror::MethodType>());
  DCHECK(result->GetClass()->DescriptorEquals("Ljava/lang/invoke/MethodType;"));
  DCHECK(result->IsReference());
  return *down_cast<const ReferenceType*>(result);
}

inline const ReferenceType& RegTypeCache::JavaLangThrowable() {
  const RegType* result = &FromClass(GetClassRoot<mirror::Throwable>());
  DCHECK(result->GetClass()->DescriptorEquals("Ljava/lang/Throwable;"));
  DCHECK(result->IsReference());
  return *down_cast<const ReferenceType*>(result);
}

inline const JavaLangObjectType& RegTypeCache::JavaLangObject() {
  const RegType& result = GetFromId(kJavaLangObjectCacheId);
  DCHECK_EQ(result.GetDescriptor(), "Ljava/lang/Object;");
  DCHECK(result.IsJavaLangObject());
  return down_cast<const JavaLangObjectType&>(result);
}

template <class RegTypeType>
inline RegTypeType& RegTypeCache::AddEntry(RegTypeType* new_entry) {
  DCHECK(new_entry != nullptr);
  entries_.push_back(new_entry);
  if (new_entry->HasClass()) {
    Handle<mirror::Class> klass = new_entry->GetClassHandle();
    DCHECK(!klass->IsPrimitive());
    klass_entries_.push_back(std::make_pair(klass, new_entry));
  }
  return *new_entry;
}

}  // namespace verifier
}  // namespace art
#endif  // ART_RUNTIME_VERIFIER_REG_TYPE_CACHE_INL_H_
