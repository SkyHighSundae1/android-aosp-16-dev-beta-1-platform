/*
 * Copyright (C) 2022 The Android Open Source Project
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

package com.android.server.art.model;

import static com.google.common.truth.Truth.assertThat;

import androidx.test.filters.SmallTest;
import androidx.test.runner.AndroidJUnit4;

import com.android.server.art.proto.BatchDexoptParamsProto;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.Arrays;
import java.util.List;
import java.util.stream.Collectors;

@SmallTest
@RunWith(AndroidJUnit4.class)
public class BatchDexoptParamsTest {
    @Test
    public void testToProto() {
        // Update this test with new fields if this assertion fails.
        checkFieldCoverage();

        BatchDexoptParams params = new BatchDexoptParams
                                           .Builder(List.of("package_a", "package_b"),
                                                   new DexoptParams.Builder("install").build())
                                           .build();

        BatchDexoptParamsProto proto = params.toProto();

        assertThat(proto.getPackageList())
                .containsExactlyElementsIn(params.getPackages())
                .inOrder();
        assertThat(proto.getDexoptParams().getReason())
                .isEqualTo(params.getDexoptParams().getReason());
    }

    @Test
    public void testFromProto() {
        // Update this test with new fields if this assertion fails.
        checkFieldCoverage();

        BatchDexoptParamsProto proto =
                BatchDexoptParamsProto.newBuilder()
                        .addAllPackage(List.of("package_a", "package_b"))
                        .setDexoptParams(new DexoptParams.Builder("install").build().toProto())
                        .build();

        BatchDexoptParams params = BatchDexoptParams.fromProto(proto);

        assertThat(params.getPackages())
                .containsExactlyElementsIn(proto.getPackageList())
                .inOrder();
        assertThat(params.getDexoptParams().getReason())
                .isEqualTo(proto.getDexoptParams().getReason());
    }

    private void checkFieldCoverage() {
        assertThat(Arrays.stream(BatchDexoptParams.class.getDeclaredMethods())
                           .filter(method -> Modifier.isAbstract(method.getModifiers()))
                           .map(Method::getName)
                           .collect(Collectors.toList()))
                .containsExactly("getPackages", "getDexoptParams");
    }
}
