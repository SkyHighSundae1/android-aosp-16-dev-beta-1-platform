/*
 * Copyright (C) 2013 The Android Open Source Project
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

#ifndef ART_RUNTIME_VERIFIER_REGISTER_LINE_INL_H_
#define ART_RUNTIME_VERIFIER_REGISTER_LINE_INL_H_

#include "register_line.h"

#include "base/logging.h"  // For VLOG.
#include "method_verifier.h"
#include "reg_type_cache-inl.h"

namespace art HIDDEN {
namespace verifier {

// Should we dump a warning on failures to verify balanced locking? That would be an indication to
// developers that their code will be slow.
static constexpr bool kDumpLockFailures = true;

inline uint16_t RegisterLine::GetRegisterTypeId(uint32_t vsrc) const {
  // The register index was validated during the static pass, so we don't need to check it here.
  DCHECK_LT(vsrc, num_regs_);
  return line_[vsrc];
}

inline const RegType& RegisterLine::GetRegisterType(MethodVerifier* verifier, uint32_t vsrc) const {
  return verifier->GetRegTypeCache()->GetFromId(GetRegisterTypeId(vsrc));
}

template <LockOp kLockOp>
inline void RegisterLine::SetRegisterTypeImpl(uint32_t vdst, uint16_t new_id) {
  DCHECK_LT(vdst, num_regs_);
  // Note: previously we failed when asked to set a conflict. However, conflicts are OK as long
  //       as they are not accessed, and our backends can handle this nowadays.
  line_[vdst] = new_id;
  switch (kLockOp) {
    case LockOp::kClear:
      // Clear the monitor entry bits for this register.
      ClearAllRegToLockDepths(vdst);
      break;
    case LockOp::kKeep:
      break;
  }
}

inline void RegisterLine::SetRegisterType(uint32_t vdst, RegType::Kind new_kind) {
  DCHECK(!RegType::IsLowHalf(new_kind));
  DCHECK(!RegType::IsHighHalf(new_kind));
  SetRegisterTypeImpl<LockOp::kClear>(vdst, RegTypeCache::IdForRegKind(new_kind));
}

template <LockOp kLockOp>
inline void RegisterLine::SetRegisterType(uint32_t vdst, const RegType& new_type) {
  DCHECK(!new_type.IsLowHalf());
  DCHECK(!new_type.IsHighHalf());
  // Should only keep locks for reference types.
  DCHECK_IMPLIES(kLockOp == LockOp::kKeep, new_type.IsReferenceTypes());
  SetRegisterTypeImpl<kLockOp>(vdst, new_type.GetId());
}

inline void RegisterLine::SetRegisterTypeWideImpl(uint32_t vdst,
                                                  uint16_t new_id1,
                                                  uint16_t new_id2) {
  DCHECK_LT(vdst + 1, num_regs_);
  line_[vdst] = new_id1;
  line_[vdst + 1] = new_id2;
  // Clear the monitor entry bits for this register.
  ClearAllRegToLockDepths(vdst);
  ClearAllRegToLockDepths(vdst + 1);
}

inline void RegisterLine::SetRegisterTypeWide(uint32_t vdst,
                                              RegType::Kind new_kind1,
                                              RegType::Kind new_kind2) {
  DCHECK(RegType::CheckWidePair(new_kind1, new_kind2));
  SetRegisterTypeWideImpl(
      vdst, RegTypeCache::IdForRegKind(new_kind1), RegTypeCache::IdForRegKind(new_kind2));
}

inline void RegisterLine::SetRegisterTypeWide(uint32_t vdst,
                                              const RegType& new_type1,
                                              const RegType& new_type2) {
  DCHECK(new_type1.CheckWidePair(new_type2));
  SetRegisterTypeWideImpl(vdst, new_type1.GetId(), new_type2.GetId());
}

inline void RegisterLine::SetResultTypeToUnknown(RegTypeCache* reg_types) {
  result_[0] = reg_types->Undefined().GetId();
  result_[1] = result_[0];
}

inline void RegisterLine::SetResultRegisterType(MethodVerifier* verifier, const RegType& new_type) {
  DCHECK(!new_type.IsLowHalf());
  DCHECK(!new_type.IsHighHalf());
  result_[0] = new_type.GetId();
  result_[1] = verifier->GetRegTypeCache()->Undefined().GetId();
}

inline void RegisterLine::SetResultRegisterTypeWide(const RegType& new_type1,
                                                    const RegType& new_type2) {
  DCHECK(new_type1.CheckWidePair(new_type2));
  result_[0] = new_type1.GetId();
  result_[1] = new_type2.GetId();
}

inline void RegisterLine::SetRegisterTypeForNewInstance(uint32_t vdst,
                                                        const RegType& uninit_type,
                                                        uint32_t dex_pc) {
  DCHECK_LT(vdst, num_regs_);
  DCHECK(NeedsAllocationDexPc(uninit_type));
  SetRegisterType<LockOp::kClear>(vdst, uninit_type);
  EnsureAllocationDexPcsAvailable();
  allocation_dex_pcs_[vdst] = dex_pc;
}

inline void RegisterLine::CopyRegister1(MethodVerifier* verifier, uint32_t vdst, uint32_t vsrc,
                                 TypeCategory cat) {
  DCHECK(cat == kTypeCategory1nr || cat == kTypeCategoryRef);
  const RegType& type = GetRegisterType(verifier, vsrc);
  if (type.IsLowHalf() || type.IsHighHalf()) {
    verifier->Fail(VERIFY_ERROR_BAD_CLASS_HARD) << "Expected category1 register type not '"
        << type << "'";
    return;
  }
  // FIXME: If `vdst == vsrc`, we clear locking information before we try to copy it below. Adding
  // `move-object v1, v1` to the middle of `OK.runStraightLine()` in run-test 088 makes it fail.
  SetRegisterType<LockOp::kClear>(vdst, type);
  if (!type.IsConflict() &&                                  // Allow conflicts to be copied around.
      ((cat == kTypeCategory1nr && !type.IsCategory1Types()) ||
       (cat == kTypeCategoryRef && !type.IsReferenceTypes()))) {
    verifier->Fail(VERIFY_ERROR_BAD_CLASS_HARD) << "copy1 v" << vdst << "<-v" << vsrc << " type=" << type
                                                 << " cat=" << static_cast<int>(cat);
  } else if (cat == kTypeCategoryRef) {
    CopyRegToLockDepth(vdst, vsrc);
    if (allocation_dex_pcs_ != nullptr) {
      // Copy allocation dex pc for uninitialized types. (Copy unused value for other types.)
      allocation_dex_pcs_[vdst] = allocation_dex_pcs_[vsrc];
    }
  }
}

inline void RegisterLine::CopyRegister2(MethodVerifier* verifier, uint32_t vdst, uint32_t vsrc) {
  const RegType& type_l = GetRegisterType(verifier, vsrc);
  const RegType& type_h = GetRegisterType(verifier, vsrc + 1);

  if (!type_l.CheckWidePair(type_h)) {
    verifier->Fail(VERIFY_ERROR_BAD_CLASS_HARD) << "copy2 v" << vdst << "<-v" << vsrc
                                                 << " type=" << type_l << "/" << type_h;
  } else {
    SetRegisterTypeWide(vdst, type_l, type_h);
  }
}

inline bool RegisterLine::NeedsAllocationDexPc(const RegType& reg_type) {
  return reg_type.IsUninitializedReference() || reg_type.IsUnresolvedUninitializedReference();
}

inline void RegisterLine::DCheckUniqueNewInstanceDexPc(MethodVerifier* verifier, uint32_t dex_pc) {
  if (kIsDebugBuild && allocation_dex_pcs_ != nullptr) {
    // Note: We do not clear the `allocation_dex_pcs_` entries when copying data from
    // a register line without `allocation_dex_pcs_`, or when we merge types and find
    // a conflict, so the same dex pc can remain in the `allocation_dex_pcs_` array
    // but it cannot be recorded for a `new-instance` uninitialized type.
    RegTypeCache* reg_types = verifier->GetRegTypeCache();
    for (uint32_t i = 0; i != num_regs_; ++i) {
      if (NeedsAllocationDexPc(reg_types->GetFromId(line_[i]))) {
        CHECK_NE(allocation_dex_pcs_[i], dex_pc) << i << " " << reg_types->GetFromId(line_[i]);
      }
    }
  }
}

inline void RegisterLine::EnsureAllocationDexPcsAvailable() {
  DCHECK_NE(num_regs_, 0u);
  if (allocation_dex_pcs_ == nullptr) {
    ArenaAllocatorAdapter<uint32_t> allocator(monitors_.get_allocator());
    allocation_dex_pcs_ = allocator.allocate(num_regs_);
    std::fill_n(allocation_dex_pcs_, num_regs_, kNoDexPc);
  }
}

inline void RegisterLine::VerifyMonitorStackEmpty(MethodVerifier* verifier) const {
  if (MonitorStackDepth() != 0) {
    verifier->Fail(VERIFY_ERROR_LOCKING, /*pending_exc=*/ false);
    if (kDumpLockFailures) {
      VLOG(verifier) << "expected empty monitor stack in "
                     << verifier->GetMethodReference().PrettyMethod();
    }
  }
}

