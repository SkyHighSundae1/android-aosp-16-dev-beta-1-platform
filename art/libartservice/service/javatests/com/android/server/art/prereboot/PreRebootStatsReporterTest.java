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

import static com.android.server.art.model.DexoptStatus.DexContainerFileDexoptStatus;
import static com.android.server.art.proto.PreRebootStats.JobRun;
import static com.android.server.art.proto.PreRebootStats.JobType;
import static com.android.server.art.proto.PreRebootStats.Status;

import static com.google.common.truth.Truth.assertThat;

import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import androidx.test.filters.SmallTest;

import com.android.server.art.ArtManagerLocal;
import com.android.server.art.ArtStatsLog;
import com.android.server.art.model.DexoptStatus;
import com.android.server.art.proto.PreRebootStats;
import com.android.server.pm.PackageManagerLocal;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.util.List;

@SmallTest
@RunWith(MockitoJUnitRunner.StrictStubs.class)
public class PreRebootStatsReporterTest {
    private static final String PKG_NAME_FOO = "com.example.foo";
    private static final String PKG_NAME_BAR = "com.example.bar";
    private static final String PKG_NAME_BAZ = "com.example.baz";

    @Mock private PreRebootStatsReporter.Injector mInjector;
    @Mock private PackageManagerLocal mPackageManagerLocal;
    @Mock private PackageManagerLocal.FilteredSnapshot mSnapshot;
    @Mock private ArtManagerLocal mArtManagerLocal;
    private File mTempFile;

    @Before
    public void setUp() throws Exception {
        lenient().when(mPackageManagerLocal.withFilteredSnapshot()).thenReturn(mSnapshot);

        mTempFile = File.createTempFile("pre-reboot-stats", ".pb");
        mTempFile.deleteOnExit();
        lenient().when(mInjector.getFilename()).thenReturn(mTempFile.getAbsolutePath());

        lenient().when(mInjector.getPackageManagerLocal()).thenReturn(mPackageManagerLocal);
        lenient().when(mInjector.getArtManagerLocal()).thenReturn(mArtManagerLocal);
    }

