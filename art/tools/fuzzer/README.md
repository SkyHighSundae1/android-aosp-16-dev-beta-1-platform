# Building the ART Fuzzer

There are two ways to run one of the existing fuzzers: on host or on device.
The building and running takes place in the full Android platform
tree (aosp-main-with-phones). For host there's the possibility of using a
smaller AOSP Android manifest (master-art). The latter is faster to build,
because it only has the sources and dependencies required for the module.

In the following tutorial we use the class verification fuzzer. We set a shell
variable with the fuzzer's name for convenience. Their names can be found in
the Android.bp file, under the cc_fuzz build rules.

```
FUZZER_NAME=libart_verify_classes_fuzzer
```

## Common steps for host and device

1. Navigate to the root directory of the android repository.

2. From the console, set up the development environment.

    ```
    source build/envsetup.sh
    ```

3. Build the fuzzer for host/device

    The command is composed of:

    ```
    lunch <product>-trunk_staging-<variant>
    SANITIZE_HOST=address make ${FUZZER_NAME}
    ```

    For host you can use any valid lunch target, for example:

    ```
    lunch silvermont-trunk_staging-eng
    ```

    For device, you have to select your target according to the device
    you are using it to run the fuzzer.

    ```
    lunch aosp_husky-trunk_staging-userdebug
    ```

## Host

4. Run the fuzzer

    In this example we assume an x86_64 host architecture:

    ```
    out/host/linux-x86/fuzz/x86_64/${FUZZER_NAME}/${FUZZER_NAME} \
    out/host/linux-x86/fuzz/x86_64/${FUZZER_NAME}/corpus
    ```

    The first part of the command is the path to the fuzzer's binary, followed by the
    corpus. See [llvm.org/docs/LibFuzzer](https://llvm.org/docs/LibFuzzer.html#options)
    for more valid flags. For example, you can add the flag `-print_pcs=1` which makes
    it more verbose.

## Device

4. Add the fuzzer's files on the device

    ```
    adb root
    adb sync data
    ```

5. Run the fuzzer

    Any supported architecture can be used. For example, for arm64:

    ```
    adb shell /data/fuzz/arm64/${FUZZER_NAME}/${FUZZER_NAME} \
    /data/fuzz/arm64/${FUZZER_NAME}/corpus
    ```

    The first part of the command is the path to the fuzzer's binary and the next
    one is the corpus.

## Corpus

The fuzzer uses a corpus as a starting point in order to generate new inputs
representing DEX files. Our current corpus contains a mix of hand-created DEX
files, regression tests, and DEX files from our test suite. Also, when the fuzzer
generates a new input and it proves that it offers more code coverage,
it is added to the existing corpus as a DEX file.

If you want to run with the initial corpus, it needs to be removed and built again.

For host, assuming an x86_64 host architecture:

```
rm -rf out/host/linux-x86/fuzz/x86_64/${FUZZER_NAME}/corpus
SANITIZE_HOST=address make ${FUZZER_NAME}
```

For device, you also need to sync the data. For example, for arm64:

```
adb shell rm -rf /data/fuzz/arm64/${FUZZER_NAME}/corpus
SANITIZE_HOST=address make ${FUZZER_NAME}
adb sync data
```
