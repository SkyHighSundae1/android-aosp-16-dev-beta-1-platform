/*
 * Copyright (C) 2015 The Android Open Source Project
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

#ifndef ART_RUNTIME_EXPERIMENTAL_FLAGS_H_
#define ART_RUNTIME_EXPERIMENTAL_FLAGS_H_

#include <ostream>

#include "base/casts.h"
#include "base/macros.h"

namespace art HIDDEN {

// Possible experimental features that might be enabled.
enum class ExperimentalFlags : uint32_t {
  kNone           = 0x0000,
  kMethodHandles  = 0x0004,  // 0b00000100
};

constexpr ExperimentalFlags operator|(ExperimentalFlags a, ExperimentalFlags b) {
  return enum_cast<ExperimentalFlags>(enum_cast(a) | enum_cast(b));
}

constexpr ExperimentalFlags operator&(ExperimentalFlags a, ExperimentalFlags b) {
  return enum_cast<ExperimentalFlags>(enum_cast(a) & enum_cast(b));
}

inline std::ostream& operator<<(std::ostream& stream, ExperimentalFlags e) {
  bool started = false;
  if ((e & ExperimentalFlags::kMethodHandles) != ExperimentalFlags::kNone) {
    stream << (started ? "|" : "") << "kMethodHandles";
    started = true;
  }
  if (!started) {
    stream << "kNone";
  }
  return stream;
}

}  // namespace art

#endif  // ART_RUNTIME_EXPERIMENTAL_FLAGS_H_
