/*
 * Copyright (C) 2015 The Android Open Source Project
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
package android.quicksettings.cts;

import static org.junit.Assert.assertTrue;

import org.junit.Test;

public class OnClickLaunchFgsTileServiceTest extends BaseTileServiceTest {
    private static final String TAG = "OnClickLaunchFgsTileServiceTest";

    /**
     * Custom tile services should be allowed to launch foreground service when user clicks the tile
     */
    @Test
    public void testAllowsOnClickLaunchFgs() throws Exception {
        initializeAndListen();
        clickTile(TestOnClickLaunchFgsTileService.getComponentName().flattenToString());
        waitFgsStarted();
    }
    /**
     * Waits for the foreground service launched. If it times out it fails the test.
     */
    private void waitFgsStarted() throws InterruptedException {
        int ct = 0;
        while (!CurrentTestState.isFgsLaunched() && (ct++ < CHECK_RETRIES)) {
            Thread.sleep(CHECK_DELAY);
        }
        assertTrue(CurrentTestState.isFgsLaunched());
    }

    @Override
    protected String getTag() {
        return TAG;
    }

    @Override
    protected String getComponentName() {
        return TestOnClickLaunchFgsTileService.getComponentName().flattenToString();
    }

    @Override
    protected String getTileServiceClassName() {
        return TestOnClickLaunchFgsTileService.class.getName();
    }

}
