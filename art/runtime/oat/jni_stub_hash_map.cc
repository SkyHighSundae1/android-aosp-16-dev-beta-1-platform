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

#include "jni_stub_hash_map.h"

#include "arch/arm64/jni_frame_arm64.h"
#include "arch/instruction_set.h"
#include "arch/riscv64/jni_frame_riscv64.h"
#include "arch/x86_64/jni_frame_x86_64.h"
#include "base/macros.h"
#include "dex/modifiers.h"

namespace art HIDDEN {

static char TranslateArgToJniShorty(char ch) {
  // Byte, char, int, short, boolean are treated the same(e.g., Wx registers for arm64) when
  // generating JNI stub, so their JNI shorty characters are same.
  //                                       ABCDEFGHIJKLMNOPQRSTUVWXYZ
  static constexpr char kTranslations[] = ".PPD.F..PJ.L......P......P";
  DCHECK_GE(ch, 'A');
  DCHECK_LE(ch, 'Z');
  DCHECK_NE(kTranslations[ch - 'A'], '.');
  return kTranslations[ch - 'A'];
}

static char TranslateReturnTypeToJniShorty(char ch, InstructionSet isa = InstructionSet::kNone) {
  // For all archs, reference type has a different JNI shorty character than others as it needs to
  // be decoded in stub.
  // For arm64, small return types need sign-/zero-extended.
  // For x86_64, small return types need sign-/zero-extended, and RAX needs to be preserved and
  // restored when thread state changes.
  // Other archs keeps untranslated for simplicity.
  // TODO: support riscv64 with an optimized version.
  //                                             ABCDEFGHIJKLMNOPQRSTUVWXYZ
  static constexpr char kArm64Translations[] =  ".BCP.P..PP.L......S..P...Z";
  static constexpr char kX86_64Translations[] = ".BCP.P..RR.L......S..P...Z";
  static constexpr char kOtherTranslations[] =  ".BCD.F..IJ.L......S..V...Z";
  DCHECK_GE(ch, 'A');
  DCHECK_LE(ch, 'Z');
  switch (isa) {
    case InstructionSet::kArm64:
      DCHECK_NE(kArm64Translations[ch - 'A'], '.');
      return kArm64Translations[ch - 'A'];
    case InstructionSet::kX86_64:
      DCHECK_NE(kX86_64Translations[ch - 'A'], '.');
      return kX86_64Translations[ch - 'A'];
    default:
      DCHECK_NE(kOtherTranslations[ch - 'A'], '.');
      return kOtherTranslations[ch - 'A'];
  }
}

static constexpr size_t GetMaxIntLikeRegisterArgs(InstructionSet isa) {
  switch (isa) {
    case InstructionSet::kArm64:
      return arm64::kMaxIntLikeRegisterArguments;
    case InstructionSet::kX86_64:
      return x86_64::kMaxIntLikeRegisterArguments;
    default:
      LOG(FATAL) << "Unrecognized isa: " << isa << " for " << __FUNCTION__;
      UNREACHABLE();
  }
}

static constexpr size_t GetMaxFloatOrDoubleRegisterArgs(InstructionSet isa) {
  switch (isa) {
    case InstructionSet::kArm64:
      return arm64::kMaxFloatOrDoubleRegisterArguments;
    case InstructionSet::kX86_64:
      return x86_64::kMaxFloatOrDoubleRegisterArguments;
    default:
      LOG(FATAL) << "Unrecognized isa: " << isa << " for " << __FUNCTION__;
      UNREACHABLE();
  }
}

static size_t StackOffset(char ch) {
  if (ch == 'J' || ch == 'D') {
    return 8;
  } else {
    return 4;
  }
}

static bool IsFloatOrDoubleArg(char ch) {
  return ch == 'F' || ch == 'D';
}

static bool IsIntegralArg(char ch) {
  return ch == 'B' || ch == 'C' || ch == 'I' || ch == 'J' || ch == 'S' || ch == 'Z';
}

static bool IsReferenceArg(char ch) {
  return ch == 'L';
}

template<InstructionSet kIsa>
size_t JniStubKeyOptimizedHash(const JniStubKey& key) {
  bool is_static = key.Flags() & kAccStatic;
  std::string_view shorty = key.Shorty();
  size_t result = key.Flags();
  result ^= TranslateReturnTypeToJniShorty(shorty[0], kIsa);
  constexpr size_t kMaxFloatOrDoubleRegisterArgs = GetMaxFloatOrDoubleRegisterArgs(kIsa);
  constexpr size_t kMaxIntLikeRegisterArgs = GetMaxIntLikeRegisterArgs(kIsa);
  size_t float_or_double_args = 0;
  // ArtMethod* and 'Object* this' for non-static method.
  // ArtMethod* for static method.
  size_t int_like_args = is_static ? 1 : 2;
  size_t stack_offset = 0;
  for (char c : shorty.substr(1u)) {
    bool stack_offset_matters = false;
    stack_offset += StackOffset(c);
    if (IsFloatOrDoubleArg(c)) {
      ++float_or_double_args;
      if (float_or_double_args > kMaxFloatOrDoubleRegisterArgs) {
        // Stack offset matters if we run out of float-like (float, double) argument registers
        // because the subsequent float-like args should be passed on the stack.
        stack_offset_matters = true;
      } else {
        // Floating-point register arguments are not touched when generating JNI stub, so could be
        // ignored when calculating hash value.
        continue;
      }
    } else {
      ++int_like_args;
      if (int_like_args > kMaxIntLikeRegisterArgs || IsReferenceArg(c)) {
        // Stack offset matters if we run out of integer-like (pointer, object, long, int, short,
        // bool, etc) argument registers because the subsequent integer-like args should be passed
        // on the stack. It also matters if current arg is reference type because it needs to be
        // spilled as raw data even if it's in a register.
        stack_offset_matters = true;
      } else if (!is_static) {
        // For non-static method, two managed arguments 'ArtMethod*' and 'Object* this' correspond
        // to two native arguments 'JNIEnv*' and 'jobject'. So trailing integral (long, int, short,
        // bool, etc) arguments will remain in the same registers, which do not need any generated
        // code.
        // But for static method, we have only one leading managed argument 'ArtMethod*' but two
        // native arguments 'JNIEnv*' and 'jclass'. So trailing integral arguments are always
        // shuffled around and affect the generated code.
        continue;
      }
    }
    // int_like_args is needed for reference type because it will determine from which register
    // we take the value to construct jobject.
    if (IsReferenceArg(c)) {
      result = result * 31u * int_like_args ^ TranslateArgToJniShorty(c);
    } else {
      result = result * 31u ^ TranslateArgToJniShorty(c);
    }
    if (stack_offset_matters) {
      result += stack_offset;
    }
  }
  return result;
}

size_t JniStubKeyGenericHash(const JniStubKey& key) {
  std::string_view shorty = key.Shorty();
  size_t result = key.Flags();
  result ^= TranslateReturnTypeToJniShorty(shorty[0]);
  for (char c : shorty.substr(1u)) {
    result = result * 31u ^ TranslateArgToJniShorty(c);
  }
  return result;
}

JniStubKeyHash::JniStubKeyHash(InstructionSet isa) {
  switch (isa) {
    case InstructionSet::kArm:
    case InstructionSet::kThumb2:
    case InstructionSet::kRiscv64:
    case InstructionSet::kX86:
      hash_func_ = JniStubKeyGenericHash;
      break;
    case InstructionSet::kArm64:
      hash_func_ = JniStubKeyOptimizedHash<InstructionSet::kArm64>;
      break;
    case InstructionSet::kX86_64:
      hash_func_ = JniStubKeyOptimizedHash<InstructionSet::kX86_64>;
      break;
    case InstructionSet::kNone:
      LOG(FATAL) << "No instruction set given for " << __FUNCTION__;
      UNREACHABLE();
  }
}

template<InstructionSet kIsa>
bool JniStubKeyOptimizedEquals(const JniStubKey& lhs, const JniStubKey& rhs) {
  if (lhs.Flags() != rhs.Flags()) {
    return false;
  }
  std::string_view shorty_lhs = lhs.Shorty();
  std::string_view shorty_rhs = rhs.Shorty();
  if (TranslateReturnTypeToJniShorty(shorty_lhs[0], kIsa) !=
      TranslateReturnTypeToJniShorty(shorty_rhs[0], kIsa)) {
    return false;
  }
  bool is_static = lhs.Flags() & kAccStatic;
  constexpr size_t kMaxFloatOrDoubleRegisterArgs = GetMaxFloatOrDoubleRegisterArgs(kIsa);
  constexpr size_t kMaxIntLikeRegisterArgs = GetMaxIntLikeRegisterArgs(kIsa);
  size_t float_or_double_args_lhs = 0;
  size_t float_or_double_args_rhs = 0;
  size_t int_like_args_lhs = is_static ? 1 : 2;
  size_t int_like_args_rhs = is_static ? 1 : 2;
  size_t stack_offset_lhs = 0;
  size_t stack_offset_rhs = 0;
  size_t i = 1;
  size_t j = 1;
  while (i < shorty_lhs.length() && j < shorty_rhs.length()) {
    bool should_skip = false;
    bool stack_offset_matters = false;
    char ch_lhs = shorty_lhs[i];
    char ch_rhs = shorty_rhs[j];

    if (IsFloatOrDoubleArg(ch_lhs) &&
        float_or_double_args_lhs < kMaxFloatOrDoubleRegisterArgs) {
      // Skip float-like register arguments.
      ++i;
      ++float_or_double_args_lhs;
      stack_offset_lhs += StackOffset(ch_lhs);
      should_skip = true;
    } else if (IsIntegralArg(ch_lhs) &&
        int_like_args_lhs < kMaxIntLikeRegisterArgs) {
      if (!is_static) {
        // Skip integral register arguments for non-static method.
        ++i;
        ++int_like_args_lhs;
        stack_offset_lhs += StackOffset(ch_lhs);
        should_skip = true;
      }
    } else {
      stack_offset_matters = true;
    }

    if (IsFloatOrDoubleArg(ch_rhs) &&
        float_or_double_args_rhs < kMaxFloatOrDoubleRegisterArgs) {
      // Skip float-like register arguments.
      ++j;
      ++float_or_double_args_rhs;
      stack_offset_rhs += StackOffset(ch_rhs);
      should_skip = true;
    } else if (IsIntegralArg(ch_rhs) &&
        int_like_args_rhs < kMaxIntLikeRegisterArgs) {
      if (!is_static) {
        // Skip integral register arguments for non-static method.
        ++j;
        ++int_like_args_rhs;
        stack_offset_rhs += StackOffset(ch_rhs);
        should_skip = true;
      }
    } else {
      stack_offset_matters = true;
    }

    if (should_skip) {
      continue;
    }
    if (TranslateArgToJniShorty(ch_lhs) != TranslateArgToJniShorty(ch_rhs)) {
      return false;
    }
    if (stack_offset_matters && stack_offset_lhs != stack_offset_rhs) {
      return false;
    }
    // int_like_args needs to be compared for reference type because it will determine from
    // which register we take the value to construct jobject.
    if (IsReferenceArg(ch_lhs) && int_like_args_lhs != int_like_args_rhs) {
      return false;
    }
    // Passed character comparison.
    ++i;
    ++j;
    stack_offset_lhs += StackOffset(ch_lhs);
    stack_offset_rhs += StackOffset(ch_rhs);
    DCHECK_EQ(IsFloatOrDoubleArg(ch_lhs), IsFloatOrDoubleArg(ch_rhs));
    if (IsFloatOrDoubleArg(ch_lhs)) {
      ++float_or_double_args_lhs;
      ++float_or_double_args_rhs;
    } else {
      ++int_like_args_lhs;
      ++int_like_args_rhs;
    }
  }
  auto remaining_shorty =
      i < shorty_lhs.length() ? shorty_lhs.substr(i) : shorty_rhs.substr(j);
  size_t float_or_double_args =
      i < shorty_lhs.length() ? float_or_double_args_lhs : float_or_double_args_rhs;
  size_t int_like_args = i < shorty_lhs.length() ? int_like_args_lhs : int_like_args_rhs;
  for (char c : remaining_shorty) {
    if (IsFloatOrDoubleArg(c) && float_or_double_args < kMaxFloatOrDoubleRegisterArgs) {
      ++float_or_double_args;
      continue;
    }
    if (!is_static && IsIntegralArg(c) && int_like_args < kMaxIntLikeRegisterArgs) {
      ++int_like_args;
      continue;
    }
    return false;
  }
  return true;
}

bool JniStubKeyGenericEquals(const JniStubKey& lhs, const JniStubKey& rhs) {
  if (lhs.Flags() != rhs.Flags()) {
    return false;
  }
  std::string_view shorty_lhs = lhs.Shorty();
  std::string_view shorty_rhs = rhs.Shorty();
  if (TranslateReturnTypeToJniShorty(shorty_lhs[0]) !=
      TranslateReturnTypeToJniShorty(shorty_rhs[0])) {
    return false;
  }
  if (shorty_lhs.length() != shorty_rhs.length()) {
    return false;
  }
  for (size_t i = 1; i < shorty_lhs.length(); ++i) {
    if (TranslateArgToJniShorty(shorty_lhs[i]) != TranslateArgToJniShorty(shorty_rhs[i])) {
      return false;
    }
  }
  return true;
}

JniStubKeyEquals::JniStubKeyEquals(InstructionSet isa) {
  switch (isa) {
    case InstructionSet::kArm:
    case InstructionSet::kThumb2:
    case InstructionSet::kRiscv64:
    case InstructionSet::kX86:
      equals_func_ = JniStubKeyGenericEquals;
      break;
    case InstructionSet::kArm64:
      equals_func_ = JniStubKeyOptimizedEquals<InstructionSet::kArm64>;
      break;
    case InstructionSet::kX86_64:
      equals_func_ = JniStubKeyOptimizedEquals<InstructionSet::kX86_64>;
      break;
    case InstructionSet::kNone:
      LOG(FATAL) << "No instruction set given for " << __FUNCTION__;
      UNREACHABLE();
  }
}

}  // namespace art
