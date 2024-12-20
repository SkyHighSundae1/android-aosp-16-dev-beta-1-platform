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

#ifndef ART_RUNTIME_INTERPRETER_INTERPRETER_SWITCH_IMPL_H_
#define ART_RUNTIME_INTERPRETER_INTERPRETER_SWITCH_IMPL_H_

#include "base/locks.h"
#include "base/macros.h"
#include "dex/dex_file.h"
#include "dex/code_item_accessors.h"
#include "jvalue.h"
#include "obj_ptr.h"

namespace art HIDDEN {

class ShadowFrame;
class Thread;

namespace interpreter {

// Group all the data that is needed in the switch interpreter.
// We need to pass it to the hand-written assembly and back,
// so it is easier to pass it through a single pointer.
// Similarly, returning the JValue type would be non-trivial.
struct SwitchImplContext {
  Thread* self;
  const CodeItemDataAccessor& accessor;
  ShadowFrame& shadow_frame;
  JValue& result_register;
  JValue result;
};

// The actual internal implementation of the switch interpreter.
template<bool transaction_active>
void ExecuteSwitchImplCpp(SwitchImplContext* ctx)
  REQUIRES_SHARED(Locks::mutator_lock_);

// Hand-written assembly method which wraps the C++ implementation,
// while defining the DEX PC in the CFI so that libunwind can resolve it.
extern "C" void ExecuteSwitchImplAsm(
    SwitchImplContext* ctx, const void* impl, const uint16_t* dexpc)
    REQUIRES_SHARED(Locks::mutator_lock_);

// Wrapper around the switch interpreter which ensures we can unwind through it.
ALWAYS_INLINE inline JValue ExecuteSwitchImpl(Thread* self,
                                              const CodeItemDataAccessor& accessor,
                                              ShadowFrame& shadow_frame,
                                              JValue result_register,
                                              const void* switch_impl_cpp)
  REQUIRES_SHARED(Locks::mutator_lock_) {
  SwitchImplContext ctx {
    .self = self,
    .accessor = accessor,
    .shadow_frame = shadow_frame,
    .result_register = result_register,
    .result = JValue(),
  };
  const uint16_t* dex_pc = ctx.accessor.Insns();
  ExecuteSwitchImplAsm(&ctx, switch_impl_cpp, dex_pc);
  return ctx.result;
}

}  // namespace interpreter
}  // namespace art

#endif  // ART_RUNTIME_INTERPRETER_INTERPRETER_SWITCH_IMPL_H_
