/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "runtime_entrypoints_list.h"

#include "string_builder_append.h"
#include "obj_ptr-inl.h"

namespace art HIDDEN {

extern "C" mirror::String* artStringBuilderAppend(uint32_t format,
                                                  const uint32_t* args,
                                                  Thread* self)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  return StringBuilderAppend::AppendF(format, args, self).Ptr();
}

}  // namespace art
