# How to update dirty-image-objects

1. Install ART APEX with imgdiag and reboot, e.g.:

```
. ./build/envsetup.sh
banchan test_imgdiag_com.android.art module_arm64
m apps_only dist
adb install out/dist/test_imgdiag_com.android.art.apex
adb reboot
```

2. Collect imgdiag output.

```
# To see all options check: art/imgdiag/run_imgdiag.py -h

art/imgdiag/run_imgdiag.py
```

3. Create new dirty-image-objects.

```
# To see all options check: art/imgdiag/create_dirty_image_objects.py -h

# Using all imgdiag files:
art/imgdiag/create_dirty_image_objects.py ./imgdiag_*

# Or using only specified files:
art/imgdiag/create_dirty_image_objects.py \
  ./imgdiag_system_server.txt \
  ./imgdiag_com.android.systemui.txt \
  ./imgdiag_com.google.android.gms.txt \
  ./imgdiag_com.google.android.gms.persistent.txt \
  ./imgdiag_com.google.android.gms.ui.txt \
  ./imgdiag_com.google.android.gms.unstable.txt
```

The resulting file will contain a list of dirty objects with optional
(enabled by default) sort keys in the following format:
```
<class_descriptor>[.<reference_field_name>:<reference_field_type>]* [<sort_key>]
```
Classes are specified using a descriptor and objects are specified by
a reference chain starting from a class. Example:
```
# Mark FileUtils class as dirty:
Landroid/os/FileUtils; 4
# Mark instance of Property class as dirty:
Landroid/view/View;.SCALE_X:Landroid/util/Property; 4
```
If present, sort keys are used to specify the ordering between dirty entries.
All dirty objects will be placed in the dirty bin of the boot image and sorted
by the sort\_key values. I.e., dirty entries with sort\_key==N will have lower
address than entries with sort\_key==N+1.

4. Push new dirty-image-objects to the device.

```
adb push dirty-image-objects.txt /etc/dirty-image-objects
```

5. Reinstall ART APEX to update the boot image.

```
adb install out/dist/com.android.art.apex
adb reboot
```

At this point the device should have new `boot.art` with optimized dirty object layout.
This can be checked by collecting imgdiag output again and comparing dirty page counts to the previous run.
