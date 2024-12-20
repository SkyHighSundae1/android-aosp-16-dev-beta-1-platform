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

#include <stdint.h>

#include "context_arm64.h"

#include "base/bit_utils.h"
#include "base/bit_utils_iterator.h"
#include "quick/quick_method_frame_info.h"
#include "thread-current-inl.h"


#if defined(__aarch64__) && defined(__BIONIC__)
#include <bionic/malloc.h>
#endif

extern "C" __attribute__((weak)) void __hwasan_handle_longjmp(const void* sp_dst);

namespace art HIDDEN {
namespace arm64 {

static constexpr uint64_t gZero = 0;

void Arm64Context::Reset() {
  std::fill_n(gprs_, arraysize(gprs_), nullptr);
  std::fill_n(fprs_, arraysize(fprs_), nullptr);
  gprs_[SP] = &sp_;
  gprs_[kPC] = &pc_;
  gprs_[X0] = &arg0_;
  // Initialize registers with easy to spot debug values.
  sp_ = kBadGprBase + SP;
  pc_ = kBadGprBase + kPC;
  arg0_ = 0;
}

void Arm64Context::FillCalleeSaves(uint8_t* frame, const QuickMethodFrameInfo& frame_info) {
  const size_t frame_size = frame_info.FrameSizeInBytes();
  int spill_pos = 0;

  // Core registers come first, from the highest down to the lowest.
  for (uint32_t core_reg : HighToLowBits(frame_info.CoreSpillMask())) {
    gprs_[core_reg] = CalleeSaveAddress<InstructionSet::kArm64>(frame, spill_pos, frame_size);
    ++spill_pos;
  }
  DCHECK_EQ(spill_pos, POPCOUNT(frame_info.CoreSpillMask()));

  // FP registers come second, from the highest down to the lowest.
  for (uint32_t fp_reg : HighToLowBits(frame_info.FpSpillMask())) {
    fprs_[fp_reg] = CalleeSaveAddress<InstructionSet::kArm64>(frame, spill_pos, frame_size);
    ++spill_pos;
  }
  DCHECK_EQ(spill_pos, POPCOUNT(frame_info.CoreSpillMask()) + POPCOUNT(frame_info.FpSpillMask()));
}

void Arm64Context::SetGPR(uint32_t reg, uintptr_t value) {
  DCHECK_LT(reg, arraysize(gprs_));
  // Note: we use kPC == XZR, so do not ensure that reg != XZR.
  DCHECK(IsAccessibleGPR(reg));
  DCHECK_NE(gprs_[reg], &gZero);  // Can't overwrite this static value since they are never reset.
  *gprs_[reg] = value;
}

void Arm64Context::SetFPR(uint32_t reg, uintptr_t value) {
  DCHECK_LT(reg, static_cast<uint32_t>(kNumberOfDRegisters));
  DCHECK(IsAccessibleFPR(reg));
  DCHECK_NE(fprs_[reg], &gZero);  // Can't overwrite this static value since they are never reset.
  *fprs_[reg] = value;
}

void Arm64Context::SmashCallerSaves() {
  // This needs to be 0 because we want a null/zero return value.
  gprs_[X0] = const_cast<uint64_t*>(&gZero);
  gprs_[X1] = nullptr;
  gprs_[X2] = nullptr;
  gprs_[X3] = nullptr;
  gprs_[X4] = nullptr;
  gprs_[X5] = nullptr;
  gprs_[X6] = nullptr;
  gprs_[X7] = nullptr;
  gprs_[X8] = nullptr;
  gprs_[X9] = nullptr;
  gprs_[X10] = nullptr;
  gprs_[X11] = nullptr;
  gprs_[X12] = nullptr;
  gprs_[X13] = nullptr;
  gprs_[X14] = nullptr;
  gprs_[X15] = nullptr;
  gprs_[X18] = nullptr;

  // d0-d7, d16-d31 are caller-saved; d8-d15 are callee-saved.

  fprs_[D0] = nullptr;
  fprs_[D1] = nullptr;
  fprs_[D2] = nullptr;
  fprs_[D3] = nullptr;
  fprs_[D4] = nullptr;
  fprs_[D5] = nullptr;
  fprs_[D6] = nullptr;
  fprs_[D7] = nullptr;

  fprs_[D16] = nullptr;
  fprs_[D17] = nullptr;
  fprs_[D18] = nullptr;
  fprs_[D19] = nullptr;
  fprs_[D20] = nullptr;
  fprs_[D21] = nullptr;
  fprs_[D22] = nullptr;
  fprs_[D23] = nullptr;
  fprs_[D24] = nullptr;
  fprs_[D25] = nullptr;
  fprs_[D26] = nullptr;
  fprs_[D27] = nullptr;
  fprs_[D28] = nullptr;
  fprs_[D29] = nullptr;
  fprs_[D30] = nullptr;
  fprs_[D31] = nullptr;
}

#if defined(__aarch64__) && defined(__BIONIC__) && defined(M_MEMTAG_STACK_IS_ON)
static inline __attribute__((no_sanitize("memtag"))) void untag_memory(void* from, void* to) {
  __asm__ __volatile__(
      ".arch_extension mte\n"
      "1:\n"
      "stg %[Ptr], [%[Ptr]], #16\n"
      "cmp %[Ptr], %[End]\n"
      "b.lt 1b\n"
      : [Ptr] "+&r"(from)
      : [End] "r"(to)
      : "memory");
}
#endif

void Arm64Context::CopyContextTo(uintptr_t* gprs, uintptr_t* fprs) {
  // The long jump routine called below expects to find the value for SP at index 31.
  DCHECK_EQ(SP, 31);

  for (size_t i = 0; i < arraysize(gprs_); ++i) {
    gprs[i] = gprs_[i] != nullptr ? *gprs_[i] : kBadGprBase + i;
  }
  for (size_t i = 0; i < kNumberOfDRegisters; ++i) {
    fprs[i] = fprs_[i] != nullptr ? *fprs_[i] : kBadFprBase + i;
  }
  // Ensure the Thread Register contains the address of the current thread.
  DCHECK_EQ(reinterpret_cast<uintptr_t>(Thread::Current()), gprs[TR]);
#if defined(__aarch64__) && defined(__BIONIC__) && defined(M_MEMTAG_STACK_IS_ON)
  bool memtag_stack;
  // This works fine because versions of Android that did not support M_MEMTAG_STACK_ON did not
  // support stack tagging either.
  if (android_mallopt(M_MEMTAG_STACK_IS_ON, &memtag_stack, sizeof(memtag_stack)) && memtag_stack)
    untag_memory(__builtin_frame_address(0), reinterpret_cast<void*>(gprs[SP]));
#endif
  // Tell HWASan about the new stack top.
  if (__hwasan_handle_longjmp != nullptr)
    __hwasan_handle_longjmp(reinterpret_cast<void*>(gprs[SP]));
  // The Marking Register will be updated after return by art_quick_do_long_jump.
}

}  // namespace arm64
}  // namespace art
