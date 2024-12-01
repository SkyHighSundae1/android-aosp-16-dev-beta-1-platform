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

import static com.android.server.art.proto.PreRebootStats.JobRun;
import static com.android.server.art.proto.PreRebootStats.JobType;
import static com.android.server.art.proto.PreRebootStats.Status;

import android.annotation.NonNull;
import android.os.Build;

import androidx.annotation.RequiresApi;

import com.android.internal.annotations.VisibleForTesting;
import com.android.server.LocalManagerRegistry;
import com.android.server.art.ArtManagerLocal;
import com.android.server.art.ArtStatsLog;
import com.android.server.art.ArtdRefCache;
import com.android.server.art.AsLog;
import com.android.server.art.ReasonMapping;
import com.android.server.art.Utils;
import com.android.server.art.model.DexoptStatus;
import com.android.server.art.proto.PreRebootStats;
import com.android.server.pm.PackageManagerLocal;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.HashSet;
import java.util.List;
import java.util.Objects;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.function.Function;

/**
 * A helper class to report the Pre-reboot Dexopt metrics to StatsD.
 *
 * This class is not thread-safe.
 *
 * During Pre-reboot Dexopt, both the old version and the new version of this code is run. The old
 * version writes to disk first, and the new version writes to disk later. After reboot, the new
 * version loads from disk.
 *
 * @hide
 */
