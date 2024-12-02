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

#include "reference_type_info.h"

#include "scoped_thread_state_change-inl.h"
#include "thread-current-inl.h"

namespace art HIDDEN {

void ReferenceTypeInfo::DCheckValidTypeInfo(TypeHandle type_handle, bool is_exact) {
  if (kIsDebugBuild) {
    ScopedObjectAccess soa(Thread::Current());
    CHECK(IsValidHandle(type_handle));
    if (!is_exact) {
      CHECK(!type_handle->CannotBeAssignedFromOtherTypes())
          << "Callers of ReferenceTypeInfo::Create should ensure is_exact is properly computed";
    }
  }
}

std::ostream& operator<<(std::ostream& os, const ReferenceTypeInfo& rhs) {
  ScopedObjectAccess soa(Thread::Current());
  os << "["
     << " is_valid=" << rhs.IsValid()
     << " type=" << (!rhs.IsValid() ? "?" : mirror::Class::PrettyClass(rhs.GetTypeHandle().Get()))
     << " is_exact=" << rhs.IsExact()
     << " ]";
  return os;
}

}  // namespace art
