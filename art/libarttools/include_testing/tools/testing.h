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

#ifndef ART_LIBARTTOOLS_INCLUDE_TESTING_TOOLS_TESTING_H_
#define ART_LIBARTTOOLS_INCLUDE_TESTING_TOOLS_TESTING_H_

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "android-base/logging.h"
#include "android-base/scopeguard.h"
#include "android-base/strings.h"
#include "base/file_utils.h"
#include "base/globals.h"
#include "base/macros.h"

namespace art {
namespace tools {

using ::android::base::make_scope_guard;
using ::android::base::ScopeGuard;
using ::android::base::Split;

[[maybe_unused]] static std::string GetArtBin(const std::string& name) {
  CHECK(kIsTargetAndroid);
  return ART_FORMAT("{}/bin/{}", GetArtRoot(), name);
}

[[maybe_unused]] static std::string GetBin(const std::string& name) {
  for (const std::string& path : Split(getenv("PATH"), ":")) {
    if (std::filesystem::exists(path + "/" + name)) {
      return path + "/" + name;
    }
  }
  LOG(FATAL) << ART_FORMAT("Binary '{}' not found", name);
  UNREACHABLE();
}

// Executes the command. If `wait` is true, waits for the process to finish and keeps it in a
// waitable state; otherwise, returns immediately after fork. When the current scope exits, destroys
// the process.
[[maybe_unused]] static std::pair<pid_t, std::unique_ptr<ScopeGuard<std::function<void()>>>>
ScopedExec(std::vector<std::string>& args, bool wait) {
  std::vector<char*> execv_args;
  execv_args.reserve(args.size() + 1);
  for (std::string& arg : args) {
    execv_args.push_back(arg.data());
  }
  execv_args.push_back(nullptr);

  pid_t pid = fork();
  if (pid == 0) {
    execv(execv_args[0], execv_args.data());
    PLOG(FATAL) << "Failed to call execv";
    UNREACHABLE();
  } else if (pid > 0) {
    if (wait) {
      siginfo_t info;
      CHECK_EQ(TEMP_FAILURE_RETRY(waitid(P_PID, pid, &info, WEXITED | WNOWAIT)), 0);
      CHECK_EQ(info.si_code, CLD_EXITED);
      CHECK_EQ(info.si_status, 0);
    }
    std::function<void()> cleanup([=] {
      siginfo_t info;
      if (!wait) {
        CHECK_EQ(kill(pid, SIGKILL), 0);
      }
      CHECK_EQ(TEMP_FAILURE_RETRY(waitid(P_PID, pid, &info, WEXITED)), 0);
    });
    return std::make_pair(
        pid,
        std::make_unique<ScopeGuard<std::function<void()>>>(make_scope_guard(std::move(cleanup))));
  } else {
    PLOG(FATAL) << "Failed to call fork";
    UNREACHABLE();
  }
}

}  // namespace tools
}  // namespace art

#endif  // ART_LIBARTTOOLS_INCLUDE_TESTING_TOOLS_TESTING_H_