@RequiresApi(Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
public class PreRebootStatsReporter {
    private static final String FILENAME = "/data/system/pre-reboot-stats.pb";

    @NonNull private final Injector mInjector;

    public PreRebootStatsReporter() {
        this(new Injector());
    }

    /** @hide */
    @VisibleForTesting
    public PreRebootStatsReporter(@NonNull Injector injector) {
        mInjector = injector;
    }

    public void recordJobScheduled(boolean isAsync, boolean isOtaUpdate) {
        PreRebootStats.Builder statsBuilder = PreRebootStats.newBuilder();
        statsBuilder.setStatus(Status.STATUS_SCHEDULED)
                .setJobType(isOtaUpdate ? JobType.JOB_TYPE_OTA : JobType.JOB_TYPE_MAINLINE);
        // Omit job_scheduled_timestamp_millis to indicate a synchronous job.
        if (isAsync) {
            statsBuilder.setJobScheduledTimestampMillis(mInjector.getCurrentTimeMillis());
        }
        save(statsBuilder);
    }

    public void recordJobNotScheduled(@NonNull Status reason, boolean isOtaUpdate) {
        Utils.check(reason == Status.STATUS_NOT_SCHEDULED_DISABLED
                || reason == Status.STATUS_NOT_SCHEDULED_JOB_SCHEDULER);
        PreRebootStats.Builder statsBuilder = PreRebootStats.newBuilder();
        statsBuilder.setStatus(reason).setJobType(
                isOtaUpdate ? JobType.JOB_TYPE_OTA : JobType.JOB_TYPE_MAINLINE);
        save(statsBuilder);
    }

    public void recordJobStarted() {
        PreRebootStats.Builder statsBuilder = load();
        if (statsBuilder.getStatus() == Status.STATUS_UNKNOWN) {
            // Failed to load, the error is already logged.
            return;
        }

        JobRun.Builder runBuilder =
                JobRun.newBuilder().setJobStartedTimestampMillis(mInjector.getCurrentTimeMillis());
        statsBuilder.setStatus(Status.STATUS_STARTED)
                .addJobRuns(runBuilder)
                .setSkippedPackageCount(0)
                .setOptimizedPackageCount(0)
                .setFailedPackageCount(0)
                .setTotalPackageCount(0)
                // Some packages may have artifacts from a previously cancelled job, but we count
                // from scratch for simplicity.
                .setPackagesWithArtifactsBeforeRebootCount(0);
        save(statsBuilder);
    }

    public class ProgressSession {
        private @NonNull PreRebootStats.Builder mStatsBuilder = load();

        public void recordProgress(int skippedPackageCount, int optimizedPackageCount,
                int failedPackageCount, int totalPackageCount,
                int packagesWithArtifactsBeforeRebootCount) {
            if (mStatsBuilder.getStatus() == Status.STATUS_UNKNOWN) {
                // Failed to load, the error is already logged.
                return;
            }

            mStatsBuilder.setSkippedPackageCount(skippedPackageCount)
                    .setOptimizedPackageCount(optimizedPackageCount)
                    .setFailedPackageCount(failedPackageCount)
                    .setTotalPackageCount(totalPackageCount)
                    .setPackagesWithArtifactsBeforeRebootCount(
                            packagesWithArtifactsBeforeRebootCount);
            save(mStatsBuilder);
        }
    }

    public void recordJobEnded(boolean success, boolean systemRequirementCheckFailed) {
        PreRebootStats.Builder statsBuilder = load();
        if (statsBuilder.getStatus() == Status.STATUS_UNKNOWN) {
            // Failed to load, the error is already logged.
            return;
        }

        List<JobRun> jobRuns = statsBuilder.getJobRunsList();
        Utils.check(jobRuns.size() > 0);
        JobRun lastRun = jobRuns.get(jobRuns.size() - 1);
        Utils.check(lastRun.getJobEndedTimestampMillis() == 0);

        JobRun.Builder runBuilder = JobRun.newBuilder(lastRun).setJobEndedTimestampMillis(
                mInjector.getCurrentTimeMillis());

        Status status;
        if (success) {
            // The job is cancelled if it hasn't done package scanning (total package count is 0),
            // or it's interrupted in the middle of package processing (package counts don't add up
            // to the total).
            // TODO(b/336239721): Move this logic to the server.
            if (statsBuilder.getTotalPackageCount() > 0
                    && (statsBuilder.getOptimizedPackageCount()
                               + statsBuilder.getFailedPackageCount()
                               + statsBuilder.getSkippedPackageCount())
                            == statsBuilder.getTotalPackageCount()) {
                status = Status.STATUS_FINISHED;
            } else {
                status = Status.STATUS_CANCELLED;
            }
        } else {
            if (systemRequirementCheckFailed) {
                status = Status.STATUS_ABORTED_SYSTEM_REQUIREMENTS;
            } else {
                status = Status.STATUS_FAILED;
            }
        }

        statsBuilder.setStatus(status).setJobRuns(jobRuns.size() - 1, runBuilder);
        save(statsBuilder);
    }

    public class AfterRebootSession {
        private @NonNull Set<String> mPackagesWithArtifacts = new HashSet<>();

        public void recordPackageWithArtifacts(@NonNull String packageName) {
            mPackagesWithArtifacts.add(packageName);
        }

        public void reportAsync() {
            new CompletableFuture().runAsync(this::report).exceptionally(t -> {
                AsLog.e("Failed to report stats", t);
                return null;
            });
        }

        @VisibleForTesting
        public void report() {
            PreRebootStats.Builder statsBuilder = load();
            delete();

            if (statsBuilder.getStatus() == Status.STATUS_UNKNOWN) {
                // Job not scheduled, probably because Pre-reboot Dexopt is not enabled.
                return;
            }

            ArtManagerLocal artManagerLocal = mInjector.getArtManagerLocal();

            // This takes some time (~3ms per package). It probably fine because we are running
            // asynchronously. Consider removing this in the future.
            int packagesWithArtifactsUsableCount;
            try (var snapshot = mInjector.getPackageManagerLocal().withFilteredSnapshot();
                    var pin = mInjector.createArtdPin()) {
                packagesWithArtifactsUsableCount =
                        (int) mPackagesWithArtifacts.stream()
                                .map(packageName
                                        -> artManagerLocal.getDexoptStatus(snapshot, packageName))
                                .filter(status -> hasUsablePreRebootArtifacts(status))
                                .count();
            }

            List<JobRun> jobRuns = statsBuilder.getJobRunsList();
            // The total duration of all runs, or -1 if any run didn't end.
            long jobDurationMs = 0;
            for (JobRun run : jobRuns) {
                if (run.getJobEndedTimestampMillis() == 0) {
                    jobDurationMs = -1;
                    break;
                }
                jobDurationMs +=
                        run.getJobEndedTimestampMillis() - run.getJobStartedTimestampMillis();
            }
            if (jobRuns.size() == 0) {
                jobDurationMs = -1;
            }
            long jobLatencyMs =
                    (jobRuns.size() > 0 && statsBuilder.getJobScheduledTimestampMillis() > 0)
                    ? (jobRuns.get(0).getJobStartedTimestampMillis()
                              - statsBuilder.getJobScheduledTimestampMillis())
                    : -1;

            mInjector.writeStats(ArtStatsLog.PREREBOOT_DEXOPT_JOB_ENDED,
                    getStatusForStatsd(statsBuilder.getStatus()),
                    statsBuilder.getOptimizedPackageCount(), statsBuilder.getFailedPackageCount(),
                    statsBuilder.getSkippedPackageCount(), statsBuilder.getTotalPackageCount(),
                    jobDurationMs, jobLatencyMs, mPackagesWithArtifacts.size(),
                    packagesWithArtifactsUsableCount, jobRuns.size(),
                    statsBuilder.getPackagesWithArtifactsBeforeRebootCount(),
                    getJobTypeForStatsd(statsBuilder.getJobType()));
        }
    }

    private int getStatusForStatsd(@NonNull Status status) {
        switch (status) {
            case STATUS_UNKNOWN:
                return ArtStatsLog.PRE_REBOOT_DEXOPT_JOB_ENDED__STATUS__STATUS_UNKNOWN;
            case STATUS_SCHEDULED:
                return ArtStatsLog.PRE_REBOOT_DEXOPT_JOB_ENDED__STATUS__STATUS_SCHEDULED;
            case STATUS_STARTED:
                return ArtStatsLog.PRE_REBOOT_DEXOPT_JOB_ENDED__STATUS__STATUS_STARTED;
            case STATUS_FINISHED:
                return ArtStatsLog.PRE_REBOOT_DEXOPT_JOB_ENDED__STATUS__STATUS_FINISHED;
            case STATUS_FAILED:
                return ArtStatsLog.PRE_REBOOT_DEXOPT_JOB_ENDED__STATUS__STATUS_FAILED;
            case STATUS_CANCELLED:
                return ArtStatsLog.PRE_REBOOT_DEXOPT_JOB_ENDED__STATUS__STATUS_CANCELLED;
            case STATUS_ABORTED_SYSTEM_REQUIREMENTS:
                return ArtStatsLog
                        .PRE_REBOOT_DEXOPT_JOB_ENDED__STATUS__STATUS_ABORTED_SYSTEM_REQUIREMENTS;
            case STATUS_NOT_SCHEDULED_DISABLED:
                return ArtStatsLog
                        .PRE_REBOOT_DEXOPT_JOB_ENDED__STATUS__STATUS_NOT_SCHEDULED_DISABLED;
            case STATUS_NOT_SCHEDULED_JOB_SCHEDULER:
                return ArtStatsLog
                        .PRE_REBOOT_DEXOPT_JOB_ENDED__STATUS__STATUS_NOT_SCHEDULED_JOB_SCHEDULER;
            default:
                throw new IllegalStateException("Unknown status: " + status.getNumber());
        }
    }

    private int getJobTypeForStatsd(@NonNull JobType jobType) {
        switch (jobType) {
            case JOB_TYPE_UNKNOWN:
                return ArtStatsLog.PRE_REBOOT_DEXOPT_JOB_ENDED__JOB_TYPE__JOB_TYPE_UNKNOWN;
            case JOB_TYPE_OTA:
                return ArtStatsLog.PRE_REBOOT_DEXOPT_JOB_ENDED__JOB_TYPE__JOB_TYPE_OTA;
            case JOB_TYPE_MAINLINE:
                return ArtStatsLog.PRE_REBOOT_DEXOPT_JOB_ENDED__JOB_TYPE__JOB_TYPE_MAINLINE;
            default:
                throw new IllegalStateException("Unknown job type: " + jobType.getNumber());
        }
    }

    private boolean hasUsablePreRebootArtifacts(@NonNull DexoptStatus status) {
        // For simplicity, we consider all artifacts of a package usable if we see at least one
        // `REASON_PRE_REBOOT_DEXOPT` because it's not easy to know which files are committed.
        return status.getDexContainerFileDexoptStatuses().stream().anyMatch(fileStatus
                -> fileStatus.getCompilationReason().equals(
                        ReasonMapping.REASON_PRE_REBOOT_DEXOPT));
    }

    @NonNull
    private PreRebootStats.Builder load() {
        PreRebootStats.Builder statsBuilder = PreRebootStats.newBuilder();
        try (InputStream in = new FileInputStream(mInjector.getFilename())) {
            statsBuilder.mergeFrom(in);
        } catch (IOException e) {
            // Nothing else we can do but to start from scratch.
            AsLog.e("Failed to load pre-reboot stats", e);
        }
        return statsBuilder;
    }

    private void save(@NonNull PreRebootStats.Builder statsBuilder) {
        var file = new File(mInjector.getFilename());
        File tempFile = null;
        try {
            tempFile = File.createTempFile(file.getName(), null /* suffix */, file.getParentFile());
            try (OutputStream out = new FileOutputStream(tempFile.getPath())) {
                statsBuilder.build().writeTo(out);
            }
            Files.move(tempFile.toPath(), file.toPath(), StandardCopyOption.REPLACE_EXISTING,
                    StandardCopyOption.ATOMIC_MOVE);
        } catch (IOException e) {
            AsLog.e("Failed to save pre-reboot stats", e);
        } finally {
            Utils.deleteIfExistsSafe(tempFile);
        }
    }

    public void delete() {
        Utils.deleteIfExistsSafe(new File(mInjector.getFilename()));
    }

    /**
     * Injector pattern for testing purpose.
     *
     * @hide
     */
    @VisibleForTesting
    public static class Injector {
        @NonNull
        public String getFilename() {
            return FILENAME;
        }

        public long getCurrentTimeMillis() {
            return System.currentTimeMillis();
        }

        @NonNull
        public PackageManagerLocal getPackageManagerLocal() {
            return Objects.requireNonNull(
                    LocalManagerRegistry.getManager(PackageManagerLocal.class));
        }

        @NonNull
        public ArtManagerLocal getArtManagerLocal() {
            return Objects.requireNonNull(LocalManagerRegistry.getManager(ArtManagerLocal.class));
        }

        @NonNull
        public ArtdRefCache.Pin createArtdPin() {
            return ArtdRefCache.getInstance().new Pin();
        }

        // Wrap the static void method to make it easier to mock. There is no good way to mock a
        // method that is both void and static, due to the poor design of Mockito API.
        public void writeStats(int code, int status, int optimizedPackageCount,
                int failedPackageCount, int skippedPackageCount, int totalPackageCount,
                long jobDurationMillis, long jobLatencyMillis,
                int packagesWithArtifactsAfterRebootCount,
                int packagesWithArtifactsUsableAfterRebootCount, int jobRunCount,
                int packagesWithArtifactsBeforeRebootCount, int jobType) {
            ArtStatsLog.write(code, status, optimizedPackageCount, failedPackageCount,
                    skippedPackageCount, totalPackageCount, jobDurationMillis, jobLatencyMillis,
                    packagesWithArtifactsAfterRebootCount,
                    packagesWithArtifactsUsableAfterRebootCount, jobRunCount,
                    packagesWithArtifactsBeforeRebootCount, jobType);
        }
    }
}
