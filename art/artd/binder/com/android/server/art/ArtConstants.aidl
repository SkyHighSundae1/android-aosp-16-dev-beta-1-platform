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

package com.android.server.art;

/**
 * Constants used by ART Service Java code that must be kept in sync with those in ART native code.
 *
 * A test in art/artd/artd_test.cc checks that the constants are in sync.
 *
 * @hide
 */
parcelable ArtConstants {
    /**
     * A special compilation reason to indicate that only the VDEX file is usable.
     *
     * This isn't a valid reason to feed into DexoptParams.
     *
     * Keep in sync with {@code kReasonVdex} in art/runtime/oat_file.h.
     */
    const @utf8InCpp String REASON_VDEX = "vdex";

    /**
     * The file extension of the dex metadata file.
     *
     * Keep in sync with {@code kDmExtension} in art/libartbase/base/file_utils.h.
     */
    const @utf8InCpp String DEX_METADATA_FILE_EXT = ".dm";

    /**
     * The file extension of the profile file.
     *
     * Currently, there is no counterpart in the runtime code because the profile paths are passed
     * from the framework.
     */
    const @utf8InCpp String PROFILE_FILE_EXT = ".prof";

    /**
     * The file extension of the secure dex metadata file.
     *
     * Keep in sync with {@code kSdmExtension} in art/libartbase/base/file_utils.h.
     */
    const @utf8InCpp String SECURE_DEX_METADATA_FILE_EXT = ".sdm";

    /**
     * The name of the profile entry in the dex metadata file.
     *
     * Keep in sync with {@code ProfileCompilationInfo::kDexMetadataProfileEntry} in
     * art/libprofile/profile/profile_compilation_info.cc.
     */
    const @utf8InCpp String DEX_METADATA_PROFILE_ENTRY = "primary.prof";

    /**
     * The name of the vdex entry in the dex metadata file.
     *
     * Keep in sync with {@code VdexFile::kVdexNameInDmFile} in art/runtime/vdex_file.h.
     */
    const @utf8InCpp String DEX_METADATA_VDEX_ENTRY = "primary.vdex";
}
