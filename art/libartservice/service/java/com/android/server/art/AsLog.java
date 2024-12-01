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
import android.util.Log;
import android.util.Slog;

import androidx.annotation.RequiresApi;

/**
 * A log wrapper that logs messages with the appropriate tag.
 *
 * @hide
 */
@RequiresApi(Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
public class AsLog {
    private static final String TAG = "ArtService";
    private static final String PRE_REBOOT_TAG = "ArtServicePreReboot";

    @NonNull
    public static String getTag() {
        return GlobalInjector.getInstance().isPreReboot() ? PRE_REBOOT_TAG : TAG;
    }

    public static void v(@NonNull String msg) {
        Log.v(getTag(), msg);
    }

    public static void v(@NonNull String msg, @Nullable Throwable tr) {
        Log.v(getTag(), msg, tr);
    }

    public static void d(@NonNull String msg) {
        Log.d(getTag(), msg);
    }

    public static void d(@NonNull String msg, @Nullable Throwable tr) {
        Log.d(getTag(), msg, tr);
    }

    public static void i(@NonNull String msg) {
        Log.i(getTag(), msg);
    }

    public static void i(@NonNull String msg, @Nullable Throwable tr) {
        Log.i(getTag(), msg, tr);
    }

    public static void w(@NonNull String msg) {
        Log.w(getTag(), msg);
    }

    public static void w(@NonNull String msg, @Nullable Throwable tr) {
        Log.w(getTag(), msg, tr);
    }

    public static void e(@NonNull String msg) {
        Log.e(getTag(), msg);
    }

    public static void e(@NonNull String msg, @Nullable Throwable tr) {
        Log.e(getTag(), msg, tr);
    }

    public static void wtf(@NonNull String msg) {
        Slog.wtf(getTag(), msg);
    }

    public static void wtf(@NonNull String msg, @Nullable Throwable tr) {
        Slog.wtf(getTag(), msg, tr);
    }
}
