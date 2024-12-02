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

#ifndef ART_COMPILER_OPTIMIZING_HANDLE_CACHE_H_
#define ART_COMPILER_OPTIMIZING_HANDLE_CACHE_H_

#include "base/macros.h"
#include "class_root.h"
#include "reference_type_info.h"

namespace art HIDDEN {

class VariableSizedHandleScope;

class HandleCache {
 public:
  explicit HandleCache(VariableSizedHandleScope* handles) : handles_(handles) { }

  VariableSizedHandleScope* GetHandles() { return handles_; }

  template <typename T>
  MutableHandle<T> NewHandle(T* object) REQUIRES_SHARED(Locks::mutator_lock_);

  template <typename T>
  MutableHandle<T> NewHandle(ObjPtr<T> object) REQUIRES_SHARED(Locks::mutator_lock_);

  ReferenceTypeInfo::TypeHandle GetObjectClassHandle() {
    return GetRootHandle(ClassRoot::kJavaLangObject, &object_class_handle_);
  }

  ReferenceTypeInfo::TypeHandle GetClassClassHandle() {
    return GetRootHandle(ClassRoot::kJavaLangClass, &class_class_handle_);
  }

  ReferenceTypeInfo::TypeHandle GetMethodHandleClassHandle() {
    return GetRootHandle(ClassRoot::kJavaLangInvokeMethodHandleImpl, &method_handle_class_handle_);
  }

  ReferenceTypeInfo::TypeHandle GetMethodTypeClassHandle() {
    return GetRootHandle(ClassRoot::kJavaLangInvokeMethodType, &method_type_class_handle_);
  }

  ReferenceTypeInfo::TypeHandle GetStringClassHandle() {
    return GetRootHandle(ClassRoot::kJavaLangString, &string_class_handle_);
  }

  ReferenceTypeInfo::TypeHandle GetThrowableClassHandle() {
    return GetRootHandle(ClassRoot::kJavaLangThrowable, &throwable_class_handle_);
  }

 private:
  inline ReferenceTypeInfo::TypeHandle GetRootHandle(ClassRoot class_root,
                                                     ReferenceTypeInfo::TypeHandle* cache) {
    if (UNLIKELY(!ReferenceTypeInfo::IsValidHandle(*cache))) {
      *cache = CreateRootHandle(handles_, class_root);
    }
    return *cache;
  }

  static ReferenceTypeInfo::TypeHandle CreateRootHandle(VariableSizedHandleScope* handles,
                                                        ClassRoot class_root);

  VariableSizedHandleScope* handles_;

  ReferenceTypeInfo::TypeHandle object_class_handle_;
  ReferenceTypeInfo::TypeHandle class_class_handle_;
  ReferenceTypeInfo::TypeHandle method_handle_class_handle_;
  ReferenceTypeInfo::TypeHandle method_type_class_handle_;
  ReferenceTypeInfo::TypeHandle string_class_handle_;
  ReferenceTypeInfo::TypeHandle throwable_class_handle_;
};

}  // namespace art

#endif  // ART_COMPILER_OPTIMIZING_HANDLE_CACHE_H_