    @Test
    public void testSuccess() throws Exception {
        var reporter = new PreRebootStatsReporter(mInjector);

        doReturn(50l).when(mInjector).getCurrentTimeMillis();
        reporter.recordJobScheduled(true /* isAsync */, false /* isOtaUpdate */);
        checkProto(PreRebootStats.newBuilder()
                           .setStatus(Status.STATUS_SCHEDULED)
                           .setJobType(JobType.JOB_TYPE_MAINLINE)
                           .setJobScheduledTimestampMillis(50)
                           .build());

        {
            doReturn(200l).when(mInjector).getCurrentTimeMillis();
            reporter.recordJobStarted();
            checkProto(PreRebootStats.newBuilder()
                               .setStatus(Status.STATUS_STARTED)
                               .setJobType(JobType.JOB_TYPE_MAINLINE)
                               .setJobScheduledTimestampMillis(50)
                               .addJobRuns(JobRun.newBuilder().setJobStartedTimestampMillis(200))
                               .setSkippedPackageCount(0)
                               .setOptimizedPackageCount(0)
                               .setFailedPackageCount(0)
                               .setTotalPackageCount(0)
                               .setPackagesWithArtifactsBeforeRebootCount(0)
                               .build());

            var reporterInChroot = new PreRebootStatsReporter(mInjector);
            var progressSession = reporterInChroot.new ProgressSession();

            progressSession.recordProgress(1 /* skippedPackageCount */,
                    2 /* optimizedPackageCount */, 3 /* failedPackageCount */,
                    10 /* totalPackageCount */, 4 /* packagesWithArtifactsBeforeRebootCount */);
            checkProto(PreRebootStats.newBuilder()
                               .setStatus(Status.STATUS_STARTED)
                               .setJobType(JobType.JOB_TYPE_MAINLINE)
                               .setJobScheduledTimestampMillis(50)
                               .addJobRuns(JobRun.newBuilder().setJobStartedTimestampMillis(200))
                               .setSkippedPackageCount(1)
                               .setOptimizedPackageCount(2)
                               .setFailedPackageCount(3)
                               .setTotalPackageCount(10)
                               .setPackagesWithArtifactsBeforeRebootCount(4)
                               .build());

            doReturn(300l).when(mInjector).getCurrentTimeMillis();
            reporter.recordJobEnded(true /* success */, false /* systemRequirementCheckFailed */);
            checkProto(PreRebootStats.newBuilder()
                               .setStatus(Status.STATUS_CANCELLED)
                               .setJobType(JobType.JOB_TYPE_MAINLINE)
                               .setJobScheduledTimestampMillis(50)
                               .addJobRuns(JobRun.newBuilder()
                                                   .setJobStartedTimestampMillis(200)
                                                   .setJobEndedTimestampMillis(300))
                               .setSkippedPackageCount(1)
                               .setOptimizedPackageCount(2)
                               .setFailedPackageCount(3)
                               .setTotalPackageCount(10)
                               .setPackagesWithArtifactsBeforeRebootCount(4)
                               .build());
        }

        {
            doReturn(400l).when(mInjector).getCurrentTimeMillis();
            reporter.recordJobStarted();
            checkProto(PreRebootStats.newBuilder()
                               .setStatus(Status.STATUS_STARTED)
                               .setJobType(JobType.JOB_TYPE_MAINLINE)
                               .setJobScheduledTimestampMillis(50)
                               .addJobRuns(JobRun.newBuilder()
                                                   .setJobStartedTimestampMillis(200)
                                                   .setJobEndedTimestampMillis(300))
                               .addJobRuns(JobRun.newBuilder().setJobStartedTimestampMillis(400))
                               .setSkippedPackageCount(0)
                               .setOptimizedPackageCount(0)
                               .setFailedPackageCount(0)
                               .setTotalPackageCount(0)
                               .setPackagesWithArtifactsBeforeRebootCount(0)
                               .build());

            var reporterInChroot = new PreRebootStatsReporter(mInjector);
            var progressSession = reporterInChroot.new ProgressSession();

            progressSession.recordProgress(1 /* skippedPackageCount */,
                    6 /* optimizedPackageCount */, 3 /* failedPackageCount */,
                    10 /* totalPackageCount */, 8 /* packagesWithArtifactsBeforeRebootCount */);
            checkProto(PreRebootStats.newBuilder()
                               .setStatus(Status.STATUS_STARTED)
                               .setJobType(JobType.JOB_TYPE_MAINLINE)
                               .setJobScheduledTimestampMillis(50)
                               .addJobRuns(JobRun.newBuilder()
                                                   .setJobStartedTimestampMillis(200)
                                                   .setJobEndedTimestampMillis(300))
                               .addJobRuns(JobRun.newBuilder().setJobStartedTimestampMillis(400))
                               .setSkippedPackageCount(1)
                               .setOptimizedPackageCount(6)
                               .setFailedPackageCount(3)
                               .setTotalPackageCount(10)
                               .setPackagesWithArtifactsBeforeRebootCount(8)
                               .build());

            doReturn(600l).when(mInjector).getCurrentTimeMillis();
            reporter.recordJobEnded(true /* success */, false /* systemRequirementCheckFailed */);
            checkProto(PreRebootStats.newBuilder()
                               .setStatus(Status.STATUS_FINISHED)
                               .setJobType(JobType.JOB_TYPE_MAINLINE)
                               .setJobScheduledTimestampMillis(50)
                               .addJobRuns(JobRun.newBuilder()
                                                   .setJobStartedTimestampMillis(200)
                                                   .setJobEndedTimestampMillis(300))
                               .addJobRuns(JobRun.newBuilder()
                                                   .setJobStartedTimestampMillis(400)
                                                   .setJobEndedTimestampMillis(600))
                               .setSkippedPackageCount(1)
                               .setOptimizedPackageCount(6)
                               .setFailedPackageCount(3)
                               .setTotalPackageCount(10)
                               .setPackagesWithArtifactsBeforeRebootCount(8)
                               .build());
        }

        {
            var reporterAfterReboot = new PreRebootStatsReporter(mInjector);
            var afterRebootSession = reporterAfterReboot.new AfterRebootSession();

            // For primary dex.
            afterRebootSession.recordPackageWithArtifacts(PKG_NAME_FOO);
            afterRebootSession.recordPackageWithArtifacts(PKG_NAME_BAR);

            // For secondary dex.
            afterRebootSession.recordPackageWithArtifacts(PKG_NAME_FOO);
            afterRebootSession.recordPackageWithArtifacts(PKG_NAME_BAZ);

            // No status has "ab-ota".
            doReturn(DexoptStatus.create(List.of(DexContainerFileDexoptStatus.create(
                             "/somewhere/app/foo/base.apk", true /* isPrimaryDex */,
                             true /* isPrimaryAbi */, "arm64-v8a", "verify", "vdex", "location"))))
                    .when(mArtManagerLocal)
                    .getDexoptStatus(mSnapshot, PKG_NAME_FOO);

            // One status has "ab-ota".
            doReturn(DexoptStatus.create(List.of(
                             DexContainerFileDexoptStatus.create("/somewhere/app/bar/base.apk",
                                     true /* isPrimaryDex */, true /* isPrimaryAbi */, "arm64-v8a",
                                     "speed", "ab-ota", "location"),
                             DexContainerFileDexoptStatus.create("/somewhere/app/bar/base.apk",
                                     true /* isPrimaryDex */, true /* isPrimaryAbi */, "arm64-v8a",
                                     "verify", "vdex", "location"))))
                    .when(mArtManagerLocal)
                    .getDexoptStatus(mSnapshot, PKG_NAME_BAR);

            // All statuses have "ab-ota".
            doReturn(DexoptStatus.create(List.of(
                             DexContainerFileDexoptStatus.create("/somewhere/app/baz/base.apk",
                                     true /* isPrimaryDex */, true /* isPrimaryAbi */, "arm64-v8a",
                                     "verify", "ab-ota", "location"),
                             DexContainerFileDexoptStatus.create("/somewhere/app/baz/base.apk",
                                     true /* isPrimaryDex */, true /* isPrimaryAbi */, "arm64-v8a",
                                     "speed-profile", "ab-ota", "location"))))
                    .when(mArtManagerLocal)
                    .getDexoptStatus(mSnapshot, PKG_NAME_BAZ);

            afterRebootSession.report();
        }

        verify(mInjector).writeStats(ArtStatsLog.PREREBOOT_DEXOPT_JOB_ENDED,
                ArtStatsLog.PRE_REBOOT_DEXOPT_JOB_ENDED__STATUS__STATUS_FINISHED,
                6 /* optimizedPackageCount */, 3 /* failedPackageCount */,
                1 /* skippedPackageCount */, 10 /* totalPackageCount */,
                300 /* jobDurationMillis */, 150 /* jobLatencyMillis */,
                3 /* packagesWithArtifactsAfterRebootCount */,
                2 /* packagesWithArtifactsUsableAfterRebootCount */, 2 /* jobRunCount */,
                8 /* packagesWithArtifactsBeforeRebootCount */,
                ArtStatsLog.PRE_REBOOT_DEXOPT_JOB_ENDED__JOB_TYPE__JOB_TYPE_MAINLINE);
    }

