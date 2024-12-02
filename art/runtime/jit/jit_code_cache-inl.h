/*
 * Copyright 2024 The Android Open Source Project
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

#ifndef ART_RUNTIME_JIT_JIT_CODE_CACHE_INL_H_
#define ART_RUNTIME_JIT_JIT_CODE_CACHE_INL_H_

#include "jit/jit_code_cache.h"

#include "base/macros.h"
#include "read_barrier.h"
#include "thread.h"
#include "well_known_classes-inl.h"

namespace art HIDDEN {

class ArtMethod;

namespace jit {

template<typename RootVisitorType>
EXPORT void JitCodeCache::VisitRootTables(ArtMethod* method, RootVisitorType& visitor) {
  if (method->IsNative()) {
    return;
  }

  Thread* self = Thread::Current();
  ScopedDebugDisallowReadBarriers sddrb(self);
  MutexLock mu(self, *Locks::jit_lock_);

  auto code_ptrs_it = method_code_map_reversed_.find(method);
  if (code_ptrs_it == method_code_map_reversed_.end()) {
    return;
  }

  const std::vector<const void*>& code_ptrs = code_ptrs_it->second;

  for (const void* code_ptr : code_ptrs) {
    uint32_t number_of_roots = 0;
    const uint8_t* root_table = GetRootTable(code_ptr, &number_of_roots);
    uint8_t* roots_data = private_region_.IsInDataSpace(root_table)
        ? private_region_.GetWritableDataAddress(root_table)
        : shared_region_.GetWritableDataAddress(root_table);
    GcRoot<mirror::Object>* roots = reinterpret_cast<GcRoot<mirror::Object>*>(roots_data);
    for (uint32_t i = 0; i < number_of_roots; ++i) {
      // This does not need a read barrier because this is called by GC.
      mirror::Object* object = roots[i].Read<kWithoutReadBarrier>();
      if (object == nullptr ||
          object == Runtime::GetWeakClassSentinel() ||
          object->IsString<kDefaultVerifyFlags>() ||
          object->IsClass<kDefaultVerifyFlags>()) {
        continue;
      }
      // We don't need to visit j.l.Class and j.l.String and the only remaining possible
      // objects are MethodType-s.
      ObjPtr<mirror::Class> method_type_class =
          WellKnownClasses::java_lang_invoke_MethodType.Get<kWithoutReadBarrier>();
      ObjPtr<mirror::Class> klass =
          object->GetClass<kDefaultVerifyFlags, kWithoutReadBarrier>();
      DCHECK(klass == method_type_class ||
             klass == ReadBarrier::IsMarked(method_type_class.Ptr()) ||
             ReadBarrier::IsMarked(klass.Ptr()) == method_type_class);

      visitor.VisitRoot(roots[i].AddressWithoutBarrier());
    }
  }
}

}  // namespace jit
}  // namespace art

#endif  // ART_RUNTIME_JIT_JIT_CODE_CACHE_INL_H_


