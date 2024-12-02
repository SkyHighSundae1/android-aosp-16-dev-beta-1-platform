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

#include "reg_type.h"

#include <set>

#include "base/bit_vector.h"
#include "base/casts.h"
#include "base/scoped_arena_allocator.h"
#include "common_runtime_test.h"
#include "compiler_callbacks.h"
#include "dex/test_dex_file_builder.h"
#include "reg_type-inl.h"
#include "reg_type_cache-inl.h"
#include "reg_type_test_utils.h"
#include "scoped_thread_state_change-inl.h"
#include "thread-current-inl.h"

namespace art HIDDEN {
namespace verifier {

class RegTypeTest : public CommonRuntimeTest {
 protected:
  RegTypeTest() {
    use_boot_image_ = true;  // Make the Runtime creation cheaper.
  }

  void SetUp() override {
    CommonRuntimeTest::SetUp();

    // Build a fake `DexFile` with some descriptors.
    static const char* const descriptors[] = {
      // References.
      "Ljava/lang/Object;", "Ljava/lang/String;", "LNonExistent;",
      // Primitives and `void`.
      "Z", "B", "C", "S", "I", "J", "F", "D", "V"
    };
    TestDexFileBuilder builder;
    for (const char* descriptor : descriptors) {
      builder.AddType(descriptor);
    }
    dex_file_ = builder.Build("arbitrary-location");
  }

  static constexpr size_t kNumConstTypes = RegType::Kind::kNull - RegType::Kind::kZero + 1;

  std::array<const RegType*, kNumConstTypes> GetConstRegTypes(const RegTypeCache& cache)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    return {
      &cache.Zero(),
      &cache.BooleanConstant(),
      &cache.PositiveByteConstant(),
      &cache.PositiveShortConstant(),
      &cache.CharConstant(),
      &cache.ByteConstant(),
      &cache.ShortConstant(),
      &cache.IntegerConstant(),
      &cache.ConstantLo(),
      &cache.ConstantHi(),
      &cache.Null(),
    };
  };

