/*
 * Copyright (C) 2017 The Android Open Source Project
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

#ifndef ART_DEX2OAT_AOT_CLASS_LINKER_H_
#define ART_DEX2OAT_AOT_CLASS_LINKER_H_

#include <forward_list>

#include "base/macros.h"
#include "sdk_checker.h"
#include "class_linker.h"

namespace art HIDDEN {

class Transaction;

namespace gc {
class Heap;
}  // namespace gc

// AotClassLinker is only used for AOT compiler, which includes some logic for class initialization
// which will only be used in pre-compilation.
class AotClassLinker : public ClassLinker {
 public:
  explicit AotClassLinker(InternTable *intern_table);
  ~AotClassLinker();

  EXPORT static void SetAppImageDexFiles(const std::vector<const DexFile*>* app_image_dex_files);

  EXPORT static bool CanReferenceInBootImageExtensionOrAppImage(
      ObjPtr<mirror::Class> klass, gc::Heap* heap) REQUIRES_SHARED(Locks::mutator_lock_);

  EXPORT void SetSdkChecker(std::unique_ptr<SdkChecker>&& sdk_checker_);
  const SdkChecker* GetSdkChecker() const;

  // Verifies if the method is accessible according to the SdkChecker (if installed).
  bool DenyAccessBasedOnPublicSdk(ArtMethod* art_method) const override
      REQUIRES_SHARED(Locks::mutator_lock_);
  // Verifies if the field is accessible according to the SdkChecker (if installed).
  bool DenyAccessBasedOnPublicSdk(ArtField* art_field) const override
      REQUIRES_SHARED(Locks::mutator_lock_);
  // Verifies if the descriptor is accessible according to the SdkChecker (if installed).
  bool DenyAccessBasedOnPublicSdk(std::string_view type_descriptor) const override;
  // Enable or disable public sdk checks.
  void SetEnablePublicSdkChecks(bool enabled) override;

  // Transaction support.
  EXPORT bool IsActiveTransaction() const;
  // EnterTransactionMode may suspend.
  EXPORT void EnterTransactionMode(bool strict, mirror::Class* root)
      REQUIRES_SHARED(Locks::mutator_lock_);
  EXPORT void ExitTransactionMode();
  EXPORT void RollbackAllTransactions() REQUIRES_SHARED(Locks::mutator_lock_);
  // Transaction rollback and exit transaction are always done together, it's convenience to
  // do them in one function.
  void RollbackAndExitTransactionMode() REQUIRES_SHARED(Locks::mutator_lock_);
  const Transaction* GetTransaction() const;
  Transaction* GetTransaction();
  bool IsActiveStrictTransactionMode() const;

  // Transaction constraint checks for AOT compilation.
  bool TransactionWriteConstraint(Thread* self, ObjPtr<mirror::Object> obj) override
      REQUIRES_SHARED(Locks::mutator_lock_);
  bool TransactionWriteValueConstraint(Thread* self, ObjPtr<mirror::Object> value) override
      REQUIRES_SHARED(Locks::mutator_lock_);
  // Note: The read constraint check is non-virtual because it's not needed by `UnstartedRuntime`.
  bool TransactionReadConstraint(Thread* self, ObjPtr<mirror::Object> obj)
      REQUIRES_SHARED(Locks::mutator_lock_);
  bool TransactionAllocationConstraint(Thread* self, ObjPtr<mirror::Class> klass) override
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Transaction bookkeeping for AOT compilation.
  void RecordWriteFieldBoolean(mirror::Object* obj,
                               MemberOffset field_offset,
                               uint8_t value,
                               bool is_volatile) override;
  void RecordWriteFieldByte(mirror::Object* obj,
                            MemberOffset field_offset,
                            int8_t value,
                            bool is_volatile) override;
  void RecordWriteFieldChar(mirror::Object* obj,
                            MemberOffset field_offset,
                            uint16_t value,
                            bool is_volatile) override;
  void RecordWriteFieldShort(mirror::Object* obj,
                             MemberOffset field_offset,
                             int16_t value,
                             bool is_volatile) override;
  void RecordWriteField32(mirror::Object* obj,
                          MemberOffset field_offset,
                          uint32_t value,
                          bool is_volatile) override;
  void RecordWriteField64(mirror::Object* obj,
                          MemberOffset field_offset,
                          uint64_t value,
                          bool is_volatile) override;
  void RecordWriteFieldReference(mirror::Object* obj,
                                 MemberOffset field_offset,
                                 ObjPtr<mirror::Object> value,
                                 bool is_volatile) override
      REQUIRES_SHARED(Locks::mutator_lock_);
  void RecordWriteArray(mirror::Array* array, size_t index, uint64_t value) override
      REQUIRES_SHARED(Locks::mutator_lock_);
  void RecordStrongStringInsertion(ObjPtr<mirror::String> s) override
      REQUIRES(Locks::intern_table_lock_);
  void RecordWeakStringInsertion(ObjPtr<mirror::String> s) override
      REQUIRES(Locks::intern_table_lock_);
  void RecordStrongStringRemoval(ObjPtr<mirror::String> s) override
      REQUIRES(Locks::intern_table_lock_);
  void RecordWeakStringRemoval(ObjPtr<mirror::String> s) override
      REQUIRES(Locks::intern_table_lock_);
  void RecordResolveString(ObjPtr<mirror::DexCache> dex_cache, dex::StringIndex string_idx) override
      REQUIRES_SHARED(Locks::mutator_lock_);
  void RecordResolveMethodType(ObjPtr<mirror::DexCache> dex_cache, dex::ProtoIndex proto_idx)
      override REQUIRES_SHARED(Locks::mutator_lock_);

  // Aborting transactions for AOT compilation.
  void ThrowTransactionAbortError(Thread* self) override
      REQUIRES_SHARED(Locks::mutator_lock_);
  void AbortTransactionF(Thread* self, const char* fmt, ...) override
      __attribute__((__format__(__printf__, 3, 4)))
      REQUIRES_SHARED(Locks::mutator_lock_);
  void AbortTransactionV(Thread* self, const char* fmt, va_list args) override
      REQUIRES_SHARED(Locks::mutator_lock_);
  bool IsTransactionAborted() const override;

  // Visit transaction roots for AOT compilation.
  void VisitTransactionRoots(RootVisitor* visitor) override
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Get transactional switch interpreter entrypoint for AOT compilation.
  const void* GetTransactionalInterpreter() override;

 protected:
  // Overridden version of PerformClassVerification allows skipping verification if the class was
  // previously verified but unloaded.
  verifier::FailureKind PerformClassVerification(Thread* self,
                                                 verifier::VerifierDeps* verifier_deps,
                                                 Handle<mirror::Class> klass,
                                                 verifier::HardFailLogMode log_level,
                                                 std::string* error_msg)
      override
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Override AllocClass because aot compiler will need to perform a transaction check to determine
  // can we allocate class from heap.
  bool CanAllocClass()
      override
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!Roles::uninterruptible_);

  bool InitializeClass(Thread *self,
                       Handle<mirror::Class> klass,
                       bool can_run_clinit,
                       bool can_init_parents)
      override
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!Locks::dex_lock_);

 private:
  std::unique_ptr<SdkChecker> sdk_checker_;

  // Transactions used for pre-initializing classes at compilation time.
  // Support nested transactions, maintain a list containing all transactions. Transactions are
  // handled under a stack discipline. Because GC needs to go over all transactions, we choose list
  // as substantial data structure instead of stack.
  std::forward_list<Transaction> preinitialization_transactions_;
};

}  // namespace art

#endif  // ART_DEX2OAT_AOT_CLASS_LINKER_H_
