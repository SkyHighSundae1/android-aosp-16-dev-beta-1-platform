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

#include "service.h"

#include <jni.h>

#include <filesystem>
#include <string_view>

#include "android-base/errors.h"
#include "android-base/file.h"
#include "android-base/properties.h"
#include "android-base/result.h"
#include "class_loader_context.h"
#include "gc/heap.h"
#include "nativehelper/JNIHelp.h"
#include "nativehelper/utils.h"
#include "runtime.h"
#include "tools/tools.h"

namespace art {
namespace service {

using ::android::base::Dirname;
using ::android::base::Result;
using ::android::base::SetProperty;

Result<void> ValidateAbsoluteNormalPath(const std::string& path_str) {
  if (path_str.empty()) {
    return Errorf("Path is empty");
  }
  if (path_str.find('\0') != std::string::npos) {
    return Errorf("Path '{}' has invalid character '\\0'", path_str);
  }
  std::filesystem::path path(path_str);
  if (!path.is_absolute()) {
    return Errorf("Path '{}' is not an absolute path", path_str);
  }
  if (path.lexically_normal() != path_str) {
    return Errorf("Path '{}' is not in normal form", path_str);
  }
  return {};
}

Result<void> ValidatePathElementSubstring(const std::string& path_element_substring,
                                          const std::string& name) {
  if (path_element_substring.empty()) {
    return Errorf("{} is empty", name);
  }
  if (path_element_substring.find('/') != std::string::npos) {
    return Errorf("{} '{}' has invalid character '/'", name, path_element_substring);
  }
  if (path_element_substring.find('\0') != std::string::npos) {
    return Errorf("{} '{}' has invalid character '\\0'", name, path_element_substring);
  }
  return {};
}

Result<void> ValidatePathElement(const std::string& path_element, const std::string& name) {
  OR_RETURN(ValidatePathElementSubstring(path_element, name));
  if (path_element == "." || path_element == "..") {
    return Errorf("Invalid {} '{}'", name, path_element);
  }
  return {};
}

Result<void> ValidateDexPath(const std::string& dex_path) {
  OR_RETURN(ValidateAbsoluteNormalPath(dex_path));
  return {};
}

android::base::Result<void> ValidateClassLoaderContext(std::string_view dex_path,
                                                       const std::string& class_loader_context) {
  if (class_loader_context == ClassLoaderContext::kUnsupportedClassLoaderContextEncoding) {
    return {};
  }

  std::unique_ptr<ClassLoaderContext> context = ClassLoaderContext::Create(class_loader_context);
  if (context == nullptr) {
    return Errorf("Class loader context '{}' is invalid", class_loader_context);
  }

  std::vector<std::string> flattened_context = context->FlattenDexPaths();
  std::string dex_dir = Dirname(dex_path);
  for (const std::string& context_element : flattened_context) {
    std::string context_path = std::filesystem::path(dex_dir).append(context_element);
    OR_RETURN(ValidateDexPath(context_path));
  }

  return {};
}

std::string GetGarbageCollector() {
  return Runtime::Current()->GetHeap()->GetForegroundCollectorName();
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_android_server_art_ArtJni_validateDexPathNative(JNIEnv* env, jobject, jstring j_dex_path) {
  std::string dex_path(GET_UTF_OR_RETURN(env, j_dex_path));

  if (Result<void> result = ValidateDexPath(dex_path); !result.ok()) {
    return CREATE_UTF_OR_RETURN(env, result.error().message()).release();
  } else {
    return nullptr;
  }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_android_server_art_ArtJni_validateClassLoaderContextNative(
    JNIEnv* env, jobject, jstring j_dex_path, jstring j_class_loader_context) {
  ScopedUtfChars dex_path = GET_UTF_OR_RETURN(env, j_dex_path);
  std::string class_loader_context(GET_UTF_OR_RETURN(env, j_class_loader_context));

  if (Result<void> result = ValidateClassLoaderContext(dex_path, class_loader_context);
      !result.ok()) {
    return CREATE_UTF_OR_RETURN(env, result.error().message()).release();
  } else {
    return nullptr;
  }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_android_server_art_ArtJni_getGarbageCollectorNative(JNIEnv* env, jobject) {
  return CREATE_UTF_OR_RETURN(env, GetGarbageCollector()).release();
}

extern "C" JNIEXPORT void JNICALL Java_com_android_server_art_ArtJni_setPropertyNative(
    JNIEnv* env, jobject, jstring j_key, jstring j_value) {
  std::string key(GET_UTF_OR_RETURN_VOID(env, j_key));
  std::string value(GET_UTF_OR_RETURN_VOID(env, j_value));
  if (!SetProperty(key, value)) {
    jniThrowExceptionFmt(env,
                         "java/lang/IllegalStateException",
                         "Failed to set property '%s' to '%s'",
                         key.c_str(),
                         value.c_str());
  }
}

extern "C" JNIEXPORT void JNICALL Java_com_android_server_art_ArtJni_ensureNoProcessInDirNative(
    JNIEnv* env, jobject, jstring j_dir, jint j_timeout_ms) {
  if (j_timeout_ms < 0) {
    jniThrowExceptionFmt(
        env, "java/lang/IllegalArgumentException", "Negative timeout '%d'", j_timeout_ms);
    return;
  }
  std::string dir(GET_UTF_OR_RETURN_VOID(env, j_dir));
  if (Result<void> result = tools::EnsureNoProcessInDir(dir, j_timeout_ms, /*try_kill=*/true);
      !result.ok()) {
    jniThrowException(env, "java/io/IOException", result.error().message().c_str());
  }
}

}  // namespace service
}  // namespace art
