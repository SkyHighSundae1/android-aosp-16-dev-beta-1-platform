/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "tools/tools.h"

#include <errno.h>
#include <fnmatch.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <functional>
#include <regex>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include "android-base/errors.h"
#include "android-base/file.h"
#include "android-base/function_ref.h"
#include "android-base/logging.h"
#include "android-base/process.h"
#include "android-base/result.h"
#include "android-base/strings.h"
#include "android-base/unique_fd.h"
#include "base/macros.h"
#include "base/pidfd.h"
#include "fstab/fstab.h"

namespace art {
namespace tools {

namespace {

using ::android::base::AllPids;
using ::android::base::ConsumeSuffix;
using ::android::base::function_ref;
using ::android::base::ReadFileToString;
using ::android::base::Readlink;
using ::android::base::Result;
using ::android::base::unique_fd;
using ::android::fs_mgr::Fstab;
using ::android::fs_mgr::FstabEntry;
using ::android::fs_mgr::ReadFstabFromProcMounts;
using ::std::placeholders::_1;

uint64_t MilliTime() {
  timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<uint64_t>(now.tv_sec) * UINT64_C(1000) + now.tv_nsec / UINT64_C(1000000);
}

// Returns true if `path_prefix` matches `pattern` or can be a prefix of a path that matches
// `pattern` (i.e., `path_prefix` represents a directory that may contain a file whose path matches
// `pattern`).
bool PartialMatch(const std::filesystem::path& pattern, const std::filesystem::path& path_prefix) {
  for (std::filesystem::path::const_iterator pattern_it = pattern.begin(),
                                             path_prefix_it = path_prefix.begin();
       ;  // NOLINT
       pattern_it++, path_prefix_it++) {
    if (path_prefix_it == path_prefix.end()) {
      return true;
    }
    if (pattern_it == pattern.end()) {
      return false;
    }
    if (*pattern_it == "**") {
      return true;
    }
    if (fnmatch(pattern_it->c_str(), path_prefix_it->c_str(), /*flags=*/0) != 0) {
      return false;
    }
  }
}

bool FullMatchRecursive(const std::filesystem::path& pattern,
                        std::filesystem::path::const_iterator pattern_it,
                        const std::filesystem::path& path,
                        std::filesystem::path::const_iterator path_it,
                        bool double_asterisk_visited = false) {
  if (pattern_it == pattern.end() && path_it == path.end()) {
    return true;
  }
  if (pattern_it == pattern.end()) {
    return false;
  }
  if (*pattern_it == "**") {
    DCHECK(!double_asterisk_visited);
    std::filesystem::path::const_iterator next_pattern_it = pattern_it;
    return FullMatchRecursive(
               pattern, ++next_pattern_it, path, path_it, /*double_asterisk_visited=*/true) ||
           (path_it != path.end() && FullMatchRecursive(pattern, pattern_it, path, ++path_it));
  }
  if (path_it == path.end()) {
    return false;
  }
  if (fnmatch(pattern_it->c_str(), path_it->c_str(), /*flags=*/0) != 0) {
    return false;
  }
  return FullMatchRecursive(pattern, ++pattern_it, path, ++path_it);
}

// Returns true if `path` fully matches `pattern`.
bool FullMatch(const std::filesystem::path& pattern, const std::filesystem::path& path) {
  return FullMatchRecursive(pattern, pattern.begin(), path, path.begin());
}

void MatchGlobRecursive(const std::vector<std::filesystem::path>& patterns,
                        const std::filesystem::path& root_dir,
                        /*out*/ std::vector<std::string>* results) {
  std::error_code ec;
  for (auto it = std::filesystem::recursive_directory_iterator(
           root_dir, std::filesystem::directory_options::skip_permission_denied, ec);
       !ec && it != std::filesystem::end(it);
       it.increment(ec)) {
    const std::filesystem::directory_entry& entry = *it;
    if (std::none_of(patterns.begin(), patterns.end(), std::bind(PartialMatch, _1, entry.path()))) {
      // Avoid unnecessary I/O and SELinux denials.
      it.disable_recursion_pending();
      continue;
    }
    std::error_code ec2;
    if (entry.is_regular_file(ec2) &&
        std::any_of(patterns.begin(), patterns.end(), std::bind(FullMatch, _1, entry.path()))) {
      results->push_back(entry.path());
    }
    if (ec2) {
      // It's expected that we don't have permission to stat some dirs/files, and we don't care
      // about them.
      if (ec2.value() != EACCES) {
        LOG(ERROR) << ART_FORMAT("Unable to lstat '{}': {}", entry.path().string(), ec2.message());
      }
      continue;
    }
  }
  if (ec) {
    LOG(ERROR) << ART_FORMAT("Unable to walk through '{}': {}", root_dir.string(), ec.message());
  }
}

}  // namespace

std::vector<std::string> Glob(const std::vector<std::string>& patterns, std::string_view root_dir) {
  std::vector<std::filesystem::path> parsed_patterns;
  parsed_patterns.reserve(patterns.size());
  for (std::string_view pattern : patterns) {
    parsed_patterns.emplace_back(pattern);
  }
  std::vector<std::string> results;
  MatchGlobRecursive(parsed_patterns, root_dir, &results);
  return results;
}

std::string EscapeGlob(const std::string& str) {
  return std::regex_replace(str, std::regex(R"re(\*|\?|\[)re"), "[$&]");
}

bool PathStartsWith(std::string_view path, std::string_view prefix) {
  CHECK(!prefix.empty() && !path.empty() && prefix[0] == '/' && path[0] == '/')
      << ART_FORMAT("path={}, prefix={}", path, prefix);
  ConsumeSuffix(&prefix, "/");
  return path.starts_with(prefix) &&
         (path.length() == prefix.length() || path[prefix.length()] == '/');
}

static Result<std::vector<FstabEntry>> GetProcMountsMatches(
    function_ref<bool(std::string_view)> predicate) {
  Fstab fstab;
  if (!ReadFstabFromProcMounts(&fstab)) {
    return Errorf("Failed to read fstab from /proc/mounts");
  }
  std::vector<FstabEntry> entries;
  for (FstabEntry& entry : fstab) {
    // Ignore swap areas as a swap area doesn't have a meaningful `mount_point` (a.k.a., `fs_file`)
    // field, according to fstab(5). In addition, ignore any other entries whose mount points are
    // not absolute paths, just in case there are other fs types that also have an meaningless mount
    // point.
    if (entry.fs_type == "swap" || !entry.mount_point.starts_with('/')) {
      continue;
    }
    if (predicate(entry.mount_point)) {
      entries.push_back(std::move(entry));
    }
  }
  return entries;
}

Result<std::vector<FstabEntry>> GetProcMountsAncestorsOfPath(std::string_view path) {
  return GetProcMountsMatches(
      [&](std::string_view mount_point) { return PathStartsWith(path, mount_point); });
}

Result<std::vector<FstabEntry>> GetProcMountsDescendantsOfPath(std::string_view path) {
  return GetProcMountsMatches(
      [&](std::string_view mount_point) { return PathStartsWith(mount_point, path); });
}

Result<void> EnsureNoProcessInDir(const std::string& dir, uint32_t timeout_ms, bool try_kill) {
  // Pairs of pid and process name, indexed by pidfd.
  std::unordered_map<int, std::pair<pid_t, std::string>> running_processes;
  std::vector<struct pollfd> pollfds;
  std::vector<unique_fd> pidfds;

  for (pid_t pid : AllPids()) {
    std::string exe;
    if (!Readlink(ART_FORMAT("/proc/{}/exe", pid), &exe)) {
      // The caller may not have access to all processes. That's okay. When using this method, we
      // must grant the caller access to the processes that we are interested in.
      continue;
    }

    if (PathStartsWith(exe, dir)) {
      unique_fd pidfd = PidfdOpen(pid, /*flags=*/0);
      if (pidfd < 0) {
        if (errno == ESRCH) {
          // The process has gone now.
          continue;
        }
        return ErrnoErrorf("Failed to pidfd_open {}", pid);
      }

      std::string name;
      if (!ReadFileToString(ART_FORMAT("/proc/{}/comm", pid), &name)) {
        PLOG(WARNING) << "Failed to get process name for pid " << pid;
      }
      size_t pos = name.find_first_of("\n\0");
      if (pos != std::string::npos) {
        name.resize(pos);
      }
      LOG(INFO) << ART_FORMAT(
          "Process '{}' (pid: {}) is still running. Waiting for it to exit", name, pid);

      struct pollfd& pollfd = pollfds.emplace_back();
      pollfd.fd = pidfd.get();
      pollfd.events = POLLIN;

      running_processes[pidfd.get()] = std::make_pair(pid, std::move(name));
      pidfds.push_back(std::move(pidfd));
    }
  }

  auto wait_for_processes = [&]() -> Result<void> {
    uint64_t start_time_ms = MilliTime();
    uint64_t remaining_timeout_ms = timeout_ms;
    while (!running_processes.empty() && remaining_timeout_ms > 0) {
      int poll_ret = TEMP_FAILURE_RETRY(poll(pollfds.data(), pollfds.size(), remaining_timeout_ms));
      if (poll_ret < 0) {
        return ErrnoErrorf("Failed to poll pidfd's");
      }
      if (poll_ret == 0) {
        // Timeout.
        break;
      }
      uint64_t elapsed_time_ms = MilliTime() - start_time_ms;
      for (struct pollfd& pollfd : pollfds) {
        if (pollfd.fd < 0) {
          continue;
        }
        if ((pollfd.revents & POLLIN) != 0) {
          const auto& [pid, name] = running_processes[pollfd.fd];
          LOG(INFO) << ART_FORMAT(
              "Process '{}' (pid: {}) exited in {}ms", name, pid, elapsed_time_ms);
          running_processes.erase(pollfd.fd);
          pollfd.fd = -1;
        }
      }
      remaining_timeout_ms = timeout_ms - elapsed_time_ms;
    }
    return {};
  };

  OR_RETURN(wait_for_processes());

  bool process_killed = false;
  for (const auto& [pidfd, pair] : running_processes) {
    const auto& [pid, name] = pair;
    LOG(ERROR) << ART_FORMAT(
        "Process '{}' (pid: {}) is still running after {}ms", name, pid, timeout_ms);
    if (try_kill) {
      LOG(INFO) << ART_FORMAT("Killing '{}' (pid: {})", name, pid);
      if (kill(pid, SIGKILL) != 0) {
        PLOG(ERROR) << ART_FORMAT("Failed to kill '{}' (pid: {})", name, pid);
      }
      process_killed = true;
    }
  }

  if (process_killed) {
    // Wait another round for processes to exit after being killed.
    OR_RETURN(wait_for_processes());
  }
  if (!running_processes.empty()) {
    return Errorf("Some process(es) are still running after {}ms", timeout_ms);
  }
  return {};
}

}  // namespace tools
}  // namespace art
