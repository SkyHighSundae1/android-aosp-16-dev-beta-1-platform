/*
 * Copyright (C) 2014 The Android Open Source Project
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

#ifndef ART_LIBELFFILE_ELF_ELF_UTILS_H_
#define ART_LIBELFFILE_ELF_ELF_UTILS_H_

#include <elf.h>

#include <sys/cdefs.h>

#include <android-base/logging.h>

namespace art {

struct ElfTypes32 {
  using Addr = Elf32_Addr;
  using Off = Elf32_Off;
  using Half = Elf32_Half;
  using Word = Elf32_Word;
  using Sword = Elf32_Sword;
  using Ehdr = Elf32_Ehdr;
  using Shdr = Elf32_Shdr;
  using Sym = Elf32_Sym;
  using Rel = Elf32_Rel;
  using Rela = Elf32_Rela;
  using Phdr = Elf32_Phdr;
  using Dyn = Elf32_Dyn;
};

struct ElfTypes64 {
  using Addr = Elf64_Addr;
  using Off = Elf64_Off;
  using Half = Elf64_Half;
  using Word = Elf64_Word;
  using Sword = Elf64_Sword;
  using Xword = Elf64_Xword;
  using Sxword = Elf64_Sxword;
  using Ehdr = Elf64_Ehdr;
  using Shdr = Elf64_Shdr;
  using Sym = Elf64_Sym;
  using Rel = Elf64_Rel;
  using Rela = Elf64_Rela;
  using Phdr = Elf64_Phdr;
  using Dyn = Elf64_Dyn;
};

// Only bionic has ELF class generic macros; glibc/musl require you to specify
// ELF32 or ELF64, even though they're the same, and that makes call sites look
// misleadingly class-specific.
#ifndef __BIONIC__
#define ELF_ST_BIND(x) ELF64_ST_BIND(x)
#define ELF_ST_TYPE(x) ELF64_ST_TYPE(x)
#endif

// Missing from musl (https://www.openwall.com/lists/musl/2024/03/21/10).
#ifndef EF_RISCV_RVC
#define EF_RISCV_RVC 0x1
#define EF_RISCV_FLOAT_ABI_DOUBLE 0x4
#endif

// Patching section type
#define SHT_OAT_PATCH        SHT_LOUSER

}  // namespace art

#endif  // ART_LIBELFFILE_ELF_ELF_UTILS_H_
