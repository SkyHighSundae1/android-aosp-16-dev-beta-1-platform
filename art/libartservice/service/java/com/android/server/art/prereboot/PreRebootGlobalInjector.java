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
import android.os.RemoteException;

import androidx.annotation.RequiresApi;

import com.android.server.art.ArtdRefCache;
import com.android.server.art.AsLog;
import com.android.server.art.DexUseManagerLocal;
import com.android.server.art.GlobalInjector;
import com.android.server.art.IArtd;
import com.android.server.art.IArtdCancellationSignal;
import com.android.server.art.IDexoptChrootSetup;

import java.util.Objects;

/**
 * The implementation of the Global injector that is used when the code is running Pre-reboot
 * Dexopt.
 *
 * During Pre-reboot Dexopt, the new version of this code is run.
 *
 * @hide
 */
@RequiresApi(Build.VERSION_CODES.VANILLA_ICE_CREAM)
public class PreRebootGlobalInjector extends GlobalInjector {
    @NonNull private ArtModuleServiceManager mArtModuleServiceManager;
    @Nullable private DexUseManagerLocal mDexUseManager;

    private PreRebootGlobalInjector(@NonNull ArtModuleServiceManager artModuleServiceManager) {
        mArtModuleServiceManager = artModuleServiceManager;
    }

    /** Returns true on success, or false on cancellation. Throws on failure. */
    public static boolean init(@NonNull ArtModuleServiceManager artModuleServiceManager,
            @NonNull Context context, @NonNull CancellationSignal cancellationSignal) {
        var instance = new PreRebootGlobalInjector(artModuleServiceManager);
        GlobalInjector.setInstance(instance);
        // This call throws if artd cannot be initialized.
        if (!instance.initArtd(cancellationSignal)) {
            return false;
        }
        try (var pin = ArtdRefCache.getInstance().new Pin()) {
            instance.mDexUseManager = DexUseManagerLocal.createInstance(context);
        }
        return true;
    }

    @Override
    public boolean isPreReboot() {
        return true;
    }

    @Override
    public void checkArtModuleServiceManager() {}

    @NonNull
    private IArtd getArtdNoInit() {
        IArtd artd = IArtd.Stub.asInterface(
                mArtModuleServiceManager.getArtdPreRebootServiceRegisterer().waitForService());
        if (artd == null) {
            throw new IllegalStateException("Unable to connect to artd for pre-reboot dexopt");
        }
        return artd;
    }

    private boolean initArtd(@NonNull CancellationSignal cancellationSignal) {
        if (cancellationSignal.isCanceled()) {
            return false;
        }
        try {
            IArtd artd = getArtdNoInit();
            IArtdCancellationSignal artdCancellationSignal = artd.createCancellationSignal();
            cancellationSignal.setOnCancelListener(() -> {
                try {
                    artdCancellationSignal.cancel();
                } catch (RemoteException e) {
                    AsLog.e("An error occurred when sending a cancellation signal", e);
                }
            });
            return artd.preRebootInit(artdCancellationSignal);
        } catch (RemoteException e) {
            throw new IllegalStateException("Unable to initialize artd for pre-reboot dexopt", e);
        } finally {
            // Make sure artd does not leak even if the caller holds `cancellationSignal` forever.
            cancellationSignal.setOnCancelListener(null);
        }
    }

    @Override
    @NonNull
    public IArtd getArtd() {
        IArtd artd = getArtdNoInit();
        try {
            artd.preRebootInit(null /* cancellationSignal */);
        } catch (RemoteException e) {
            throw new IllegalStateException("Unable to initialize artd for pre-reboot dexopt", e);
        }
        return artd;
    }

    @Override
    @NonNull
    public IDexoptChrootSetup getDexoptChrootSetup() {
        throw new UnsupportedOperationException();
    }

    @Override
    @NonNull
    public DexUseManagerLocal getDexUseManager() {
        return Objects.requireNonNull(mDexUseManager);
    }
}
