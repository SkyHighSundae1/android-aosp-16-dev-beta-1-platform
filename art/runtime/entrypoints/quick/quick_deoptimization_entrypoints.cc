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

#include "arch/context.h"
#include "base/logging.h"  // For VLOG_IS_ON.
#include "base/mutex.h"
#include "callee_save_frame.h"
#include "interpreter/interpreter.h"
#include "obj_ptr-inl.h"  // TODO: Find the other include that isn't complete, and clean this up.
#include "quick_exception_handler.h"
#include "runtime.h"
#include "thread.h"

namespace art HIDDEN {

extern "C" Context* artDeoptimize(Thread* self, bool skip_method_exit_callbacks)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  ScopedQuickEntrypointChecks sqec(self);
  std::unique_ptr<Context> context = self->Deoptimize(DeoptimizationKind::kFullFrame,
                                                      /*single_frame=*/ false,
                                                      skip_method_exit_callbacks);
  DCHECK(context != nullptr);
  return context.release();
}

// This is called directly from compiled code by an HDeoptimize.
extern "C" Context* artDeoptimizeFromCompiledCode(DeoptimizationKind kind, Thread* self)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  ScopedQuickEntrypointChecks sqec(self);
  // Before deoptimizing to interpreter, we must push the deoptimization context.
  JValue return_value;
  return_value.SetJ(0);  // we never deoptimize from compiled code with an invoke result.
  self->PushDeoptimizationContext(return_value,
                                  /* is_reference= */ false,
                                  self->GetException(),
                                  /* from_code= */ true,
                                  DeoptimizationMethodType::kDefault);
  // Deopting from compiled code, so method exit haven't run yet. Don't skip method exit callbacks
  // if required.
  std::unique_ptr<Context> context = self->Deoptimize(kind,
                                                      /*single_frame=*/ true,
                                                      /* skip_method_exit_callbacks= */ false);
  DCHECK(context != nullptr);
  return context.release();
}

}  // namespace art
