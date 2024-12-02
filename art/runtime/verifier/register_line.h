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

#ifndef ART_RUNTIME_VERIFIER_REGISTER_LINE_H_
#define ART_RUNTIME_VERIFIER_REGISTER_LINE_H_

#include <limits>
#include <memory>
#include <vector>

#include <android-base/logging.h>

#include "base/arena_containers.h"
#include "base/locks.h"
#include "base/macros.h"
#include "base/safe_map.h"
#include "reg_type.h"

namespace art HIDDEN {

class Instruction;

namespace verifier {

class MethodVerifier;
class RegType;
class RegTypeCache;

/*
 * Register type categories, for type checking.
 *
 * The spec says category 1 includes boolean, byte, char, short, int, float, reference, and
 * returnAddress. Category 2 includes long and double.
 *
 * We treat object references separately, so we have "category1nr". We don't support jsr/ret, so
 * there is no "returnAddress" type.
 */
enum TypeCategory {
  kTypeCategoryUnknown = 0,
  kTypeCategory1nr = 1,         // boolean, byte, char, short, int, float
  kTypeCategory2 = 2,           // long, double
  kTypeCategoryRef = 3,         // object reference
};

// What to do with the lock levels when setting the register type.
enum class LockOp {
  kClear,                       // Clear the lock levels recorded.
  kKeep                         // Leave the lock levels alone.
};

// During verification, we associate one of these with every "interesting" instruction. We track
// the status of all registers, and (if the method has any monitor-enter instructions) maintain a
// stack of entered monitors (identified by code unit offset).
class RegisterLine {
 public:
  using RegisterStackMask = uint32_t;
  // A map from register to a bit vector of indices into the monitors_ stack.
  using RegToLockDepthsMap = ArenaSafeMap<uint32_t, RegisterStackMask>;

  // Maximum number of nested monitors to track before giving up and
  // taking the slow path.
  static constexpr size_t kMaxMonitorStackDepth =
      std::numeric_limits<RegisterStackMask>::digits;

  // Create a register line of num_regs registers.
  static RegisterLine* Create(size_t num_regs, ArenaAllocator& allocator, RegTypeCache* reg_types);

  // Implement category-1 "move" instructions. Copy a 32-bit value from "vsrc" to "vdst".
  void CopyRegister1(MethodVerifier* verifier, uint32_t vdst, uint32_t vsrc, TypeCategory cat)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Implement category-2 "move" instructions. Copy a 64-bit value from "vsrc" to "vdst". This
  // copies both halves of the register.
  void CopyRegister2(MethodVerifier* verifier, uint32_t vdst, uint32_t vsrc)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Implement "move-result". Copy the category-1 value from the result register to another
  // register, and reset the result register.
  void CopyResultRegister1(MethodVerifier* verifier, uint32_t vdst, bool is_reference)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Implement "move-result-wide". Copy the category-2 value from the result register to another
  // register, and reset the result register.
  void CopyResultRegister2(MethodVerifier* verifier, uint32_t vdst)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Set the invisible result register to unknown
  void SetResultTypeToUnknown(RegTypeCache* reg_types) REQUIRES_SHARED(Locks::mutator_lock_);

  // Set the type of register N, verifying that the register is valid.  If "newType" is the "Lo"
  // part of a 64-bit value, register N+1 will be set to "newType+1".
  // The register index was validated during the static pass, so we don't need to check it here.
  //
  // LockOp::kClear should be used by default; it will clear the lock levels associated with the
  // register. An example is setting the register type because an instruction writes to the
  // register.
  // LockOp::kKeep keeps the lock levels of the register and only changes the register type. This
  // is typical when the underlying value did not change, but we have "different" type information
  // available now. An example is sharpening types after a check-cast. Note that when given kKeep,
  // the new_type is dchecked to be a reference type.
  ALWAYS_INLINE void SetRegisterType(uint32_t vdst, RegType::Kind new_kind)
      REQUIRES_SHARED(Locks::mutator_lock_);
  template <LockOp kLockOp>
  ALWAYS_INLINE void SetRegisterType(uint32_t vdst, const RegType& new_type)
      REQUIRES_SHARED(Locks::mutator_lock_);

  void SetRegisterTypeWide(uint32_t vdst, RegType::Kind new_kind1, RegType::Kind new_kind2)
      REQUIRES_SHARED(Locks::mutator_lock_);
  void SetRegisterTypeWide(uint32_t vdst, const RegType& new_type1, const RegType& new_type2)
      REQUIRES_SHARED(Locks::mutator_lock_);