  std::unique_ptr<const DexFile> dex_file_;
};

TEST_F(RegTypeTest, Constants) {
  // Tests creating constant types.
  ArenaPool* arena_pool = Runtime::Current()->GetArenaPool();
  ScopedObjectAccess soa(Thread::Current());
  ScopedNullHandle<mirror::ClassLoader> loader;
  RegTypeCache cache(soa.Self(), class_linker_, arena_pool, loader, dex_file_.get());
  std::array<const RegType*, kNumConstTypes> const_reg_types = GetConstRegTypes(cache);

  for (size_t i = 0; i != kNumConstTypes; ++i) {
    EXPECT_TRUE(const_reg_types[i]->IsConstantTypes());
  }

  for (size_t i = 0; i != kNumConstTypes; ++i) {
    for (size_t j = 0; j != kNumConstTypes; ++j) {
      EXPECT_EQ(i == j, const_reg_types[i]->Equals(*(const_reg_types[j]))) << i << " " << j;
    }
  }
}

TEST_F(RegTypeTest, Pairs) {
  ArenaPool* arena_pool = Runtime::Current()->GetArenaPool();
  ScopedObjectAccess soa(Thread::Current());
  ScopedNullHandle<mirror::ClassLoader> loader;
  RegTypeCache cache(soa.Self(), class_linker_, arena_pool, loader, dex_file_.get());
  int64_t val = static_cast<int32_t>(1234);
  const RegType& const_lo = cache.ConstantLo();
  const RegType& const_hi = cache.ConstantHi();
  const RegType& long_lo = cache.LongLo();
  const RegType& long_hi = cache.LongHi();
  const RegType& int_const = cache.IntegerConstant();
  // Check the expectations for types.
  EXPECT_TRUE(const_lo.IsLowHalf());
  EXPECT_FALSE(const_hi.IsLowHalf());
  EXPECT_FALSE(const_lo.IsHighHalf());
  EXPECT_TRUE(const_hi.IsHighHalf());
  EXPECT_TRUE(const_lo.IsLongTypes());
  EXPECT_FALSE(const_hi.IsLongTypes());
  EXPECT_FALSE(const_lo.IsLongHighTypes());
  EXPECT_TRUE(const_hi.IsLongHighTypes());
  EXPECT_TRUE(long_lo.IsLowHalf());
  EXPECT_FALSE(long_hi.IsLowHalf());
  EXPECT_FALSE(long_lo.IsHighHalf());
  EXPECT_TRUE(long_hi.IsHighHalf());
  EXPECT_TRUE(long_lo.IsLongTypes());
  EXPECT_FALSE(long_hi.IsLongTypes());
  EXPECT_FALSE(long_lo.IsLongHighTypes());
  EXPECT_TRUE(long_hi.IsLongHighTypes());
  // Check Pairing.
  EXPECT_FALSE(const_lo.CheckWidePair(const_lo));
  EXPECT_TRUE(const_lo.CheckWidePair(const_hi));
  EXPECT_FALSE(const_lo.CheckWidePair(long_lo));
  EXPECT_FALSE(const_lo.CheckWidePair(long_hi));
  EXPECT_FALSE(const_lo.CheckWidePair(int_const));
  EXPECT_FALSE(const_hi.CheckWidePair(const_lo));
  EXPECT_FALSE(const_hi.CheckWidePair(const_hi));
  EXPECT_FALSE(const_hi.CheckWidePair(long_lo));
  EXPECT_FALSE(const_hi.CheckWidePair(long_hi));
  EXPECT_FALSE(const_hi.CheckWidePair(int_const));
  EXPECT_FALSE(long_lo.CheckWidePair(const_lo));
  EXPECT_FALSE(long_lo.CheckWidePair(const_hi));
  EXPECT_FALSE(long_lo.CheckWidePair(long_lo));
  EXPECT_TRUE(long_lo.CheckWidePair(long_hi));
  EXPECT_FALSE(long_lo.CheckWidePair(int_const));
  EXPECT_FALSE(long_hi.CheckWidePair(const_lo));
  EXPECT_FALSE(long_hi.CheckWidePair(const_hi));
  EXPECT_FALSE(long_hi.CheckWidePair(long_lo));
  EXPECT_FALSE(long_hi.CheckWidePair(long_hi));
  EXPECT_FALSE(long_hi.CheckWidePair(int_const));
  // Test Merging.
  EXPECT_TRUE((long_lo.Merge(const_lo, &cache, /* verifier= */ nullptr)).IsLongTypes());
  EXPECT_TRUE((long_hi.Merge(const_hi, &cache, /* verifier= */ nullptr)).IsLongHighTypes());
}

TEST_F(RegTypeTest, Primitives) {
  ArenaPool* arena_pool = Runtime::Current()->GetArenaPool();
  ScopedObjectAccess soa(Thread::Current());
  ScopedNullHandle<mirror::ClassLoader> loader;
  RegTypeCache cache(soa.Self(), class_linker_, arena_pool, loader, dex_file_.get());

  const RegType& bool_reg_type = cache.Boolean();
  EXPECT_FALSE(bool_reg_type.IsUndefined());
  EXPECT_FALSE(bool_reg_type.IsConflict());
  EXPECT_FALSE(bool_reg_type.IsConstantTypes());
  EXPECT_TRUE(bool_reg_type.IsBoolean());
  EXPECT_FALSE(bool_reg_type.IsByte());
  EXPECT_FALSE(bool_reg_type.IsChar());
  EXPECT_FALSE(bool_reg_type.IsShort());
  EXPECT_FALSE(bool_reg_type.IsInteger());
  EXPECT_FALSE(bool_reg_type.IsLongLo());
  EXPECT_FALSE(bool_reg_type.IsFloat());
  EXPECT_FALSE(bool_reg_type.IsDoubleLo());
  EXPECT_FALSE(bool_reg_type.IsReference());
  EXPECT_FALSE(bool_reg_type.IsLowHalf());
  EXPECT_FALSE(bool_reg_type.IsHighHalf());
  EXPECT_FALSE(bool_reg_type.IsLongOrDoubleTypes());
  EXPECT_FALSE(bool_reg_type.IsReferenceTypes());
  EXPECT_TRUE(bool_reg_type.IsCategory1Types());
  EXPECT_FALSE(bool_reg_type.IsCategory2Types());
  EXPECT_TRUE(bool_reg_type.IsBooleanTypes());
  EXPECT_TRUE(bool_reg_type.IsByteTypes());
  EXPECT_TRUE(bool_reg_type.IsShortTypes());
  EXPECT_TRUE(bool_reg_type.IsCharTypes());
  EXPECT_TRUE(bool_reg_type.IsIntegralTypes());
  EXPECT_FALSE(bool_reg_type.IsFloatTypes());
  EXPECT_FALSE(bool_reg_type.IsLongTypes());
  EXPECT_FALSE(bool_reg_type.IsDoubleTypes());
  EXPECT_TRUE(bool_reg_type.IsArrayIndexTypes());
  EXPECT_FALSE(bool_reg_type.IsNonZeroReferenceTypes());
  EXPECT_FALSE(bool_reg_type.HasClass());

  const RegType& byte_reg_type = cache.Byte();
  EXPECT_FALSE(byte_reg_type.IsUndefined());
  EXPECT_FALSE(byte_reg_type.IsConflict());
  EXPECT_FALSE(bool_reg_type.IsConstantTypes());
  EXPECT_FALSE(byte_reg_type.IsBoolean());
  EXPECT_TRUE(byte_reg_type.IsByte());
  EXPECT_FALSE(byte_reg_type.IsChar());
  EXPECT_FALSE(byte_reg_type.IsShort());
  EXPECT_FALSE(byte_reg_type.IsInteger());
  EXPECT_FALSE(byte_reg_type.IsLongLo());
  EXPECT_FALSE(byte_reg_type.IsFloat());
  EXPECT_FALSE(byte_reg_type.IsDoubleLo());
  EXPECT_FALSE(byte_reg_type.IsReference());
  EXPECT_FALSE(byte_reg_type.IsLowHalf());
  EXPECT_FALSE(byte_reg_type.IsHighHalf());
  EXPECT_FALSE(byte_reg_type.IsLongOrDoubleTypes());
  EXPECT_FALSE(byte_reg_type.IsReferenceTypes());
  EXPECT_TRUE(byte_reg_type.IsCategory1Types());
  EXPECT_FALSE(byte_reg_type.IsCategory2Types());
  EXPECT_FALSE(byte_reg_type.IsBooleanTypes());
  EXPECT_TRUE(byte_reg_type.IsByteTypes());
  EXPECT_TRUE(byte_reg_type.IsShortTypes());
  EXPECT_FALSE(byte_reg_type.IsCharTypes());
  EXPECT_TRUE(byte_reg_type.IsIntegralTypes());
  EXPECT_FALSE(byte_reg_type.IsFloatTypes());
  EXPECT_FALSE(byte_reg_type.IsLongTypes());
  EXPECT_FALSE(byte_reg_type.IsDoubleTypes());
  EXPECT_TRUE(byte_reg_type.IsArrayIndexTypes());
  EXPECT_FALSE(byte_reg_type.IsNonZeroReferenceTypes());
  EXPECT_FALSE(byte_reg_type.HasClass());

  const RegType& char_reg_type = cache.Char();
  EXPECT_FALSE(char_reg_type.IsUndefined());
  EXPECT_FALSE(char_reg_type.IsConflict());
  EXPECT_FALSE(bool_reg_type.IsConstantTypes());
  EXPECT_FALSE(char_reg_type.IsBoolean());
  EXPECT_FALSE(char_reg_type.IsByte());
  EXPECT_TRUE(char_reg_type.IsChar());
  EXPECT_FALSE(char_reg_type.IsShort());
  EXPECT_FALSE(char_reg_type.IsInteger());
  EXPECT_FALSE(char_reg_type.IsLongLo());
  EXPECT_FALSE(char_reg_type.IsFloat());
  EXPECT_FALSE(char_reg_type.IsDoubleLo());
  EXPECT_FALSE(char_reg_type.IsReference());
  EXPECT_FALSE(char_reg_type.IsLowHalf());
  EXPECT_FALSE(char_reg_type.IsHighHalf());
  EXPECT_FALSE(char_reg_type.IsLongOrDoubleTypes());
  EXPECT_FALSE(char_reg_type.IsReferenceTypes());
  EXPECT_TRUE(char_reg_type.IsCategory1Types());
  EXPECT_FALSE(char_reg_type.IsCategory2Types());
  EXPECT_FALSE(char_reg_type.IsBooleanTypes());
  EXPECT_FALSE(char_reg_type.IsByteTypes());
  EXPECT_FALSE(char_reg_type.IsShortTypes());
  EXPECT_TRUE(char_reg_type.IsCharTypes());
  EXPECT_TRUE(char_reg_type.IsIntegralTypes());
  EXPECT_FALSE(char_reg_type.IsFloatTypes());
  EXPECT_FALSE(char_reg_type.IsLongTypes());
  EXPECT_FALSE(char_reg_type.IsDoubleTypes());
  EXPECT_TRUE(char_reg_type.IsArrayIndexTypes());
  EXPECT_FALSE(char_reg_type.IsNonZeroReferenceTypes());
  EXPECT_FALSE(char_reg_type.HasClass());

  const RegType& short_reg_type = cache.Short();
  EXPECT_FALSE(short_reg_type.IsUndefined());
  EXPECT_FALSE(short_reg_type.IsConflict());
  EXPECT_FALSE(bool_reg_type.IsConstantTypes());
  EXPECT_FALSE(short_reg_type.IsBoolean());
  EXPECT_FALSE(short_reg_type.IsByte());
  EXPECT_FALSE(short_reg_type.IsChar());
  EXPECT_TRUE(short_reg_type.IsShort());
  EXPECT_FALSE(short_reg_type.IsInteger());
  EXPECT_FALSE(short_reg_type.IsLongLo());
  EXPECT_FALSE(short_reg_type.IsFloat());
  EXPECT_FALSE(short_reg_type.IsDoubleLo());
  EXPECT_FALSE(short_reg_type.IsReference());
  EXPECT_FALSE(short_reg_type.IsLowHalf());
  EXPECT_FALSE(short_reg_type.IsHighHalf());
  EXPECT_FALSE(short_reg_type.IsLongOrDoubleTypes());
  EXPECT_FALSE(short_reg_type.IsReferenceTypes());
  EXPECT_TRUE(short_reg_type.IsCategory1Types());
  EXPECT_FALSE(short_reg_type.IsCategory2Types());
  EXPECT_FALSE(short_reg_type.IsBooleanTypes());
  EXPECT_FALSE(short_reg_type.IsByteTypes());
  EXPECT_TRUE(short_reg_type.IsShortTypes());
  EXPECT_FALSE(short_reg_type.IsCharTypes());
  EXPECT_TRUE(short_reg_type.IsIntegralTypes());
  EXPECT_FALSE(short_reg_type.IsFloatTypes());
  EXPECT_FALSE(short_reg_type.IsLongTypes());
  EXPECT_FALSE(short_reg_type.IsDoubleTypes());
  EXPECT_TRUE(short_reg_type.IsArrayIndexTypes());
  EXPECT_FALSE(short_reg_type.IsNonZeroReferenceTypes());
  EXPECT_FALSE(short_reg_type.HasClass());

  const RegType& int_reg_type = cache.Integer();
  EXPECT_FALSE(int_reg_type.IsUndefined());
  EXPECT_FALSE(int_reg_type.IsConflict());
  EXPECT_FALSE(bool_reg_type.IsConstantTypes());
  EXPECT_FALSE(int_reg_type.IsBoolean());
  EXPECT_FALSE(int_reg_type.IsByte());
  EXPECT_FALSE(int_reg_type.IsChar());
  EXPECT_FALSE(int_reg_type.IsShort());
  EXPECT_TRUE(int_reg_type.IsInteger());
  EXPECT_FALSE(int_reg_type.IsLongLo());
  EXPECT_FALSE(int_reg_type.IsFloat());
  EXPECT_FALSE(int_reg_type.IsDoubleLo());
  EXPECT_FALSE(int_reg_type.IsReference());
  EXPECT_FALSE(int_reg_type.IsLowHalf());
  EXPECT_FALSE(int_reg_type.IsHighHalf());
  EXPECT_FALSE(int_reg_type.IsLongOrDoubleTypes());
  EXPECT_FALSE(int_reg_type.IsReferenceTypes());
  EXPECT_TRUE(int_reg_type.IsCategory1Types());
  EXPECT_FALSE(int_reg_type.IsCategory2Types());
  EXPECT_FALSE(int_reg_type.IsBooleanTypes());
  EXPECT_FALSE(int_reg_type.IsByteTypes());
  EXPECT_FALSE(int_reg_type.IsShortTypes());
  EXPECT_FALSE(int_reg_type.IsCharTypes());
  EXPECT_TRUE(int_reg_type.IsIntegralTypes());
  EXPECT_FALSE(int_reg_type.IsFloatTypes());
  EXPECT_FALSE(int_reg_type.IsLongTypes());
  EXPECT_FALSE(int_reg_type.IsDoubleTypes());
  EXPECT_TRUE(int_reg_type.IsArrayIndexTypes());
  EXPECT_FALSE(int_reg_type.IsNonZeroReferenceTypes());
  EXPECT_FALSE(int_reg_type.HasClass());

  const RegType& long_reg_type = cache.LongLo();
  EXPECT_FALSE(long_reg_type.IsUndefined());
  EXPECT_FALSE(long_reg_type.IsConflict());
  EXPECT_FALSE(bool_reg_type.IsConstantTypes());
  EXPECT_FALSE(long_reg_type.IsBoolean());
  EXPECT_FALSE(long_reg_type.IsByte());
  EXPECT_FALSE(long_reg_type.IsChar());
  EXPECT_FALSE(long_reg_type.IsShort());
  EXPECT_FALSE(long_reg_type.IsInteger());
  EXPECT_TRUE(long_reg_type.IsLongLo());
  EXPECT_FALSE(long_reg_type.IsFloat());
  EXPECT_FALSE(long_reg_type.IsDoubleLo());
  EXPECT_FALSE(long_reg_type.IsReference());
  EXPECT_TRUE(long_reg_type.IsLowHalf());
  EXPECT_FALSE(long_reg_type.IsHighHalf());
  EXPECT_TRUE(long_reg_type.IsLongOrDoubleTypes());
  EXPECT_FALSE(long_reg_type.IsReferenceTypes());
  EXPECT_FALSE(long_reg_type.IsCategory1Types());
  EXPECT_TRUE(long_reg_type.IsCategory2Types());
  EXPECT_FALSE(long_reg_type.IsBooleanTypes());
  EXPECT_FALSE(long_reg_type.IsByteTypes());
  EXPECT_FALSE(long_reg_type.IsShortTypes());
  EXPECT_FALSE(long_reg_type.IsCharTypes());
  EXPECT_FALSE(long_reg_type.IsIntegralTypes());
  EXPECT_FALSE(long_reg_type.IsFloatTypes());
  EXPECT_TRUE(long_reg_type.IsLongTypes());
  EXPECT_FALSE(long_reg_type.IsDoubleTypes());
  EXPECT_FALSE(long_reg_type.IsArrayIndexTypes());
  EXPECT_FALSE(long_reg_type.IsNonZeroReferenceTypes());
  EXPECT_FALSE(long_reg_type.HasClass());

  const RegType& float_reg_type = cache.Float();
  EXPECT_FALSE(float_reg_type.IsUndefined());
  EXPECT_FALSE(float_reg_type.IsConflict());
  EXPECT_FALSE(bool_reg_type.IsConstantTypes());
  EXPECT_FALSE(float_reg_type.IsBoolean());
  EXPECT_FALSE(float_reg_type.IsByte());
  EXPECT_FALSE(float_reg_type.IsChar());
  EXPECT_FALSE(float_reg_type.IsShort());
  EXPECT_FALSE(float_reg_type.IsInteger());
  EXPECT_FALSE(float_reg_type.IsLongLo());
  EXPECT_TRUE(float_reg_type.IsFloat());
  EXPECT_FALSE(float_reg_type.IsDoubleLo());
  EXPECT_FALSE(float_reg_type.IsReference());
  EXPECT_FALSE(float_reg_type.IsLowHalf());
  EXPECT_FALSE(float_reg_type.IsHighHalf());
  EXPECT_FALSE(float_reg_type.IsLongOrDoubleTypes());
  EXPECT_FALSE(float_reg_type.IsReferenceTypes());
  EXPECT_TRUE(float_reg_type.IsCategory1Types());
  EXPECT_FALSE(float_reg_type.IsCategory2Types());
  EXPECT_FALSE(float_reg_type.IsBooleanTypes());
  EXPECT_FALSE(float_reg_type.IsByteTypes());
  EXPECT_FALSE(float_reg_type.IsShortTypes());
  EXPECT_FALSE(float_reg_type.IsCharTypes());
  EXPECT_FALSE(float_reg_type.IsIntegralTypes());
  EXPECT_TRUE(float_reg_type.IsFloatTypes());
  EXPECT_FALSE(float_reg_type.IsLongTypes());
  EXPECT_FALSE(float_reg_type.IsDoubleTypes());
  EXPECT_FALSE(float_reg_type.IsArrayIndexTypes());
  EXPECT_FALSE(float_reg_type.IsNonZeroReferenceTypes());
  EXPECT_FALSE(float_reg_type.HasClass());

  const RegType& double_reg_type = cache.DoubleLo();
  EXPECT_FALSE(double_reg_type.IsUndefined());
  EXPECT_FALSE(double_reg_type.IsConflict());
  EXPECT_FALSE(bool_reg_type.IsConstantTypes());
  EXPECT_FALSE(double_reg_type.IsBoolean());
  EXPECT_FALSE(double_reg_type.IsByte());
  EXPECT_FALSE(double_reg_type.IsChar());
  EXPECT_FALSE(double_reg_type.IsShort());
  EXPECT_FALSE(double_reg_type.IsInteger());
  EXPECT_FALSE(double_reg_type.IsLongLo());
  EXPECT_FALSE(double_reg_type.IsFloat());
  EXPECT_TRUE(double_reg_type.IsDoubleLo());
  EXPECT_FALSE(double_reg_type.IsReference());
  EXPECT_TRUE(double_reg_type.IsLowHalf());
  EXPECT_FALSE(double_reg_type.IsHighHalf());
  EXPECT_TRUE(double_reg_type.IsLongOrDoubleTypes());
  EXPECT_FALSE(double_reg_type.IsReferenceTypes());
  EXPECT_FALSE(double_reg_type.IsCategory1Types());
  EXPECT_TRUE(double_reg_type.IsCategory2Types());
  EXPECT_FALSE(double_reg_type.IsBooleanTypes());
  EXPECT_FALSE(double_reg_type.IsByteTypes());
  EXPECT_FALSE(double_reg_type.IsShortTypes());
  EXPECT_FALSE(double_reg_type.IsCharTypes());
  EXPECT_FALSE(double_reg_type.IsIntegralTypes());
  EXPECT_FALSE(double_reg_type.IsFloatTypes());
  EXPECT_FALSE(double_reg_type.IsLongTypes());
  EXPECT_TRUE(double_reg_type.IsDoubleTypes());
  EXPECT_FALSE(double_reg_type.IsArrayIndexTypes());
  EXPECT_FALSE(double_reg_type.IsNonZeroReferenceTypes());
  EXPECT_FALSE(double_reg_type.HasClass());
}

class RegTypeReferenceTest : public RegTypeTest {};

TEST_F(RegTypeReferenceTest, UnresolvedType) {
  // Tests creating unresolved types. Miss for the first time asking the cache and
  // a hit second time.
  ArenaPool* arena_pool = Runtime::Current()->GetArenaPool();
  ScopedObjectAccess soa(Thread::Current());
  ScopedNullHandle<mirror::ClassLoader> loader;
  RegTypeCache cache(soa.Self(), class_linker_, arena_pool, loader, dex_file_.get());
  const RegType& ref_type_0 = cache.FromDescriptor("Ljava/lang/DoesNotExist;");
  EXPECT_TRUE(ref_type_0.IsUnresolvedReference());
  EXPECT_TRUE(ref_type_0.IsNonZeroReferenceTypes());

  const RegType& ref_type_1 = cache.FromDescriptor("Ljava/lang/DoesNotExist;");
  EXPECT_TRUE(ref_type_0.Equals(ref_type_1));
}

TEST_F(RegTypeReferenceTest, UnresolvedUnintializedType) {
  // Tests creating types uninitialized types from unresolved types.
  ArenaPool* arena_pool = Runtime::Current()->GetArenaPool();
  ScopedObjectAccess soa(Thread::Current());
  ScopedNullHandle<mirror::ClassLoader> loader;
  RegTypeCache cache(soa.Self(), class_linker_, arena_pool, loader, dex_file_.get());
  const RegType& ref_type_0 = cache.FromDescriptor("Ljava/lang/DoesNotExist;");
  EXPECT_TRUE(ref_type_0.IsUnresolvedReference());
  const RegType& ref_type = cache.FromDescriptor("Ljava/lang/DoesNotExist;");
  EXPECT_TRUE(ref_type_0.Equals(ref_type));
  // Create an uninitialized type of this unresolved type.
  const RegType& unresolved_uninitialized = cache.Uninitialized(ref_type);
  EXPECT_TRUE(unresolved_uninitialized.IsUnresolvedUninitializedReference());
  EXPECT_TRUE(unresolved_uninitialized.IsUninitializedTypes());
  EXPECT_TRUE(unresolved_uninitialized.IsNonZeroReferenceTypes());
  // Create an another uninitialized type of this unresolved type.
  const RegType& unresolved_uninitialized_2 = cache.Uninitialized(ref_type);
  EXPECT_TRUE(unresolved_uninitialized.Equals(unresolved_uninitialized_2));
}

TEST_F(RegTypeReferenceTest, Dump) {
  // Tests types for proper Dump messages.
  ArenaPool* arena_pool = Runtime::Current()->GetArenaPool();
  ScopedObjectAccess soa(Thread::Current());
  ScopedNullHandle<mirror::ClassLoader> loader;
  RegTypeCache cache(soa.Self(), class_linker_, arena_pool, loader, dex_file_.get());
  const RegType& unresolved_ref = cache.FromDescriptor("Ljava/lang/DoesNotExist;");
  const RegType& unresolved_ref_another = cache.FromDescriptor("Ljava/lang/DoesNotExistEither;");
  const RegType& resolved_ref = cache.JavaLangString();
  const RegType& resolved_uninitialized = cache.Uninitialized(resolved_ref);
  const RegType& unresolved_uninitialized = cache.Uninitialized(unresolved_ref);
  const RegType& unresolved_merged = cache.FromUnresolvedMerge(
      unresolved_ref, unresolved_ref_another, /* verifier= */ nullptr);

  std::string expected = "Unresolved Reference: java.lang.DoesNotExist";
  EXPECT_EQ(expected, unresolved_ref.Dump());
  expected = "Reference: java.lang.String";
  EXPECT_EQ(expected, resolved_ref.Dump());
  expected ="Uninitialized Reference: java.lang.String";
  EXPECT_EQ(expected, resolved_uninitialized .Dump());
  expected = "Unresolved And Uninitialized Reference: java.lang.DoesNotExist";
  EXPECT_EQ(expected, unresolved_uninitialized.Dump());
  expected = "UnresolvedMergedReferences(Zero/null | Unresolved Reference: java.lang.DoesNotExist, Unresolved Reference: java.lang.DoesNotExistEither)";
  EXPECT_EQ(expected, unresolved_merged.Dump());
}

TEST_F(RegTypeReferenceTest, JavalangString) {
  // Add a class to the cache then look for the same class and make sure it is  a
  // Hit the second time. Then check for the same effect when using
  // The JavaLangObject method instead of FromDescriptor. String class is final.
  ArenaPool* arena_pool = Runtime::Current()->GetArenaPool();
  ScopedObjectAccess soa(Thread::Current());
  ScopedNullHandle<mirror::ClassLoader> loader;
  RegTypeCache cache(soa.Self(), class_linker_, arena_pool, loader, dex_file_.get());
  const RegType& ref_type = cache.JavaLangString();
  const RegType& ref_type_2 = cache.JavaLangString();
  const RegType& ref_type_3 = cache.FromDescriptor("Ljava/lang/String;");

  EXPECT_TRUE(ref_type.Equals(ref_type_2));
  EXPECT_TRUE(ref_type_2.Equals(ref_type_3));
  EXPECT_TRUE(ref_type.IsReference());

  // Create an uninitialized type out of this:
  const RegType& ref_type_uninitialized = cache.Uninitialized(ref_type);
  EXPECT_TRUE(ref_type_uninitialized.IsUninitializedReference());
  EXPECT_FALSE(ref_type_uninitialized.IsUnresolvedUninitializedReference());
}

TEST_F(RegTypeReferenceTest, JavalangObject) {
  // Add a class to the cache then look for the same class and make sure it is  a
  // Hit the second time. Then I am checking for the same effect when using
  // The JavaLangObject method instead of FromDescriptor. Object Class in not final.
  ArenaPool* arena_pool = Runtime::Current()->GetArenaPool();
  ScopedObjectAccess soa(Thread::Current());
  ScopedNullHandle<mirror::ClassLoader> loader;
  RegTypeCache cache(soa.Self(), class_linker_, arena_pool, loader, dex_file_.get());
  const RegType& ref_type = cache.JavaLangObject();
  const RegType& ref_type_2 = cache.JavaLangObject();
  const RegType& ref_type_3 = cache.FromDescriptor("Ljava/lang/Object;");

  EXPECT_TRUE(ref_type.Equals(ref_type_2));
  EXPECT_TRUE(ref_type_3.Equals(ref_type_2));
  EXPECT_EQ(ref_type.GetId(), ref_type_3.GetId());
}

TEST_F(RegTypeReferenceTest, Merging) {
  // Tests merging logic
  // String and object , LUB is object.
  ScopedObjectAccess soa(Thread::Current());
  ArenaPool* arena_pool = Runtime::Current()->GetArenaPool();
  ScopedNullHandle<mirror::ClassLoader> loader;
  RegTypeCache cache_new(soa.Self(), class_linker_, arena_pool, loader, dex_file_.get());
  const RegType& string = cache_new.JavaLangString();
  const RegType& Object = cache_new.JavaLangObject();
  EXPECT_TRUE(string.Merge(Object, &cache_new, /* verifier= */ nullptr).IsJavaLangObject());
  // Merge two unresolved types.
  const RegType& ref_type_0 = cache_new.FromDescriptor("Ljava/lang/DoesNotExist;");
  EXPECT_TRUE(ref_type_0.IsUnresolvedReference());
  const RegType& ref_type_1 = cache_new.FromDescriptor("Ljava/lang/DoesNotExistToo;");
  EXPECT_FALSE(ref_type_0.Equals(ref_type_1));

  const RegType& merged = ref_type_1.Merge(ref_type_0, &cache_new, /* verifier= */ nullptr);
  EXPECT_TRUE(merged.IsUnresolvedMergedReference());
  RegType& merged_nonconst = const_cast<RegType&>(merged);

  const BitVector& unresolved_parts =
      down_cast<UnresolvedMergedReferenceType*>(&merged_nonconst)->GetUnresolvedTypes();
  EXPECT_TRUE(unresolved_parts.IsBitSet(ref_type_0.GetId()));
  EXPECT_TRUE(unresolved_parts.IsBitSet(ref_type_1.GetId()));
}

TEST_F(RegTypeTest, MergingFloat) {
  // Testing merging logic with float and float constants.
  ArenaPool* arena_pool = Runtime::Current()->GetArenaPool();
  ScopedObjectAccess soa(Thread::Current());
  ScopedNullHandle<mirror::ClassLoader> loader;
  RegTypeCache cache(soa.Self(), class_linker_, arena_pool, loader, dex_file_.get());
  std::array<const RegType*, kNumConstTypes> const_reg_types = GetConstRegTypes(cache);

  const RegType& float_type = cache.Float();
  for (const RegType* const_type : const_reg_types) {
    // float MERGE cst => float.
    const RegType& merged = float_type.Merge(*const_type, &cache, /* verifier= */ nullptr);
    if (const_type->IsConstant()) {
      EXPECT_TRUE(merged.IsFloat()) << RegTypeWrapper(*const_type);
    } else {
      DCHECK(const_type->IsConstantLo() || const_type->IsConstantHi() || const_type->IsNull());
      EXPECT_TRUE(merged.IsConflict()) << RegTypeWrapper(*const_type);
    }
  }
  for (const RegType* const_type : const_reg_types) {
    // cst MERGE float => float.
    const RegType& merged = const_type->Merge(float_type, &cache, /* verifier= */ nullptr);
    if (const_type->IsConstant()) {
      EXPECT_TRUE(merged.IsFloat()) << RegTypeWrapper(*const_type);
    } else {
      DCHECK(const_type->IsConstantLo() || const_type->IsConstantHi() || const_type->IsNull());
      EXPECT_TRUE(merged.IsConflict()) << RegTypeWrapper(*const_type);
    }
  }
}

TEST_F(RegTypeTest, MergingLong) {
  // Testing merging logic with long and long constants.
  ArenaPool* arena_pool = Runtime::Current()->GetArenaPool();
  ScopedObjectAccess soa(Thread::Current());
  ScopedNullHandle<mirror::ClassLoader> loader;
  RegTypeCache cache(soa.Self(), class_linker_, arena_pool, loader, dex_file_.get());
  std::array<const RegType*, kNumConstTypes> const_reg_types = GetConstRegTypes(cache);

  const RegType& long_lo_type = cache.LongLo();
  const RegType& long_hi_type = cache.LongHi();
  for (const RegType* const_type : const_reg_types) {
    // lo MERGE cst lo => lo.
    const RegType& merged = long_lo_type.Merge(*const_type, &cache, /* verifier= */ nullptr);
    if (const_type->IsConstantLo()) {
      EXPECT_TRUE(merged.IsLongLo()) << RegTypeWrapper(*const_type);
    } else {
      EXPECT_TRUE(merged.IsConflict()) << RegTypeWrapper(*const_type);
    }
  }
  for (const RegType* const_type : const_reg_types) {
    // cst lo MERGE lo => lo.
    const RegType& merged = const_type->Merge(long_lo_type, &cache, /* verifier= */ nullptr);
    if (const_type->IsConstantLo()) {
      EXPECT_TRUE(merged.IsLongLo()) << RegTypeWrapper(*const_type);
    } else {
      EXPECT_TRUE(merged.IsConflict()) << RegTypeWrapper(*const_type);
    }
  }
  for (const RegType* const_type : const_reg_types) {
    // hi MERGE cst hi => hi.
    const RegType& merged = long_hi_type.Merge(*const_type, &cache, /* verifier= */ nullptr);
    if (const_type->IsConstantHi()) {
      EXPECT_TRUE(merged.IsLongHi()) << RegTypeWrapper(*const_type);
    } else {
      EXPECT_TRUE(merged.IsConflict()) << RegTypeWrapper(*const_type);
    }
  }
  for (const RegType* const_type : const_reg_types) {
    // cst hi MERGE hi => hi.
    const RegType& merged = const_type->Merge(long_hi_type, &cache, /* verifier= */ nullptr);
    if (const_type->IsConstantHi()) {
      EXPECT_TRUE(merged.IsLongHi()) << RegTypeWrapper(*const_type);
    } else {
      EXPECT_TRUE(merged.IsConflict()) << RegTypeWrapper(*const_type);
    }
  }
}

TEST_F(RegTypeTest, MergingDouble) {
  // Testing merging logic with double and double constants.
  ArenaPool* arena_pool = Runtime::Current()->GetArenaPool();
  ScopedObjectAccess soa(Thread::Current());
  ScopedNullHandle<mirror::ClassLoader> loader;
  RegTypeCache cache(soa.Self(), class_linker_, arena_pool, loader, dex_file_.get());
  std::array<const RegType*, kNumConstTypes> const_reg_types = GetConstRegTypes(cache);

  const RegType& double_lo_type = cache.DoubleLo();
  const RegType& double_hi_type = cache.DoubleHi();
  for (const RegType* const_type : const_reg_types) {
    // lo MERGE cst lo => lo.
    const RegType& merged = double_lo_type.Merge(*const_type, &cache, /* verifier= */ nullptr);
    if (const_type->IsConstantLo()) {
      EXPECT_TRUE(merged.IsDoubleLo()) << RegTypeWrapper(*const_type);
    } else {
      EXPECT_TRUE(merged.IsConflict()) << RegTypeWrapper(*const_type);
    }
  }
  for (const RegType* const_type : const_reg_types) {
    // cst lo MERGE lo => lo.
    const RegType& merged = const_type->Merge(double_lo_type, &cache, /* verifier= */ nullptr);
    if (const_type->IsConstantLo()) {
      EXPECT_TRUE(merged.IsDoubleLo()) << RegTypeWrapper(*const_type);
    } else {
      EXPECT_TRUE(merged.IsConflict()) << RegTypeWrapper(*const_type);
    }
  }
  for (const RegType* const_type : const_reg_types) {
    // hi MERGE cst hi => hi.
    const RegType& merged = double_hi_type.Merge(*const_type, &cache, /* verifier= */ nullptr);
    if (const_type->IsConstantHi()) {
      EXPECT_TRUE(merged.IsDoubleHi()) << RegTypeWrapper(*const_type);
    } else {
      EXPECT_TRUE(merged.IsConflict()) << RegTypeWrapper(*const_type);
    }
  }
  for (const RegType* const_type : const_reg_types) {
    // cst hi MERGE hi => hi.
    const RegType& merged = const_type->Merge(double_hi_type, &cache, /* verifier= */ nullptr);
    if (const_type->IsConstantHi()) {
      EXPECT_TRUE(merged.IsDoubleHi()) << RegTypeWrapper(*const_type);
    } else {
      EXPECT_TRUE(merged.IsConflict()) << RegTypeWrapper(*const_type);
    }
  }
}

// Without a running MethodVerifier, the class-bearing register types may become stale as the GC
// will not visit them. It is easiest to disable moving GC.
//
// For some of the tests we need (or want) a working RegTypeCache that can load classes. So it is
// not generally possible to disable GC using ScopedGCCriticalSection (as it blocks GC and
// suspension completely).
struct ScopedDisableMovingGC {
  explicit ScopedDisableMovingGC(Thread* t) : self(t) {
    Runtime::Current()->GetHeap()->IncrementDisableMovingGC(self);
  }
  ~ScopedDisableMovingGC() {
    Runtime::Current()->GetHeap()->DecrementDisableMovingGC(self);
  }

