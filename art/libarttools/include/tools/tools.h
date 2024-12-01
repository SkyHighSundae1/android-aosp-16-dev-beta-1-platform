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

#ifndef ART_LIBARTTOOLS_INCLUDE_TOOLS_TOOLS_H_
#define ART_LIBARTTOOLS_INCLUDE_TOOLS_TOOLS_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "android-base/result.h"
#include "fstab/fstab.h"

namespace art {
namespace tools {

// Searches in a filesystem, starting from `root_dir`. Returns all regular files (i.e., excluding
// directories, symlinks, etc.) that match at least one pattern in `patterns`. Each pattern is an
// absolute path that contains zero or more wildcards. The scan does not follow symlinks to
// directories.
//
// Supported wildcards are:
// - Those documented in glob(7)
// - '**': Matches zero or more path elements. This is only recognised by itself as a path segment.
//
// For simplicity and efficiency, at most one '**' is allowed.
std::vector<std::string> Glob(const std::vector<std::string>& patterns,
                              std::string_view root_dir = "/");

// Escapes a string so that it's not recognized as a wildcard pattern for `Glob`.
std::string EscapeGlob(const std::string& str);

// Returns true if `path` starts with `prefix` (i.e., if `prefix` represents a directory that
// contains a file/directory at `path`, or if `prefix` and `path` represents the same
// file/directory). Only supports absolute paths.
bool PathStartsWith(std::string_view path, std::string_view prefix);

// Returns the fstab entries in /proc/mounts where the mount point is a prefix of the given path.
android::base::Result<std::vector<android::fs_mgr::FstabEntry>> GetProcMountsAncestorsOfPath(
    std::string_view path);

// Returns the fstab entries in /proc/mounts where the given path is a prefix of the mount point.
android::base::Result<std::vector<android::fs_mgr::FstabEntry>> GetProcMountsDescendantsOfPath(
    std::string_view path);

// See `ArtJni.ensureNoProcessInDir`.
android::base::Result<void> EnsureNoProcessInDir(const std::string& dir,
                                                 uint32_t timeout_ms,
                                                 bool try_kill);

}  // namespace tools
}  // namespace art

#endif  // ART_LIBARTTOOLS_INCLUDE_TOOLS_TOOLS_H_
