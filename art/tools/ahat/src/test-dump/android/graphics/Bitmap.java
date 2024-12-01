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

package android.graphics;

/**
 * Fake android.graphics.Bitmap class that's minimum for testing
 */
public final class Bitmap {

  private final long mNativePtr;

  private final int mWidth;
  private final int mHeight;

  public Bitmap(int width, int height, long nativePtr, byte[] buffer) {
    this.mWidth = width;
    this.mHeight = height;
    this.mNativePtr = nativePtr;
    dumpData.add(nativePtr, buffer);
  }

  private static final class DumpData {
    private final int format;
    private final long[] natives;
    private final byte[][] buffers;
    private final int max;
    private int count;

    public DumpData(int format, int max) {
      this.max = max;
      this.format = format;
      this.natives = new long[max];
      this.buffers = new byte[max][];
      this.count = 0;
    }

    public void add(long nativePtr, byte[] buffer) {
      natives[count] = nativePtr;
      buffers[count] = buffer;
      count = (count >= max) ? max : count + 1;
    }
  };

  // assume default format 'PNG' and maximum 10 test bitmaps
  private static DumpData dumpData = new DumpData(1, 10);
}
