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

#ifndef ART_RUNTIME_TRACE_PROFILE_H_
#define ART_RUNTIME_TRACE_PROFILE_H_

#include <unordered_set>

#include "base/locks.h"
#include "base/macros.h"
#include "base/os.h"

namespace art HIDDEN {

class ArtMethod;

// TODO(mythria): A randomly chosen value. Tune it later based on the number of
// entries required in the buffer.
static constexpr size_t kAlwaysOnTraceBufSize = 2048;

// This class implements low-overhead tracing. This feature is available only when
// always_enable_profile_code is enabled which is a build time flag defined in
// build/flags/art-flags.aconfig. When this flag is enabled, AOT and JITed code can record events
// on each method execution. When a profile is started, method entry / exit events are recorded in
// a per-thread circular buffer. When requested the recorded events in the buffer are dumped into a
// file. The buffers are released when the profile is stopped.
class TraceProfiler {
 public:
  // Starts profiling by allocating a per-thread buffer for all the threads.
  static void Start();

  // Releases all the buffers.
  static void Stop();

  // Dumps the recorded events in the buffer from all threads in the specified file.
  static void Dump(int fd);
  static void Dump(const char* trace_filename);

  // Called when thread is exiting to release the allocated buffer.
  static void ReleaseThreadBuffer(Thread* self) REQUIRES(Locks::trace_lock_);

  static bool IsTraceProfileInProgress() REQUIRES(Locks::trace_lock_);

  // Allocates a buffer for the specified thread.
  static void AllocateBuffer(Thread* thread);

 private:
  // Dumps the events from all threads into the trace_file.
  static void Dump(std::unique_ptr<File>&& trace_file);

  // This method goes over all the events in the thread_buffer and stores the encoded event in the
  // buffer. It returns the pointer to the next free entry in the buffer.
  // This also records the ArtMethods from the events in the thread_buffer in a set. This set is
  // used to dump the information about the methods once buffers from all threads have been
  // processed.
  static uint8_t* DumpBuffer(uint32_t thread_id,
                             uintptr_t* thread_buffer,
                             uint8_t* buffer /* out */,
                             std::unordered_set<ArtMethod*>& methods /* out */);

  static std::string GetMethodInfoLine(ArtMethod* method) REQUIRES(Locks::mutator_lock_);

  static bool profile_in_progress_ GUARDED_BY(Locks::trace_lock_);
  DISALLOW_COPY_AND_ASSIGN(TraceProfiler);
};

}  // namespace art

#endif  // ART_RUNTIME_TRACE_PROFILE_H_