    @Test
    public void testSuccessSync() throws Exception {
        var reporter = new PreRebootStatsReporter(mInjector);

        reporter.recordJobScheduled(false /* isAsync */, true /* isOtaUpdate */);
        checkProto(PreRebootStats.newBuilder()
                           .setStatus(Status.STATUS_SCHEDULED)
                           .setJobType(JobType.JOB_TYPE_OTA)
                           .build());

        {
            doReturn(200l).when(mInjector).getCurrentTimeMillis();
            reporter.recordJobStarted();
            checkProto(PreRebootStats.newBuilder()
                               .setStatus(Status.STATUS_STARTED)
                               .setJobType(JobType.JOB_TYPE_OTA)
                               .addJobRuns(JobRun.newBuilder().setJobStartedTimestampMillis(200))
                               .setSkippedPackageCount(0)
                               .setOptimizedPackageCount(0)
                               .setFailedPackageCount(0)
                               .setTotalPackageCount(0)
                               .setPackagesWithArtifactsBeforeRebootCount(0)
                               .build());

            var reporterInChroot = new PreRebootStatsReporter(mInjector);
            var progressSession = reporterInChroot.new ProgressSession();

            progressSession.recordProgress(1 /* skippedPackageCount */,
                    6 /* optimizedPackageCount */, 3 /* failedPackageCount */,
                    10 /* totalPackageCount */, 8 /* packagesWithArtifactsBeforeRebootCount */);
            checkProto(PreRebootStats.newBuilder()
                               .setStatus(Status.STATUS_STARTED)
                               .setJobType(JobType.JOB_TYPE_OTA)
                               .addJobRuns(JobRun.newBuilder().setJobStartedTimestampMillis(200))
                               .setSkippedPackageCount(1)
                               .setOptimizedPackageCount(6)
                               .setFailedPackageCount(3)
                               .setTotalPackageCount(10)
                               .setPackagesWithArtifactsBeforeRebootCount(8)
                               .build());

            doReturn(300l).when(mInjector).getCurrentTimeMillis();
            reporter.recordJobEnded(true /* success */, false /* systemRequirementCheckFailed */);
            checkProto(PreRebootStats.newBuilder()
                               .setStatus(Status.STATUS_FINISHED)
                               .setJobType(JobType.JOB_TYPE_OTA)
                               .addJobRuns(JobRun.newBuilder()
                                                   .setJobStartedTimestampMillis(200)
                                                   .setJobEndedTimestampMillis(300))
                               .setSkippedPackageCount(1)
                               .setOptimizedPackageCount(6)
                               .setFailedPackageCount(3)
                               .setTotalPackageCount(10)
                               .setPackagesWithArtifactsBeforeRebootCount(8)
                               .build());
        }

        {
            var reporterAfterReboot = new PreRebootStatsReporter(mInjector);
            var afterRebootSession = reporterAfterReboot.new AfterRebootSession();

            afterRebootSession.report();
        }

        verify(mInjector).writeStats(ArtStatsLog.PREREBOOT_DEXOPT_JOB_ENDED,
                ArtStatsLog.PRE_REBOOT_DEXOPT_JOB_ENDED__STATUS__STATUS_FINISHED,
                6 /* optimizedPackageCount */, 3 /* failedPackageCount */,
                1 /* skippedPackageCount */, 10 /* totalPackageCount */,
                100 /* jobDurationMillis */, -1 /* jobLatencyMillis */,
                0 /* packagesWithArtifactsAfterRebootCount */,
                0 /* packagesWithArtifactsUsableAfterRebootCount */, 1 /* jobRunCount */,
                8 /* packagesWithArtifactsBeforeRebootCount */,
                ArtStatsLog.PRE_REBOOT_DEXOPT_JOB_ENDED__JOB_TYPE__JOB_TYPE_OTA);
    }

