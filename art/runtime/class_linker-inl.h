/*
 * Copyright (C) 2011 The Android Open Source Project
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

#ifndef ART_RUNTIME_CLASS_LINKER_INL_H_
#define ART_RUNTIME_CLASS_LINKER_INL_H_

#include <atomic>

#include "android-base/thread_annotations.h"
#include "art_field-inl.h"
#include "art_method-inl.h"
#include "base/mutex.h"
#include "class_linker.h"
#include "class_table-inl.h"
#include "dex/dex_file-inl.h"
#include "dex/dex_file_structs.h"
#include "gc_root-inl.h"
#include "handle_scope-inl.h"
#include "jni/jni_internal.h"
#include "mirror/class_loader.h"
#include "mirror/dex_cache-inl.h"
#include "mirror/iftable.h"
#include "mirror/object_array-inl.h"
#include "obj_ptr-inl.h"
#include "scoped_thread_state_change-inl.h"

namespace art HIDDEN {

inline ObjPtr<mirror::Class> ClassLinker::FindArrayClass(Thread* self,
                                                         ObjPtr<mirror::Class> element_class) {
  for (size_t i = 0; i < kFindArrayCacheSize; ++i) {
    // Read the cached array class once to avoid races with other threads setting it.
    ObjPtr<mirror::Class> array_class =
        find_array_class_cache_[i].load(std::memory_order_acquire).Read();
    if (array_class != nullptr && array_class->GetComponentType() == element_class) {
      return array_class;
    }
  }
  std::string descriptor = "[";
  std::string temp;
  descriptor += element_class->GetDescriptor(&temp);
  StackHandleScope<1> hs(Thread::Current());
  Handle<mirror::ClassLoader> class_loader(hs.NewHandle(element_class->GetClassLoader()));
  ObjPtr<mirror::Class> array_class =
      FindClass(self, descriptor.c_str(), descriptor.length(), class_loader);
  if (array_class != nullptr) {
    // Benign races in storing array class and incrementing index.
    size_t victim_index = find_array_class_cache_next_victim_;
    find_array_class_cache_[victim_index].store(GcRoot<mirror::Class>(array_class),
                                                std::memory_order_release);
    find_array_class_cache_next_victim_ = (victim_index + 1) % kFindArrayCacheSize;
  } else {
    // We should have a NoClassDefFoundError.
    self->AssertPendingException();
  }
  return array_class;
}

inline ObjPtr<mirror::String> ClassLinker::ResolveString(dex::StringIndex string_idx,
                                                         ArtField* referrer) {
  Thread::PoisonObjectPointersIfDebug();
  DCHECK(!Thread::Current()->IsExceptionPending());
  ObjPtr<mirror::DexCache> dex_cache = referrer->GetDexCache();
  ObjPtr<mirror::String> resolved = dex_cache->GetResolvedString(string_idx);
  if (resolved == nullptr) {
    resolved = DoResolveString(string_idx, dex_cache);
  }
  return resolved;
}

inline ObjPtr<mirror::String> ClassLinker::ResolveString(dex::StringIndex string_idx,
                                                         ArtMethod* referrer) {
  Thread::PoisonObjectPointersIfDebug();
  DCHECK(!Thread::Current()->IsExceptionPending());
  ObjPtr<mirror::DexCache> dex_cache = referrer->GetDexCache();
  ObjPtr<mirror::String> resolved = dex_cache->GetResolvedString(string_idx);
  if (resolved == nullptr) {
    resolved = DoResolveString(string_idx, dex_cache);
  }
  return resolved;
}

inline ObjPtr<mirror::String> ClassLinker::ResolveString(dex::StringIndex string_idx,
                                                         Handle<mirror::DexCache> dex_cache) {
  Thread::PoisonObjectPointersIfDebug();
  DCHECK(!Thread::Current()->IsExceptionPending());
  ObjPtr<mirror::String> resolved = dex_cache->GetResolvedString(string_idx);
  if (resolved == nullptr) {
    resolved = DoResolveString(string_idx, dex_cache);
  }
  return resolved;
}

inline ObjPtr<mirror::String> ClassLinker::LookupString(dex::StringIndex string_idx,
                                                        ObjPtr<mirror::DexCache> dex_cache) {
  ObjPtr<mirror::String> resolved = dex_cache->GetResolvedString(string_idx);
  if (resolved == nullptr) {
    resolved = DoLookupString(string_idx, dex_cache);
  }
  return resolved;
}

inline ObjPtr<mirror::Class> ClassLinker::ResolveType(dex::TypeIndex type_idx,
                                                      ObjPtr<mirror::Class> referrer) {
  if (kObjPtrPoisoning) {
    StackHandleScope<1> hs(Thread::Current());
    HandleWrapperObjPtr<mirror::Class> referrer_wrapper = hs.NewHandleWrapper(&referrer);
    Thread::Current()->PoisonObjectPointers();
  }
  DCHECK(!Thread::Current()->IsExceptionPending());
  ObjPtr<mirror::Class> resolved_type = referrer->GetDexCache()->GetResolvedType(type_idx);
  if (resolved_type == nullptr) {
    resolved_type = DoResolveType(type_idx, referrer);
  }
  return resolved_type;
}

inline ObjPtr<mirror::Class> ClassLinker::ResolveType(dex::TypeIndex type_idx,
                                                      ArtField* referrer) {
  Thread::PoisonObjectPointersIfDebug();
  DCHECK(!Thread::Current()->IsExceptionPending());
  ObjPtr<mirror::Class> resolved_type = referrer->GetDexCache()->GetResolvedType(type_idx);
  if (UNLIKELY(resolved_type == nullptr)) {
    resolved_type = DoResolveType(type_idx, referrer);
  }
  return resolved_type;
}

inline ObjPtr<mirror::Class> ClassLinker::ResolveType(dex::TypeIndex type_idx,
                                                      ArtMethod* referrer) {
  Thread::PoisonObjectPointersIfDebug();
  DCHECK(!Thread::Current()->IsExceptionPending());
  ObjPtr<mirror::Class> resolved_type = referrer->GetDexCache()->GetResolvedType(type_idx);
  if (UNLIKELY(resolved_type == nullptr)) {
    resolved_type = DoResolveType(type_idx, referrer);
  }
  return resolved_type;
}

inline ObjPtr<mirror::Class> ClassLinker::ResolveType(dex::TypeIndex type_idx,
                                                      Handle<mirror::DexCache> dex_cache,
                                                      Handle<mirror::ClassLoader> class_loader) {
  DCHECK(dex_cache != nullptr);
  DCHECK(dex_cache->GetClassLoader() == class_loader.Get());
  Thread::PoisonObjectPointersIfDebug();
  ObjPtr<mirror::Class> resolved = dex_cache->GetResolvedType(type_idx);
  if (resolved == nullptr) {
    resolved = DoResolveType(type_idx, dex_cache, class_loader);
  }
  return resolved;
}

inline ObjPtr<mirror::Class> ClassLinker::LookupResolvedType(dex::TypeIndex type_idx,
                                                             ObjPtr<mirror::Class> referrer) {
  ObjPtr<mirror::Class> type = referrer->GetDexCache()->GetResolvedType(type_idx);
  if (type == nullptr) {
    type = DoLookupResolvedType(type_idx, referrer);
  }
  return type;
}

inline ObjPtr<mirror::Class> ClassLinker::LookupResolvedType(dex::TypeIndex type_idx,
                                                             ArtField* referrer) {
  // We do not need the read barrier for getting the DexCache for the initial resolved type
  // lookup as both from-space and to-space copies point to the same native resolved types array.
  ObjPtr<mirror::Class> type = referrer->GetDexCache()->GetResolvedType(type_idx);
  if (type == nullptr) {
    type = DoLookupResolvedType(type_idx, referrer->GetDeclaringClass());
  }
  return type;
}

inline ObjPtr<mirror::Class> ClassLinker::LookupResolvedType(dex::TypeIndex type_idx,
                                                             ArtMethod* referrer) {
  // We do not need the read barrier for getting the DexCache for the initial resolved type
  // lookup as both from-space and to-space copies point to the same native resolved types array.
  ObjPtr<mirror::Class> type = referrer->GetDexCache()->GetResolvedType(type_idx);
  if (type == nullptr) {
    type = DoLookupResolvedType(type_idx, referrer->GetDeclaringClass());
  }
  return type;
}

inline ObjPtr<mirror::Class> ClassLinker::LookupResolvedType(
    dex::TypeIndex type_idx,
    ObjPtr<mirror::DexCache> dex_cache,
    ObjPtr<mirror::ClassLoader> class_loader) {
  DCHECK(dex_cache->GetClassLoader() == class_loader);
  ObjPtr<mirror::Class> type = dex_cache->GetResolvedType(type_idx);
  if (type == nullptr) {
    type = DoLookupResolvedType(type_idx, dex_cache, class_loader);
  }
  return type;
}

inline ArtMethod* ClassLinker::LookupResolvedMethod(uint32_t method_idx,
                                                    ObjPtr<mirror::DexCache> dex_cache,
                                                    ObjPtr<mirror::ClassLoader> class_loader) {
  DCHECK(dex_cache->GetClassLoader() == class_loader);
  ArtMethod* resolved = dex_cache->GetResolvedMethod(method_idx);
  if (resolved == nullptr) {
    const DexFile& dex_file = *dex_cache->GetDexFile();
    const dex::MethodId& method_id = dex_file.GetMethodId(method_idx);
    ObjPtr<mirror::Class> klass = LookupResolvedType(method_id.class_idx_, dex_cache, class_loader);
    if (klass != nullptr) {
      resolved = FindResolvedMethod(klass, dex_cache, class_loader, method_idx);
    }
  }
  return resolved;
}

inline ArtMethod* ClassLinker::ResolveMethodId(uint32_t method_idx, ArtMethod* referrer) {
  ArtMethod* resolved = referrer->GetDexCache()->GetResolvedMethod(method_idx);
  if (resolved != nullptr) {
    DCHECK(!resolved->IsRuntimeMethod());
    DCHECK(resolved->GetDeclaringClassUnchecked() != nullptr) << resolved->GetDexMethodIndex();
    return resolved;
  }
  // Fail, get the declaring class.
  referrer = referrer->GetInterfaceMethodIfProxy(image_pointer_size_);
  const dex::MethodId& method_id = referrer->GetDexFile()->GetMethodId(method_idx);
  ObjPtr<mirror::Class> klass = ResolveType(method_id.class_idx_, referrer);
  if (klass == nullptr) {
    Thread::Current()->AssertPendingException();
    return nullptr;
  }

  resolved =
      FindResolvedMethod(klass, referrer->GetDexCache(), referrer->GetClassLoader(), method_idx);
  if (resolved != nullptr) {
    return resolved;
  }

  const char* name = referrer->GetDexFile()->GetStringData(method_id.name_idx_);
  const Signature signature = referrer->GetDexFile()->GetMethodSignature(method_id);
  ThrowNoSuchMethodError(klass, name, signature);
  return nullptr;
}

inline bool ClassLinker::ThrowIfInvokeClassMismatch(ObjPtr<mirror::Class> klass,
                                                    const DexFile& dex_file,
                                                    InvokeType type) {
  if (type == kInterface) {
    if (UNLIKELY(!klass->IsInterface())) {
      ThrowIncompatibleClassChangeError(klass,
                                        "Found class %s, but interface was expected",
                                        klass->PrettyDescriptor().c_str());
      return true;
    }
  } else if (type == kVirtual) {
    if (UNLIKELY(klass->IsInterface())) {
      ThrowIncompatibleClassChangeError(klass,
                                        "Found interface %s, but class was expected",
                                        klass->PrettyDescriptor().c_str());
      return true;
    }
  } else if (type == kDirect) {
    if (UNLIKELY(klass->IsInterface()) && !dex_file.SupportsDefaultMethods()) {
      ThrowIncompatibleClassChangeError(klass,
                                        "Found interface %s, but class was expected",
                                        klass->PrettyDescriptor().c_str());
      return true;
    }
  }
  return false;
}

inline ArtMethod* ClassLinker::ResolveMethodWithChecks(uint32_t method_idx,
                                                       ArtMethod* referrer,
                                                       InvokeType type) {
  DCHECK(referrer != nullptr);
  DCHECK_IMPLIES(referrer->IsProxyMethod(), referrer->IsConstructor());

  // For a Proxy constructor, we need to do the lookup in the context of the original method
  // from where it steals the code.
  referrer = referrer->GetInterfaceMethodIfProxy(image_pointer_size_);

  const dex::MethodId& method_id = referrer->GetDexFile()->GetMethodId(method_idx);
  ObjPtr<mirror::Class> klass = ResolveType(method_id.class_idx_, referrer);
  if (klass == nullptr || ThrowIfInvokeClassMismatch(klass, *referrer->GetDexFile(), type)) {
    DCHECK(Thread::Current()->IsExceptionPending());
    return nullptr;
  }

  ArtMethod* resolved = referrer->GetDexCache()->GetResolvedMethod(method_idx);
  if (resolved == nullptr) {
    resolved = FindResolvedMethod(klass,
                                  referrer->GetDexCache(),
                                  referrer->GetDexCache()->GetClassLoader(),
                                  method_idx);
  }

  if (resolved != nullptr) {
    ObjPtr<mirror::Class> methods_class = resolved->GetDeclaringClass();
    ObjPtr<mirror::Class> referring_class = referrer->GetDeclaringClass();
    if (UNLIKELY(!referring_class->CanAccess(methods_class))) {
      // The referrer class can't access the method's declaring class but may still be able
      // to access the method if the MethodId specifies an accessible subclass of the declaring
      // class rather than the declaring class itself.
      if (UNLIKELY(!referring_class->CanAccess(klass))) {
        ThrowIllegalAccessErrorClassForMethodDispatch(referring_class,
                                                      klass,
                                                      resolved,
                                                      type);
        return nullptr;
      }
    }
    if (UNLIKELY(!referring_class->CanAccessMember(methods_class, resolved->GetAccessFlags()))) {
      ThrowIllegalAccessErrorMethod(referring_class, resolved);
      return nullptr;
    }

    if (LIKELY(!resolved->CheckIncompatibleClassChange(type))) {
      return resolved;
    }
  } else {
    resolved = FindIncompatibleMethod(klass,
                                      referrer->GetDexCache(),
                                      referrer->GetDexCache()->GetClassLoader(),
                                      method_idx);
  }
  if (resolved != nullptr) {
    ThrowIncompatibleClassChangeError(type, resolved->GetInvokeType(), resolved, referrer);
    return nullptr;
  }

  // We failed to find the method (using all lookup types), so throw a NoSuchMethodError.
  const char* name = referrer->GetDexFile()->GetStringData(method_id.name_idx_);
  const Signature signature = referrer->GetDexFile()->GetMethodSignature(method_id);
  ThrowNoSuchMethodError(type, klass, name, signature);
  return nullptr;
}

inline ArtField* ClassLinker::LookupResolvedField(uint32_t field_idx,
                                                  ArtMethod* referrer,
                                                  bool is_static) {
  ObjPtr<mirror::DexCache> dex_cache = referrer->GetDexCache();
  ArtField* field = dex_cache->GetResolvedField(field_idx);
  if (field == nullptr) {
    referrer = referrer->GetInterfaceMethodIfProxy(image_pointer_size_);
    ObjPtr<mirror::ClassLoader> class_loader = referrer->GetDeclaringClass()->GetClassLoader();
    field = LookupResolvedField(field_idx, dex_cache, class_loader, is_static);
  }
  return field;
}

inline ArtField* ClassLinker::ResolveField(uint32_t field_idx,
                                           ArtMethod* referrer,
                                           bool is_static) {
  Thread::PoisonObjectPointersIfDebug();
  ObjPtr<mirror::DexCache> dex_cache = referrer->GetDexCache();
  ArtField* resolved_field = dex_cache->GetResolvedField(field_idx);
  if (UNLIKELY(resolved_field == nullptr)) {
    StackHandleScope<2> hs(Thread::Current());
    referrer = referrer->GetInterfaceMethodIfProxy(image_pointer_size_);
    ObjPtr<mirror::Class> referring_class = referrer->GetDeclaringClass();
    Handle<mirror::DexCache> h_dex_cache(hs.NewHandle(dex_cache));
    Handle<mirror::ClassLoader> class_loader(hs.NewHandle(referring_class->GetClassLoader()));
    resolved_field = ResolveField(field_idx, h_dex_cache, class_loader, is_static);
    // Note: We cannot check here to see whether we added the field to the cache. The type
    //       might be an erroneous class, which results in it being hidden from us.
  }
  return resolved_field;
}

inline ArtField* ClassLinker::ResolveField(uint32_t field_idx,
                                           Handle<mirror::DexCache> dex_cache,
                                           Handle<mirror::ClassLoader> class_loader,
                                           bool is_static) {
  DCHECK(dex_cache != nullptr);
  DCHECK(dex_cache->GetClassLoader().Ptr() == class_loader.Get());
  DCHECK(!Thread::Current()->IsExceptionPending()) << Thread::Current()->GetException()->Dump();
  ArtField* resolved = dex_cache->GetResolvedField(field_idx);
  Thread::PoisonObjectPointersIfDebug();
  if (resolved != nullptr) {
    return resolved;
  }
  const DexFile& dex_file = *dex_cache->GetDexFile();
  const dex::FieldId& field_id = dex_file.GetFieldId(field_idx);
  ObjPtr<mirror::Class> klass = ResolveType(field_id.class_idx_, dex_cache, class_loader);
  if (klass == nullptr) {
    DCHECK(Thread::Current()->IsExceptionPending());
    return nullptr;
  }

  // Look for the field again in case the type resolution updated the cache.
  resolved = dex_cache->GetResolvedField(field_idx);
  if (resolved != nullptr) {
    return resolved;
  }

  resolved = FindResolvedField(klass, dex_cache.Get(), class_loader.Get(), field_idx, is_static);
  if (resolved == nullptr) {
    const char* name = dex_file.GetFieldName(field_id);
    const char* type = dex_file.GetFieldTypeDescriptor(field_id);
    ThrowNoSuchFieldError(is_static ? "static " : "instance ", klass, type, name);
  }
  return resolved;
}

template <typename Visitor>
inline void ClassLinker::VisitBootClasses(Visitor* visitor) {
  boot_class_table_->Visit(*visitor);
}

template <class Visitor>
inline void ClassLinker::VisitClassTables(const Visitor& visitor) {
  Thread* const self = Thread::Current();
  WriterMutexLock mu(self, *Locks::classlinker_classes_lock_);
  for (const ClassLoaderData& data : class_loaders_) {
    if (data.class_table != nullptr) {
      visitor(data.class_table);
    }
  }
}

template <ReadBarrierOption kReadBarrierOption>
inline ObjPtr<mirror::ObjectArray<mirror::Class>> ClassLinker::GetClassRoots() {
  ObjPtr<mirror::ObjectArray<mirror::Class>> class_roots =
      class_roots_.Read<kReadBarrierOption>();
  DCHECK(class_roots != nullptr);
  return class_roots;
}

template <typename Visitor>
void ClassLinker::VisitKnownDexFiles(Thread* self, Visitor visitor) {
  ReaderMutexLock rmu(self, *Locks::dex_lock_);
  std::for_each(dex_caches_.begin(),
                dex_caches_.end(),
                [&](const auto& entry) REQUIRES(Locks::mutator_lock_) {
                  visitor(/*dex_file=*/entry.first);
                });
}

}  // namespace art

#endif  // ART_RUNTIME_CLASS_LINKER_INL_H_
