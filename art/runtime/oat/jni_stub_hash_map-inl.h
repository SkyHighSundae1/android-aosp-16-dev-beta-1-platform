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

#ifndef ART_RUNTIME_OAT_JNI_STUB_HASH_MAP_INL_H_
#define ART_RUNTIME_OAT_JNI_STUB_HASH_MAP_INL_H_

#include "jni_stub_hash_map.h"

#include "art_method-inl.h"

namespace art {

inline JniStubKey::JniStubKey(uint32_t flags, std::string_view shorty)
    : flags_(flags & (kAccStatic | kAccSynchronized | kAccFastNative | kAccCriticalNative)),
      shorty_(shorty) {
  DCHECK(ArtMethod::IsNative(flags));
}

inline JniStubKey::JniStubKey(ArtMethod* method)
    : JniStubKey(method->GetAccessFlags(), method->GetShortyView()) {}

}  // namespace art

#endif  // ART_RUNTIME_OAT_JNI_STUB_HASH_MAP_INL_H_
