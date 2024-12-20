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

#ifndef ART_ODREFRESH_ODR_ARTIFACTS_H_
#define ART_ODREFRESH_ODR_ARTIFACTS_H_

#include <iosfwd>
#include <string>

#include "base/file_utils.h"

namespace art {
namespace odrefresh {

// A grouping of odrefresh generated artifacts.
class OdrArtifacts {
 public:
  static OdrArtifacts ForBootImage(const std::string& image_path) {
    return OdrArtifacts(image_path, /*image_kind=*/"image", /*aot_extension=*/kOatExtension);
  }

  static OdrArtifacts ForSystemServer(const std::string& image_path) {
    return OdrArtifacts(image_path, /*image_kind=*/"app-image", /*aot_extension=*/kOdexExtension);
  }

  const std::string& ImagePath() const { return image_path_; }
  const char* ImageKind() const { return image_kind_; }
  const std::string& OatPath() const { return oat_path_; }
  const std::string& VdexPath() const { return vdex_path_; }

 private:
  OdrArtifacts(const std::string& image_path, const char* image_kind, const char* aot_extension)
      : image_path_{image_path},
        image_kind_{image_kind},
        oat_path_{ReplaceFileExtension(image_path, aot_extension)},
        vdex_path_{ReplaceFileExtension(image_path, kVdexExtension)} {}

  OdrArtifacts() = delete;
  OdrArtifacts(const OdrArtifacts&) = delete;
  OdrArtifacts& operator=(const OdrArtifacts&) = delete;

  const std::string image_path_;
  const char* image_kind_;
  const std::string oat_path_;
  const std::string vdex_path_;
};

}  // namespace odrefresh
}  // namespace art

#endif  // ART_ODREFRESH_ODR_ARTIFACTS_H_
