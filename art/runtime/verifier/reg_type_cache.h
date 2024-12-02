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

#ifndef ART_RUNTIME_VERIFIER_REG_TYPE_CACHE_H_
#define ART_RUNTIME_VERIFIER_REG_TYPE_CACHE_H_

#include <stdint.h>
#include <string_view>
#include <vector>

#include "base/arena_containers.h"
#include "base/casts.h"
#include "base/macros.h"
#include "dex/primitive.h"
#include "gc_root.h"
#include "handle_scope.h"
#include "reg_type.h"

namespace art HIDDEN {

namespace mirror {
class Class;
class ClassLoader;
}  // namespace mirror

class ClassLinker;
class DexFile;

namespace verifier {

class MethodVerifier;

// Use 8 bytes since that is the default arena allocator alignment.
static constexpr size_t kDefaultArenaBitVectorBytes = 8;

class RegTypeCache {
 public:
  EXPORT RegTypeCache(Thread* self,
                      ClassLinker* class_linker,
                      ArenaPool* arena_pool,
                      Handle<mirror::ClassLoader> class_loader,
                      const DexFile* dex_file,
                      bool can_load_classes = true,
                      bool can_suspend = true);

  Handle<mirror::ClassLoader> GetClassLoader() const {
    return class_loader_;
  }

  const DexFile* GetDexFile() const {
    return dex_file_;
  }

  bool CanLoadClasses() const {
    return can_load_classes_;
  }

  bool CanSuspend() const {
    return can_suspend_;
  }

  static constexpr uint32_t NumberOfRegKindCacheIds() { return kNumberOfRegKindCacheIds; }

  // Translate `RegType::Kind` to id for a pre-initialized register type.
  // Cannot be used for non-zero reference kinds other than `JavaLangObject()`; all other
  // kinds (undefined, conflict, primitive and constant kinds) have pre-initialized types.
  static constexpr uint16_t IdForRegKind(RegType::Kind kind);

  // Translate `id` to `RegType::Kind`.
  // The `id` must be lower than `NumberOfRegKindCacheIds()`.
  static constexpr RegType::Kind RegKindForId(uint16_t id);

  // Get register type for a `RegType::Kind` with the same restrictions as `IdForRegKind()`.
  const RegType& GetFromRegKind(RegType::Kind kind) const;

  const RegType& GetFromId(uint16_t id) const;
  // Get or insert a reg type for a klass.
  const RegType& FromClass(ObjPtr<mirror::Class> klass)
      REQUIRES_SHARED(Locks::mutator_lock_);
  const RegType& FromDescriptor(const char* descriptor)
      REQUIRES_SHARED(Locks::mutator_lock_);
  const RegType& FromUnresolvedMerge(const RegType& left,
                                     const RegType& right,
                                     MethodVerifier* verifier)
      REQUIRES_SHARED(Locks::mutator_lock_);

  const RegType& FromTypeIndex(dex::TypeIndex type_index) REQUIRES_SHARED(Locks::mutator_lock_);

  // Note: this should not be used outside of RegType::ClassJoin!
  const RegType& MakeUnresolvedReference() REQUIRES_SHARED(Locks::mutator_lock_);

  size_t GetCacheSize() {
    return entries_.size();
  }

  const UndefinedType& Undefined() const REQUIRES_SHARED(Locks::mutator_lock_);
  const ConflictType& Conflict() const;
  const NullType& Null() const;

  const BooleanType& Boolean() const REQUIRES_SHARED(Locks::mutator_lock_);
  const ByteType& Byte() const REQUIRES_SHARED(Locks::mutator_lock_);
  const CharType& Char() const REQUIRES_SHARED(Locks::mutator_lock_);
  const ShortType& Short() const REQUIRES_SHARED(Locks::mutator_lock_);
  const IntegerType& Integer() const REQUIRES_SHARED(Locks::mutator_lock_);
  const FloatType& Float() const REQUIRES_SHARED(Locks::mutator_lock_);
  const LongLoType& LongLo() const REQUIRES_SHARED(Locks::mutator_lock_);
  const LongHiType& LongHi() const REQUIRES_SHARED(Locks::mutator_lock_);
  const DoubleLoType& DoubleLo() const REQUIRES_SHARED(Locks::mutator_lock_);
  const DoubleHiType& DoubleHi() const REQUIRES_SHARED(Locks::mutator_lock_);

