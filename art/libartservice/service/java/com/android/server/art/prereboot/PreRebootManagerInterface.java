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

package com.android.server.art.prereboot;

import android.annotation.NonNull;
import android.annotation.Nullable;
import android.content.Context;
import android.os.ArtModuleServiceManager;
import android.os.Build;
import android.os.CancellationSignal;

import androidx.annotation.RequiresApi;

import com.android.server.art.proto.BatchDexoptParamsProto;
import com.android.server.pm.PackageManagerLocal;

/**
 * The interface for the entry point of Pre-reboot Dexopt, called through reflection from an old
 * version of the ART module. This interface must be kept stable from one version of the ART module
 * to another. In principle, a method here should be kept as long as devices that receive Mainline
 * updates call it from their old factory installed modules, unless there is a good reason to drop
 * the Pre-reboot Dexopt support earlier for certain versions of the ART module.
 *
 * Dropping the support for certain versions will only make devices lose the opportunity to optimize
 * apps before the reboot, but it won't cause severe results such as crashes because even the oldest
 * version that uses this interface can elegantly handle reflection exceptions.
 *
 * During Pre-reboot Dexopt, the new version of this code is run.
 *
 * Methods in this interface must not take an instance of a class defined in `service-art.jar`. See
 * comments in {@link PreRebootDriver#runFromChroot} for the reason.
 *
 * @hide
 */
@RequiresApi(Build.VERSION_CODES.VANILLA_ICE_CREAM)
public interface PreRebootManagerInterface {
    /** Since Android V. For backward compatibility only. */
    void run(@NonNull ArtModuleServiceManager artModuleServiceManager, @NonNull Context context,
            @NonNull CancellationSignal cancellationSignal) throws SystemRequirementException;

    /**
     * Since Android B. The current API.
     *
     * @param snapshot The snapshot that {@code batchDexoptParamsProto} is created with.
     * @param batchDexoptParamsProto {@link BatchDexoptParamsProto}, in binary format.
     */
    void run(@NonNull ArtModuleServiceManager artModuleServiceManager, @NonNull Context context,
            @NonNull CancellationSignal cancellationSignal,
            @NonNull PackageManagerLocal.FilteredSnapshot snapshot,
            @Nullable byte[] batchDexoptParamsProto) throws SystemRequirementException;

    public static class SystemRequirementException extends Exception {
        public SystemRequirementException(@NonNull String message) {
            super(message);
        }
    }
}
