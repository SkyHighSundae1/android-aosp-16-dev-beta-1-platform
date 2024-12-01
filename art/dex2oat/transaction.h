/*
 * Copyright (C) 2014 The Android Open Source Project
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

#ifndef ART_DEX2OAT_TRANSACTION_H_
#define ART_DEX2OAT_TRANSACTION_H_

#include "base/scoped_arena_containers.h"
#include "base/macros.h"
#include "base/mutex.h"
#include "base/safe_map.h"
#include "base/value_object.h"
#include "dex/dex_file_types.h"
#include "dex/primitive.h"
#include "gc_root.h"
#include "offsets.h"

#include <list>
#include <map>

namespace art HIDDEN {
namespace gc {
class Heap;
}  // namespace gc
namespace mirror {
class Array;
class Class;
class DexCache;
class Object;
class String;
}  // namespace mirror
class InternTable;
template<class MirrorType> class ObjPtr;

class Transaction final {
 public:
  Transaction(bool strict, mirror::Class* root, ArenaStack* arena_stack, ArenaPool* arena_pool);
  ~Transaction();

  ArenaStack* GetArenaStack() {
    return allocator_.GetArenaStack();
  }

  void Abort(const std::string& abort_message)
      REQUIRES_SHARED(Locks::mutator_lock_);
  void ThrowAbortError(Thread* self, const std::string* abort_message)
      REQUIRES_SHARED(Locks::mutator_lock_);
  bool IsAborted() const {
    return aborted_;
  }

  // If the transaction is rollbacking. Transactions will set this flag when they start rollbacking,
  // because the nested transaction should be disabled when rollbacking to restore the memory.
  bool IsRollingBack() const {
    return rolling_back_;
  }

  // If the transaction is in strict mode, then all access of static fields will be constrained,
  // one class's clinit will not be allowed to read or modify another class's static fields, unless
  // the transaction is aborted.
  bool IsStrict() const {
    return strict_;
  }

  // Record newly allocated object/array.
  //
  // There is no reason to record old values for newly allocated objects because they become
  // unreachable when the transaction is rolled back, so their data does not need to be rolled back.
  //
  // Implementation details: We track all newly allocated objects/arrays by creating an
  // `ObjectLog`/`ArrayLog` flagged as a new object/array. We also cache the last allocated
  // object/array which often helps avoid the search for the flagged `ObjectLog`/`ArrayLog`.
  void RecordNewObject(ObjPtr<mirror::Object> allocated_object)
      REQUIRES_SHARED(Locks::mutator_lock_);
  void RecordNewArray(ObjPtr<mirror::Array> allocated_object)
      REQUIRES_SHARED(Locks::mutator_lock_);

  bool ObjectNeedsTransactionRecords(ObjPtr<mirror::Object> obj)
      REQUIRES_SHARED(Locks::mutator_lock_);
  bool ArrayNeedsTransactionRecords(ObjPtr<mirror::Array> array)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Record object field changes.
  void RecordWriteFieldBoolean(mirror::Object* obj,
                               MemberOffset field_offset,
                               uint8_t value,
                               bool is_volatile);
  void RecordWriteFieldByte(mirror::Object* obj,
                            MemberOffset field_offset,
                            int8_t value,
                            bool is_volatile);
  void RecordWriteFieldChar(mirror::Object* obj,
                            MemberOffset field_offset,
                            uint16_t value,
                            bool is_volatile);
  void RecordWriteFieldShort(mirror::Object* obj,
                             MemberOffset field_offset,
                             int16_t value,
                             bool is_volatile);
  void RecordWriteField32(mirror::Object* obj,
                          MemberOffset field_offset,
                          uint32_t value,
                          bool is_volatile);
  void RecordWriteField64(mirror::Object* obj,
                          MemberOffset field_offset,
                          uint64_t value,
                          bool is_volatile);
  void RecordWriteFieldReference(mirror::Object* obj,
                                 MemberOffset field_offset,
                                 mirror::Object* value,
                                 bool is_volatile);

  // Record array change.
  void RecordWriteArray(mirror::Array* array, size_t index, uint64_t value)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Record intern string table changes.
  void RecordStrongStringInsertion(ObjPtr<mirror::String> s)
      REQUIRES(Locks::intern_table_lock_);
  void RecordWeakStringInsertion(ObjPtr<mirror::String> s)
      REQUIRES(Locks::intern_table_lock_);
  void RecordStrongStringRemoval(ObjPtr<mirror::String> s)
      REQUIRES(Locks::intern_table_lock_);
  void RecordWeakStringRemoval(ObjPtr<mirror::String> s)
      REQUIRES(Locks::intern_table_lock_);

  // Record resolve string.
  void RecordResolveString(ObjPtr<mirror::DexCache> dex_cache, dex::StringIndex string_idx)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Record resolve method type.
  void RecordResolveMethodType(ObjPtr<mirror::DexCache> dex_cache, dex::ProtoIndex proto_idx)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Abort transaction by undoing all recorded changes.
  void Rollback()
      REQUIRES_SHARED(Locks::mutator_lock_);

  void VisitRoots(RootVisitor* visitor)
      REQUIRES_SHARED(Locks::mutator_lock_);

  bool ReadConstraint(ObjPtr<mirror::Object> obj) const
      REQUIRES_SHARED(Locks::mutator_lock_);

  bool WriteConstraint(ObjPtr<mirror::Object> obj) const
      REQUIRES_SHARED(Locks::mutator_lock_);

  bool WriteValueConstraint(ObjPtr<mirror::Object> value) const
      REQUIRES_SHARED(Locks::mutator_lock_);

 private:
  class ObjectLog : public ValueObject {
   public:
    void LogBooleanValue(MemberOffset offset, uint8_t value, bool is_volatile);
    void LogByteValue(MemberOffset offset, int8_t value, bool is_volatile);
    void LogCharValue(MemberOffset offset, uint16_t value, bool is_volatile);
    void LogShortValue(MemberOffset offset, int16_t value, bool is_volatile);
    void Log32BitsValue(MemberOffset offset, uint32_t value, bool is_volatile);
    void Log64BitsValue(MemberOffset offset, uint64_t value, bool is_volatile);
    void LogReferenceValue(MemberOffset offset, mirror::Object* obj, bool is_volatile);

    void Undo(mirror::Object* obj) const REQUIRES_SHARED(Locks::mutator_lock_);
    void VisitRoots(RootVisitor* visitor) REQUIRES_SHARED(Locks::mutator_lock_);

    size_t Size() const {
      return field_values_.size();
    }

    void MarkAsNewObject() {
      DCHECK(field_values_.empty());
      is_new_object_ = true;
    }

    bool IsNewObject() const {
      return is_new_object_;
    }

    explicit ObjectLog(ScopedArenaAllocator* allocator)
        : is_new_object_(false),
          field_values_(std::less<uint32_t>(), allocator->Adapter(kArenaAllocTransaction)) {}
    ObjectLog(ObjectLog&& log) = default;

   private:
    enum FieldValueKind {
      kBoolean,
      kByte,
      kChar,
      kShort,
      k32Bits,
      k64Bits,
      kReference
    };
    struct FieldValue : public ValueObject {
      // TODO use JValue instead ?
      uint64_t value;
      FieldValueKind kind;
      bool is_volatile;

      FieldValue() : value(0), kind(FieldValueKind::kBoolean), is_volatile(false) {}
      FieldValue(FieldValue&& log) = default;

     private:
      DISALLOW_COPY_AND_ASSIGN(FieldValue);
    };

    void LogValue(FieldValueKind kind, MemberOffset offset, uint64_t value, bool is_volatile);
    void UndoFieldWrite(mirror::Object* obj,
                        MemberOffset field_offset,
                        const FieldValue& field_value) const REQUIRES_SHARED(Locks::mutator_lock_);

    // Whether this is a new object. We do not need to keep transaction records for objects
    // created inside a transaction because they become unreachable on rollback.
    bool is_new_object_;

    // Maps field's offset to its value.
    ScopedArenaSafeMap<uint32_t, FieldValue> field_values_;

    DISALLOW_COPY_AND_ASSIGN(ObjectLog);
  };

  class ArrayLog : public ValueObject {
   public:
    void LogValue(size_t index, uint64_t value);

    void Undo(mirror::Array* obj) const REQUIRES_SHARED(Locks::mutator_lock_);

    size_t Size() const {
      return array_values_.size();
    }

    void MarkAsNewArray() {
      DCHECK(array_values_.empty());
      is_new_array_ = true;
    }

    bool IsNewArray() const {
      return is_new_array_;
    }

    explicit ArrayLog(ScopedArenaAllocator* allocator)
        : is_new_array_(false),
          array_values_(std::less<size_t>(), allocator->Adapter(kArenaAllocTransaction)) {}

    ArrayLog(ArrayLog&& log) = default;

   private:
    void UndoArrayWrite(mirror::Array* array,
                        Primitive::Type array_type,
                        size_t index,
                        uint64_t value) const REQUIRES_SHARED(Locks::mutator_lock_);

    // Whether this is a new array. We do not need to keep transaction records for arrays
    // created inside a transaction because they become unreachable on rollback.
    bool is_new_array_;

    // Maps index to value.
    // TODO use JValue instead ?
    ScopedArenaSafeMap<size_t, uint64_t> array_values_;

    DISALLOW_COPY_AND_ASSIGN(ArrayLog);
  };

  class InternStringLog : public ValueObject {
   public:
    enum StringKind {
      kStrongString,
      kWeakString
    };
    enum StringOp {
      kInsert,
      kRemove
    };
    InternStringLog(ObjPtr<mirror::String> s, StringKind kind, StringOp op);

    void Undo(InternTable* intern_table) const
        REQUIRES_SHARED(Locks::mutator_lock_)
        REQUIRES(Locks::intern_table_lock_);
    void VisitRoots(RootVisitor* visitor) REQUIRES_SHARED(Locks::mutator_lock_);

    // Only the move constructor is supported.
    InternStringLog() = delete;
    InternStringLog(const InternStringLog& log) = delete;
    InternStringLog& operator=(const InternStringLog& log) = delete;
    InternStringLog(InternStringLog&& log) = default;
    InternStringLog& operator=(InternStringLog&& log) = delete;

   private:
    mutable GcRoot<mirror::String> str_;
    const StringKind string_kind_;
    const StringOp string_op_;
  };

  class ResolveStringLog : public ValueObject {
   public:
    ResolveStringLog(ObjPtr<mirror::DexCache> dex_cache, dex::StringIndex string_idx);

    void Undo() const REQUIRES_SHARED(Locks::mutator_lock_);

    void VisitRoots(RootVisitor* visitor) REQUIRES_SHARED(Locks::mutator_lock_);

   private:
    GcRoot<mirror::DexCache> dex_cache_;
    const dex::StringIndex string_idx_;

    DISALLOW_COPY_AND_ASSIGN(ResolveStringLog);
  };

  class ResolveMethodTypeLog : public ValueObject {
   public:
    ResolveMethodTypeLog(ObjPtr<mirror::DexCache> dex_cache, dex::ProtoIndex proto_idx);

    void Undo() const REQUIRES_SHARED(Locks::mutator_lock_);

    void VisitRoots(RootVisitor* visitor) REQUIRES_SHARED(Locks::mutator_lock_);

   private:
    GcRoot<mirror::DexCache> dex_cache_;
    const dex::ProtoIndex proto_idx_;

    DISALLOW_COPY_AND_ASSIGN(ResolveMethodTypeLog);
  };

  void LogInternedString(InternStringLog&& log)
      REQUIRES(Locks::intern_table_lock_);

  void UndoObjectModifications()
      REQUIRES_SHARED(Locks::mutator_lock_);
  void UndoArrayModifications()
      REQUIRES_SHARED(Locks::mutator_lock_);
  void UndoInternStringTableModifications()
      REQUIRES(Locks::intern_table_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);
  void UndoResolveStringModifications()
      REQUIRES_SHARED(Locks::mutator_lock_);
  void UndoResolveMethodTypeModifications()
      REQUIRES_SHARED(Locks::mutator_lock_);

  void VisitObjectLogs(RootVisitor* visitor, ArenaStack* arena_stack)
      REQUIRES_SHARED(Locks::mutator_lock_);
  void VisitArrayLogs(RootVisitor* visitor, ArenaStack* arena_stack)
      REQUIRES_SHARED(Locks::mutator_lock_);
  void VisitInternStringLogs(RootVisitor* visitor)
      REQUIRES_SHARED(Locks::mutator_lock_);
  void VisitResolveStringLogs(RootVisitor* visitor)
      REQUIRES_SHARED(Locks::mutator_lock_);
  void VisitResolveMethodTypeLogs(RootVisitor* visitor)
      REQUIRES_SHARED(Locks::mutator_lock_);

  const std::string& GetAbortMessage() const;

  ObjectLog& GetOrCreateObjectLog(mirror::Object* obj);

  // The top-level transaction creates an `ArenaStack` which is then
  // passed down to nested transactions.
  std::optional<ArenaStack> arena_stack_;
  // The allocator uses the `ArenaStack` from the top-level transaction.
  ScopedArenaAllocator allocator_;

  ScopedArenaSafeMap<mirror::Object*, ObjectLog> object_logs_;
  ScopedArenaSafeMap<mirror::Array*, ArrayLog> array_logs_;
  ScopedArenaForwardList<InternStringLog> intern_string_logs_;
  ScopedArenaForwardList<ResolveStringLog> resolve_string_logs_;
  ScopedArenaForwardList<ResolveMethodTypeLog> resolve_method_type_logs_;
  bool aborted_;
  bool rolling_back_;  // Single thread, no race.
  gc::Heap* const heap_;
  const bool strict_;
  std::string abort_message_;
  mirror::Class* root_;
  mirror::Object* last_allocated_object_;

  DISALLOW_COPY_AND_ASSIGN(Transaction);
};

}  // namespace art

#endif  // ART_DEX2OAT_TRANSACTION_H_