    private void checkFailure(boolean systemRequirementCheckFailed) throws Exception {
        var reporter = new PreRebootStatsReporter(mInjector);

        doReturn(50l).when(mInjector).getCurrentTimeMillis();
        reporter.recordJobScheduled(true /* isAsync */, false /* isOtaUpdate */);
        checkProto(PreRebootStats.newBuilder()
                           .setStatus(Status.STATUS_SCHEDULED)
                           .setJobType(JobType.JOB_TYPE_MAINLINE)
                           .setJobScheduledTimestampMillis(50)
                           .build());

        {
            doReturn(200l).when(mInjector).getCurrentTimeMillis();
            reporter.recordJobStarted();
            checkProto(PreRebootStats.newBuilder()
                               .setStatus(Status.STATUS_STARTED)
                               .setJobType(JobType.JOB_TYPE_MAINLINE)
                               .setJobScheduledTimestampMillis(50)
                               .addJobRuns(JobRun.newBuilder().setJobStartedTimestampMillis(200))
                               .setSkippedPackageCount(0)
                               .setOptimizedPackageCount(0)
                               .setFailedPackageCount(0)
                               .setTotalPackageCount(0)
                               .setPackagesWithArtifactsBeforeRebootCount(0)
                               .build());

            doReturn(300l).when(mInjector).getCurrentTimeMillis();
            reporter.recordJobEnded(false /* success */, systemRequirementCheckFailed);
            checkProto(PreRebootStats.newBuilder()
                               .setStatus(systemRequirementCheckFailed
                                               ? Status.STATUS_ABORTED_SYSTEM_REQUIREMENTS
                                               : Status.STATUS_FAILED)
                               .setJobType(JobType.JOB_TYPE_MAINLINE)
                               .setJobScheduledTimestampMillis(50)
                               .addJobRuns(JobRun.newBuilder()
                                                   .setJobStartedTimestampMillis(200)
                                                   .setJobEndedTimestampMillis(300))
                               .setSkippedPackageCount(0)
                               .setOptimizedPackageCount(0)
                               .setFailedPackageCount(0)
                               .setTotalPackageCount(0)
                               .setPackagesWithArtifactsBeforeRebootCount(0)
                               .build());
        }

        {
            var reporterAfterReboot = new PreRebootStatsReporter(mInjector);
            var afterRebootSession = reporterAfterReboot.new AfterRebootSession();

            afterRebootSession.report();
        }

        verify(mInjector).writeStats(ArtStatsLog.PREREBOOT_DEXOPT_JOB_ENDED,
                systemRequirementCheckFailed
                        ? ArtStatsLog
                                  .PRE_REBOOT_DEXOPT_JOB_ENDED__STATUS__STATUS_ABORTED_SYSTEM_REQUIREMENTS
                        : ArtStatsLog.PRE_REBOOT_DEXOPT_JOB_ENDED__STATUS__STATUS_FAILED,
                0 /* optimizedPackageCount */, 0 /* failedPackageCount */,
                0 /* skippedPackageCount */, 0 /* totalPackageCount */, 100 /* jobDurationMillis */,
                150 /* jobLatencyMillis */, 0 /* packagesWithArtifactsAfterRebootCount */,
                0 /* packagesWithArtifactsUsableAfterRebootCount */, 1 /* jobRunCount */,
                0 /* packagesWithArtifactsBeforeRebootCount */,
                ArtStatsLog.PRE_REBOOT_DEXOPT_JOB_ENDED__JOB_TYPE__JOB_TYPE_MAINLINE);
    }

