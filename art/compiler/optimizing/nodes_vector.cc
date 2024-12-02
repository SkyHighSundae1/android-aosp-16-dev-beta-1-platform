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

#include "nodes.h"

namespace art HIDDEN {

HVecCondition* HVecCondition::Create(HGraph* graph,
                                     IfCondition cond,
                                     HInstruction* lhs,
                                     HInstruction* rhs,
                                     DataType::Type packed_type,
                                     size_t vector_length,
                                     uint32_t dex_pc) {
  ArenaAllocator* allocator = graph->GetAllocator();
  switch (cond) {
    case kCondEQ: return new (allocator) HVecEqual(allocator,
                                                   lhs,
                                                   rhs,
                                                   packed_type,
                                                   vector_length,
                                                   dex_pc);
    case kCondNE: return new (allocator) HVecNotEqual(allocator,
                                                      lhs,
                                                      rhs,
                                                      packed_type,
                                                      vector_length,
                                                      dex_pc);
    case kCondLT: return new (allocator) HVecLessThan(allocator,
                                                      lhs,
                                                      rhs,
                                                      packed_type,
                                                      vector_length,
                                                      dex_pc);
    case kCondLE: return new (allocator) HVecLessThanOrEqual(allocator,
                                                             lhs,
                                                             rhs,
                                                             packed_type,
                                                             vector_length,
                                                             dex_pc);
    case kCondGT: return new (allocator) HVecGreaterThan(allocator,
                                                         lhs,
                                                         rhs,
                                                         packed_type,
                                                         vector_length,
                                                         dex_pc);
    case kCondGE: return new (allocator) HVecGreaterThanOrEqual(allocator,
                                                                lhs,
                                                                rhs,
                                                                packed_type,
                                                                vector_length,
                                                                dex_pc);
    case kCondB:  return new (allocator) HVecBelow(allocator,
                                                   lhs,
                                                   rhs,
                                                   packed_type,
                                                   vector_length,
                                                   dex_pc);
    case kCondBE: return new (allocator) HVecBelowOrEqual(allocator,
                                                          lhs,
                                                          rhs,
                                                          packed_type,
                                                          vector_length,
                                                          dex_pc);
    case kCondA:  return new (allocator) HVecAbove(allocator,
                                                   lhs,
                                                   rhs,
                                                   packed_type,
                                                   vector_length,
                                                   dex_pc);
    case kCondAE: return new (allocator) HVecAboveOrEqual(allocator,
                                                          lhs,
                                                          rhs,
                                                          packed_type,
                                                          vector_length,
                                                          dex_pc);
  }
  LOG(FATAL) << "Unexpected condition " << cond;
  UNREACHABLE();
}

}  // namespace art
