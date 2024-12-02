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

#ifndef ART_RUNTIME_ENTRYPOINTS_QUICK_RUNTIME_ENTRYPOINTS_LIST_H_
#define ART_RUNTIME_ENTRYPOINTS_QUICK_RUNTIME_ENTRYPOINTS_LIST_H_


#include "entrypoints/entrypoint_utils.h"
#include "arch/instruction_set.h"
#include <math.h>

namespace art {

namespace mirror {
class Array;
class Class;
template<class MirrorType> class CompressedReference;
class Object;
class String;
class Throwable;
template<class T> class PrimitiveArray;
using ByteArray = PrimitiveArray<int8_t>;
using CharArray = PrimitiveArray<uint16_t>;
}  // namespace mirror

class ArtMethod;
template<class MirrorType> class GcRoot;
template<class MirrorType> class StackReference;
class Thread;
class Context;
enum class DeoptimizationKind;

// All C++ quick entrypoints, i.e.: C++ entrypoint functions called from quick assembly code.
// Format is name, attribute, return type, argument types.
#define RUNTIME_ENTRYPOINT_LIST(V) \
  V(artDeliverPendingExceptionFromCode, REQUIRES_SHARED(Locks::mutator_lock_), Context*,          \
      Thread* self)                                                                               \
  V(artInvokeObsoleteMethod, REQUIRES_SHARED(Locks::mutator_lock_), Context*,                     \
      ArtMethod* method,                                                                          \
      Thread* self)                                                                               \
  V(artDeliverExceptionFromCode, REQUIRES_SHARED(Locks::mutator_lock_), Context*,                 \
      mirror::Throwable* exception,                                                               \
      Thread* self)                                                                               \
  V(artThrowNullPointerExceptionFromCode, REQUIRES_SHARED(Locks::mutator_lock_), Context*,        \
      Thread* self)                                                                               \
  V(artThrowNullPointerExceptionFromSignal, REQUIRES_SHARED(Locks::mutator_lock_), Context*,      \
      uintptr_t addr,                                                                             \
      Thread* self)                                                                               \
  V(artThrowDivZeroFromCode, REQUIRES_SHARED(Locks::mutator_lock_), Context*,                     \
      Thread* self)                                                                               \
  V(artThrowArrayBoundsFromCode, REQUIRES_SHARED(Locks::mutator_lock_), Context*,                 \
      int index,                                                                                  \
      int length,                                                                                 \
      Thread* self)                                                                               \
  V(artThrowStringBoundsFromCode, REQUIRES_SHARED(Locks::mutator_lock_), Context*,                \
      int index,                                                                                  \
      int length,                                                                                 \
      Thread* self)                                                                               \
  V(artThrowStackOverflowFromCode, REQUIRES_SHARED(Locks::mutator_lock_), Context*,               \
      Thread* self)                                                                               \
  V(artThrowClassCastExceptionForObject, REQUIRES_SHARED(Locks::mutator_lock_), Context*,         \
      mirror::Object* obj,                                                                        \
      mirror::Class* dest_type,                                                                   \
      Thread* self)                                                                               \
  V(artThrowArrayStoreException, REQUIRES_SHARED(Locks::mutator_lock_), Context*,                 \
      mirror::Object* array,                                                                      \
      mirror::Object* value,                                                                      \
      Thread* self)                                                                               \
                                                                                                  \
  V(artDeoptimizeIfNeeded, REQUIRES_SHARED(Locks::mutator_lock_), Context*,                       \
      Thread* self,                                                                               \
      uintptr_t result,                                                                           \
      bool is_ref)                                                                                \
  V(artTestSuspendFromCode, REQUIRES_SHARED(Locks::mutator_lock_), Context*,                      \
      Thread* self)                                                                               \
  V(artImplicitSuspendFromCode, REQUIRES_SHARED(Locks::mutator_lock_), Context*,                  \
      Thread* self)                                                                               \
  V(artCompileOptimized, REQUIRES_SHARED(Locks::mutator_lock_), void,                             \
      ArtMethod* method,                                                                          \
      Thread* self)                                                                               \
                                                                                                  \
  V(artQuickToInterpreterBridge, REQUIRES_SHARED(Locks::mutator_lock_), uint64_t,                 \
      ArtMethod* method,                                                                          \
      Thread* self,                                                                               \
      ArtMethod** sp)                                                                             \
  V(artQuickProxyInvokeHandler, REQUIRES_SHARED(Locks::mutator_lock_), uint64_t,                  \
      ArtMethod* proxy_method,                                                                    \
      mirror::Object* receiver,                                                                   \
      Thread* self,                                                                               \
      ArtMethod** sp)                                                                             \
  V(artQuickResolutionTrampoline, REQUIRES_SHARED(Locks::mutator_lock_), const void*,             \
      ArtMethod* called,                                                                          \
      mirror::Object* receiver,                                                                   \
      Thread* self,                                                                               \
      ArtMethod** sp)                                                                             \
  V(artQuickGenericJniTrampoline, REQUIRES_SHARED(Locks::mutator_lock_)                           \
      NO_THREAD_SAFETY_ANALYSIS, const void*,                                                     \
      Thread* self,                                                                               \
      ArtMethod** managed_sp,                                                                     \
      uintptr_t* reserved_area)                                                                   \
  V(artQuickGenericJniEndTrampoline, , uint64_t,                                                  \
      Thread* self,                                                                               \
      jvalue result,                                                                              \
      uint64_t result_fp)                                                                         \
  V(artInvokeInterfaceTrampolineWithAccessCheck, REQUIRES_SHARED(Locks::mutator_lock_),           \
      TwoWordReturn,                                                                              \
      uint32_t method_idx,                                                                        \
      mirror::Object* this_object,                                                                \
      Thread* self,                                                                               \
      ArtMethod** sp)                                                                             \
  V(artInvokeDirectTrampolineWithAccessCheck, REQUIRES_SHARED(Locks::mutator_lock_),              \
      TwoWordReturn,                                                                              \
      uint32_t method_idx,                                                                        \
      mirror::Object* this_object,                                                                \
      Thread* self,                                                                               \
      ArtMethod** sp)                                                                             \
  V(artInvokeStaticTrampolineWithAccessCheck, REQUIRES_SHARED(Locks::mutator_lock_),              \
      TwoWordReturn,                                                                              \
      uint32_t method_idx,                                                                        \
      [[maybe_unused]] mirror::Object* this_object,                                               \
      Thread* self,                                                                               \
      ArtMethod** sp)                                                                             \
  V(artInvokeSuperTrampolineWithAccessCheck, REQUIRES_SHARED(Locks::mutator_lock_),               \
      TwoWordReturn,                                                                              \
      uint32_t method_idx,                                                                        \
      mirror::Object* this_object,                                                                \
      Thread* self,                                                                               \
      ArtMethod** sp)                                                                             \
  V(artInvokeVirtualTrampolineWithAccessCheck, REQUIRES_SHARED(Locks::mutator_lock_),             \
      TwoWordReturn,                                                                              \
      uint32_t method_idx,                                                                        \
      mirror::Object* this_object,                                                                \
      Thread* self,                                                                               \
      ArtMethod** sp)                                                                             \
  V(artInvokeInterfaceTrampoline, REQUIRES_SHARED(Locks::mutator_lock_), TwoWordReturn,           \
      ArtMethod* interface_method,                                                                \
      mirror::Object* raw_this_object,                                                            \
      Thread* self,                                                                               \
      ArtMethod** sp)                                                                             \
  V(artInvokePolymorphic, REQUIRES_SHARED(Locks::mutator_lock_), uint64_t,                        \
      mirror::Object* raw_receiver,                                                               \
      Thread* self,                                                                               \
      ArtMethod** sp)                                                                             \
  V(artInvokePolymorphicWithHiddenReceiver, REQUIRES_SHARED(Locks::mutator_lock_), uint64_t,      \
      mirror::Object* raw_receiver,                                                               \
      Thread* self,                                                                               \
      ArtMethod** sp)                                                                             \
  V(artInvokeCustom, REQUIRES_SHARED(Locks::mutator_lock_), uint64_t,                             \
      uint32_t call_site_idx,                                                                     \
      Thread* self,                                                                               \
      ArtMethod** sp)                                                                             \
  V(artJniMethodEntryHook, REQUIRES_SHARED(Locks::mutator_lock_), void,                           \
      Thread* self)                                                                               \
  V(artMethodEntryHook, REQUIRES_SHARED(Locks::mutator_lock_), Context*,                          \
      ArtMethod* method,                                                                          \
      Thread* self,                                                                               \
      ArtMethod** sp)                                                                             \
  V(artMethodExitHook, REQUIRES_SHARED(Locks::mutator_lock_), Context*,                           \
      Thread* self,                                                                               \
      ArtMethod** sp,                                                                             \
      uint64_t* gpr_result,                                                                       \
      uint64_t* fpr_result,                                                                       \
      uint32_t frame_size)                                                                        \
                                                                                                  \
  V(artIsAssignableFromCode, REQUIRES_SHARED(Locks::mutator_lock_), size_t,                       \
      mirror::Class* klass,                                                                       \
      mirror::Class* ref_class)                                                                   \
  V(artInstanceOfFromCode, REQUIRES_SHARED(Locks::mutator_lock_), size_t,                         \
      mirror::Object* obj,                                                                        \
      mirror::Class* ref_class)                                                                   \
                                                                                                  \
  V(artInitializeStaticStorageFromCode, REQUIRES_SHARED(Locks::mutator_lock_), mirror::Class*,    \
      mirror::Class* klass,                                                                       \
      Thread* self)                                                                               \
  V(artResolveTypeFromCode, REQUIRES_SHARED(Locks::mutator_lock_), mirror::Class*,                \
      uint32_t type_idx,                                                                          \
      Thread* self)                                                                               \
  V(artResolveTypeAndVerifyAccessFromCode, REQUIRES_SHARED(Locks::mutator_lock_), mirror::Class*, \
      uint32_t type_idx,                                                                          \
      Thread* self)                                                                               \
  V(artResolveMethodHandleFromCode, REQUIRES_SHARED(Locks::mutator_lock_), mirror::MethodHandle*, \
      uint32_t method_handle_idx,                                                                 \
      Thread* self)                                                                               \
  V(artResolveMethodTypeFromCode, REQUIRES_SHARED(Locks::mutator_lock_), mirror::MethodType*,     \
      uint32_t proto_idx,                                                                         \
      Thread* self)                                                                               \
  V(artResolveStringFromCode, REQUIRES_SHARED(Locks::mutator_lock_), mirror::String*,             \
      int32_t string_idx, Thread* self)                                                           \
                                                                                                  \
  V(artDeoptimize, REQUIRES_SHARED(Locks::mutator_lock_), Context*,                               \
      Thread* self,                                                                               \
      bool skip_method_exit_callbacks)                                                            \
  V(artDeoptimizeFromCompiledCode, REQUIRES_SHARED(Locks::mutator_lock_), Context*,               \
      DeoptimizationKind kind,                                                                    \
      Thread* self)                                                                               \
                                                                                                  \
  V(artHandleFillArrayDataFromCode, REQUIRES_SHARED(Locks::mutator_lock_), int,                   \
      const Instruction::ArrayDataPayload* payload,                                               \
      mirror::Array* array,                                                                       \
      Thread* self)                                                                               \
                                                                                                  \
  V(artJniReadBarrier, REQUIRES_SHARED(Locks::mutator_lock_) HOT_ATTR, void,                      \
      ArtMethod* method)                                                                          \
  V(artJniMethodStart, UNLOCK_FUNCTION(Locks::mutator_lock_) HOT_ATTR, void,                      \
      Thread* self)                                                                               \
  V(artJniUnlockObject, NO_THREAD_SAFETY_ANALYSIS REQUIRES(!Roles::uninterruptible_)              \
      REQUIRES_SHARED(Locks::mutator_lock_) HOT_ATTR, void,                                       \
      mirror::Object* locked,                                                                     \
      Thread* self)                                                                               \
  V(artJniMethodEnd, SHARED_LOCK_FUNCTION(Locks::mutator_lock_) HOT_ATTR, void,                   \
      Thread* self)                                                                               \
  V(artJniMonitoredMethodStart, UNLOCK_FUNCTION(Locks::mutator_lock_), void,                      \
      Thread* self)                                                                               \
  V(artJniMonitoredMethodEnd, SHARED_LOCK_FUNCTION(Locks::mutator_lock_), void,                   \
      Thread* self)                                                                               \
                                                                                                  \
  V(artStringBuilderAppend, REQUIRES_SHARED(Locks::mutator_lock_) HOT_ATTR, mirror::String*,      \
      uint32_t format,                                                                            \
      const uint32_t* args,                                                                       \
      Thread* self)                                                                               \
                                                                                                  \
  V(artContextCopyForLongJump, , void,                                                            \
      Context* context,                                                                           \
      uintptr_t* gprs,                                                                            \
      uintptr_t* fprs)                                                                            \
                                                                                                  \
  GENERATE_ENTRYPOINTS_DECL_FOR_ALLOCATOR(V, DlMalloc)                                            \
  GENERATE_ENTRYPOINTS_DECL_FOR_ALLOCATOR(V, RosAlloc)                                            \
  GENERATE_ENTRYPOINTS_DECL_FOR_ALLOCATOR(V, BumpPointer)                                         \
  GENERATE_ENTRYPOINTS_DECL_FOR_ALLOCATOR(V, TLAB)                                                \
  GENERATE_ENTRYPOINTS_DECL_FOR_ALLOCATOR(V, Region)                                              \
  GENERATE_ENTRYPOINTS_DECL_FOR_ALLOCATOR(V, RegionTLAB)                                          \
                                                                                                  \
  ART_GET_FIELD_FROM_CODE_DECL(V, Byte, ssize_t, uint32_t)                                        \
  ART_GET_FIELD_FROM_CODE_DECL(V, Boolean, size_t, uint32_t)                                      \
  ART_GET_FIELD_FROM_CODE_DECL(V, Short, ssize_t, uint16_t)                                       \
  ART_GET_FIELD_FROM_CODE_DECL(V, Char, size_t, uint16_t)                                         \
  ART_GET_FIELD_FROM_CODE_DECL(V, 32, FIELD_RETURN_TYPE_32, uint32_t)                             \
  ART_GET_FIELD_FROM_CODE_DECL(V, 64, uint64_t, uint64_t)                                         \
  ART_GET_FIELD_FROM_CODE_DECL(V, Obj, mirror::Object*, mirror::Object*)                          \
  V(artSet8StaticFromCompiledCode, REQUIRES_SHARED(Locks::mutator_lock_), int,                    \
      uint32_t field_idx,                                                                         \
      uint32_t new_value,                                                                         \
      Thread* self)                                                                               \
  V(artSet16StaticFromCompiledCode, REQUIRES_SHARED(Locks::mutator_lock_), int,                   \
      uint32_t field_idx,                                                                         \
      uint16_t new_value,                                                                         \
      Thread* self)                                                                               \
  V(artSet8InstanceFromCompiledCode, REQUIRES_SHARED(Locks::mutator_lock_), int,                  \
      uint32_t field_idx,                                                                         \
      mirror::Object* obj,                                                                        \
      uint8_t new_value,                                                                          \
      Thread* self)                                                                               \
  V(artSet16InstanceFromCompiledCode, REQUIRES_SHARED(Locks::mutator_lock_), int,                 \
      uint32_t field_idx,                                                                         \
      mirror::Object* obj,                                                                        \
      uint16_t new_value,                                                                         \
      Thread* self)                                                                               \
  V(artSet8StaticFromCode, REQUIRES_SHARED(Locks::mutator_lock_), int,                            \
      uint32_t field_idx,                                                                         \
      uint32_t new_value,                                                                         \
      ArtMethod* referrer,                                                                        \
      Thread* self)                                                                               \
  V(artSet16StaticFromCode, REQUIRES_SHARED(Locks::mutator_lock_), int,                           \
      uint32_t field_idx,                                                                         \
      uint16_t new_value,                                                                         \
      ArtMethod* referrer,                                                                        \
      Thread* self)                                                                               \
  V(artSet8InstanceFromCode, REQUIRES_SHARED(Locks::mutator_lock_), int,                          \
      uint32_t field_idx,                                                                         \
      mirror::Object* obj,                                                                        \
      uint8_t new_value,                                                                          \
      ArtMethod* referrer,                                                                        \
      Thread* self)                                                                               \
  V(artSet16InstanceFromCode, REQUIRES_SHARED(Locks::mutator_lock_), int,                         \
      uint32_t field_idx,                                                                         \
      mirror::Object* obj,                                                                        \
      uint16_t new_value,                                                                         \
      ArtMethod* referrer,                                                                        \
      Thread* self)                                                                               \
  V(artReadBarrierMark, REQUIRES_SHARED(Locks::mutator_lock_) HOT_ATTR, mirror::Object*,          \
      mirror::Object* obj)                                                                        \
  V(artReadBarrierSlow, REQUIRES_SHARED(Locks::mutator_lock_) HOT_ATTR, mirror::Object*,          \
      mirror::Object* ref,                                                                        \
      mirror::Object* obj,                                                                        \
      uint32_t offset)                                                                            \
  V(artReadBarrierForRootSlow, REQUIRES_SHARED(Locks::mutator_lock_) HOT_ATTR, mirror::Object*,   \
      GcRoot<mirror::Object>* root)                                                               \
                                                                                                  \
  V(artLockObjectFromCode, NO_THREAD_SAFETY_ANALYSIS REQUIRES(!Roles::uninterruptible_)           \
      REQUIRES_SHARED(Locks::mutator_lock_), int,                                                 \
      mirror::Object* obj,                                                                        \
      Thread* self)                                                                               \
  V(artUnlockObjectFromCode, NO_THREAD_SAFETY_ANALYSIS REQUIRES(!Roles::uninterruptible_)         \
      REQUIRES_SHARED(Locks::mutator_lock_), int,                                                 \
      mirror::Object* obj,                                                                        \
      Thread* self)                                                                               \
                                                                                                  \
  V(artFindNativeMethodRunnable, REQUIRES_SHARED(Locks::mutator_lock_), const void*,              \
      Thread* self)                                                                               \
  V(artFindNativeMethod, , const void*,                                                           \
      Thread* self)                                                                               \
  V(artCriticalNativeFrameSize, REQUIRES_SHARED(Locks::mutator_lock_), size_t,                    \
      ArtMethod* method,                                                                          \
      uintptr_t caller_pc)                                                                        \
                                                                                                  \
  V(artLmul, , int64_t,                                                                           \
      int64_t a,                                                                                  \
      int64_t b)                                                                                  \
  V(artLdiv, , int64_t,                                                                           \
      int64_t a,                                                                                  \
      int64_t b)                                                                                  \
  V(artLmod, , int64_t,                                                                           \
      int64_t a,                                                                                  \
      int64_t b)                                                                                  \
                                                                                                  \
  V(art_l2d, , double,                                                                            \
      int64_t l)                                                                                  \
  V(art_l2f, , float,                                                                             \
      int64_t l)                                                                                  \
  V(art_d2l, , int64_t,                                                                           \
      double d)                                                                                   \
  V(art_f2l, , int64_t,                                                                           \
      float f)                                                                                    \
  V(art_d2i, , int32_t,                                                                           \
      double d)                                                                                   \
  V(art_f2i, , int32_t,                                                                           \
      float f)                                                                                    \
  V(fmodf, , float,                                                                               \
      float,                                                                                      \
      float)                                                                                      \
  V(fmod, , double,                                                                               \
      double,                                                                                     \
      double)

// Declarations from quick_alloc_entrypoints.cc
#define GENERATE_ENTRYPOINTS_DECL_FOR_ALLOCATOR_INST(V, suffix, suffix2)                          \
  V(artAllocObjectFromCodeWithChecks##suffix##suffix2, REQUIRES_SHARED(Locks::mutator_lock_),     \
      mirror::Object*,                                                                            \
      mirror::Class* klass,                                                                       \
      Thread* self)                                                                               \
  V(artAllocObjectFromCodeResolved##suffix##suffix2, REQUIRES_SHARED(Locks::mutator_lock_),       \
      mirror::Object*,                                                                            \
      mirror::Class* klass,                                                                       \
      Thread* self)                                                                               \
  V(artAllocObjectFromCodeInitialized##suffix##suffix2, REQUIRES_SHARED(Locks::mutator_lock_),    \
      mirror::Object*,                                                                            \
      mirror::Class* klass,                                                                       \
      Thread* self)                                                                               \
  V(artAllocStringObject##suffix##suffix2, REQUIRES_SHARED(Locks::mutator_lock_),                 \
      mirror::String*,                                                                            \
      mirror::Class* klass,                                                                       \
      Thread* self)                                                                               \
  V(artAllocArrayFromCodeResolved##suffix##suffix2, REQUIRES_SHARED(Locks::mutator_lock_),        \
      mirror::Array*,                                                                             \
      mirror::Class* klass,                                                                       \
      int32_t component_count,                                                                    \
      Thread* self)                                                                               \
  V(artAllocStringFromBytesFromCode##suffix##suffix2, REQUIRES_SHARED(Locks::mutator_lock_),      \
      mirror::String*,                                                                            \
      mirror::ByteArray* byte_array,                                                              \
      int32_t high,                                                                               \
      int32_t offset,                                                                             \
      int32_t byte_count,                                                                         \
      Thread* self)                                                                               \
  V(artAllocStringFromCharsFromCode##suffix##suffix2, REQUIRES_SHARED(Locks::mutator_lock_),      \
      mirror::String*,                                                                            \
      int32_t offset,                                                                             \
      int32_t char_count,                                                                         \
      mirror::CharArray* char_array,                                                              \
      Thread* self)                                                                               \
  V(artAllocStringFromStringFromCode##suffix##suffix2, REQUIRES_SHARED(Locks::mutator_lock_),     \
      mirror::String*,                                                                            \
      mirror::String* string,                                                                     \
      Thread* self)

#define GENERATE_ENTRYPOINTS_DECL_FOR_ALLOCATOR(V, suffix)              \
  GENERATE_ENTRYPOINTS_DECL_FOR_ALLOCATOR_INST(V, suffix, Instrumented) \
  GENERATE_ENTRYPOINTS_DECL_FOR_ALLOCATOR_INST(V, suffix, )

// Declarations from quick_field_entrypoints.cc
#define ART_GET_FIELD_FROM_CODE_DECL(V, Kind, RetType, SetType)                                 \
  V(artGet ## Kind ## StaticFromCode, REQUIRES_SHARED(Locks::mutator_lock_), RetType,           \
      uint32_t field_idx,                                                                       \
      ArtMethod* referrer,                                                                      \
      Thread* self)                                                                             \
  V(artGet ## Kind ## InstanceFromCode, REQUIRES_SHARED(Locks::mutator_lock_), RetType,         \
      uint32_t field_idx,                                                                       \
      mirror::Object* obj,                                                                      \
      ArtMethod* referrer,                                                                      \
      Thread* self)                                                                             \
  V(artSet ## Kind ## StaticFromCode, REQUIRES_SHARED(Locks::mutator_lock_), int,               \
      uint32_t field_idx,                                                                       \
      SetType new_value,                                                                        \
      ArtMethod* referrer,                                                                      \
      Thread* self)                                                                             \
  V(artSet ## Kind ## InstanceFromCode, REQUIRES_SHARED(Locks::mutator_lock_), int,             \
      uint32_t field_idx,                                                                       \
      mirror::Object* obj,                                                                      \
      SetType new_value,                                                                        \
      ArtMethod* referrer,                                                                      \
      Thread* self)                                                                             \
  V(artGet ## Kind ## StaticFromCompiledCode, REQUIRES_SHARED(Locks::mutator_lock_), RetType,   \
      uint32_t field_idx,                                                                       \
      Thread* self)                                                                             \
  V(artGet ## Kind ## InstanceFromCompiledCode, REQUIRES_SHARED(Locks::mutator_lock_), RetType, \
      uint32_t field_idx,                                                                       \
      mirror::Object* obj,                                                                      \
      Thread* self)                                                                             \
  V(artSet ## Kind ## StaticFromCompiledCode, REQUIRES_SHARED(Locks::mutator_lock_), int,       \
      uint32_t field_idx,                                                                       \
      SetType new_value,                                                                        \
      Thread* self)                                                                             \
  V(artSet ## Kind ## InstanceFromCompiledCode, REQUIRES_SHARED(Locks::mutator_lock_), int,     \
      uint32_t field_idx,                                                                       \
      mirror::Object* obj,                                                                      \
      SetType new_value,                                                                        \
      Thread* self)

#if defined(__riscv)
#define FIELD_RETURN_TYPE_32 uint32_t
#else
#define FIELD_RETURN_TYPE_32 size_t
#endif

// Define a macro that will extract information from RUNTIME_ENTRYPOINT_LIST to create a function
// declaration.
#define ENTRYPOINT_ENUM(name, attr, rettype, ...) \
  extern "C" rettype name(__VA_ARGS__) attr;

// Declare all C++ quick entrypoints.
RUNTIME_ENTRYPOINT_LIST(ENTRYPOINT_ENUM)
#undef ENTRYPOINT_ENUM

}  // namespace art

#endif  // ART_RUNTIME_ENTRYPOINTS_QUICK_RUNTIME_ENTRYPOINTS_LIST_H_
