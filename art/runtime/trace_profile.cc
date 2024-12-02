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

#include "trace_profile.h"

#include "android-base/stringprintf.h"
#include "art_method-inl.h"
#include "base/leb128.h"
#include "base/mutex.h"
#include "base/unix_file/fd_file.h"
#include "com_android_art_flags.h"
#include "dex/descriptors_names.h"
#include "runtime.h"
#include "thread-current-inl.h"
#include "thread.h"
#include "thread_list.h"
#include "trace.h"

namespace art_flags = com::android::art::flags;

namespace art HIDDEN {

using android::base::StringPrintf;

// This specifies the maximum number of bits we need for encoding one entry. Each entry just
// consists of a SLEB encoded value of method and action encodig which is a maximum of
// sizeof(uintptr_t).
static constexpr size_t kMaxBytesPerTraceEntry = sizeof(uintptr_t);

// We don't handle buffer overflows when processing the raw trace entries. We have a maximum of
// kAlwaysOnTraceBufSize raw entries and we need a maximum of kMaxBytesPerTraceEntry to encode
// each entry. To avoid overflow, we ensure that there are at least kMinBufSizeForEncodedData
// bytes free space in the buffer.
static constexpr size_t kMinBufSizeForEncodedData = kAlwaysOnTraceBufSize * kMaxBytesPerTraceEntry;

static constexpr size_t kProfileMagicValue = 0x4C4F4D54;

// TODO(mythria): 10 is a randomly chosen value. Tune it if required.
static constexpr size_t kBufSizeForEncodedData = kMinBufSizeForEncodedData * 10;

static constexpr size_t kAlwaysOnTraceHeaderSize = 8;

bool TraceProfiler::profile_in_progress_ = false;

void TraceProfiler::AllocateBuffer(Thread* thread) {
  if (!art_flags::always_enable_profile_code()) {
    return;
  }

  Thread* self = Thread::Current();
  MutexLock mu(self, *Locks::trace_lock_);
  if (!profile_in_progress_) {
    return;
  }

  auto buffer = new uintptr_t[kAlwaysOnTraceBufSize];
  memset(buffer, 0, kAlwaysOnTraceBufSize * sizeof(uintptr_t));
  thread->SetMethodTraceBuffer(buffer, kAlwaysOnTraceBufSize);
}

void TraceProfiler::Start() {
  if (!art_flags::always_enable_profile_code()) {
    LOG(ERROR) << "Feature not supported. Please build with ART_ALWAYS_ENABLE_PROFILE_CODE.";
    return;
  }

  Thread* self = Thread::Current();
  MutexLock mu(self, *Locks::trace_lock_);
  if (profile_in_progress_) {
    LOG(ERROR) << "Profile already in progress. Ignoring this request";
    return;
  }

  if (Trace::IsTracingEnabledLocked()) {
    LOG(ERROR) << "Cannot start a profile when method tracing is in progress";
    return;
  }

  profile_in_progress_ = true;

  ScopedSuspendAll ssa(__FUNCTION__);
  MutexLock tl(self, *Locks::thread_list_lock_);
  for (Thread* thread : Runtime::Current()->GetThreadList()->GetList()) {
    auto buffer = new uintptr_t[kAlwaysOnTraceBufSize];
    memset(buffer, 0, kAlwaysOnTraceBufSize * sizeof(uintptr_t));
    thread->SetMethodTraceBuffer(buffer, kAlwaysOnTraceBufSize);
  }
}

void TraceProfiler::Stop() {
  if (!art_flags::always_enable_profile_code()) {
    LOG(ERROR) << "Feature not supported. Please build with ART_ALWAYS_ENABLE_PROFILE_CODE.";
    return;
  }

  Thread* self = Thread::Current();
  MutexLock mu(self, *Locks::trace_lock_);
  if (!profile_in_progress_) {
    LOG(ERROR) << "No Profile in progress but a stop was requested";
    return;
  }

  ScopedSuspendAll ssa(__FUNCTION__);
  MutexLock tl(self, *Locks::thread_list_lock_);
  for (Thread* thread : Runtime::Current()->GetThreadList()->GetList()) {
    auto buffer = thread->GetMethodTraceBuffer();
    if (buffer != nullptr) {
      delete[] buffer;
      thread->SetMethodTraceBuffer(/* buffer= */ nullptr, /* offset= */ 0);
    }
  }

  profile_in_progress_ = false;
}

uint8_t* TraceProfiler::DumpBuffer(uint32_t thread_id,
                                   uintptr_t* method_trace_entries,
                                   uint8_t* buffer,
                                   std::unordered_set<ArtMethod*>& methods) {
  // Encode header at the end once we compute the number of records.
  uint8_t* curr_buffer_ptr = buffer + kAlwaysOnTraceHeaderSize;

  int num_records = 0;
  uintptr_t prev_method_action_encoding = 0;
  int prev_action = -1;
  for (size_t i = kAlwaysOnTraceBufSize - 1; i > 0; i-=1) {
    uintptr_t method_action_encoding = method_trace_entries[i];
    // 0 value indicates the rest of the entries are empty.
    if (method_action_encoding == 0) {
      break;
    }

    int action = method_action_encoding & ~kMaskTraceAction;
    int64_t diff;
    if (action == TraceAction::kTraceMethodEnter) {
      diff = method_action_encoding - prev_method_action_encoding;

      ArtMethod* method = reinterpret_cast<ArtMethod*>(method_action_encoding & kMaskTraceAction);
      methods.insert(method);
    } else {
      // On a method exit, we don't record the information about method. We just need a 1 in the
      // lsb and the method information can be derived from the last method that entered. To keep
      // the encoded value small just add the smallest value to make the lsb one.
      if (prev_action == TraceAction::kTraceMethodExit) {
        diff = 0;
      } else {
        diff = 1;
      }
    }
    curr_buffer_ptr = EncodeSignedLeb128(curr_buffer_ptr, diff);
    num_records++;
    prev_method_action_encoding = method_action_encoding;
    prev_action = action;
  }

  // Fill in header information:
  // 1 byte of header identifier
  // 4 bytes of thread_id
  // 3 bytes of number of records
  buffer[0] = kEntryHeaderV2;
  Append4LE(buffer + 1, thread_id);
  Append3LE(buffer + 5, num_records);
  return curr_buffer_ptr;
}

void TraceProfiler::Dump(int fd) {
  if (!art_flags::always_enable_profile_code()) {
    LOG(ERROR) << "Feature not supported. Please build with ART_ALWAYS_ENABLE_PROFILE_CODE.";
    return;
  }

  std::unique_ptr<File> trace_file(new File(fd, /*check_usage=*/true));
  Dump(std::move(trace_file));
}

std::string TraceProfiler::GetMethodInfoLine(ArtMethod* method) {
  return StringPrintf("%s\t%s\t%s\t%s\n",
                      PrettyDescriptor(method->GetDeclaringClassDescriptor()).c_str(),
                      method->GetName(),
                      method->GetSignature().ToString().c_str(),
                      method->GetDeclaringClassSourceFile());
}

void TraceProfiler::Dump(const char* filename) {
  if (!art_flags::always_enable_profile_code()) {
    LOG(ERROR) << "Feature not supported. Please build with ART_ALWAYS_ENABLE_PROFILE_CODE.";
    return;
  }

  std::unique_ptr<File> trace_file(OS::CreateEmptyFileWriteOnly(filename));
  if (trace_file == nullptr) {
    PLOG(ERROR) << "Unable to open trace file " << filename;
    return;
  }

  Dump(std::move(trace_file));
}

void TraceProfiler::Dump(std::unique_ptr<File>&& trace_file) {
  Thread* self = Thread::Current();
  std::unordered_set<ArtMethod*> traced_methods;
  std::unordered_map<size_t, std::string> traced_threads;
  MutexLock mu(self, *Locks::trace_lock_);
  if (!profile_in_progress_) {
    LOG(ERROR) << "No Profile in progress. Nothing to dump.";
    return;
  }

  uint8_t* buffer_ptr = new uint8_t[kBufSizeForEncodedData];
  uint8_t* curr_buffer_ptr = buffer_ptr;

  // Add a header for the trace: 4-bits of magic value and 2-bits for the version.
  Append4LE(curr_buffer_ptr, kProfileMagicValue);
  Append2LE(curr_buffer_ptr + 4, /*trace_version=*/ 1);
  curr_buffer_ptr += 6;

  ScopedSuspendAll ssa(__FUNCTION__);
  MutexLock tl(self, *Locks::thread_list_lock_);
  for (Thread* thread : Runtime::Current()->GetThreadList()->GetList()) {
    auto method_trace_entries = thread->GetMethodTraceBuffer();
    if (method_trace_entries == nullptr) {
      continue;
    }

    std::string thread_name;
    thread->GetThreadName(thread_name);
    traced_threads.emplace(thread->GetThreadId(), thread_name);

    size_t offset = curr_buffer_ptr - buffer_ptr;
    if (offset >= kMinBufSizeForEncodedData) {
      if (!trace_file->WriteFully(buffer_ptr, offset)) {
        PLOG(WARNING) << "Failed streaming a tracing event.";
      }
      curr_buffer_ptr = buffer_ptr;
    }
    curr_buffer_ptr =
        DumpBuffer(thread->GetTid(), method_trace_entries, curr_buffer_ptr, traced_methods);
    // Reset the buffer and continue profiling. We need to set the buffer to
    // zeroes, since we use a circular buffer and detect empty entries by
    // checking for zeroes.
    memset(method_trace_entries, 0, kAlwaysOnTraceBufSize * sizeof(uintptr_t));
    // Reset the current pointer.
    thread->SetMethodTraceBufferCurrentEntry(kAlwaysOnTraceBufSize);
  }

  // Write any remaining data to file and close the file.
  if (curr_buffer_ptr != buffer_ptr) {
    if (!trace_file->WriteFully(buffer_ptr, curr_buffer_ptr - buffer_ptr)) {
      PLOG(WARNING) << "Failed streaming a tracing event.";
    }
  }

  std::ostringstream os;
  // Dump data about thread information.
  os << "\n*threads\n";
  for (const auto& it : traced_threads) {
    os << it.first << "\t" << it.second << "\n";
  }

  // Dump data about method information.
  os << "*methods\n";
  for (ArtMethod* method : traced_methods) {
    uint64_t method_id = reinterpret_cast<uint64_t>(method);
    os << method_id << "\t" << GetMethodInfoLine(method);
  }

  os << "*end";

  std::string info = os.str();
  if (!trace_file->WriteFully(info.c_str(), info.length())) {
    PLOG(WARNING) << "Failed writing information to file";
  }

  if (!trace_file->Close()) {
    PLOG(WARNING) << "Failed to close file.";
  }
}

void TraceProfiler::ReleaseThreadBuffer(Thread* self) {
  if (!IsTraceProfileInProgress()) {
    return;
  }
  // TODO(mythria): Maybe it's good to cache these and dump them when requested. For now just
  // relese the buffer when a thread is exiting.
  auto buffer = self->GetMethodTraceBuffer();
  delete[] buffer;
  self->SetMethodTraceBuffer(nullptr, 0);
}

bool TraceProfiler::IsTraceProfileInProgress() {
  return profile_in_progress_;
}

}  // namespace art
