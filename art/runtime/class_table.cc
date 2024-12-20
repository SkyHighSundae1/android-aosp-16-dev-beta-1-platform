/*
 * Copyright (C) 2015 The Android Open Source Project
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

#include "class_table-inl.h"

#include "base/stl_util.h"
#include "mirror/class-inl.h"
#include "mirror/string-inl.h"
#include "oat/oat_file.h"

namespace art HIDDEN {

ClassTable::ClassTable() : lock_("Class loader classes", kClassLoaderClassesLock) {
  Runtime* const runtime = Runtime::Current();
  classes_.push_back(ClassSet(runtime->GetHashTableMinLoadFactor(),
                              runtime->GetHashTableMaxLoadFactor()));
}

void ClassTable::FreezeSnapshot() {
  WriterMutexLock mu(Thread::Current(), lock_);
  // Propagate the min/max load factor from the old active set.
  DCHECK(!classes_.empty());
  const ClassSet& last_set = classes_.back();
  ClassSet new_set(last_set.GetMinLoadFactor(), last_set.GetMaxLoadFactor());
  classes_.push_back(std::move(new_set));
}

ObjPtr<mirror::Class> ClassTable::UpdateClass(ObjPtr<mirror::Class> klass, size_t hash) {
  WriterMutexLock mu(Thread::Current(), lock_);
  // Should only be updating latest table.
  TableSlot slot(klass, hash);
  auto existing_it = classes_.back().FindWithHash(slot, hash);
  if (UNLIKELY(existing_it == classes_.back().end())) {
    for (const ClassSet& class_set : classes_) {
      if (class_set.FindWithHash(slot, hash) != class_set.end()) {
        LOG(FATAL) << "Updating class found in frozen table " << klass->PrettyDescriptor();
        UNREACHABLE();
      }
    }
    LOG(FATAL) << "Updating class not found " << klass->PrettyDescriptor();
    UNREACHABLE();
  }
  const ObjPtr<mirror::Class> existing = existing_it->Read();
  CHECK_NE(existing, klass) << klass->PrettyDescriptor();
  CHECK(!existing->IsResolved()) << klass->PrettyDescriptor();
  CHECK_EQ(klass->GetStatus(), ClassStatus::kResolving) << klass->PrettyDescriptor();
  CHECK(!klass->IsTemp()) << klass->PrettyDescriptor();
  VerifyObject(klass);
  // Update the element in the hash set with the new class. This is safe to do since the descriptor
  // doesn't change.
  *existing_it = slot;
  return existing;
}

size_t ClassTable::CountDefiningLoaderClasses(ObjPtr<mirror::ClassLoader> defining_loader,
                                              const ClassSet& set) const {
  size_t count = 0;
  for (const TableSlot& root : set) {
    if (root.Read()->GetClassLoader() == defining_loader) {
      ++count;
    }
  }
  return count;
}

size_t ClassTable::NumZygoteClasses(ObjPtr<mirror::ClassLoader> defining_loader) const {
  ReaderMutexLock mu(Thread::Current(), lock_);
  size_t sum = 0;
  for (size_t i = 0; i < classes_.size() - 1; ++i) {
    sum += CountDefiningLoaderClasses(defining_loader, classes_[i]);
  }
  return sum;
}

size_t ClassTable::NumNonZygoteClasses(ObjPtr<mirror::ClassLoader> defining_loader) const {
  ReaderMutexLock mu(Thread::Current(), lock_);
  return CountDefiningLoaderClasses(defining_loader, classes_.back());
}

size_t ClassTable::NumReferencedZygoteClasses() const {
  ReaderMutexLock mu(Thread::Current(), lock_);
  size_t sum = 0;
  for (size_t i = 0; i < classes_.size() - 1; ++i) {
    sum += classes_[i].size();
  }
  return sum;
}

size_t ClassTable::NumReferencedNonZygoteClasses() const {
  ReaderMutexLock mu(Thread::Current(), lock_);
  return classes_.back().size();
}

ObjPtr<mirror::Class> ClassTable::Lookup(std::string_view descriptor, size_t hash) {
  DescriptorHashPair pair(descriptor, hash);
  ReaderMutexLock mu(Thread::Current(), lock_);
  // Search from the last table, assuming that apps shall search for their own classes
  // more often than for boot image classes. For prebuilt boot images, this also helps
  // by searching the large table from the framework boot image extension compiled as
  // single-image before the individual small tables from the primary boot image
  // compiled as multi-image.
  for (ClassSet& class_set : ReverseRange(classes_)) {
    auto it = class_set.FindWithHash(pair, hash);
    if (it != class_set.end()) {
      return it->Read();
    }
  }
  return nullptr;
}

void ClassTable::Insert(ObjPtr<mirror::Class> klass) {
  InsertWithHash(klass, klass->DescriptorHash());
}

void ClassTable::InsertWithHash(ObjPtr<mirror::Class> klass, size_t hash) {
  WriterMutexLock mu(Thread::Current(), lock_);
  classes_.back().InsertWithHash(TableSlot(klass, hash), hash);
}

bool ClassTable::InsertStrongRoot(ObjPtr<mirror::Object> obj) {
  WriterMutexLock mu(Thread::Current(), lock_);
  DCHECK(obj != nullptr);
  for (GcRoot<mirror::Object>& root : strong_roots_) {
    if (root.Read() == obj) {
      return false;
    }
  }
  strong_roots_.push_back(GcRoot<mirror::Object>(obj));
  // If `obj` is a dex cache associated with a new oat file with GC roots, add it to oat_files_.
  if (obj->IsDexCache()) {
    const DexFile* dex_file = ObjPtr<mirror::DexCache>::DownCast(obj)->GetDexFile();
    if (dex_file != nullptr && dex_file->GetOatDexFile() != nullptr) {
      const OatFile* oat_file = dex_file->GetOatDexFile()->GetOatFile();
      if (oat_file != nullptr && !oat_file->GetBssGcRoots().empty()) {
        InsertOatFileLocked(oat_file);  // Ignore return value.
      }
    }
  }
  return true;
}

bool ClassTable::InsertOatFile(const OatFile* oat_file) {
  WriterMutexLock mu(Thread::Current(), lock_);
  return InsertOatFileLocked(oat_file);
}

bool ClassTable::InsertOatFileLocked(const OatFile* oat_file) {
  if (ContainsElement(oat_files_, oat_file)) {
    return false;
  }
  oat_files_.push_back(oat_file);
  return true;
}

size_t ClassTable::ReadFromMemory(uint8_t* ptr) {
  size_t read_count = 0;
  AddClassSet(ClassSet(ptr, /*make copy*/false, &read_count));
  return read_count;
}

void ClassTable::AddClassSet(ClassSet&& set) {
  WriterMutexLock mu(Thread::Current(), lock_);
  // Insert before the last (unfrozen) table since we add new classes into the back.
  // Keep the order of previous frozen tables unchanged, so that we can can remember
  // the number of searched frozen tables and not search them again.
  // TODO: Make use of this in `ClassLinker::FindClass()`.
  DCHECK(!classes_.empty());
  classes_.insert(classes_.end() - 1, std::move(set));
}

void ClassTable::ClearStrongRoots() {
  WriterMutexLock mu(Thread::Current(), lock_);
  oat_files_.clear();
  strong_roots_.clear();
}

}  // namespace art
