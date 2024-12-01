/*
 * Copyright (C) 2023 The Android Open Source Project
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

/** Binder utilities. Consumers of this library must link to "libbinder_ndk". */

#ifndef ART_LIBARTTOOLS_INCLUDE_BINDER_UTILS_TOOLS_BINDER_UTILS_H_
#define ART_LIBARTTOOLS_INCLUDE_BINDER_UTILS_TOOLS_BINDER_UTILS_H_

#include <string>

#include "android-base/result.h"
#include "android-base/strings.h"
#include "android/binder_auto_utils.h"

namespace art {
namespace tools {

static std::string EscapeErrorMessage(const std::string& message) {
  return android::base::StringReplace(message, std::string("\0", /*n=*/1), "\\0", /*all=*/true);
}

// Indicates an error that should never happen (e.g., illegal arguments passed by service-art
// internally). System server should crash if this kind of error happens.
[[maybe_unused]] static ndk::ScopedAStatus Fatal(const std::string& message) {
  return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_STATE,
                                                          EscapeErrorMessage(message).c_str());
}

// Indicates an error that service-art should handle (e.g., I/O errors, sub-process crashes).
// The scope of the error depends on the function that throws it, so service-art should catch the
// error at every call site and take different actions.
// Ideally, this should be a checked exception or an additional return value that forces service-art
// to handle it, but `ServiceSpecificException` (a separate runtime exception type) is the best
// approximate we have given the limitation of Java and Binder.
[[maybe_unused]] static ndk::ScopedAStatus NonFatal(const std::string& message) {
  constexpr int32_t kServiceArtNonFatalErrorCode = 1;
  return ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(
      kServiceArtNonFatalErrorCode, EscapeErrorMessage(message).c_str());
}

}  // namespace tools
}  // namespace art

#define OR_RETURN_ERROR(func, expr)                           \
  ({                                                          \
    decltype(expr)&& __or_return_error_tmp = (expr);          \
    if (!__or_return_error_tmp.ok()) {                        \
      return (func)(__or_return_error_tmp.error().message()); \
    }                                                         \
    std::move(__or_return_error_tmp).value();                 \
  })

#define OR_RETURN_FATAL(expr)     OR_RETURN_ERROR(Fatal, expr)
#define OR_RETURN_NON_FATAL(expr) OR_RETURN_ERROR(NonFatal, expr)
#define OR_LOG_AND_RETURN_OK(expr)     \
  OR_RETURN_ERROR(                     \
      [](const std::string& message) { \
        LOG(ERROR) << message;         \
        return ScopedAStatus::ok();    \
      },                               \
      expr)

#define ASSERT_STATUS_OK(expr)                                                               \
  ({                                                                                         \
    ndk::ScopedAStatus __assert_status_ok_status = (expr);                                   \
    ASSERT_TRUE(__assert_status_ok_status.isOk()) << __assert_status_ok_status.getMessage(); \
  })

#endif  // ART_LIBARTTOOLS_INCLUDE_BINDER_UTILS_TOOLS_BINDER_UTILS_H_
