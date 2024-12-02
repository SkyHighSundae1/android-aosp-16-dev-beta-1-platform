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

#ifndef ART_RUNTIME_SCOPED_ASSERT_NO_TRANSACTION_CHECKS_H_
#define ART_RUNTIME_SCOPED_ASSERT_NO_TRANSACTION_CHECKS_H_

#include "thread-current-inl.h"

namespace art {

class ScopedAssertNoTransactionChecks {
 public:
  explicit ScopedAssertNoTransactionChecks(const char* cause)
      : self_(kIsDebugBuild ? Thread::Current() : nullptr),
        old_cause_(self_ != nullptr ? self_->tlsPtr_.last_no_transaction_checks_cause : nullptr) {
    DCHECK(cause != nullptr);
    if (kIsDebugBuild && self_ != nullptr) {
      self_->tlsPtr_.last_no_transaction_checks_cause = cause;
    }
  }

  ~ScopedAssertNoTransactionChecks() {
    if (kIsDebugBuild && self_ != nullptr) {
      self_->tlsPtr_.last_no_transaction_checks_cause = old_cause_;
    }
  }

 private:
  Thread* const self_;
  const char* const old_cause_;
};

}  // namespace art

#endif  // ART_RUNTIME_SCOPED_ASSERT_NO_TRANSACTION_CHECKS_H_
