/*
 * Copyright (C) 2011 The Android Open Source Project
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

#ifndef ART_RUNTIME_OAT_OAT_QUICK_METHOD_HEADER_H_
#define ART_RUNTIME_OAT_OAT_QUICK_METHOD_HEADER_H_

#include <optional>

#include "arch/instruction_set.h"
#include "base/locks.h"
#include "base/macros.h"
#include "base/utils.h"
#include "quick/quick_method_frame_info.h"
#include "stack_map.h"

namespace art HIDDEN {

class ArtMethod;

// Size in bytes of the should_deoptimize flag on stack.
// We just need 4 bytes for our purpose regardless of the architecture. Frame size
// calculation will automatically do alignment for the final frame size.
static constexpr size_t kShouldDeoptimizeFlagSize = 4;

// OatQuickMethodHeader precedes the raw code chunk generated by the compiler.
class PACKED(4) OatQuickMethodHeader {
 public:
  OatQuickMethodHeader(uint32_t code_info_offset = 0) {
    SetCodeInfoOffset(code_info_offset);
  }

  static OatQuickMethodHeader* NterpMethodHeader;
  EXPORT static ArrayRef<const uint8_t> NterpWithClinitImpl;
  EXPORT static ArrayRef<const uint8_t> NterpImpl;

  EXPORT bool IsNterpMethodHeader() const;

  static bool IsNterpPc(uintptr_t pc) {
    return OatQuickMethodHeader::NterpMethodHeader != nullptr &&
        OatQuickMethodHeader::NterpMethodHeader->Contains(pc);
  }

  static OatQuickMethodHeader* FromCodePointer(const void* code_ptr) {
    uintptr_t code = reinterpret_cast<uintptr_t>(code_ptr);
    uintptr_t header = code - OFFSETOF_MEMBER(OatQuickMethodHeader, code_);
    DCHECK(IsAlignedParam(code, GetInstructionSetCodeAlignment(kRuntimeQuickCodeISA)) ||
           IsAlignedParam(header, GetInstructionSetCodeAlignment(kRuntimeQuickCodeISA)))
        << std::hex << code << " " << std::hex << header;
    return reinterpret_cast<OatQuickMethodHeader*>(header);
  }

  static OatQuickMethodHeader* FromEntryPoint(const void* entry_point) {
    return FromCodePointer(EntryPointToCodePointer(entry_point));
  }

  static size_t InstructionAlignedSize() {
    return RoundUp(sizeof(OatQuickMethodHeader),
                   GetInstructionSetCodeAlignment(kRuntimeQuickCodeISA));
  }

  OatQuickMethodHeader(const OatQuickMethodHeader&) = default;
  OatQuickMethodHeader& operator=(const OatQuickMethodHeader&) = default;

  uintptr_t NativeQuickPcOffset(const uintptr_t pc) const {
    return pc - reinterpret_cast<uintptr_t>(GetEntryPoint());
  }

  // Check if this is hard-written assembly (i.e. inside libart.so).
  // Returns std::nullop on Mac.
  static std::optional<bool> IsStub(const uint8_t* pc);

  ALWAYS_INLINE bool IsOptimized() const {
    if (code_ == NterpWithClinitImpl.data() || code_ == NterpImpl.data()) {
      DCHECK(IsStub(code_).value_or(true));
      return false;
    }
    DCHECK(!IsStub(code_).value_or(false));
    return true;
  }

  ALWAYS_INLINE const uint8_t* GetOptimizedCodeInfoPtr() const {
    uint32_t offset = GetCodeInfoOffset();
    DCHECK_NE(offset, 0u);
    return code_ - offset;
  }

  ALWAYS_INLINE uint8_t* GetOptimizedCodeInfoPtr() {
    uint32_t offset = GetCodeInfoOffset();
    DCHECK_NE(offset, 0u);
    return code_ - offset;
  }

  ALWAYS_INLINE const uint8_t* GetCode() const {
    return code_;
  }

  ALWAYS_INLINE uint32_t GetCodeSize() const {
    if (code_ == NterpWithClinitImpl.data()) {
      return NterpWithClinitImpl.size();
    }
    if (code_ == NterpImpl.data()) {
      return NterpImpl.size();
    }
    return CodeInfo::DecodeCodeSize(GetOptimizedCodeInfoPtr());
  }

  ALWAYS_INLINE uint32_t GetCodeInfoOffset() const {
    DCHECK(IsOptimized());
    return code_info_offset_;
  }

  void SetCodeInfoOffset(uint32_t offset) { code_info_offset_ = offset; }

  bool Contains(uintptr_t pc) const {
    uintptr_t code_start = reinterpret_cast<uintptr_t>(code_);
// Let's not make assumptions about other architectures.
#if defined(__aarch64__) || defined(__riscv__) || defined(__riscv)
    // Verify that the code pointer is not tagged. Memory for code gets allocated with
    // mspace_memalign or memory mapped from a file, neither of which is tagged by MTE/HWASan.
    DCHECK_EQ(code_start, reinterpret_cast<uintptr_t>(code_start) & ((UINT64_C(1) << 56) - 1));
#endif
    static_assert(kRuntimeQuickCodeISA != InstructionSet::kThumb2,
                  "kThumb2 cannot be a runtime ISA");
    if (kRuntimeQuickCodeISA == InstructionSet::kArm) {
      // On Thumb-2, the pc is offset by one.
      code_start++;
    }
    return code_start <= pc && pc <= (code_start + GetCodeSize());
  }

  const uint8_t* GetEntryPoint() const {
    // When the runtime architecture is ARM, `kRuntimeQuickCodeISA` is set to `kArm`
    // (not `kThumb2`), *but* we always generate code for the Thumb-2
    // instruction set anyway. Thumb-2 requires the entrypoint to be of
    // offset 1.
    static_assert(kRuntimeQuickCodeISA != InstructionSet::kThumb2,
                  "kThumb2 cannot be a runtime ISA");
    return (kRuntimeQuickCodeISA == InstructionSet::kArm)
        ? reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(code_) | 1)
        : code_;
  }

  template <bool kCheckFrameSize = true>
  uint32_t GetFrameSizeInBytes() const {
    uint32_t result = GetFrameInfo().FrameSizeInBytes();
    if (kCheckFrameSize) {
      DCHECK_ALIGNED(result, kStackAlignment);
    }
    return result;
  }

  QuickMethodFrameInfo GetFrameInfo() const {
    DCHECK(IsOptimized());
    return CodeInfo::DecodeFrameInfo(GetOptimizedCodeInfoPtr());
  }

  size_t GetShouldDeoptimizeFlagOffset() const {
    DCHECK(IsOptimized());
    QuickMethodFrameInfo frame_info = GetFrameInfo();
    size_t frame_size = frame_info.FrameSizeInBytes();
    size_t core_spill_size =
        POPCOUNT(frame_info.CoreSpillMask()) * GetBytesPerGprSpillLocation(kRuntimeQuickCodeISA);
    size_t fpu_spill_size =
        POPCOUNT(frame_info.FpSpillMask()) * GetBytesPerFprSpillLocation(kRuntimeQuickCodeISA);
    return frame_size - core_spill_size - fpu_spill_size - kShouldDeoptimizeFlagSize;
  }

  // For non-catch handlers. Only used in test code.
  EXPORT uintptr_t ToNativeQuickPc(ArtMethod* method,
                                   const uint32_t dex_pc,
                                   bool abort_on_failure = true) const;

  // For catch handlers.
  uintptr_t ToNativeQuickPcForCatchHandlers(ArtMethod* method,
                                            ArrayRef<const uint32_t> dex_pc_list,
                                            /* out */ uint32_t* stack_map_row,
                                            bool abort_on_failure = true) const;

  uint32_t ToDexPc(ArtMethod** frame,
                   const uintptr_t pc,
                   bool abort_on_failure = true) const
      REQUIRES_SHARED(Locks::mutator_lock_);

  bool HasShouldDeoptimizeFlag() const {
    return IsOptimized() && CodeInfo::HasShouldDeoptimizeFlag(GetOptimizedCodeInfoPtr());
  }

 private:
  uint32_t code_info_offset_ = 0u;
  uint8_t code_[0];     // The actual method code.
};

}  // namespace art

#endif  // ART_RUNTIME_OAT_OAT_QUICK_METHOD_HEADER_H_
