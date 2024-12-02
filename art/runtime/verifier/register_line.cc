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

#include "register_line.h"

#include "android-base/stringprintf.h"

#include "dex/dex_instruction-inl.h"
#include "method_verifier-inl.h"
#include "reg_type-inl.h"
#include "register_line-inl.h"

namespace art HIDDEN {
namespace verifier {

using android::base::StringPrintf;

bool RegisterLine::CheckConstructorReturn(MethodVerifier* verifier) const {
  if (kIsDebugBuild && this_initialized_) {
    // Ensure that there is no UninitializedThisReference type anymore if this_initialized_ is true.
    for (size_t i = 0; i < num_regs_; i++) {
      const RegType& type = GetRegisterType(verifier, i);
      CHECK(!type.IsUninitializedThisReference() &&
            !type.IsUnresolvedUninitializedThisReference())
          << i << ": " << type.IsUninitializedThisReference() << " in "
          << verifier->GetMethodReference().PrettyMethod();
    }
  }
  if (!this_initialized_) {
    verifier->Fail(VERIFY_ERROR_BAD_CLASS_HARD)
        << "Constructor returning without calling superclass constructor";
  }
  return this_initialized_;
}

void RegisterLine::CopyFromLine(const RegisterLine* src) {
  DCHECK_EQ(num_regs_, src->num_regs_);
  memcpy(&line_, &src->line_, num_regs_ * sizeof(uint16_t));
  // Copy `allocation_dex_pcs_`. Note that if the `src` does not have `allocation_dex_pcs_`
  // allocated, we retain the array allocated for this register line to avoid wasting
  // memory by allocating a new array later. This means that the `allocation_dex_pcs_` can
  // be filled with bogus values not tied to a `new-instance` uninitialized type.
  if (src->allocation_dex_pcs_ != nullptr) {
    EnsureAllocationDexPcsAvailable();
    memcpy(allocation_dex_pcs_, src->allocation_dex_pcs_, num_regs_ * sizeof(uint32_t));
  }
  monitors_ = src->monitors_;
  reg_to_lock_depths_ = src->reg_to_lock_depths_;
  this_initialized_ = src->this_initialized_;
}

void RegisterLine::MarkRefsAsInitialized(MethodVerifier* verifier, uint32_t vsrc) {
  const RegType& uninit_type = GetRegisterType(verifier, vsrc);
  DCHECK(uninit_type.IsUninitializedTypes());
  const RegType& init_type = verifier->GetRegTypeCache()->FromUninitialized(uninit_type);
  size_t changed = 0;
  // Is this initializing "this"?
  if (uninit_type.IsUninitializedThisReference() ||
      uninit_type.IsUnresolvedUninitializedThisReference()) {
    this_initialized_ = true;
    for (uint32_t i = 0; i < num_regs_; i++) {
      if (GetRegisterType(verifier, i).Equals(uninit_type)) {
        line_[i] = init_type.GetId();
        changed++;
      }
    }
  } else {
    DCHECK(NeedsAllocationDexPc(uninit_type));
    DCHECK(allocation_dex_pcs_ != nullptr);
    uint32_t dex_pc = allocation_dex_pcs_[vsrc];
    for (uint32_t i = 0; i < num_regs_; i++) {
      if (GetRegisterType(verifier, i).Equals(uninit_type) && allocation_dex_pcs_[i] == dex_pc) {
        line_[i] = init_type.GetId();
        changed++;
      }
    }
  }
  DCHECK_GT(changed, 0u);
}

void RegisterLine::MarkAllRegistersAsConflicts(MethodVerifier* verifier) {
  uint16_t conflict_type_id = verifier->GetRegTypeCache()->Conflict().GetId();
  for (uint32_t i = 0; i < num_regs_; i++) {
    line_[i] = conflict_type_id;
  }
}

void RegisterLine::MarkAllRegistersAsConflictsExcept(MethodVerifier* verifier, uint32_t vsrc) {
  uint16_t conflict_type_id = verifier->GetRegTypeCache()->Conflict().GetId();
  for (uint32_t i = 0; i < num_regs_; i++) {
    if (i != vsrc) {
      line_[i] = conflict_type_id;
    }
  }
}

void RegisterLine::MarkAllRegistersAsConflictsExceptWide(MethodVerifier* verifier, uint32_t vsrc) {
  uint16_t conflict_type_id = verifier->GetRegTypeCache()->Conflict().GetId();
  for (uint32_t i = 0; i < num_regs_; i++) {
    if ((i != vsrc) && (i != (vsrc + 1))) {
      line_[i] = conflict_type_id;
    }
  }
}

std::string RegisterLine::Dump(MethodVerifier* verifier) const {
  std::string result;
  for (size_t i = 0; i < num_regs_; i++) {
    result += StringPrintf("%zd:[", i);
    result += GetRegisterType(verifier, i).Dump();
    result += "],";
  }
  for (const auto& monitor : monitors_) {
    result += StringPrintf("{%d},", monitor);
  }
  for (auto& pairs : reg_to_lock_depths_) {
    result += StringPrintf("<%d -> %" PRIx64 ">",
                           pairs.first,
                           static_cast<uint64_t>(pairs.second));
  }
  return result;
}

void RegisterLine::CopyResultRegister1(MethodVerifier* verifier, uint32_t vdst, bool is_reference) {
  const RegType& type = verifier->GetRegTypeCache()->GetFromId(result_[0]);
  if ((!is_reference && !type.IsCategory1Types()) ||
      (is_reference && !type.IsReferenceTypes())) {
    verifier->Fail(VERIFY_ERROR_BAD_CLASS_HARD)
        << "copyRes1 v" << vdst << "<- result0"  << " type=" << type;
  } else {
    DCHECK(verifier->GetRegTypeCache()->GetFromId(result_[1]).IsUndefined());
    SetRegisterType<LockOp::kClear>(vdst, type);
    result_[0] = verifier->GetRegTypeCache()->Undefined().GetId();
  }
}

/*
 * Implement "move-result-wide". Copy the category-2 value from the result
 * register to another register, and reset the result register.
 */
void RegisterLine::CopyResultRegister2(MethodVerifier* verifier, uint32_t vdst) {
  const RegType& type_l = verifier->GetRegTypeCache()->GetFromId(result_[0]);
  const RegType& type_h = verifier->GetRegTypeCache()->GetFromId(result_[1]);
  if (!type_l.IsCategory2Types()) {
    verifier->Fail(VERIFY_ERROR_BAD_CLASS_HARD)
        << "copyRes2 v" << vdst << "<- result0"  << " type=" << type_l;
  } else {
    DCHECK(type_l.CheckWidePair(type_h));  // Set should never allow this case
    SetRegisterTypeWide(vdst, type_l, type_h);  // also sets the high
    result_[0] = verifier->GetRegTypeCache()->Undefined().GetId();
    result_[1] = verifier->GetRegTypeCache()->Undefined().GetId();
  }
}

static constexpr uint32_t kVirtualNullRegister = std::numeric_limits<uint32_t>::max();

void RegisterLine::PushMonitor(MethodVerifier* verifier, uint32_t reg_idx, int32_t insn_idx) {
  const RegType& reg_type = GetRegisterType(verifier, reg_idx);
  if (!reg_type.IsReferenceTypes()) {
    verifier->Fail(VERIFY_ERROR_BAD_CLASS_HARD) << "monitor-enter on non-object ("
        << reg_type << ")";
  } else if (monitors_.size() >= kMaxMonitorStackDepth) {
    verifier->Fail(VERIFY_ERROR_LOCKING);
    if (kDumpLockFailures) {
      VLOG(verifier) << "monitor-enter stack overflow while verifying "
                     << verifier->GetMethodReference().PrettyMethod();
    }
  } else {
    if (SetRegToLockDepth(reg_idx, monitors_.size())) {
      // Null literals can establish aliases that we can't easily track. As such, handle the zero
      // case as the 2^32-1 register (which isn't available in dex bytecode).
      if (reg_type.IsZero()) {
        SetRegToLockDepth(kVirtualNullRegister, monitors_.size());
      }

      monitors_.push_back(insn_idx);
    } else {
      verifier->Fail(VERIFY_ERROR_LOCKING);
      if (kDumpLockFailures) {
        VLOG(verifier) << "unexpected monitor-enter on register v" <<  reg_idx << " in "
                       << verifier->GetMethodReference().PrettyMethod();
      }
    }
  }
}

void RegisterLine::PopMonitor(MethodVerifier* verifier, uint32_t reg_idx) {
  const RegType& reg_type = GetRegisterType(verifier, reg_idx);
  if (!reg_type.IsReferenceTypes()) {
    verifier->Fail(VERIFY_ERROR_BAD_CLASS_HARD) << "monitor-exit on non-object (" << reg_type << ")";
  } else if (monitors_.empty()) {
    verifier->Fail(VERIFY_ERROR_LOCKING);
    if (kDumpLockFailures) {
      VLOG(verifier) << "monitor-exit stack underflow while verifying "
                     << verifier->GetMethodReference().PrettyMethod();
    }
  } else {
    monitors_.pop_back();

    bool success = IsSetLockDepth(reg_idx, monitors_.size());

    if (!success && reg_type.IsZero()) {
      // Null literals can establish aliases that we can't easily track. As such, handle the zero
      // case as the 2^32-1 register (which isn't available in dex bytecode).
      success = IsSetLockDepth(kVirtualNullRegister, monitors_.size());
      if (success) {
        reg_idx = kVirtualNullRegister;
      }
    }

    if (!success) {
      verifier->Fail(VERIFY_ERROR_LOCKING);
      if (kDumpLockFailures) {
        VLOG(verifier) << "monitor-exit not unlocking the top of the monitor stack while verifying "
                       << verifier->GetMethodReference().PrettyMethod();
      }
    } else {
      // Record the register was unlocked. This clears all aliases, thus it will also clear the
      // null lock, if necessary.
      ClearRegToLockDepth(reg_idx, monitors_.size());
    }
  }
}

bool FindLockAliasedRegister(uint32_t src,
                             const RegisterLine::RegToLockDepthsMap& src_map,
                             const RegisterLine::RegToLockDepthsMap& search_map) {
  auto it = src_map.find(src);
  if (it == src_map.end()) {
    // "Not locked" is trivially aliased.
    return true;
  }
  uint32_t src_lock_levels = it->second;
  if (src_lock_levels == 0) {
    // "Not locked" is trivially aliased.
    return true;
  }

  // Scan the map for the same value.
  for (const std::pair<const uint32_t, uint32_t>& pair : search_map) {
    if (pair.first != src && pair.second == src_lock_levels) {
      return true;
    }
  }

  // Nothing found, no alias.
  return false;
}

bool RegisterLine::MergeRegisters(MethodVerifier* verifier, const RegisterLine* incoming_line) {
  bool changed = false;
  DCHECK(incoming_line != nullptr);
  for (size_t idx = 0; idx < num_regs_; idx++) {
    if (line_[idx] != incoming_line->line_[idx]) {
      const RegType& incoming_reg_type = incoming_line->GetRegisterType(verifier, idx);
      const RegType& cur_type = GetRegisterType(verifier, idx);
      const RegType& new_type = cur_type.Merge(
          incoming_reg_type, verifier->GetRegTypeCache(), verifier);
      changed = changed || !cur_type.Equals(new_type);
      line_[idx] = new_type.GetId();
    } else {
      auto needs_allocation_dex_pc = [&]() {
        return NeedsAllocationDexPc(verifier->GetRegTypeCache()->GetFromId(line_[idx]));
      };
      DCHECK_IMPLIES(needs_allocation_dex_pc(), allocation_dex_pcs_ != nullptr);
      DCHECK_IMPLIES(needs_allocation_dex_pc(), incoming_line->allocation_dex_pcs_ != nullptr);
      // Check for allocation dex pc mismatch first to try and avoid costly virtual calls.
      // For methods without any `new-instance` instructions, the `allocation_dex_pcs_` is null.
      if (allocation_dex_pcs_ != nullptr &&
          incoming_line->allocation_dex_pcs_ != nullptr &&
          allocation_dex_pcs_[idx] != incoming_line->allocation_dex_pcs_[idx] &&
          needs_allocation_dex_pc()) {
        line_[idx] = verifier->GetRegTypeCache()->Conflict().GetId();
      }
    }
  }
  if (monitors_.size() > 0 || incoming_line->monitors_.size() > 0) {
    if (monitors_.size() != incoming_line->monitors_.size()) {
      verifier->Fail(VERIFY_ERROR_LOCKING, /*pending_exc=*/ false);
      if (kDumpLockFailures) {
        VLOG(verifier) << "mismatched stack depths (depth=" << MonitorStackDepth()
                       << ", incoming depth=" << incoming_line->MonitorStackDepth() << ") in "
                       << verifier->GetMethodReference().PrettyMethod();
      }
    } else if (reg_to_lock_depths_ != incoming_line->reg_to_lock_depths_) {
      for (uint32_t idx = 0; idx < num_regs_; idx++) {
        size_t depths = reg_to_lock_depths_.count(idx);
        size_t incoming_depths = incoming_line->reg_to_lock_depths_.count(idx);
        if (depths != incoming_depths) {
          // Stack levels aren't matching. This is potentially bad, as we don't do a
          // flow-sensitive analysis.
          // However, this could be an alias of something locked in one path, and the alias was
          // destroyed in another path. It is fine to drop this as long as there's another alias
          // for the lock around. The last vanishing alias will then report that things would be
          // left unlocked. We need to check for aliases for both lock levels.
          //
          // Example (lock status in curly braces as pair of register and lock leels):
          //
          //                            lock v1 {v1=1}
          //                        |                    |
          //              v0 = v1 {v0=1, v1=1}       v0 = v2 {v1=1}
          //                        |                    |
          //                                 {v1=1}
          //                                         // Dropping v0, as the status can't be merged
          //                                         // but the lock info ("locked at depth 1" and)
          //                                         // "not locked at all") is available.
          if (!FindLockAliasedRegister(idx,
                                       reg_to_lock_depths_,
                                       reg_to_lock_depths_) ||
              !FindLockAliasedRegister(idx,
                                       incoming_line->reg_to_lock_depths_,
                                       reg_to_lock_depths_)) {
            verifier->Fail(VERIFY_ERROR_LOCKING, /*pending_exc=*/ false);
            if (kDumpLockFailures) {
              VLOG(verifier) << "mismatched stack depths for register v" << idx
                             << ": " << depths  << " != " << incoming_depths << " in "
                             << verifier->GetMethodReference().PrettyMethod();
            }
            break;
          }
          // We found aliases, set this to zero.
          reg_to_lock_depths_.erase(idx);
        } else if (depths > 0) {
          // Check whether they're actually the same levels.
          uint32_t locked_levels = reg_to_lock_depths_.find(idx)->second;
          uint32_t incoming_locked_levels = incoming_line->reg_to_lock_depths_.find(idx)->second;
          if (locked_levels != incoming_locked_levels) {
            // Lock levels aren't matching. This is potentially bad, as we don't do a
            // flow-sensitive analysis.
            // However, this could be an alias of something locked in one path, and the alias was
            // destroyed in another path. It is fine to drop this as long as there's another alias
            // for the lock around. The last vanishing alias will then report that things would be
            // left unlocked. We need to check for aliases for both lock levels.
            //
            // Example (lock status in curly braces as pair of register and lock leels):
            //
            //                          lock v1 {v1=1}
            //                          lock v2 {v1=1, v2=2}
            //                        |                      |
            //         v0 = v1 {v0=1, v1=1, v2=2}  v0 = v2 {v0=2, v1=1, v2=2}
            //                        |                      |
            //                             {v1=1, v2=2}
            //                                           // Dropping v0, as the status can't be
            //                                           // merged but the lock info ("locked at
            //                                           // depth 1" and "locked at depth 2") is
            //                                           // available.
            if (!FindLockAliasedRegister(idx,
                                         reg_to_lock_depths_,
                                         reg_to_lock_depths_) ||
                !FindLockAliasedRegister(idx,
                                         incoming_line->reg_to_lock_depths_,
                                         reg_to_lock_depths_)) {
              // No aliases for both current and incoming, we'll lose information.
              verifier->Fail(VERIFY_ERROR_LOCKING, /*pending_exc=*/ false);
              if (kDumpLockFailures) {
                VLOG(verifier) << "mismatched lock levels for register v" << idx << ": "
                               << std::hex << locked_levels << std::dec  << " != "
                               << std::hex << incoming_locked_levels << std::dec << " in "
                               << verifier->GetMethodReference().PrettyMethod();
              }
              break;
            }
            // We found aliases, set this to zero.
            reg_to_lock_depths_.erase(idx);
          }
        }
      }
    }
  }

  // Check whether "this" was initialized in both paths.
  if (this_initialized_ && !incoming_line->this_initialized_) {
    this_initialized_ = false;
    changed = true;
  }
  return changed;
}

}  // namespace verifier
}  // namespace art