  Thread* self;
};

TEST_F(RegTypeTest, MergeSemiLatticeRef) {
  //  (Incomplete) semilattice:
  //
  //  Excluded for now: * category-2 types
  //                    * interfaces
  //                    * all of category-1 primitive types, including constants.
  //  This is to demonstrate/codify the reference side, mostly.
  //
  //  Note: It is not a real semilattice because int = float makes this wonky. :-(
  //
  //                                       Conflict
  //                                           |
  //      #---------#--------------------------#-----------------------------#
  //      |         |                                                        |
  //      |         |                                                      Object
  //      |         |                                                        |
  //     int   uninit types              #---------------#--------#------------------#---------#
  //      |                              |               |        |                  |         |
  //      |                  unresolved-merge-types      |      Object[]           char[]   byte[]
  //      |                              |    |  |       |        |                  |         |
  //      |                  unresolved-types |  #------Number    #---------#        |         |
  //      |                              |    |          |        |         |        |         |
  //      |                              |    #--------Integer  Number[] Number[][]  |         |
  //      |                              |               |        |         |        |         |
  //      |                              #---------------#--------#---------#--------#---------#
  //      |                                                       |
  //      |                                                     null
  //      |                                                       |
  //      #--------------------------#----------------------------#
  //                                 |
  //                                 0

  ArenaPool* arena_pool = Runtime::Current()->GetArenaPool();
  ScopedObjectAccess soa(Thread::Current());

  ScopedDisableMovingGC no_gc(soa.Self());

  ScopedNullHandle<mirror::ClassLoader> loader;
  RegTypeCache cache(soa.Self(), class_linker_, arena_pool, loader, dex_file_.get());

  const RegType& conflict = cache.Conflict();
  const RegType& zero = cache.Zero();
  const RegType& null = cache.Null();
  const RegType& int_type = cache.Integer();

  const RegType& obj = cache.JavaLangObject();
  const RegType& obj_arr = cache.FromDescriptor("[Ljava/lang/Object;");
  ASSERT_FALSE(obj_arr.IsUnresolvedReference());

  const RegType& unresolved_a = cache.FromDescriptor("Ldoes/not/resolve/A;");
  ASSERT_TRUE(unresolved_a.IsUnresolvedReference());
  const RegType& unresolved_b = cache.FromDescriptor("Ldoes/not/resolve/B;");
  ASSERT_TRUE(unresolved_b.IsUnresolvedReference());
  const RegType& unresolved_ab = cache.FromUnresolvedMerge(unresolved_a, unresolved_b, nullptr);
  ASSERT_TRUE(unresolved_ab.IsUnresolvedMergedReference());

  const RegType& uninit_this = cache.UninitializedThisArgument(obj);
  const RegType& uninit_obj = cache.Uninitialized(obj);

  const RegType& uninit_unres_this = cache.UninitializedThisArgument(unresolved_a);
  const RegType& uninit_unres_a = cache.Uninitialized(unresolved_a);
  const RegType& uninit_unres_b = cache.Uninitialized(unresolved_b);

  const RegType& number = cache.FromDescriptor("Ljava/lang/Number;");
  ASSERT_FALSE(number.IsUnresolvedReference());
  const RegType& integer = cache.FromDescriptor("Ljava/lang/Integer;");
  ASSERT_FALSE(integer.IsUnresolvedReference());

  const RegType& uninit_number = cache.Uninitialized(number);
  const RegType& uninit_integer = cache.Uninitialized(integer);

  const RegType& number_arr = cache.FromDescriptor("[Ljava/lang/Number;");
  ASSERT_FALSE(number_arr.IsUnresolvedReference());
  const RegType& integer_arr = cache.FromDescriptor("[Ljava/lang/Integer;");
  ASSERT_FALSE(integer_arr.IsUnresolvedReference());

  const RegType& number_arr_arr = cache.FromDescriptor("[[Ljava/lang/Number;");
  ASSERT_FALSE(number_arr_arr.IsUnresolvedReference());

  const RegType& char_arr = cache.FromDescriptor("[C");
  ASSERT_FALSE(char_arr.IsUnresolvedReference());
  const RegType& byte_arr = cache.FromDescriptor("[B");
  ASSERT_FALSE(byte_arr.IsUnresolvedReference());

  const RegType& unresolved_a_num = cache.FromUnresolvedMerge(unresolved_a, number, nullptr);
  ASSERT_TRUE(unresolved_a_num.IsUnresolvedMergedReference());
  const RegType& unresolved_b_num = cache.FromUnresolvedMerge(unresolved_b, number, nullptr);
  ASSERT_TRUE(unresolved_b_num.IsUnresolvedMergedReference());
  const RegType& unresolved_ab_num = cache.FromUnresolvedMerge(unresolved_ab, number, nullptr);
  ASSERT_TRUE(unresolved_ab_num.IsUnresolvedMergedReference());

  const RegType& unresolved_a_int = cache.FromUnresolvedMerge(unresolved_a, integer, nullptr);
  ASSERT_TRUE(unresolved_a_int.IsUnresolvedMergedReference());
  const RegType& unresolved_b_int = cache.FromUnresolvedMerge(unresolved_b, integer, nullptr);
  ASSERT_TRUE(unresolved_b_int.IsUnresolvedMergedReference());
  const RegType& unresolved_ab_int = cache.FromUnresolvedMerge(unresolved_ab, integer, nullptr);
  ASSERT_TRUE(unresolved_ab_int.IsUnresolvedMergedReference());
  std::vector<const RegType*> uninitialized_types = {
      &uninit_this, &uninit_obj, &uninit_number, &uninit_integer
  };
  std::vector<const RegType*> unresolved_types = {
      &unresolved_a,
      &unresolved_b,
      &unresolved_ab,
      &unresolved_a_num,
      &unresolved_b_num,
      &unresolved_ab_num,
      &unresolved_a_int,
      &unresolved_b_int,
      &unresolved_ab_int
  };
  std::vector<const RegType*> uninit_unresolved_types = {
      &uninit_unres_this, &uninit_unres_a, &uninit_unres_b
  };
  std::vector<const RegType*> plain_nonobj_classes = { &number, &integer };
  std::vector<const RegType*> plain_nonobj_arr_classes = {
      &number_arr,
      &number_arr_arr,
      &integer_arr,
      &char_arr,
  };
  // std::vector<const RegType*> others = { &conflict, &zero, &null, &obj, &int_type };

  std::vector<const RegType*> all_minus_uninit_conflict;
  all_minus_uninit_conflict.insert(all_minus_uninit_conflict.end(),
                                   unresolved_types.begin(),
                                   unresolved_types.end());
  all_minus_uninit_conflict.insert(all_minus_uninit_conflict.end(),
                                   plain_nonobj_classes.begin(),
                                   plain_nonobj_classes.end());
  all_minus_uninit_conflict.insert(all_minus_uninit_conflict.end(),
                                   plain_nonobj_arr_classes.begin(),
                                   plain_nonobj_arr_classes.end());
  all_minus_uninit_conflict.push_back(&zero);
  all_minus_uninit_conflict.push_back(&null);
  all_minus_uninit_conflict.push_back(&obj);

  std::vector<const RegType*> all_minus_uninit;
  all_minus_uninit.insert(all_minus_uninit.end(),
                          all_minus_uninit_conflict.begin(),
                          all_minus_uninit_conflict.end());
  all_minus_uninit.push_back(&conflict);


  std::vector<const RegType*> all;
  all.insert(all.end(), uninitialized_types.begin(), uninitialized_types.end());
  all.insert(all.end(), uninit_unresolved_types.begin(), uninit_unresolved_types.end());
  all.insert(all.end(), all_minus_uninit.begin(), all_minus_uninit.end());
  all.push_back(&int_type);

  auto check = [&](const RegType& in1, const RegType& in2, const RegType& expected_out)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    const RegType& merge_result = in1.SafeMerge(in2, &cache, nullptr);
    EXPECT_EQ(&expected_out, &merge_result)
        << in1.Dump() << " x " << in2.Dump() << " = " << merge_result.Dump()
        << " != " << expected_out.Dump();
  };

