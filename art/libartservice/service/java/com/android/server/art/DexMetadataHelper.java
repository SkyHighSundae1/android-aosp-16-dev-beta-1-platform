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

package com.android.server.art;

import android.annotation.NonNull;
import android.annotation.Nullable;
import android.os.Build;

import androidx.annotation.RequiresApi;

import com.android.internal.annotations.VisibleForTesting;
import com.android.server.art.model.DexMetadata;
import com.android.server.art.proto.DexMetadataConfig;

import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.NoSuchFileException;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/**
 * A helper class to handle dex metadata (dm) files.
 *
 * Note that this is not the only consumer of dm files. A dm file is a container that contains
 * various types of files for various purposes, passed down to various layers and consumed by them.
 *
 * @hide
 */
@RequiresApi(Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
public class DexMetadataHelper {
    @NonNull private final Injector mInjector;

    public DexMetadataHelper() {
        this(new Injector());
    }

    @VisibleForTesting
    public DexMetadataHelper(@NonNull Injector injector) {
        mInjector = injector;
    }

    @NonNull
    public DexMetadataInfo getDexMetadataInfo(@Nullable DexMetadataPath dmPath) {
        if (dmPath == null) {
            return getDefaultDexMetadataInfo(DexMetadata.TYPE_NONE);
        }

        String realDmPath = getDmPath(dmPath);
        try (var zipFile = mInjector.openZipFile(realDmPath)) {
            ZipEntry entry = zipFile.getEntry("config.pb");
            if (entry == null) {
                return new DexMetadataInfo(
                        dmPath, DexMetadataConfig.getDefaultInstance(), getType(zipFile));
            }
            try (InputStream stream = zipFile.getInputStream(entry)) {
                return new DexMetadataInfo(
                        dmPath, DexMetadataConfig.parseFrom(stream), getType(zipFile));
            }
        } catch (IOException e) {
            if (!(e instanceof FileNotFoundException || e instanceof NoSuchFileException)) {
                AsLog.e(String.format("Failed to read dm file '%s'", realDmPath), e);
            }
            return getDefaultDexMetadataInfo(DexMetadata.TYPE_ERROR);
        }
    }

    @NonNull
    private DexMetadataInfo getDefaultDexMetadataInfo(@DexMetadata.Type int type) {
        return new DexMetadataInfo(null /* dmPath */, DexMetadataConfig.getDefaultInstance(), type);
    }

    @NonNull
    public static String getDmPath(@NonNull DexMetadataPath dmPath) {
        return Utils.replaceFileExtension(dmPath.dexPath, ArtConstants.DEX_METADATA_FILE_EXT);
    }

    private static @DexMetadata.Type int getType(@NonNull ZipFile zipFile) {
        var profile = zipFile.getEntry(ArtConstants.DEX_METADATA_PROFILE_ENTRY);
        var vdex = zipFile.getEntry(ArtConstants.DEX_METADATA_VDEX_ENTRY);

        if (profile != null && vdex != null) {
            return DexMetadata.TYPE_PROFILE_AND_VDEX;
        } else if (profile != null) {
            return DexMetadata.TYPE_PROFILE;
        } else if (vdex != null) {
            return DexMetadata.TYPE_VDEX;
        } else {
            return DexMetadata.TYPE_NONE;
        }
    }

    /**
     * @param dmPath Represents the path to the dm file, if it exists. Or null if the file doesn't
     *         exist or an error occurred.
     * @param config The config deserialized from `config.pb`, if it exists. Or the default instance
     *         if the file doesn't exist or an error occurred.
     * @param type An enum value representing whether the dm file contains a profile, a VDEX file,
     *         none, or both.
     */
    public record DexMetadataInfo(@Nullable DexMetadataPath dmPath,
            @NonNull DexMetadataConfig config, @DexMetadata.Type int type) {}

    /**
     * Injector pattern for testing purpose.
     *
     * @hide
     */
    @VisibleForTesting
    public static class Injector {
        @NonNull
        ZipFile openZipFile(@NonNull String path) throws IOException {
            return new ZipFile(path);
        }
    }
}