  const ZeroType& Zero() const REQUIRES_SHARED(Locks::mutator_lock_);
  const BooleanConstantType& BooleanConstant() const REQUIRES_SHARED(Locks::mutator_lock_);
  const ByteConstantType& ByteConstant() const REQUIRES_SHARED(Locks::mutator_lock_);
  const CharConstantType& CharConstant() const REQUIRES_SHARED(Locks::mutator_lock_);
  const ShortConstantType& ShortConstant() const REQUIRES_SHARED(Locks::mutator_lock_);
  const IntegerConstantType& IntegerConstant() const REQUIRES_SHARED(Locks::mutator_lock_);
  const PositiveByteConstantType& PositiveByteConstant() const
      REQUIRES_SHARED(Locks::mutator_lock_);
  const PositiveShortConstantType& PositiveShortConstant() const
      REQUIRES_SHARED(Locks::mutator_lock_);
  const ConstantLoType& ConstantLo() const REQUIRES_SHARED(Locks::mutator_lock_);
  const ConstantHiType& ConstantHi() const REQUIRES_SHARED(Locks::mutator_lock_);

  const ReferenceType& JavaLangClass() REQUIRES_SHARED(Locks::mutator_lock_);
  const ReferenceType& JavaLangString() REQUIRES_SHARED(Locks::mutator_lock_);
  const ReferenceType& JavaLangInvokeMethodHandle() REQUIRES_SHARED(Locks::mutator_lock_);
  const ReferenceType& JavaLangInvokeMethodType() REQUIRES_SHARED(Locks::mutator_lock_);
  const ReferenceType& JavaLangThrowable() REQUIRES_SHARED(Locks::mutator_lock_);
  const JavaLangObjectType& JavaLangObject() REQUIRES_SHARED(Locks::mutator_lock_);

  const UninitializedType& Uninitialized(const RegType& type)
      REQUIRES_SHARED(Locks::mutator_lock_);
  // Create an uninitialized 'this' argument for the given type.
  const UninitializedType& UninitializedThisArgument(const RegType& type)
      REQUIRES_SHARED(Locks::mutator_lock_);
  const RegType& FromUninitialized(const RegType& uninit_type)
      REQUIRES_SHARED(Locks::mutator_lock_);

  const RegType& GetComponentType(const RegType& array) REQUIRES_SHARED(Locks::mutator_lock_);
  void Dump(std::ostream& os) REQUIRES_SHARED(Locks::mutator_lock_);
  const RegType& RegTypeFromPrimitiveType(Primitive::Type) const
      REQUIRES_SHARED(Locks::mutator_lock_);

  ClassLinker* GetClassLinker() const {
    return class_linker_;
  }

