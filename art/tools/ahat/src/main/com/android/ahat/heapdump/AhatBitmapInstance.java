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

package com.android.ahat.heapdump;

import java.awt.image.BufferedImage;
import java.util.Arrays;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.stream.Collectors;
import com.google.common.collect.TreeMultimap;

/**
 * A java object that has `android.graphics.Bitmap` as its base class.
 */
public class AhatBitmapInstance extends AhatClassInstance implements Comparable<AhatBitmapInstance> {

  private BitmapInfo mBitmapInfo = null;

  AhatBitmapInstance(long id) {
    super(id);
  }

  @Override
  public boolean isBitmapInstance() {
    return true;
  }

  @Override
  public AhatBitmapInstance asBitmapInstance() {
    return this;
  }

  /**
   * simple order for all bitmap instances on TreeMultimap
   */
  public int compareTo(AhatBitmapInstance other) {
    return Long.compare(this.getId(), other.getId());
  }

  /**
   * Parsed information for bitmap contents dumped in the heapdump
   */
  public static class BitmapDumpData {
    private int count;
    // See android.graphics.Bitmap.CompressFormat for format values.
    // -1 means no compression for backward compatibility
    private int format;
    private Map<Long, byte[]> buffers;
    private Set<Long> referenced;
    private TreeMultimap<BitmapInfo, AhatBitmapInstance> instances;

    BitmapDumpData(int count, int format) {
      this.count = count;
      this.format = format;
      this.buffers = new HashMap<Long, byte[]>(count);
      this.referenced = new HashSet<Long>(count);
      this.instances = TreeMultimap.create();
    }
  };

  /**
   * find the BitmapDumpData that is included in the heap dump
   *
   * @param root root of the heap dump
   * @param instances all the instances from where the bitmap dump data will be excluded
   * @return true if valid bitmap dump data is found, false if not
   */
  public static BitmapDumpData findBitmapDumpData(SuperRoot root, Instances<AhatInstance> instances) {
    final BitmapDumpData result;
    AhatClassObj cls = null;

    for (Reference ref : root.getReferences()) {
      if (ref.ref.isClassObj()) {
        cls = ref.ref.asClassObj();
        if (cls.getName().equals("android.graphics.Bitmap")) {
          break;
        }
      }
    }

    if (cls == null) {
      return null;
    }

    Value value = cls.getStaticField("dumpData");
    if (value == null || !value.isAhatInstance()) {
      return null;
    }

    AhatClassInstance inst = value.asAhatInstance().asClassInstance();
    if (inst == null) {
        return null;
    }

    result = toBitmapDumpData(inst);
    if (result == null) {
      return null;
    }

    /* Build the map for all the bitmap instances with its BitmapInfo as key,
     * the map would be used to identify duplicated bitmaps later.  This also
     * initializes `mBitmapInfo` of each bitmap instance.
     */
    for (AhatInstance obj : instances) {
      AhatBitmapInstance bmp = obj.asBitmapInstance();
      if (bmp != null) {
        BitmapInfo info = bmp.getBitmapInfo(result);
        if (info != null) {
          result.instances.put(info, bmp);
        }
      }
    }

    /* remove all instances referenced from BitmapDumpData,
     * these instances shall *not* be counted
     */
    instances.removeIf(i -> { return result.referenced.contains(i.getId()); });
    return result;
  }

  private static BitmapDumpData toBitmapDumpData(AhatClassInstance inst) {
    if (!inst.isInstanceOfClass("android.graphics.Bitmap$DumpData")) {
      return null;
    }

    int count = inst.getIntField("count", 0);
    int format = inst.getIntField("format", -1);

    if (count == 0 || format == -1) {
      return null;
    }

    BitmapDumpData result = new BitmapDumpData(count, format);

    AhatArrayInstance natives = inst.getArrayField("natives");
    AhatArrayInstance buffers = inst.getArrayField("buffers");
    if (natives == null || buffers == null) {
      return null;
    }

    result.referenced.add(natives.getId());
    result.referenced.add(buffers.getId());

    result.buffers = new HashMap<>(result.count);
    for (int i = 0; i < result.count; i++) {
      Value nativePtr = natives.getValue(i);
      Value bufferVal = buffers.getValue(i);
      if (nativePtr == null || bufferVal == null) {
        continue;
      }
      AhatInstance buffer = bufferVal.asAhatInstance();
      result.buffers.put(nativePtr.asLong(), buffer.asArrayInstance().asByteArray());
      result.referenced.add(buffer.getId());
    }
    return result;
  }

  /**
   * find duplicated bitmap instances
   *
   * @param bitmapDumpData parsed bitmap dump data
   * @return A list of duplicated bitmaps (the same duplication stored in a sub-list)
   */
  public static List<List<AhatBitmapInstance>> findDuplicates(BitmapDumpData bitmapDumpData) {
    if (bitmapDumpData != null) {
      return bitmapDumpData.instances.keySet().stream()
          .filter(k -> bitmapDumpData.instances.get(k).size() > 1)
          .map(k -> new ArrayList<>(bitmapDumpData.instances.get(k)))
          .collect(Collectors.toList());
    }
    return null;
  }

  private static class BitmapInfo implements Comparable<BitmapInfo> {
    private final int width;
    private final int height;
    private final int format;
    private final byte[] buffer;
    private final int bufferHash;