inline size_t RegisterLine::ComputeSize(size_t num_regs) {
  return OFFSETOF_MEMBER(RegisterLine, line_) + num_regs * sizeof(uint16_t);
}

inline RegisterLine* RegisterLine::Create(size_t num_regs,
                                          ArenaAllocator& allocator,
                                          RegTypeCache* reg_types) {
  void* memory = allocator.Alloc(ComputeSize(num_regs));
  return new (memory) RegisterLine(num_regs, allocator, reg_types);
}

inline RegisterLine::RegisterLine(size_t num_regs,
                                  ArenaAllocator& allocator,
                                  RegTypeCache* reg_types)
    : num_regs_(num_regs),
      allocation_dex_pcs_(nullptr),
      monitors_(allocator.Adapter(kArenaAllocVerifier)),
      reg_to_lock_depths_(std::less<uint32_t>(),
                          allocator.Adapter(kArenaAllocVerifier)),
      this_initialized_(false) {
  // `ArenaAllocator` guarantees zero-initialization.
  static_assert(RegTypeCache::kUndefinedCacheId == 0u);
  DCHECK(std::all_of(line_,
                     line_ + num_regs_,
                     [](auto id) { return id == RegTypeCache::kUndefinedCacheId;}));
  SetResultTypeToUnknown(reg_types);
}