  // Identity.
  {
    for (auto r : all) {
      check(*r, *r, *r);
    }
  }

  // Define a covering relation through a list of Edges. We'll then derive LUBs from this and
  // create checks for every pair of types.

  struct Edge {
    const RegType& from;
    const RegType& to;

    Edge(const RegType& from_, const RegType& to_) : from(from_), to(to_) {}
  };
  std::vector<Edge> edges;
#define ADD_EDGE(from, to) edges.emplace_back((from), (to))

  // To Conflict.
  {
    for (auto r : uninitialized_types) {
      ADD_EDGE(*r, conflict);
    }
    for (auto r : uninit_unresolved_types) {
      ADD_EDGE(*r, conflict);
    }
    ADD_EDGE(obj, conflict);
    ADD_EDGE(int_type, conflict);
  }

  ADD_EDGE(zero, null);

  // Unresolved.
  {
    ADD_EDGE(null, unresolved_a);
    ADD_EDGE(null, unresolved_b);
    ADD_EDGE(unresolved_a, unresolved_ab);
    ADD_EDGE(unresolved_b, unresolved_ab);

    ADD_EDGE(number, unresolved_a_num);
    ADD_EDGE(unresolved_a, unresolved_a_num);
    ADD_EDGE(number, unresolved_b_num);
    ADD_EDGE(unresolved_b, unresolved_b_num);
    ADD_EDGE(number, unresolved_ab_num);
    ADD_EDGE(unresolved_a_num, unresolved_ab_num);
    ADD_EDGE(unresolved_b_num, unresolved_ab_num);
    ADD_EDGE(unresolved_ab, unresolved_ab_num);

    ADD_EDGE(integer, unresolved_a_int);
    ADD_EDGE(unresolved_a, unresolved_a_int);
    ADD_EDGE(integer, unresolved_b_int);
    ADD_EDGE(unresolved_b, unresolved_b_int);
    ADD_EDGE(integer, unresolved_ab_int);
    ADD_EDGE(unresolved_a_int, unresolved_ab_int);
    ADD_EDGE(unresolved_b_int, unresolved_ab_int);
    ADD_EDGE(unresolved_ab, unresolved_ab_int);

    ADD_EDGE(unresolved_a_int, unresolved_a_num);
    ADD_EDGE(unresolved_b_int, unresolved_b_num);
    ADD_EDGE(unresolved_ab_int, unresolved_ab_num);

    ADD_EDGE(unresolved_ab_num, obj);
  }

