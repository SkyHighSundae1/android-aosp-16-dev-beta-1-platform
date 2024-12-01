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

#ifndef ART_LIBNATIVELOADER_NATIVE_LOADER_TEST_H_
#define ART_LIBNATIVELOADER_NATIVE_LOADER_TEST_H_

#include <string.h>

#include <memory>

#include "gmock/gmock.h"
#include "jni.h"

namespace android {
namespace nativeloader {

class MockJni {
 public:
  virtual ~MockJni() {}
  MOCK_METHOD1(JniObject_getParent, const char*(const char*));
};

static std::unique_ptr<MockJni> jni_mock;

// A very simple JNI mock.
// jstring is a pointer to utf8 char array. We don't need utf16 char here.
// jobject, jclass, and jmethodID are also a pointer to utf8 char array
// Only a few JNI methods that are actually used in libnativeloader are mocked.
JNINativeInterface* CreateJNINativeInterface() {
  JNINativeInterface* inf = new JNINativeInterface();
  memset(inf, 0, sizeof(JNINativeInterface));

  inf->GetStringUTFChars = [](JNIEnv*, jstring s, jboolean*) -> const char* {
    return reinterpret_cast<const char*>(s);
  };

  inf->ReleaseStringUTFChars = [](JNIEnv*, jstring, const char*) -> void { return; };

  inf->NewStringUTF = [](JNIEnv*, const char* bytes) -> jstring {
    return reinterpret_cast<jstring>(const_cast<char*>(bytes));
  };

  inf->FindClass = [](JNIEnv*, const char* name) -> jclass {
    return reinterpret_cast<jclass>(const_cast<char*>(name));
  };

  inf->CallObjectMethodV = [](JNIEnv*, jobject obj, jmethodID mid, va_list) -> jobject {
    if (strcmp("getParent", reinterpret_cast<const char*>(mid)) == 0) {
      // JniObject_getParent can be a valid jobject or nullptr if there is
      // no parent classloader.
      const char* ret = jni_mock->JniObject_getParent(reinterpret_cast<const char*>(obj));
      return reinterpret_cast<jobject>(const_cast<char*>(ret));
    }
    return nullptr;
  };

  inf->GetMethodID = [](JNIEnv*, jclass, const char* name, const char*) -> jmethodID {
    return reinterpret_cast<jmethodID>(const_cast<char*>(name));
  };

  inf->NewWeakGlobalRef = [](JNIEnv*, jobject obj) -> jobject { return obj; };

  inf->IsSameObject = [](JNIEnv*, jobject a, jobject b) -> jboolean {
    return strcmp(reinterpret_cast<const char*>(a), reinterpret_cast<const char*>(b)) == 0;
  };

  return inf;
}

}  // namespace nativeloader
}  // namespace android

#endif  // ART_LIBNATIVELOADER_NATIVE_LOADER_TEST_H_