  /* Set the type of the "result" register. */
  void SetResultRegisterType(MethodVerifier* verifier, const RegType& new_type)
      REQUIRES_SHARED(Locks::mutator_lock_);

  void SetResultRegisterTypeWide(const RegType& new_type1, const RegType& new_type2)
      REQUIRES_SHARED(Locks::mutator_lock_);

  /*
   * Set register type for a `new-instance` instruction.
   * For `new-instance`, we additionally record the allocation dex pc for vreg `vdst`.
   * This is used to keep track of registers that hold the same uninitialized reference,
   * so that we can update them all when a constructor is called on any of them.
   */
  void SetRegisterTypeForNewInstance(uint32_t vdst, const RegType& uninit_type, uint32_t dex_pc)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Get the id of the register tyoe of register vsrc.
  uint16_t GetRegisterTypeId(uint32_t vsrc) const;

  // Get the type of register vsrc.
  const RegType& GetRegisterType(MethodVerifier* verifier, uint32_t vsrc) const;

  void CopyFromLine(const RegisterLine* src);

  std::string Dump(MethodVerifier* verifier) const REQUIRES_SHARED(Locks::mutator_lock_);

  void FillWithGarbage() {
    memset(&line_, 0xf1, num_regs_ * sizeof(uint16_t));
    monitors_.clear();
    reg_to_lock_depths_.clear();
  }

  /*
   * In debug mode, assert that the register line does not contain an uninitialized register
   * type for a `new-instance` allocation at a specific dex pc. We do this check before recording
   * the uninitialized register type and dex pc for a `new-instance` instruction.
   */
  void DCheckUniqueNewInstanceDexPc(MethodVerifier* verifier, uint32_t dex_pc)
      REQUIRES_SHARED(Locks::mutator_lock_);

  /*
   * Update all registers holding the uninitialized type currently recorded for vreg `vsrc` to
   * instead hold the corresponding initialized reference type. This is called when an appropriate
   * constructor is invoked -- all copies of the reference must be marked as initialized.
   */
  void MarkRefsAsInitialized(MethodVerifier* verifier, uint32_t vsrc)
      REQUIRES_SHARED(Locks::mutator_lock_);

  /*
   * Update all registers to be Conflict except vsrc.
   */
  void MarkAllRegistersAsConflicts(MethodVerifier* verifier);
  void MarkAllRegistersAsConflictsExcept(MethodVerifier* verifier, uint32_t vsrc);
  void MarkAllRegistersAsConflictsExceptWide(MethodVerifier* verifier, uint32_t vsrc);

  void SetThisInitialized() {
    this_initialized_ = true;
  }

  void CopyThisInitialized(const RegisterLine& src) {
    this_initialized_ = src.this_initialized_;
  }

  /*
   * Check constraints on constructor return. Specifically, make sure that the "this" argument got
   * initialized.
   * The "this" argument to <init> uses code offset kUninitThisArgAddr, which puts it at the start
   * of the list in slot 0. If we see a register with an uninitialized slot 0 reference, we know it
   * somehow didn't get initialized.
   */
  bool CheckConstructorReturn(MethodVerifier* verifier) const;

  // Compare two register lines. Returns 0 if they match.
  // Using this for a sort is unwise, since the value can change based on machine endianness.
  int CompareLine(const RegisterLine* line2) const {
    if (monitors_ != line2->monitors_) {
      return 1;
    }
    // TODO: DCHECK(reg_to_lock_depths_ == line2->reg_to_lock_depths_);
    return memcmp(&line_, &line2->line_, num_regs_ * sizeof(uint16_t));
  }

  size_t NumRegs() const {
    return num_regs_;
  }

  // Return how many bytes of memory a register line uses.
  ALWAYS_INLINE static size_t ComputeSize(size_t num_regs);

  // Verify/push monitor onto the monitor stack, locking the value in reg_idx at location insn_idx.
  void PushMonitor(MethodVerifier* verifier, uint32_t reg_idx, int32_t insn_idx)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Verify/pop monitor from monitor stack ensuring that we believe the monitor is locked
  void PopMonitor(MethodVerifier* verifier, uint32_t reg_idx)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Stack of currently held monitors and where they were locked
  size_t MonitorStackDepth() const {
    return monitors_.size();
  }