  static constexpr uint32_t kUndefinedCacheId = 0;
  static constexpr uint32_t kConflictCacheId = kUndefinedCacheId + 1u;
  static constexpr uint32_t kBooleanCacheId = kConflictCacheId + 1u;
  static constexpr uint32_t kByteCacheId = kBooleanCacheId + 1u;
  static constexpr uint32_t kCharCacheId = kByteCacheId + 1u;
  static constexpr uint32_t kShortCacheId = kCharCacheId + 1u;
  static constexpr uint32_t kIntegerCacheId = kShortCacheId + 1u;
  static constexpr uint32_t kLongLoCacheId = kIntegerCacheId + 1u;
  static constexpr uint32_t kLongHiCacheId = kLongLoCacheId + 1u;
  static constexpr uint32_t kFloatCacheId = kLongHiCacheId + 1u;
  static constexpr uint32_t kDoubleLoCacheId = kFloatCacheId + 1u;
  static constexpr uint32_t kDoubleHiCacheId = kDoubleLoCacheId + 1u;
  static constexpr uint32_t kZeroCacheId = kDoubleHiCacheId + 1u;
  static constexpr uint32_t kBooleanConstantCacheId = kZeroCacheId + 1u;
  static constexpr uint32_t kPositiveByteConstantCacheId = kBooleanConstantCacheId + 1u;
  static constexpr uint32_t kPositiveShortConstantCacheId = kPositiveByteConstantCacheId + 1u;
  static constexpr uint32_t kCharConstantCacheId = kPositiveShortConstantCacheId + 1u;
  static constexpr uint32_t kByteConstantCacheId = kCharConstantCacheId + 1u;
  static constexpr uint32_t kShortConstantCacheId = kByteConstantCacheId + 1u;
  static constexpr uint32_t kIntegerConstantCacheId = kShortConstantCacheId + 1u;
  static constexpr uint32_t kConstantLoCacheId = kIntegerConstantCacheId + 1u;
  static constexpr uint32_t kConstantHiCacheId = kConstantLoCacheId + 1u;
  static constexpr uint32_t kNullCacheId = kConstantHiCacheId + 1u;
  static constexpr uint32_t kJavaLangObjectCacheId = kNullCacheId + 1u;
  static constexpr uint32_t kNumberOfRegKindCacheIds = kJavaLangObjectCacheId + 1u;

  static constexpr uint32_t kUninitializedJavaLangObjectCacheId = kNumberOfRegKindCacheIds;
  static constexpr uint32_t kNumberOfFixedCacheIds = kUninitializedJavaLangObjectCacheId + 1u;

 private:
  // We want 0 to mean an empty slot in `ids_for_type_index_`, so that we do not need to fill
  // the array after allocaing zero-initialized storage. This needs to correspond to a fixed
  // cache id that cannot be returned for a type index, such as `kUndefinedCacheId`.
  static constexpr uint32_t kNoIdForTypeIndex = 0u;
  static_assert(kNoIdForTypeIndex == kUndefinedCacheId);

  void FillPrimitiveAndConstantTypes() REQUIRES_SHARED(Locks::mutator_lock_);
  ObjPtr<mirror::Class> ResolveClass(const char* descriptor, size_t descriptor_length)
      REQUIRES_SHARED(Locks::mutator_lock_);
  bool MatchDescriptor(size_t idx, const std::string_view& descriptor)
      REQUIRES_SHARED(Locks::mutator_lock_);

  const RegType& From(const char* descriptor)
      REQUIRES_SHARED(Locks::mutator_lock_);

  const RegType& FromTypeIndexUncached(dex::TypeIndex type_index)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Returns the pass in RegType.
  template <class RegTypeType>
  RegTypeType& AddEntry(RegTypeType* new_entry) REQUIRES_SHARED(Locks::mutator_lock_);

  // Add a string to the arena allocator so that it stays live for the lifetime of the
  // verifier and return a string view.
  std::string_view AddString(const std::string_view& str);

  // Arena allocator.
  ArenaAllocator allocator_;

  // The actual storage for the RegTypes.
  ArenaVector<const RegType*> entries_;

  // Fast lookup for quickly finding entries that have a matching class.
  ArenaVector<std::pair<Handle<mirror::Class>, const RegType*>> klass_entries_;

  // Handle scope containing classes.
  VariableSizedHandleScope handles_;

  ClassLinker* class_linker_;
  Handle<mirror::ClassLoader> class_loader_;
  const DexFile* const dex_file_;

  // Fast lookup by type index.
  uint16_t* const ids_for_type_index_;

  // Cache last uninitialized "this" type used for constructors.
  const UninitializedType* last_uninitialized_this_type_;

  // Whether or not we're allowed to load classes.
  const bool can_load_classes_;

  // Whether or not we're allowed to suspend.
  const bool can_suspend_;

  DISALLOW_COPY_AND_ASSIGN(RegTypeCache);
};

}  // namespace verifier
}  // namespace art

#endif  // ART_RUNTIME_VERIFIER_REG_TYPE_CACHE_H_