inline void RegisterLine::ClearRegToLockDepth(size_t reg, size_t depth) {
  CHECK_LT(depth, 32u);
  DCHECK(IsSetLockDepth(reg, depth));
  auto it = reg_to_lock_depths_.find(reg);
  DCHECK(it != reg_to_lock_depths_.end());
  uint32_t depths = it->second ^ (1 << depth);
  if (depths != 0) {
    it->second = depths;
  } else {
    reg_to_lock_depths_.erase(it);
  }
  // Need to unlock every register at the same lock depth. These are aliased locks.
  uint32_t mask = 1 << depth;
  for (auto& pair : reg_to_lock_depths_) {
    if ((pair.second & mask) != 0) {
      VLOG(verifier) << "Also unlocking " << pair.first;
      pair.second ^= mask;
    }
  }
}

inline void RegisterLineArenaDelete::operator()(RegisterLine* ptr) const {
  if (ptr != nullptr) {
    uint32_t num_regs = ptr->NumRegs();
    uint32_t* allocation_dex_pcs = ptr->allocation_dex_pcs_;
    ptr->~RegisterLine();
    ProtectMemory(ptr, RegisterLine::ComputeSize(num_regs));
    if (allocation_dex_pcs != nullptr) {
      struct AllocationDexPcsDelete : ArenaDelete<uint32_t> {
        void operator()(uint32_t* ptr, size_t size) {
          ProtectMemory(ptr, size);
        }
      };
      AllocationDexPcsDelete()(allocation_dex_pcs, num_regs * sizeof(*allocation_dex_pcs));
    }
  }
}

}  // namespace verifier
}  // namespace art

#endif  // ART_RUNTIME_VERIFIER_REGISTER_LINE_INL_H_