    public BitmapInfo(int width, int height, int format, byte[] buffer) {
      this.width = width;
      this.height = height;
      this.format = format;
      this.buffer = buffer;
      bufferHash = Arrays.hashCode(buffer);
    }

    @Override
    public int hashCode() {
      return Objects.hash(width, height, format, bufferHash);
    }

    /**
     * order bitmaps by their size (dimension WxH), format, and buffer hash,
     * by default in descending order so large bitmaps are more significant
     */
    @Override
    public int compareTo(BitmapInfo other) {
      if (other == this) {
        return 0;
      }
      if (other.width * other.height != this.width * this.height) {
        return other.width * other.height - this.width * this.height;
      }
      if (other.format != this.format) {
        return other.format - this.format;
      }
      if (other.bufferHash != this.bufferHash) {
        return other.bufferHash - this.bufferHash;
      }
      return 0;
    }

    @Override
    public boolean equals(Object o) {
      if (o == this) {
        return true;
      }
      if (!(o instanceof BitmapInfo)) {
        return false;
      }
      BitmapInfo other = (BitmapInfo)o;
      return (this.width == other.width)
          && (this.height == other.height)
          && (this.format == other.format)
          && (this.bufferHash == other.bufferHash);
    }
  }

  /**
   * Return bitmap info for this object, or null if no appropriate bitmap
   * info is available.
   */
  private BitmapInfo getBitmapInfo(BitmapDumpData bitmapDumpData) {
    if (mBitmapInfo != null) {
      return mBitmapInfo;
    }

    if (!isInstanceOfClass("android.graphics.Bitmap")) {
      return null;
    }

    Integer width = getIntField("mWidth", null);
    if (width == null) {
      return null;
    }

    Integer height = getIntField("mHeight", null);
    if (height == null) {
      return null;
    }

    byte[] buffer = getByteArrayField("mBuffer");
    if (buffer != null) {
      if (buffer.length < 4 * height * width) {
        return null;
      }
      mBitmapInfo = new BitmapInfo(width, height, -1, buffer);
      return mBitmapInfo;
    }

    long nativePtr = getLongField("mNativePtr", -1l);
    if (nativePtr == -1) {
      return null;
    }

    if (bitmapDumpData == null || bitmapDumpData.count == 0) {
      return null;
    }

    if (!bitmapDumpData.buffers.containsKey(nativePtr)) {
      return null;
    }

    buffer = bitmapDumpData.buffers.get(nativePtr);
    if (buffer == null) {
      return null;
    }

    mBitmapInfo = new BitmapInfo(width, height, bitmapDumpData.format, buffer);
    return mBitmapInfo;
  }

  /**
   * Represents a bitmap with either
   *   - its format and content in `buffer`
   *   - or a BufferedImage with its raw pixels
   */
  public static class Bitmap {
    /**
     * format of the bitmap content in buffer
     */
    public String format;
    /**
     * byte buffer of the bitmap content
     */
    public byte[] buffer;
    /**
     * BufferedImage with the bitmap's raw pixels
     */
    public BufferedImage image;

    /**
     * Initialize a Bitmap instance
     * @param format - format of the bitmap
     * @param buffer - buffer of the bitmap content
     * @param image  - BufferedImage with the bitmap's raw pixel
     */
    public Bitmap(String format, byte[] buffer, BufferedImage image) {
      this.format = format;
      this.buffer = buffer;
      this.image = image;
    }
  }

  private BufferedImage asBufferedImage(BitmapInfo info) {
    // Convert the raw data to an image
    // Convert BGRA to ABGR
    int[] abgr = new int[info.height * info.width];
    for (int i = 0; i < abgr.length; i++) {
      abgr[i] = (
          (((int) info.buffer[i * 4 + 3] & 0xFF) << 24)
          + (((int) info.buffer[i * 4 + 0] & 0xFF) << 16)
          + (((int) info.buffer[i * 4 + 1] & 0xFF) << 8)
          + ((int) info.buffer[i * 4 + 2] & 0xFF));
    }

    BufferedImage bitmap = new BufferedImage(
        info.width, info.height, BufferedImage.TYPE_4BYTE_ABGR);
    bitmap.setRGB(0, 0, info.width, info.height, abgr, 0, info.width);
    return bitmap;
  }

  /**
   * Returns the bitmap associated with this instance.
   * This is relevant for instances of android.graphics.Bitmap.
   * Returns null if there is no bitmap pixel data associated
   * with the given instance.
   *
   * @return the bitmap pixel data associated with this image
   */
  public Bitmap getBitmap() {
    final BitmapInfo info = mBitmapInfo;
    if (info == null) {
      return null;
    }

    /**
     * See android.graphics.Bitmap.CompressFormat for definitions
     * -1 for legacy objects with content in `Bitmap.mBuffer`
     */
    switch (info.format) {
    case 0: /* JPEG */
      return new Bitmap("image/jpg", info.buffer, null);
    case 1: /* PNG */
      return new Bitmap("image/png", info.buffer, null);
    case 2: /* WEBP */
    case 3: /* WEBP_LOSSY */
    case 4: /* WEBP_LOSSLESS */
      return new Bitmap("image/webp", info.buffer, null);
    case -1:/* Legacy */
      return new Bitmap(null, null, asBufferedImage(info));
    default:
      return null;
    }
  }

}