  // Classes.
  {
    ADD_EDGE(null, integer);
    ADD_EDGE(integer, number);
    ADD_EDGE(number, obj);
  }

  // Arrays.
  {
    ADD_EDGE(integer_arr, number_arr);
    ADD_EDGE(number_arr, obj_arr);
    ADD_EDGE(obj_arr, obj);
    ADD_EDGE(number_arr_arr, obj_arr);

    ADD_EDGE(char_arr, obj);
    ADD_EDGE(byte_arr, obj);

    ADD_EDGE(null, integer_arr);
    ADD_EDGE(null, number_arr_arr);
    ADD_EDGE(null, char_arr);
    ADD_EDGE(null, byte_arr);
  }

  // Primitive.
  {
    ADD_EDGE(zero, int_type);
  }
#undef ADD_EDGE

  // Create merge triples by using the covering relation established by edges to derive the
  // expected merge for any pair of types.

  // Expect merge(in1, in2) == out.
  struct MergeExpectation {
    const RegType& in1;
    const RegType& in2;
    const RegType& out;

    MergeExpectation(const RegType& in1_, const RegType& in2_, const RegType& out_)
        : in1(in1_), in2(in2_), out(out_) {}
  };
  std::vector<MergeExpectation> expectations;

  for (auto r1 : all) {
    for (auto r2 : all) {
      if (r1 == r2) {
        continue;
      }

      // Very simple algorithm here that is usually used with adjacency lists. Our graph is
      // small, it didn't make sense to have lists per node. Thus, the regular guarantees
      // of O(n + |e|) don't apply, but that is acceptable.
      //
      // To compute r1 lub r2 = merge(r1, r2):
      //   1) Generate the reachable set of r1, name it grey.
      //   2) Mark all grey reachable nodes of r2 as black.
      //   3) Find black nodes with no in-edges from other black nodes.
      //   4) If |3)| == 1, that's the lub.

      // Generic BFS of the graph induced by edges, starting at start. new_node will be called
      // with any discovered node, in order.
      auto bfs = [&](auto new_node, const RegType* start) {
        std::unordered_set<const RegType*> seen;
        std::queue<const RegType*> work_list;
        work_list.push(start);
        while (!work_list.empty()) {
          const RegType* cur = work_list.front();
          work_list.pop();
          auto it = seen.find(cur);
          if (it != seen.end()) {
            continue;
          }
          seen.insert(cur);
          new_node(cur);

          for (const Edge& edge : edges) {
            if (&edge.from == cur) {
              work_list.push(&edge.to);
            }
          }
        }
      };

      std::unordered_set<const RegType*> grey;
      auto compute_grey = [&](const RegType* cur) {
        grey.insert(cur);  // Mark discovered node as grey.
      };
      bfs(compute_grey, r1);

      std::set<const RegType*> black;
      auto compute_black = [&](const RegType* cur) {
        // Mark discovered grey node as black.
        if (grey.find(cur) != grey.end()) {
          black.insert(cur);
        }
      };
      bfs(compute_black, r2);

      std::set<const RegType*> no_in_edge(black);  // Copy of black, remove nodes with in-edges.
      for (auto r : black) {
        for (Edge& e : edges) {
          if (&e.from == r) {
            no_in_edge.erase(&e.to);  // It doesn't matter whether "to" is black or not, just
                                      // attempt to remove it.
          }
        }
      }

      // Helper to print sets when something went wrong.
      auto print_set = [](auto& container) REQUIRES_SHARED(Locks::mutator_lock_) {
        std::string result;
        for (auto r : container) {
          result.append(" + ");
          result.append(r->Dump());
        }
        return result;
      };
      ASSERT_EQ(no_in_edge.size(), 1u) << r1->Dump() << " u " << r2->Dump()
                                       << " grey=" << print_set(grey)
                                       << " black=" << print_set(black)
                                       << " no-in-edge=" << print_set(no_in_edge);
      expectations.emplace_back(*r1, *r2, **no_in_edge.begin());
    }
  }

