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

#include "instruction_set_features_x86.h"

#include <gtest/gtest.h>

namespace art HIDDEN {

TEST(X86InstructionSetFeaturesTest, X86FeaturesFromDefaultVariant) {
  const bool is_runtime_isa = kRuntimeISA == InstructionSet::kX86;
  std::string error_msg;
  std::unique_ptr<const InstructionSetFeatures> x86_features(
      InstructionSetFeatures::FromVariant(InstructionSet::kX86, "default", &error_msg));
  ASSERT_TRUE(x86_features.get() != nullptr) << error_msg;
  EXPECT_EQ(x86_features->GetInstructionSet(), InstructionSet::kX86);
  EXPECT_TRUE(x86_features->Equals(x86_features.get()));
  EXPECT_EQ(x86_features->GetFeatureString(),
            is_runtime_isa ? X86InstructionSetFeatures::FromCppDefines()->GetFeatureString()
                    : "-ssse3,-sse4.1,-sse4.2,-avx,-avx2,-popcnt");
  EXPECT_EQ(x86_features->AsBitmap(),
            is_runtime_isa ? X86InstructionSetFeatures::FromCppDefines()->AsBitmap() : 0);
}

TEST(X86InstructionSetFeaturesTest, X86FeaturesFromAtomVariant) {
  // Build features for a 32-bit x86 atom processor.
  std::string error_msg;
  std::unique_ptr<const InstructionSetFeatures> x86_features(
      InstructionSetFeatures::FromVariant(InstructionSet::kX86, "atom", &error_msg));
  ASSERT_TRUE(x86_features.get() != nullptr) << error_msg;
  EXPECT_EQ(x86_features->GetInstructionSet(), InstructionSet::kX86);
  EXPECT_TRUE(x86_features->Equals(x86_features.get()));
  EXPECT_STREQ("ssse3,-sse4.1,-sse4.2,-avx,-avx2,-popcnt",
               x86_features->GetFeatureString().c_str());
  EXPECT_EQ(x86_features->AsBitmap(), 1U);

  // Build features for a 64-bit x86-64 atom processor.
  std::unique_ptr<const InstructionSetFeatures> x86_64_features(
      InstructionSetFeatures::FromVariant(InstructionSet::kX86_64, "atom", &error_msg));
  ASSERT_TRUE(x86_64_features.get() != nullptr) << error_msg;
  EXPECT_EQ(x86_64_features->GetInstructionSet(), InstructionSet::kX86_64);
  EXPECT_TRUE(x86_64_features->Equals(x86_64_features.get()));
  EXPECT_STREQ("ssse3,-sse4.1,-sse4.2,-avx,-avx2,-popcnt",
               x86_64_features->GetFeatureString().c_str());
  EXPECT_EQ(x86_64_features->AsBitmap(), 1U);

  EXPECT_FALSE(x86_64_features->Equals(x86_features.get()));
}

TEST(X86InstructionSetFeaturesTest, X86FeaturesFromSandybridgeVariant) {
  // Build features for a 32-bit x86 sandybridge processor.
  std::string error_msg;
  std::unique_ptr<const InstructionSetFeatures> x86_features(
      InstructionSetFeatures::FromVariant(InstructionSet::kX86, "sandybridge", &error_msg));
  ASSERT_TRUE(x86_features.get() != nullptr) << error_msg;
  EXPECT_EQ(x86_features->GetInstructionSet(), InstructionSet::kX86);
  EXPECT_TRUE(x86_features->Equals(x86_features.get()));
  EXPECT_STREQ("ssse3,sse4.1,sse4.2,-avx,-avx2,popcnt",
               x86_features->GetFeatureString().c_str());
  EXPECT_EQ(x86_features->AsBitmap(), 39U);

  // Build features for a 64-bit x86-64 sandybridge processor.
  std::unique_ptr<const InstructionSetFeatures> x86_64_features(
      InstructionSetFeatures::FromVariant(InstructionSet::kX86_64, "sandybridge", &error_msg));
  ASSERT_TRUE(x86_64_features.get() != nullptr) << error_msg;
  EXPECT_EQ(x86_64_features->GetInstructionSet(), InstructionSet::kX86_64);
  EXPECT_TRUE(x86_64_features->Equals(x86_64_features.get()));
  EXPECT_STREQ("ssse3,sse4.1,sse4.2,-avx,-avx2,popcnt",
               x86_64_features->GetFeatureString().c_str());
  EXPECT_EQ(x86_64_features->AsBitmap(), 39U);

  EXPECT_FALSE(x86_64_features->Equals(x86_features.get()));
}

TEST(X86InstructionSetFeaturesTest, X86FeaturesFromSilvermontVariant) {
  // Build features for a 32-bit x86 silvermont processor.
  std::string error_msg;
  std::unique_ptr<const InstructionSetFeatures> x86_features(
      InstructionSetFeatures::FromVariant(InstructionSet::kX86, "silvermont", &error_msg));
  ASSERT_TRUE(x86_features.get() != nullptr) << error_msg;
  EXPECT_EQ(x86_features->GetInstructionSet(), InstructionSet::kX86);
  EXPECT_TRUE(x86_features->Equals(x86_features.get()));
  EXPECT_STREQ("ssse3,sse4.1,sse4.2,-avx,-avx2,popcnt",
               x86_features->GetFeatureString().c_str());
  EXPECT_EQ(x86_features->AsBitmap(), 39U);

  // Build features for a 64-bit x86-64 silvermont processor.
  std::unique_ptr<const InstructionSetFeatures> x86_64_features(
      InstructionSetFeatures::FromVariant(InstructionSet::kX86_64, "silvermont", &error_msg));
  ASSERT_TRUE(x86_64_features.get() != nullptr) << error_msg;
  EXPECT_EQ(x86_64_features->GetInstructionSet(), InstructionSet::kX86_64);
  EXPECT_TRUE(x86_64_features->Equals(x86_64_features.get()));
  EXPECT_STREQ("ssse3,sse4.1,sse4.2,-avx,-avx2,popcnt",
               x86_64_features->GetFeatureString().c_str());
  EXPECT_EQ(x86_64_features->AsBitmap(), 39U);

  EXPECT_FALSE(x86_64_features->Equals(x86_features.get()));
}

TEST(X86InstructionSetFeaturesTest, X86FeaturesFromGoldmontVariant) {
  // Build features for a 32-bit x86 goldmont processor.
  std::string error_msg;
  std::unique_ptr<const InstructionSetFeatures> x86_features(
      InstructionSetFeatures::FromVariant(InstructionSet::kX86, "goldmont", &error_msg));
  ASSERT_TRUE(x86_features.get() != nullptr) << error_msg;
  EXPECT_EQ(x86_features->GetInstructionSet(), InstructionSet::kX86);
  EXPECT_TRUE(x86_features->Equals(x86_features.get()));
  EXPECT_STREQ("ssse3,sse4.1,sse4.2,-avx,-avx2,popcnt",
               x86_features->GetFeatureString().c_str());
  EXPECT_EQ(x86_features->AsBitmap(), 39U);

  // Build features for a 64-bit x86-64 goldmont processor.
  std::unique_ptr<const InstructionSetFeatures> x86_64_features(
      InstructionSetFeatures::FromVariant(InstructionSet::kX86_64, "goldmont", &error_msg));
  ASSERT_TRUE(x86_64_features.get() != nullptr) << error_msg;
  EXPECT_EQ(x86_64_features->GetInstructionSet(), InstructionSet::kX86_64);
  EXPECT_TRUE(x86_64_features->Equals(x86_64_features.get()));
  EXPECT_STREQ("ssse3,sse4.1,sse4.2,-avx,-avx2,popcnt",
               x86_64_features->GetFeatureString().c_str());
  EXPECT_EQ(x86_64_features->AsBitmap(), 39U);

  EXPECT_FALSE(x86_64_features->Equals(x86_features.get()));
}

TEST(X86InstructionSetFeaturesTest, X86FeaturesFromGoldmontPlusVariant) {
  // Build features for a 32-bit x86 goldmont-plus processor.
  std::string error_msg;
  std::unique_ptr<const InstructionSetFeatures> x86_features(
      InstructionSetFeatures::FromVariant(InstructionSet::kX86, "goldmont-plus", &error_msg));
  ASSERT_TRUE(x86_features.get() != nullptr) << error_msg;
  EXPECT_EQ(x86_features->GetInstructionSet(), InstructionSet::kX86);
  EXPECT_TRUE(x86_features->Equals(x86_features.get()));
  EXPECT_STREQ("ssse3,sse4.1,sse4.2,-avx,-avx2,popcnt",
               x86_features->GetFeatureString().c_str());
  EXPECT_EQ(x86_features->AsBitmap(), 39U);

  // Build features for a 64-bit x86-64 goldmont-plus processor.
  std::unique_ptr<const InstructionSetFeatures> x86_64_features(
      InstructionSetFeatures::FromVariant(InstructionSet::kX86_64, "goldmont-plus", &error_msg));
  ASSERT_TRUE(x86_64_features.get() != nullptr) << error_msg;
  EXPECT_EQ(x86_64_features->GetInstructionSet(), InstructionSet::kX86_64);
  EXPECT_TRUE(x86_64_features->Equals(x86_64_features.get()));
  EXPECT_STREQ("ssse3,sse4.1,sse4.2,-avx,-avx2,popcnt",
               x86_64_features->GetFeatureString().c_str());
  EXPECT_EQ(x86_64_features->AsBitmap(), 39U);

  EXPECT_FALSE(x86_64_features->Equals(x86_features.get()));
}

TEST(X86InstructionSetFeaturesTest, X86FeaturesFromTremontVariant) {
  // Build features for a 32-bit x86 tremont processor.
  std::string error_msg;
  std::unique_ptr<const InstructionSetFeatures> x86_features(
      InstructionSetFeatures::FromVariant(InstructionSet::kX86, "tremont", &error_msg));
  ASSERT_TRUE(x86_features.get() != nullptr) << error_msg;
  EXPECT_EQ(x86_features->GetInstructionSet(), InstructionSet::kX86);
  EXPECT_TRUE(x86_features->Equals(x86_features.get()));
  EXPECT_STREQ("ssse3,sse4.1,sse4.2,-avx,-avx2,popcnt",
               x86_features->GetFeatureString().c_str());
  EXPECT_EQ(x86_features->AsBitmap(), 39U);

  // Build features for a 64-bit x86-64 tremont processor.
  std::unique_ptr<const InstructionSetFeatures> x86_64_features(
      InstructionSetFeatures::FromVariant(InstructionSet::kX86_64, "tremont", &error_msg));
  ASSERT_TRUE(x86_64_features.get() != nullptr) << error_msg;
  EXPECT_EQ(x86_64_features->GetInstructionSet(), InstructionSet::kX86_64);
  EXPECT_TRUE(x86_64_features->Equals(x86_64_features.get()));
  EXPECT_STREQ("ssse3,sse4.1,sse4.2,-avx,-avx2,popcnt",
               x86_64_features->GetFeatureString().c_str());
  EXPECT_EQ(x86_64_features->AsBitmap(), 39U);

  EXPECT_FALSE(x86_64_features->Equals(x86_features.get()));
}

TEST(X86InstructionSetFeaturesTest, X86FeaturesFromKabylakeVariant) {
  // Build features for a 32-bit kabylake x86 processor.
  std::string error_msg;
  std::unique_ptr<const InstructionSetFeatures> x86_features(
      InstructionSetFeatures::FromVariant(InstructionSet::kX86, "kabylake", &error_msg));
  ASSERT_TRUE(x86_features.get() != nullptr) << error_msg;
  EXPECT_EQ(x86_features->GetInstructionSet(), InstructionSet::kX86);
  EXPECT_TRUE(x86_features->Equals(x86_features.get()));
  EXPECT_STREQ("ssse3,sse4.1,sse4.2,avx,avx2,popcnt",
               x86_features->GetFeatureString().c_str());
  EXPECT_EQ(x86_features->AsBitmap(), 63U);

  // Build features for a 64-bit x86-64 kabylake processor.
  std::unique_ptr<const InstructionSetFeatures> x86_64_features(
      InstructionSetFeatures::FromVariant(InstructionSet::kX86_64, "kabylake", &error_msg));
  ASSERT_TRUE(x86_64_features.get() != nullptr) << error_msg;
  EXPECT_EQ(x86_64_features->GetInstructionSet(), InstructionSet::kX86_64);
  EXPECT_TRUE(x86_64_features->Equals(x86_64_features.get()));
  EXPECT_STREQ("ssse3,sse4.1,sse4.2,avx,avx2,popcnt",
               x86_64_features->GetFeatureString().c_str());
  EXPECT_EQ(x86_64_features->AsBitmap(), 63U);

  EXPECT_FALSE(x86_64_features->Equals(x86_features.get()));
}

TEST(X86InstructionSetFeaturesTest, X86FeaturesFromAlderlakeVariant) {
  // Build features for a 32-bit alderlake x86 processor.
  std::string error_msg;
  std::unique_ptr<const InstructionSetFeatures> x86_features(
      InstructionSetFeatures::FromVariant(InstructionSet::kX86, "alderlake", &error_msg));
  ASSERT_TRUE(x86_features.get() != nullptr) << error_msg;
  EXPECT_EQ(x86_features->GetInstructionSet(), InstructionSet::kX86);
  EXPECT_TRUE(x86_features->Equals(x86_features.get()));
  EXPECT_STREQ("ssse3,sse4.1,sse4.2,avx,avx2,popcnt", x86_features->GetFeatureString().c_str());
  EXPECT_EQ(x86_features->AsBitmap(), 63U);

  // Build features for a 64-bit x86-64 alderlake processor.
  std::unique_ptr<const InstructionSetFeatures> x86_64_features(
      InstructionSetFeatures::FromVariant(InstructionSet::kX86_64, "alderlake", &error_msg));
  ASSERT_TRUE(x86_64_features.get() != nullptr) << error_msg;
  EXPECT_EQ(x86_64_features->GetInstructionSet(), InstructionSet::kX86_64);
  EXPECT_TRUE(x86_64_features->Equals(x86_64_features.get()));
  EXPECT_STREQ("ssse3,sse4.1,sse4.2,avx,avx2,popcnt", x86_64_features->GetFeatureString().c_str());
  EXPECT_EQ(x86_64_features->AsBitmap(), 63U);

  EXPECT_FALSE(x86_64_features->Equals(x86_features.get()));
}
}  // namespace art