    @Test
    public void testUnexpectedFailure() throws Exception {
        checkFailure(false /* systemRequirementCheckFailed */);
    }

    @Test
    public void testSystemRequirementCheckFailure() throws Exception {
        checkFailure(true /* systemRequirementCheckFailed */);
    }

    @Test
    public void testStarted() throws Exception {
        var reporter = new PreRebootStatsReporter(mInjector);

        doReturn(50l).when(mInjector).getCurrentTimeMillis();
        reporter.recordJobScheduled(true /* isAsync */, false /* isOtaUpdate */);
        checkProto(PreRebootStats.newBuilder()
                           .setStatus(Status.STATUS_SCHEDULED)
                           .setJobType(JobType.JOB_TYPE_MAINLINE)
                           .setJobScheduledTimestampMillis(50)
                           .build());

        {
            doReturn(200l).when(mInjector).getCurrentTimeMillis();
            reporter.recordJobStarted();
            checkProto(PreRebootStats.newBuilder()
                               .setStatus(Status.STATUS_STARTED)
                               .setJobType(JobType.JOB_TYPE_MAINLINE)
                               .setJobScheduledTimestampMillis(50)
                               .addJobRuns(JobRun.newBuilder().setJobStartedTimestampMillis(200))
                               .setSkippedPackageCount(0)
                               .setOptimizedPackageCount(0)
                               .setFailedPackageCount(0)
                               .setTotalPackageCount(0)
                               .setPackagesWithArtifactsBeforeRebootCount(0)
                               .build());

            var reporterInChroot = new PreRebootStatsReporter(mInjector);
            var progressSession = reporterInChroot.new ProgressSession();

            progressSession.recordProgress(1 /* skippedPackageCount */,
                    2 /* optimizedPackageCount */, 3 /* failedPackageCount */,
                    10 /* totalPackageCount */, 4 /* packagesWithArtifactsBeforeRebootCount */);
            checkProto(PreRebootStats.newBuilder()
                               .setStatus(Status.STATUS_STARTED)
                               .setJobType(JobType.JOB_TYPE_MAINLINE)
                               .setJobScheduledTimestampMillis(50)
                               .addJobRuns(JobRun.newBuilder().setJobStartedTimestampMillis(200))
                               .setSkippedPackageCount(1)
                               .setOptimizedPackageCount(2)
                               .setFailedPackageCount(3)
                               .setTotalPackageCount(10)
                               .setPackagesWithArtifactsBeforeRebootCount(4)
                               .build());
        }

        {
            var reporterAfterReboot = new PreRebootStatsReporter(mInjector);
            var afterRebootSession = reporterAfterReboot.new AfterRebootSession();

            afterRebootSession.report();
        }

        verify(mInjector).writeStats(ArtStatsLog.PREREBOOT_DEXOPT_JOB_ENDED,
                ArtStatsLog.PRE_REBOOT_DEXOPT_JOB_ENDED__STATUS__STATUS_STARTED,
                2 /* optimizedPackageCount */, 3 /* failedPackageCount */,
                1 /* skippedPackageCount */, 10 /* totalPackageCount */, -1 /* jobDurationMillis */,
                150 /* jobLatencyMillis */, 0 /* packagesWithArtifactsAfterRebootCount */,
                0 /* packagesWithArtifactsUsableAfterRebootCount */, 1 /* jobRunCount */,
                4 /* packagesWithArtifactsBeforeRebootCount */,
                ArtStatsLog.PRE_REBOOT_DEXOPT_JOB_ENDED__JOB_TYPE__JOB_TYPE_MAINLINE);
    }