  // Evaluate merge expectations. The merge is expected to be commutative.

  for (auto& triple : expectations) {
    check(triple.in1, triple.in2, triple.out);
    check(triple.in2, triple.in1, triple.out);
  }
}

class RegTypeOOMTest : public RegTypeTest {
 protected:
  void SetUpRuntimeOptions(RuntimeOptions *options) override {
    SetUpRuntimeOptionsForFillHeap(options);

    // We must not appear to be a compiler, or we'll abort on the host.
    callbacks_.reset();
  }
};

TEST_F(RegTypeOOMTest, ClassJoinOOM) {
  // TODO: Figure out why FillHeap isn't good enough under CMS.
  TEST_DISABLED_WITHOUT_BAKER_READ_BARRIERS();

  // Tests that we don't abort with OOMs.

  ArenaPool* arena_pool = Runtime::Current()->GetArenaPool();
  ScopedObjectAccess soa(Thread::Current());

  ScopedDisableMovingGC no_gc(soa.Self());

  // We merge nested array of primitive wrappers. These have a join type of an array of Number of
  // the same depth. We start with depth five, as we want at least two newly created classes to
  // test recursion (it's just more likely that nobody uses such deep arrays in runtime bringup).
  constexpr const char* kIntArrayFive = "[[[[[Ljava/lang/Integer;";
  constexpr const char* kFloatArrayFive = "[[[[[Ljava/lang/Float;";
  constexpr const char* kNumberArrayFour = "[[[[Ljava/lang/Number;";
  constexpr const char* kNumberArrayFive = "[[[[[Ljava/lang/Number;";

  ScopedNullHandle<mirror::ClassLoader> loader;
  RegTypeCache cache(soa.Self(), class_linker_, arena_pool, loader, dex_file_.get());
  const RegType& int_array_array = cache.FromDescriptor(kIntArrayFive);
  ASSERT_TRUE(int_array_array.HasClass());
  const RegType& float_array_array = cache.FromDescriptor(kFloatArrayFive);
  ASSERT_TRUE(float_array_array.HasClass());

  // Check assumptions: the joined classes don't exist, yet.
  ASSERT_TRUE(class_linker_->LookupClass(soa.Self(), kNumberArrayFour, nullptr) == nullptr);
  ASSERT_TRUE(class_linker_->LookupClass(soa.Self(), kNumberArrayFive, nullptr) == nullptr);

  // Fill the heap.
  VariableSizedHandleScope hs(soa.Self());
  FillHeap(soa.Self(), class_linker_, &hs);

  const RegType& join_type = int_array_array.Merge(float_array_array, &cache, nullptr);
  ASSERT_TRUE(join_type.IsUnresolvedReference());
}

