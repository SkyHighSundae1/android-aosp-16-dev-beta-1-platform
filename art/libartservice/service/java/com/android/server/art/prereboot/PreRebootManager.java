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

import static com.android.server.art.model.DexoptResult.PackageDexoptResult;
import static com.android.server.art.proto.PreRebootStats.Status;

import android.annotation.NonNull;
import android.annotation.Nullable;
import android.content.Context;
import android.os.ArtModuleServiceManager;
import android.os.Build;
import android.os.CancellationSignal;

import androidx.annotation.RequiresApi;

import com.android.server.LocalManagerRegistry;
import com.android.server.art.ArtManagerLocal;
import com.android.server.art.ArtdRefCache;
import com.android.server.art.AsLog;
import com.android.server.art.ReasonMapping;
import com.android.server.art.Utils;
import com.android.server.art.model.ArtFlags;
import com.android.server.art.model.BatchDexoptParams;
import com.android.server.art.model.DexoptResult;
import com.android.server.art.model.OperationProgress;
import com.android.server.art.proto.BatchDexoptParamsProto;
import com.android.server.pm.PackageManagerLocal;

import com.google.protobuf.InvalidProtocolBufferException;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.function.Consumer;

/**
 * Implementation of {@link PreRebootManagerInterface}.
 *
 * DO NOT add a constructor with parameters! There can't be stability guarantees on constructors as
 * they can't be checked against the interface.
 *
 * During Pre-reboot Dexopt, the new version of this code is run.
 *
 * @hide
 */
@RequiresApi(Build.VERSION_CODES.VANILLA_ICE_CREAM)
public class PreRebootManager implements PreRebootManagerInterface {
    public void run(@NonNull ArtModuleServiceManager artModuleServiceManager,
            @NonNull Context context, @NonNull CancellationSignal cancellationSignal) {
        PackageManagerLocal packageManagerLocal =
                Objects.requireNonNull(LocalManagerRegistry.getManager(PackageManagerLocal.class));
        try (var snapshot = packageManagerLocal.withFilteredSnapshot()) {
            run(artModuleServiceManager, context, cancellationSignal, snapshot,
                    null /* batchDexoptParamsProto */);
        }
    }

    public void run(@NonNull ArtModuleServiceManager artModuleServiceManager,
            @NonNull Context context, @NonNull CancellationSignal cancellationSignal,
            @NonNull PackageManagerLocal.FilteredSnapshot snapshot,
            @Nullable byte[] batchDexoptParamsProto) {
        ExecutorService callbackExecutor = Executors.newSingleThreadExecutor();
        try {
            if (!PreRebootGlobalInjector.init(
                        artModuleServiceManager, context, cancellationSignal)) {
                return;
            }
            ArtManagerLocal artManagerLocal = new ArtManagerLocal(context);

            var progressSession = new PreRebootStatsReporter().new ProgressSession();

            // Contains four values: skipped, performed, failed, has pre-reboot artifacts.
            List<Integer> values = new ArrayList(List.of(0, 0, 0, 0));

            // Record every progress change right away, in case the job is interrupted by a reboot.
            Consumer<OperationProgress> progressCallback = progress -> {
                PackageDexoptResult result = progress.getLastPackageDexoptResult();
                if (result == null) {
                    return;
                }
                switch (result.getStatus()) {
                    case DexoptResult.DEXOPT_SKIPPED:
                        if (hasExistingArtifacts(result)) {
                            values.set(1, values.get(1) + 1);
                        } else {
                            values.set(0, values.get(0) + 1);
                        }
                        break;
                    case DexoptResult.DEXOPT_PERFORMED:
                        values.set(1, values.get(1) + 1);
                        break;
                    case DexoptResult.DEXOPT_FAILED:
                        values.set(2, values.get(2) + 1);
                        break;
                    case DexoptResult.DEXOPT_CANCELLED:
                        break;
                    default:
                        throw new IllegalStateException("Unknown status: " + result.getStatus());
                }
                if (hasExistingArtifacts(result) || result.hasUpdatedArtifacts()) {
                    values.set(3, values.get(3) + 1);
                }

                progressSession.recordProgress(values.get(0), values.get(1), values.get(2),
                        progress.getTotal(), values.get(3));
            };

            BatchDexoptParams params;
            try {
                params = batchDexoptParamsProto != null
                        ? BatchDexoptParams.fromProto(
                                  BatchDexoptParamsProto.parseFrom(batchDexoptParamsProto))
                        : null;
            } catch (InvalidProtocolBufferException e) {
                throw new IllegalArgumentException(e);
            }

            artManagerLocal.dexoptPackagesWithParams(snapshot,
                    ReasonMapping.REASON_PRE_REBOOT_DEXOPT, cancellationSignal, callbackExecutor,
                    Map.of(ArtFlags.PASS_MAIN, progressCallback), params);
        } finally {
            ArtdRefCache.getInstance().reset();
            callbackExecutor.shutdown();
            try {
                // Make sure we have no running threads when we tear down.
                callbackExecutor.awaitTermination(5, TimeUnit.SECONDS);
            } catch (InterruptedException e) {
                AsLog.wtf("Interrupted", e);
            }
        }
    }

    private boolean hasExistingArtifacts(@NonNull PackageDexoptResult result) {
        return result.getDexContainerFileDexoptResults().stream().anyMatch(fileResult
                -> (fileResult.getExtendedStatusFlags()
                           & DexoptResult.EXTENDED_SKIPPED_PRE_REBOOT_ALREADY_EXIST)
                        != 0);
    }
}
