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

import static android.os.IBinder.DeathRecipient;

import android.annotation.NonNull;
import android.annotation.Nullable;
import android.os.Build;
import android.os.IBinder;
import android.os.RemoteException;
import android.system.SystemCleaner;
import android.util.CloseGuard;

import androidx.annotation.RequiresApi;

import com.android.internal.annotations.GuardedBy;
import com.android.internal.annotations.VisibleForTesting;

import java.lang.ref.Cleaner;
import java.lang.ref.Reference;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;

/**
 * A helper class that caches a reference to artd, to avoid repetitive calls to `waitForService`,
 * as the latter is considered expensive.
 *
 * @hide
 */
@RequiresApi(Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
public class ArtdRefCache {
    // The 15s timeout is arbitrarily picked.
    // TODO(jiakaiz): Revisit this based on real CUJs.
    @VisibleForTesting public static final long CACHE_TIMEOUT_MS = 15_000;

    // The static field is associated with the class and the class loader that loads it. In the
    // Pre-reboot Dexopt case, this class is loaded by a separate class loader, so it doesn't share
    // the same static field with the class outside of the class loader.
    @GuardedBy("ArtdRefCache.class") @Nullable private static ArtdRefCache sInstance = null;

    @NonNull private final Injector mInjector;
    @NonNull private final Debouncer mDebouncer;

    /**
     * A lock that guards the <b>reference</b> to artd.
     *
     * Warning: This lock does not guard artd itself. Do not hold this lock when calling artd as it
     * will prevent parallelism.
     */
    @NonNull private final Object mLock = new Object();

    @GuardedBy("mLock") private int mPinCount = 0;
    @GuardedBy("mLock") @Nullable private IArtd mArtd = null;

    public ArtdRefCache() {
        this(new Injector());
    }

    @VisibleForTesting
    public ArtdRefCache(@NonNull Injector injector) {
        mInjector = injector;
        mDebouncer = new Debouncer(CACHE_TIMEOUT_MS, mInjector::createScheduledExecutor);
    }

    @NonNull
    public static synchronized ArtdRefCache getInstance() {
        if (sInstance == null) {
            sInstance = new ArtdRefCache();
        }
        return sInstance;
    }

    /**
     * Returns a reference to artd, from the cache or created on demand.
     *
     * If this method is called when there is no pin, it behaves as if a pin is created and
     * immediately destroyed.
     */
    @NonNull
    public IArtd getArtd() {
        synchronized (mLock) {
            if (mArtd == null) {
                IArtd artd = mInjector.getArtd();
                try {
                    // Force clear the cache when the artd instance is dead.
                    artd.asBinder().linkToDeath(new CacheDeathRecipient(), 0 /* flags */);
                    // Cache the instance.
                    mArtd = artd;
                } catch (RemoteException e) {
                    Utils.logArtdException(e);
                    // Not expected. Let the caller decide what to do with it.
                    return artd;
                }
            }
            delayedDropIfNoPinLocked();
            return mArtd;
        }
    }

    /**
     * Resets ArtdRefCache to its initial state. ArtdRefCache is guaranteed to be GC-able after
     * this call.
     *
     * Can only be called when there is no pin.
     */
    public void reset() {
        synchronized (mLock) {
            if (mPinCount != 0) {
                throw new IllegalStateException("Cannot reset ArtdRefCache when there are pins");
            }
            mArtd = null;
            mDebouncer.cancel();
        }
    }

    @GuardedBy("mLock")
    private void delayedDropIfNoPinLocked() {
        if (mPinCount == 0) {
            // During the timeout:
            // - If there is no more pinning and unpinning, the cache will be dropped.
            // - If there are pinnings and unpinnings, and `mPinCount` never reaches 0 again,
            //   `dropIfNoPin` will be run, but it will not drop the cache.
            // - If there are pinnings and unpinnings, and `mPinCount` reaches 0 again,
            //   `dropIfNoPin` will be debounced.
            mDebouncer.maybeRunAsync(this::dropIfNoPin);
        }
    }

    private void dropIfNoPin() {
        synchronized (mLock) {
            if (mPinCount == 0) {
                mArtd = null;
            }
        }
    }

    /**
     * A scope that pins any reference to artd, either an existing one or one created within the
     * scope. The reference is dropped when there is no more pin within {@link #CACHE_TIMEOUT_MS}.
     */
    public class Pin implements AutoCloseable {
        @NonNull private final CloseGuard mGuard = new CloseGuard();
        @NonNull private final Cleaner.Cleanable mCleanable;

        public Pin() {
            synchronized (mLock) {
                mPinCount++;
            }
            mGuard.open("close");
            mCleanable =
                    SystemCleaner.cleaner().register(this, new Cleanup(ArtdRefCache.this, mGuard));
        }

        @Override
        public void close() {
            try {
                mGuard.close();
                mCleanable.clean();
            } finally {
                // This prevents the cleaner from running the cleanup during the execution of
                // `close`.
                Reference.reachabilityFence(this);
            }
        }

        // Don't use a lambda. See {@link Cleaner} for the reason.
        static class Cleanup implements Runnable {
            @NonNull private final ArtdRefCache mRefCache;
            @NonNull private final CloseGuard mGuard;

            Cleanup(@NonNull ArtdRefCache refCache, @NonNull CloseGuard guard) {
                mRefCache = refCache;
                mGuard = guard;
            }

            @Override
            public void run() {
                mGuard.warnIfOpen();
                synchronized (mRefCache.mLock) {
                    mRefCache.mPinCount--;
                    Utils.check(mRefCache.mPinCount >= 0);
                    mRefCache.delayedDropIfNoPinLocked();
                }
            }
        }
    }

    private class CacheDeathRecipient implements DeathRecipient {
        @Override
        public void binderDied() {
            // Legacy.
        }

        @Override
        public void binderDied(@NonNull IBinder who) {
            synchronized (mLock) {
                if (mArtd != null && mArtd.asBinder() == who) {
                    mArtd = null;
                }
            }
        }
    }

    /** Injector pattern for testing purpose. */
    @VisibleForTesting
    public static class Injector {
        Injector() {
            // Call the getters for various dependencies, to ensure correct initialization order.
            GlobalInjector.getInstance().checkArtModuleServiceManager();
        }

        @NonNull
        public IArtd getArtd() {
            return GlobalInjector.getInstance().getArtd();
        }

        @NonNull
        public ScheduledExecutorService createScheduledExecutor() {
            return Executors.newScheduledThreadPool(1 /* corePoolSize */);
        }
    }
}