class RegTypeClassJoinTest : public RegTypeTest {
 protected:
  void TestClassJoin(const char* in1, const char* in2, const char* out) {
    ArenaPool* arena_pool = Runtime::Current()->GetArenaPool();

    ScopedObjectAccess soa(Thread::Current());
    jobject jclass_loader = LoadDex("Interfaces");
    StackHandleScope<4> hs(soa.Self());
    Handle<mirror::ClassLoader> class_loader(
        hs.NewHandle(soa.Decode<mirror::ClassLoader>(jclass_loader)));

    Handle<mirror::Class> c1 = hs.NewHandle(FindClass(in1, class_loader));
    Handle<mirror::Class> c2 = hs.NewHandle(FindClass(in2, class_loader));
    ASSERT_TRUE(c1 != nullptr);
    ASSERT_TRUE(c2 != nullptr);
    const DexFile* dex_file = &c1->GetDexFile();
    ASSERT_EQ(dex_file, &c2->GetDexFile());

    ScopedDisableMovingGC no_gc(soa.Self());

    RegTypeCache cache(soa.Self(), class_linker_, arena_pool, class_loader, dex_file);
    const RegType& c1_reg_type = cache.FromClass(c1.Get());
    if (!c1_reg_type.IsJavaLangObject()) {
      ASSERT_TRUE(c1_reg_type.HasClass());
      ASSERT_TRUE(c1_reg_type.GetClass() == c1.Get());
    }
    const RegType& c2_reg_type = cache.FromClass(c2.Get());
    if (!c2_reg_type.IsJavaLangObject()) {
      ASSERT_TRUE(c2_reg_type.HasClass());
      ASSERT_TRUE(c2_reg_type.GetClass() == c2.Get());
    }

    const RegType& join_type = c1_reg_type.Merge(c2_reg_type, &cache, nullptr);
    EXPECT_TRUE(join_type.IsJavaLangObject() || join_type.HasClass());
    EXPECT_EQ(join_type.GetDescriptor(), std::string_view(out));
  }
};

