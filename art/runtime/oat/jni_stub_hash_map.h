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

#ifndef ART_RUNTIME_OAT_JNI_STUB_HASH_MAP_H_
#define ART_RUNTIME_OAT_JNI_STUB_HASH_MAP_H_

#include <memory>
#include <string_view>

#include "arch/instruction_set.h"
#include "base/hash_map.h"
#include "base/locks.h"

namespace art HIDDEN {

class ArtMethod;

class JniStubKey {
 public:
  JniStubKey() = default;
  JniStubKey(const JniStubKey& other) = default;
  JniStubKey& operator=(const JniStubKey& other) = default;

  JniStubKey(uint32_t flags, std::string_view shorty);

  explicit JniStubKey(ArtMethod* method) REQUIRES_SHARED(Locks::mutator_lock_);

  uint32_t Flags() const {
    return flags_;
  }

  std::string_view Shorty() const {
    return shorty_;
  }

  bool IsEmpty() const {
    return Shorty().empty();
  }

  void MakeEmpty() {
    shorty_ = {};
  }

 private:
  uint32_t flags_;
  std::string_view shorty_;
};

template <typename Value>
class JniStubKeyEmpty {
 public:
  bool IsEmpty(const std::pair<JniStubKey, Value>& pair) const {
    return pair.first.IsEmpty();
  }

  void MakeEmpty(std::pair<JniStubKey, Value>& pair) {
    pair.first.MakeEmpty();
  }
};

using JniStubKeyHashFunction = size_t (*)(const JniStubKey& key);

class JniStubKeyHash {
 public:
  EXPORT explicit JniStubKeyHash(InstructionSet isa);

  size_t operator()(const JniStubKey& key) const {
    return hash_func_(key);
  }

 private:
  JniStubKeyHashFunction hash_func_;
};

using JniStubKeyEqualsFunction = bool (*)(const JniStubKey& lhs, const JniStubKey& rhs);

class JniStubKeyEquals {
 public:
  EXPORT explicit JniStubKeyEquals(InstructionSet isa);

  bool operator()(const JniStubKey& lhs, const JniStubKey& rhs) const {
    return equals_func_(lhs, rhs);
  }

 private:
  JniStubKeyEqualsFunction equals_func_;
};

template <typename Value,
          typename Alloc = std::allocator<std::pair<JniStubKey, Value>>>
using JniStubHashMap =
    HashMap<JniStubKey, Value, JniStubKeyEmpty<Value>, JniStubKeyHash, JniStubKeyEquals>;

}  // namespace art

#endif  // ART_RUNTIME_OAT_JNI_STUB_HASH_MAP_H_
