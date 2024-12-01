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

#include "inline_cache_format_util.h"

#include "profile/profile_compilation_info.h"

namespace art {

std::string GetInlineCacheLine(const SafeMap<TypeReference,
                                             FlattenProfileData::ItemMetadata::InlineCacheInfo,
                                             TypeReferenceValueComparator>& inline_cache) {
  if (inline_cache.empty()) {
    return "";
  }
  std::ostringstream dump_ic;
  dump_ic << kProfileParsingInlineChacheSep;
  for (const auto& [target_ref, inline_cache_data] : inline_cache) {
    dump_ic << kProfileParsingInlineChacheTargetSep;
    dump_ic << target_ref.dex_file->GetTypeDescriptor(
        target_ref.dex_file->GetTypeId(target_ref.TypeIndex()));
    if (inline_cache_data.is_missing_types_) {
      dump_ic << kMissingTypesMarker;
    } else if (inline_cache_data.is_megamorphic_) {
      dump_ic << kMegamorphicTypesMarker;
    } else {
      bool first = true;
      for (const std::string& cls : inline_cache_data.classes_) {
        if (!first) {
          dump_ic << kProfileParsingTypeSep;
        }
        first = false;
        dump_ic << cls;
      }
    }
  }
  return dump_ic.str();
}

}  // namespace art