TEST_F(RegTypeClassJoinTest, ClassJoinInterfaces) {
  TestClassJoin("LInterfaces$K;", "LInterfaces$L;", "LInterfaces$J;");
}

TEST_F(RegTypeClassJoinTest, ClassJoinInterfaceClass) {
  TestClassJoin("LInterfaces$B;", "LInterfaces$L;", "LInterfaces$J;");
}

TEST_F(RegTypeClassJoinTest, ClassJoinClassClass) {
  // This test codifies that we prefer the class hierarchy over interfaces. It's a mostly
  // arbitrary choice, optimally we'd have set types and could handle multi-inheritance precisely.
  TestClassJoin("LInterfaces$A;", "LInterfaces$B;", "Ljava/lang/Object;");
}

TEST_F(RegTypeClassJoinTest, LookupByTypeIndex) {
  ArenaPool* arena_pool = Runtime::Current()->GetArenaPool();
  ScopedObjectAccess soa(Thread::Current());
  ScopedNullHandle<mirror::ClassLoader> loader;
  RegTypeCache cache(soa.Self(), class_linker_, arena_pool, loader, dex_file_.get());

  auto get_type_index = [&](std::string_view descriptor) {
    const dex::TypeId* type_id = dex_file_->FindTypeId(descriptor);
    CHECK(type_id != nullptr);
    return dex_file_->GetIndexForTypeId(*type_id);
  };

  ASSERT_EQ(&cache.Boolean(), &cache.FromTypeIndex(get_type_index("Z")));
  ASSERT_EQ(&cache.Byte(), &cache.FromTypeIndex(get_type_index("B")));
  ASSERT_EQ(&cache.Char(), &cache.FromTypeIndex(get_type_index("C")));
  ASSERT_EQ(&cache.Short(), &cache.FromTypeIndex(get_type_index("S")));
  ASSERT_EQ(&cache.Integer(), &cache.FromTypeIndex(get_type_index("I")));
  ASSERT_EQ(&cache.LongLo(), &cache.FromTypeIndex(get_type_index("J")));
  ASSERT_EQ(&cache.Float(), &cache.FromTypeIndex(get_type_index("F")));
  ASSERT_EQ(&cache.DoubleLo(), &cache.FromTypeIndex(get_type_index("D")));
  ASSERT_EQ(&cache.Conflict(), &cache.FromTypeIndex(get_type_index("V")));
}

}  // namespace verifier
}  // namespace art