  // We expect no monitors to be held at certain points, such a method returns. Verify the stack
  // is empty, queueing a LOCKING error else.
  void VerifyMonitorStackEmpty(MethodVerifier* verifier) const;

  bool MergeRegisters(MethodVerifier* verifier, const RegisterLine* incoming_line)
      REQUIRES_SHARED(Locks::mutator_lock_);

  size_t GetMonitorEnterCount() const {
    return monitors_.size();
  }

  uint32_t GetMonitorEnterDexPc(size_t i) const {
    return monitors_[i];
  }

  // We give access to the lock depth map to avoid an expensive poll loop for FindLocksAtDexPC.
  template <typename T>
  void IterateRegToLockDepths(T fn) const {
    for (const auto& pair : reg_to_lock_depths_) {
      const uint32_t reg = pair.first;
      uint32_t depths = pair.second;
      uint32_t depth = 0;
      while (depths != 0) {
        if ((depths & 1) != 0) {
          fn(reg, depth);
        }
        depths >>= 1;
        depth++;
      }
    }
  }

 private:
  // For uninitialized types we need to check for allocation dex pc mismatch when merging.
  // This does not apply to uninitialized "this" reference types.
  static bool NeedsAllocationDexPc(const RegType& reg_type);

  void EnsureAllocationDexPcsAvailable();

  template <LockOp kLockOp>
  ALWAYS_INLINE void SetRegisterTypeImpl(uint32_t vdst, uint16_t new_id)
      REQUIRES_SHARED(Locks::mutator_lock_);
  void SetRegisterTypeWideImpl(uint32_t vdst, uint16_t new_id1, uint16_t new_id2)
      REQUIRES_SHARED(Locks::mutator_lock_);

  void CopyRegToLockDepth(size_t dst, size_t src) {
    auto it = reg_to_lock_depths_.find(src);
    if (it != reg_to_lock_depths_.end()) {
      reg_to_lock_depths_.Put(dst, it->second);
    }
  }

  bool IsSetLockDepth(size_t reg, size_t depth) {
    auto it = reg_to_lock_depths_.find(reg);
    if (it != reg_to_lock_depths_.end()) {
      return (it->second & (1 << depth)) != 0;
    } else {
      return false;
    }
  }

  bool SetRegToLockDepth(size_t reg, size_t depth) {
    CHECK_LT(depth, kMaxMonitorStackDepth);
    if (IsSetLockDepth(reg, depth)) {
      return false;  // Register already holds lock so locking twice is erroneous.
    }
    auto it = reg_to_lock_depths_.find(reg);
    if (it == reg_to_lock_depths_.end()) {
      reg_to_lock_depths_.Put(reg, 1 << depth);
    } else {
      it->second |= (1 << depth);
    }
    return true;
  }

  void ClearRegToLockDepth(size_t reg, size_t depth);

  void ClearAllRegToLockDepths(size_t reg) {
    reg_to_lock_depths_.erase(reg);
  }

  RegisterLine(size_t num_regs, ArenaAllocator& allocator, RegTypeCache* reg_types);

  static constexpr uint32_t kNoDexPc = static_cast<uint32_t>(-1);

  // Length of reg_types_
  const uint32_t num_regs_;

  // Storage for the result register's type, valid after an invocation.
  uint16_t result_[2];

  // Track allocation dex pcs for `new-instance` results moved to other registers.
  uint32_t* allocation_dex_pcs_;

  // A stack of monitor enter locations.
  ArenaVector<uint32_t> monitors_;

  // A map from register to a bit vector of indices into the monitors_ stack. As we pop the monitor
  // stack we verify that monitor-enter/exit are correctly nested. That is, if there was a
  // monitor-enter on v5 and then on v6, we expect the monitor-exit to be on v6 then on v5.
  RegToLockDepthsMap reg_to_lock_depths_;

  // Whether "this" initialization (a constructor supercall) has happened.
  bool this_initialized_;

  // An array of RegType Ids associated with each dex register.
  uint16_t line_[1];

  friend class RegisterLineArenaDelete;

  DISALLOW_COPY_AND_ASSIGN(RegisterLine);
};

class RegisterLineArenaDelete : public ArenaDelete<RegisterLine> {
 public:
  void operator()(RegisterLine* ptr) const;
};

using RegisterLineArenaUniquePtr = std::unique_ptr<RegisterLine, RegisterLineArenaDelete>;

}  // namespace verifier
}  // namespace art

#endif  // ART_RUNTIME_VERIFIER_REGISTER_LINE_H_
