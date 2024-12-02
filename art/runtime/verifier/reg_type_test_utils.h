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

#ifndef ART_RUNTIME_VERIFIER_REG_TYPE_TEST_UTILS_H_
#define ART_RUNTIME_VERIFIER_REG_TYPE_TEST_UTILS_H_

#include "reg_type.h"

namespace art HIDDEN {
namespace verifier {

// Helper class to avoid thread safety analysis errors from gtest's `operator<<`
// instantiation for `RegType` not being marked as holding the mutator lock.
class RegTypeWrapper {
 public:
  explicit RegTypeWrapper(const RegType& reg_type) : reg_type_(reg_type) {}
 private:
  const RegType& reg_type_;
  friend std::ostream& operator<<(std::ostream& os, const RegTypeWrapper& rtw);
};

inline std::ostream& operator<<(std::ostream& os, const RegTypeWrapper& rtw)
    NO_THREAD_SAFETY_ANALYSIS {
  return os << rtw.reg_type_;
}

}  // namespace verifier
}  // namespace art

#endif  // ART_RUNTIME_VERIFIER_REG_TYPE_TEST_UTILS_H_
