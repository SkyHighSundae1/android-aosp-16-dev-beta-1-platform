/*
 * Copyright (C) 2021 The Android Open Source Project
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

import static android.content.pm.ServiceInfo.FOREGROUND_SERVICE_TYPE_SHORT_SERVICE;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.ComponentName;
import android.content.Intent;
import android.os.IBinder;

public class TestOnClickLaunchFgsTileService extends TestTileService {

    public static final String TAG = "TestOnClickLaunchFgsTileService";
    public static final String PKG = "android.app.stubs";

    public static String getId() {
        return String.format("%s/%s", TestOnClickLaunchFgsTileService.class.getPackage().getName(),
                TestOnClickLaunchFgsTileService.class.getName());
    }

    public static ComponentName getComponentName() {
        return new ComponentName(TestOnClickLaunchFgsTileService.class.getPackage().getName(),
                TestOnClickLaunchFgsTileService.class.getName());
    }

    @Override
    public void onClick() {
        startForegroundService(new Intent(this, TestForegroundService.class));
        CurrentTestState.setTileHasBeenClicked(true);
    }

    public static class TestForegroundService extends Service {
        private static final String CHANNEL_ID = "test_FGS_channel";

        @Override
        public void onCreate() {
            super.onCreate();
            // Create a notification channel for Android Oreo and above
            NotificationChannel notificationChannel = new NotificationChannel(CHANNEL_ID,
                    "Foreground Service", NotificationManager.IMPORTANCE_DEFAULT);
            notificationChannel.setDescription("Notification for the foreground service");
            NotificationManager notificationManager = getSystemService(NotificationManager.class);
            notificationManager.createNotificationChannel(notificationChannel);
        }
        @Override
        public int onStartCommand(Intent intent, int flags, int startId) {
            // Create a notification to show the service is running
            Notification notification = new Notification.Builder(this, CHANNEL_ID)
                    .setSmallIcon(R.drawable.robot)
                    .setContentTitle("Test FGS")
                    .setContentText("Test FGS content").build();
            startForeground(101, notification, FOREGROUND_SERVICE_TYPE_SHORT_SERVICE);
            CurrentTestState.setFgsLaunched(true);
            try {
                Thread.sleep(1000);
            } catch (InterruptedException e) {
                throw new RuntimeException(e);
            }
            stopSelf();
            return super.onStartCommand(intent, flags, startId);
        }

        @Override
        public IBinder onBind(Intent intent) {
            return null;
        }
    }
}