    @Test
    public void testScheduled() throws Exception {
        var reporter = new PreRebootStatsReporter(mInjector);

        doReturn(50l).when(mInjector).getCurrentTimeMillis();
        reporter.recordJobScheduled(true /* isAsync */, false /* isOtaUpdate */);
        checkProto(PreRebootStats.newBuilder()
                           .setStatus(Status.STATUS_SCHEDULED)
                           .setJobType(JobType.JOB_TYPE_MAINLINE)
                           .setJobScheduledTimestampMillis(50)
                           .build());

        {
            var reporterAfterReboot = new PreRebootStatsReporter(mInjector);
            var afterRebootSession = reporterAfterReboot.new AfterRebootSession();

            afterRebootSession.report();
        }

        verify(mInjector).writeStats(ArtStatsLog.PREREBOOT_DEXOPT_JOB_ENDED,
                ArtStatsLog.PRE_REBOOT_DEXOPT_JOB_ENDED__STATUS__STATUS_SCHEDULED,
                0 /* optimizedPackageCount */, 0 /* failedPackageCount */,
                0 /* skippedPackageCount */, 0 /* totalPackageCount */, -1 /* jobDurationMillis */,
                -1 /* jobLatencyMillis */, 0 /* packagesWithArtifactsAfterRebootCount */,
                0 /* packagesWithArtifactsUsableAfterRebootCount */, 0 /* jobRunCount */,
                0 /* packagesWithArtifactsBeforeRebootCount */,
                ArtStatsLog.PRE_REBOOT_DEXOPT_JOB_ENDED__JOB_TYPE__JOB_TYPE_MAINLINE);
    }

    @Test
    public void testNotScheduled() throws Exception {
        var reporter = new PreRebootStatsReporter(mInjector);

        reporter.recordJobNotScheduled(
                Status.STATUS_NOT_SCHEDULED_DISABLED, false /* isOtaUpdate */);
        checkProto(PreRebootStats.newBuilder()
                           .setStatus(Status.STATUS_NOT_SCHEDULED_DISABLED)
                           .setJobType(JobType.JOB_TYPE_MAINLINE)
                           .build());

        {
            var reporterAfterReboot = new PreRebootStatsReporter(mInjector);
            var afterRebootSession = reporterAfterReboot.new AfterRebootSession();

            afterRebootSession.report();
        }

        verify(mInjector).writeStats(ArtStatsLog.PREREBOOT_DEXOPT_JOB_ENDED,
                ArtStatsLog.PRE_REBOOT_DEXOPT_JOB_ENDED__STATUS__STATUS_NOT_SCHEDULED_DISABLED,
                0 /* optimizedPackageCount */, 0 /* failedPackageCount */,
                0 /* skippedPackageCount */, 0 /* totalPackageCount */, -1 /* jobDurationMillis */,
                -1 /* jobLatencyMillis */, 0 /* packagesWithArtifactsAfterRebootCount */,
                0 /* packagesWithArtifactsUsableAfterRebootCount */, 0 /* jobRunCount */,
                0 /* packagesWithArtifactsBeforeRebootCount */,
                ArtStatsLog.PRE_REBOOT_DEXOPT_JOB_ENDED__JOB_TYPE__JOB_TYPE_MAINLINE);
    }

    private void checkProto(PreRebootStats expected) throws Exception {
        PreRebootStats actual;
        try (InputStream in = new FileInputStream(mTempFile.getPath())) {
            actual = PreRebootStats.parseFrom(in);
        }
        assertThat(actual).isEqualTo(expected);
    }
}
