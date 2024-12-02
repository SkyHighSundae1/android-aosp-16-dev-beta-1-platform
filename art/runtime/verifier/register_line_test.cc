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

#include "register_line-inl.h"

#include "common_runtime_test.h"
#include "method_verifier.h"
#include "reg_type_cache-inl.h"
#include "reg_type_test_utils.h"

namespace art HIDDEN {
namespace verifier {

class RegisterLineTest : public CommonRuntimeTest {
 protected:
  RegisterLineTest() {
    use_boot_image_ = true;  // Make the Runtime creation cheaper.
  }

  MethodVerifier* CreateVerifier(Thread* self,
                                 RegTypeCache* reg_types,
                                 Handle<mirror::DexCache> dex_cache,
                                 ArtMethod* method) REQUIRES_SHARED(Locks::mutator_lock_) {
    return MethodVerifier::CreateVerifier(
        self,
        reg_types,
        /*verifier_deps=*/ nullptr,
        dex_cache,
        *method->GetDeclaringClass()->GetClassDef(),
        method->GetCodeItem(),
        method->GetDexMethodIndex(),
        method->GetAccessFlags(),
        /*verify_to_dump=*/ false,
        /*api_level=*/ 0u);
  }

  ArenaAllocator& GetArenaAllocator(MethodVerifier* verifier) {
    return verifier->allocator_;
  }
};

TEST_F(RegisterLineTest, NewInstanceDexPcsMerging) {
  ArenaPool* arena_pool = Runtime::Current()->GetArenaPool();
  ScopedObjectAccess soa(Thread::Current());
  StackHandleScope<2u> hs(soa.Self());
  Handle<mirror::Class> object_class = hs.NewHandle(GetClassRoot<mirror::Object>());
  Handle<mirror::DexCache> dex_cache = hs.NewHandle(object_class->GetDexCache());
  const DexFile* dex_file = dex_cache->GetDexFile();
  ScopedNullHandle<mirror::ClassLoader> loader;
  RegTypeCache reg_types(soa.Self(), class_linker_, arena_pool, loader, dex_file);
  ArtMethod* method = object_class->FindClassMethod("wait", "()V", kRuntimePointerSize);
  ASSERT_TRUE(method != nullptr);
  std::unique_ptr<MethodVerifier> verifier(
      CreateVerifier(soa.Self(), &reg_types, dex_cache, method));
  const RegType& resolved_type1 = reg_types.FromDescriptor("Ljava/lang/Object;");
  const RegType& resolved_type2 = reg_types.FromDescriptor("Ljava/lang/String;");
  const RegType& unresolved_type1 = reg_types.FromDescriptor("Ljava/lang/DoesNotExist;");
  const RegType& unresolved_type2 = reg_types.FromDescriptor("Ljava/lang/DoesNotExistEither;");
  const RegType& uninit_resolved_type1 = reg_types.Uninitialized(resolved_type1);
  const RegType& uninit_resolved_type2 = reg_types.Uninitialized(resolved_type2);
  const RegType& uninit_unresolved_type1 = reg_types.Uninitialized(unresolved_type1);
  const RegType& uninit_unresolved_type2 = reg_types.Uninitialized(unresolved_type2);
  const RegType& conflict = reg_types.Conflict();

  struct TestCase {
    const RegType& reg_type1;
    uint32_t dex_pc1;
    const RegType& reg_type2;
    uint32_t dex_pc2;
    const RegType& expected;
  };
  const TestCase test_cases[] = {
    // Merge the same uninitialized type and allocation dex pc.
    {uninit_resolved_type1, 1u, uninit_resolved_type1, 1u, uninit_resolved_type1},
    {uninit_resolved_type2, 1u, uninit_resolved_type2, 1u, uninit_resolved_type2},
    {uninit_unresolved_type1, 1u, uninit_unresolved_type1, 1u, uninit_unresolved_type1},
    {uninit_unresolved_type2, 1u, uninit_unresolved_type2, 1u, uninit_unresolved_type2},
    // Merge the same uninitialized type and different allocation dex pcs.
    {uninit_resolved_type1, 1u, uninit_resolved_type1, 2u, conflict},
    {uninit_resolved_type2, 1u, uninit_resolved_type2, 2u, conflict},
    {uninit_unresolved_type1, 1u, uninit_unresolved_type1, 2u, conflict},
    {uninit_unresolved_type2, 1u, uninit_unresolved_type2, 2u, conflict},
    // Merge different uninitialized types and the same allocation dex pc.
    {uninit_resolved_type1, 1u, uninit_resolved_type2, 1u, conflict},
    {uninit_resolved_type1, 1u, uninit_unresolved_type1, 1u, conflict},
    {uninit_resolved_type1, 1u, uninit_unresolved_type2, 1u, conflict},
    {uninit_resolved_type2, 1u, uninit_resolved_type1, 1u, conflict},
    {uninit_resolved_type2, 1u, uninit_unresolved_type1, 1u, conflict},
    {uninit_resolved_type2, 1u, uninit_unresolved_type2, 1u, conflict},
    {uninit_unresolved_type1, 1u, uninit_resolved_type1, 1u, conflict},
    {uninit_unresolved_type1, 1u, uninit_resolved_type2, 1u, conflict},
    {uninit_unresolved_type1, 1u, uninit_unresolved_type2, 1u, conflict},
    {uninit_unresolved_type2, 1u, uninit_resolved_type1, 1u, conflict},
    {uninit_unresolved_type2, 1u, uninit_resolved_type2, 1u, conflict},
    {uninit_unresolved_type2, 1u, uninit_unresolved_type1, 1u, conflict},
    // Merge uninitialized types with their initialized counterparts.
    {uninit_resolved_type1, 1u, resolved_type1, 1u, conflict},
    {uninit_resolved_type2, 1u, resolved_type2, 1u, conflict},
    {uninit_unresolved_type1, 1u, unresolved_type1, 1u, conflict},
    {uninit_unresolved_type2, 1u, unresolved_type2, 1u, conflict},
    {resolved_type1, 1u, uninit_resolved_type1, 1u, conflict},
    {resolved_type2, 1u, uninit_resolved_type2, 1u, conflict},
    {unresolved_type1, 1u, uninit_unresolved_type1, 1u, conflict},
    {unresolved_type2, 1u, uninit_unresolved_type2, 1u, conflict},
  };

  constexpr size_t kNumRegs = 1u;
  constexpr uint32_t kVReg = 0u;
  ArenaAllocator& allocator = GetArenaAllocator(verifier.get());
  RegisterLineArenaUniquePtr line1(RegisterLine::Create(kNumRegs, allocator, &reg_types));
  RegisterLineArenaUniquePtr line2(RegisterLine::Create(kNumRegs, allocator, &reg_types));
  for (const TestCase& test_case : test_cases) {
    ASSERT_TRUE(test_case.reg_type1.IsUninitializedTypes() ||
                test_case.reg_type2.IsUninitializedTypes());
    auto set_reg_type_and_dex_pc = [&](RegisterLine* line,
                                       const RegType& reg_type,
                                       uint32_t dex_pc,
                                       const RegType& other_reg_type)
        REQUIRES_SHARED(Locks::mutator_lock_) {
      if (reg_type.IsUninitializedTypes()) {
        line->SetRegisterTypeForNewInstance(kVReg, reg_type, dex_pc);
      } else {
        // Initialize the allocation dex pc using the `other_reg_type`, then set the `reg_type`.
        line->SetRegisterTypeForNewInstance(kVReg, other_reg_type, dex_pc);
        line->SetRegisterType<LockOp::kClear>(kVReg, reg_type);
      }
    };
    set_reg_type_and_dex_pc(
        line1.get(), test_case.reg_type1, test_case.dex_pc1, test_case.reg_type2);
    set_reg_type_and_dex_pc(
        line2.get(), test_case.reg_type2, test_case.dex_pc2, test_case.reg_type1);
    line1->MergeRegisters(verifier.get(), line2.get());
    const RegType& result = line1->GetRegisterType(verifier.get(), kVReg);
    ASSERT_TRUE(result.Equals(test_case.expected))
        << RegTypeWrapper(test_case.reg_type1) << " @" << test_case.dex_pc1 << " merge with "
        << RegTypeWrapper(test_case.reg_type2) << " @" << test_case.dex_pc2 << " yielded "
        << RegTypeWrapper(result) << " but we expected " << RegTypeWrapper(test_case.expected);
  }
}

}  // namespace verifier
}  // namespace art
