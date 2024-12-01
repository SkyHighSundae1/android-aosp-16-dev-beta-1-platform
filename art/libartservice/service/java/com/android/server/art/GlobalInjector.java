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
import android.os.ArtModuleServiceManager;
import android.os.Build;

import androidx.annotation.RequiresApi;

import com.android.internal.annotations.GuardedBy;
import com.android.server.LocalManagerRegistry;

import dalvik.system.VMRuntime;

import java.util.Objects;

/**
 * An interface for getting different behaviors depending on whether the code is running for
 * Pre-reboot Dexopt or not.
 *
 * @hide
 */
@RequiresApi(Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
public abstract class GlobalInjector {
    // The static field is associated with the class and the class loader that loads it. In the
    // Pre-reboot Dexopt case, this class is loaded by a separate class loader, so it doesn't share
    // the same static field with the class outside of the class loader.
    @GuardedBy("GlobalInjector.class") @Nullable private static GlobalInjector sInstance;

    /** Returns the instance set by {@link #setInstance} or the default instance. */
    @NonNull
    public static synchronized GlobalInjector getInstance() {
        if (sInstance == null) {
            sInstance = new DefaultGlobalInjector();
        }
        return sInstance;
    }

    /**
     * Sets the global injector instance. Can only be called before the first call to {@link
     * #getInstance}.
     */
    public static synchronized void setInstance(@NonNull GlobalInjector instance) {
        if (sInstance != null) {
            throw new IllegalStateException("GlobalInjector already initialized");
        }
        sInstance = instance;
    }

    /** Whether the code is running for Pre-reboot Dexopt or not. */
    public abstract boolean isPreReboot();

    public abstract void checkArtModuleServiceManager();

    @NonNull public abstract IArtd getArtd();

    @RequiresApi(Build.VERSION_CODES.VANILLA_ICE_CREAM)
    @NonNull
    public abstract IDexoptChrootSetup getDexoptChrootSetup();

    @NonNull public abstract DexUseManagerLocal getDexUseManager();

    /**
     * The default implementation that is used when the code is running in the normal situation
     * (i.e., not for Pre-reboot Dexopt).
     */
    public static class DefaultGlobalInjector extends GlobalInjector {
        @Override
        public boolean isPreReboot() {
            return false;
        }

        @Override
        public void checkArtModuleServiceManager() {
            Objects.requireNonNull(ArtModuleServiceInitializer.getArtModuleServiceManager());
        }

        @Override
        @NonNull
        public IArtd getArtd() {
            IArtd artd =
                    IArtd.Stub.asInterface(ArtModuleServiceInitializer.getArtModuleServiceManager()
                                                   .getArtdServiceRegisterer()
                                                   .waitForService());
            if (artd == null) {
                throw new IllegalStateException("Unable to connect to artd");
            }
            return artd;
        }

        @RequiresApi(Build.VERSION_CODES.VANILLA_ICE_CREAM)
        @Override
        @NonNull
        public IDexoptChrootSetup getDexoptChrootSetup() {
            IDexoptChrootSetup dexoptChrootSetup = IDexoptChrootSetup.Stub.asInterface(
                    ArtModuleServiceInitializer.getArtModuleServiceManager()
                            .getDexoptChrootSetupServiceRegisterer()
                            .waitForService());
            if (dexoptChrootSetup == null) {
                throw new IllegalStateException("Unable to connect to dexopt_chroot_setup");
            }
            return dexoptChrootSetup;
        }

        @Override
        @NonNull
        public DexUseManagerLocal getDexUseManager() {
            return Objects.requireNonNull(
                    LocalManagerRegistry.getManager(DexUseManagerLocal.class));
        }
    }
}
