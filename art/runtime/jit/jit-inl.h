/*
 * Copyright 2018 The Android Open Source Project
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

#ifndef ART_RUNTIME_JIT_JIT_INL_H_
#define ART_RUNTIME_JIT_JIT_INL_H_

#include "android-base/macros.h"
#include "jit/jit.h"

#include "art_method.h"
#include "base/bit_utils.h"
#include "thread.h"
#include "runtime-inl.h"

namespace art HIDDEN {
namespace jit {

inline void Jit::AddSamples(Thread* self, ArtMethod* method) {
  // `hotness_count_` should always be 0 for intrinsics (which are considered hot from the first
  // call), and for memory shared methods which use `shared_method_hotness`.
  DCHECK_IMPLIES(method->IsIntrinsic(), method->CounterIsHot());
  DCHECK_IMPLIES(method->IsMemorySharedMethod(), method->CounterIsHot());

  if (method->CounterIsHot()) {
    if (method->IsMemorySharedMethod()) {
      // Intrinsics do not use `shared_method_hotness`.
      if (!method->IsIntrinsic()) {
        if (self->DecrementSharedMethodHotness() == 0) {
          self->ResetSharedMethodHotness();
        } else {
          return;
        }
      }
    } else {
      method->ResetCounter(Runtime::Current()->GetJITOptions()->GetWarmupThreshold());
    }
    MaybeEnqueueCompilation(method, self);
  } else {
    method->UpdateCounter(1);
  }
}

}  // namespace jit
}  // namespace art

#endif  // ART_RUNTIME_JIT_JIT_INL_H_
