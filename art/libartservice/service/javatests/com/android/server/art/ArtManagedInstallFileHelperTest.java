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
 * limitations under the License
 */

package com.android.server.art;

import static com.google.common.truth.Truth.assertThat;

import static org.junit.Assert.assertThrows;

import androidx.test.filters.SmallTest;
import androidx.test.runner.AndroidJUnit4;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.List;

@SmallTest
@RunWith(AndroidJUnit4.class)
public class ArtManagedInstallFileHelperTest {
    @Test
    public void testIsArtManaged() throws Exception {
        assertThat(ArtManagedInstallFileHelper.isArtManaged("/foo/bar.dm")).isTrue();
        assertThat(ArtManagedInstallFileHelper.isArtManaged("/foo/bar.prof")).isTrue();
        assertThat(ArtManagedInstallFileHelper.isArtManaged("/foo/bar.sdm")).isTrue();
        assertThat(ArtManagedInstallFileHelper.isArtManaged("/foo/bar.abc")).isFalse();
    }

    @Test
    public void testFilterPathsForApk() throws Exception {
        assertThat(ArtManagedInstallFileHelper.filterPathsForApk(
                           List.of("/foo/bar.dm", "/foo/bar.prof", "/foo/bar.sdm", "/foo/bar.abc",
                                   "/foo/baz.dm"),
                           "/foo/bar.apk"))
                .containsExactly("/foo/bar.dm", "/foo/bar.prof", "/foo/bar.sdm");

        // Filenames don't match.
        assertThat(ArtManagedInstallFileHelper.filterPathsForApk(
                           List.of("/foo/bar.dm", "/foo/bar.prof", "/foo/bar.sdm", "/foo/bar.abc",
                                   "/foo/baz.dm"),
                           "/foo/qux.apk"))
                .isEmpty();

        // Directories don't match.
        assertThat(ArtManagedInstallFileHelper.filterPathsForApk(
                           List.of("/foo/bar.dm", "/foo/bar.prof", "/foo/bar.sdm", "/foo/bar.abc",
                                   "/foo/baz.dm"),
                           "/quz/bar.apk"))
                .isEmpty();
    }

    @Test
    public void testGetTargetPathForApk() throws Exception {
        assertThat(ArtManagedInstallFileHelper.getTargetPathForApk(
                           "/foo/bar.dm", "/somewhere/base.apk"))
                .isEqualTo("/somewhere/base.dm");
        assertThat(ArtManagedInstallFileHelper.getTargetPathForApk(
                           "/foo/bar.prof", "/somewhere/base.apk"))
                .isEqualTo("/somewhere/base.prof");
        assertThat(ArtManagedInstallFileHelper.getTargetPathForApk(
                           "/foo/bar.sdm", "/somewhere/base.apk"))
                .isEqualTo("/somewhere/base.sdm");

        assertThrows(IllegalArgumentException.class, () -> {
            ArtManagedInstallFileHelper.getTargetPathForApk("/foo/bar.abc", "/somewhere/base.apk");
        });
    }
}
