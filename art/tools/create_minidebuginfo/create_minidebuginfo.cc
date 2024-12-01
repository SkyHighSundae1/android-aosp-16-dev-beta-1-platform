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

#include <algorithm>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "android-base/logging.h"
#include "base/os.h"
#include "base/unix_file/fd_file.h"
#include "elf/elf_builder.h"
#include "elf/elf_debug_reader.h"
#include "elf/xz_utils.h"
#include "stream/file_output_stream.h"
#include "stream/vector_output_stream.h"

namespace art {

static constexpr size_t kBlockSize = 32 * KB;

constexpr const char kSortedSymbolName[] = "$android.symtab.sorted";

template<typename ElfTypes>
static void WriteMinidebugInfo(const std::vector<uint8_t>& input, std::vector<uint8_t>* output) {
  using Elf_Addr = typename ElfTypes::Addr;
  using Elf_Shdr = typename ElfTypes::Shdr;
  using Elf_Sym = typename ElfTypes::Sym;
  using Elf_Word = typename ElfTypes::Word;
  using CIE = typename ElfDebugReader<ElfTypes>::CIE;
  using FDE = typename ElfDebugReader<ElfTypes>::FDE;

  ElfDebugReader<ElfTypes> reader(input);

  std::vector<uint8_t> output_elf_data;
  VectorOutputStream output_stream("Output ELF", &output_elf_data);
  InstructionSet isa = ElfBuilder<ElfTypes>::GetIsaFromHeader(*reader.GetHeader());
  std::unique_ptr<ElfBuilder<ElfTypes>> builder(new ElfBuilder<ElfTypes>(isa, &output_stream));
  builder->Start(/*write_program_headers=*/ false);

  auto* text = builder->GetText();
  const Elf_Shdr* original_text = reader.GetSection(".text");
  CHECK(original_text != nullptr);
  text->AllocateVirtualMemory(original_text->sh_addr, original_text->sh_size);

  auto* strtab = builder->GetStrTab();
  auto* symtab = builder->GetSymTab();
  strtab->Start();
  {
    std::unordered_map<uint64_t, uint64_t> dyn_funcs_by_offset;
    reader.VisitDynamicSymbols([&](Elf_Sym sym, const char*) {
      // Keep track of all of the dynamic function symbols.
      if (ELF32_ST_TYPE(sym.st_info) == STT_FUNC && sym.st_size != 0) {
        auto it = dyn_funcs_by_offset.find(sym.st_value);
        if (it == dyn_funcs_by_offset.end() || it->second < sym.st_size) {
          dyn_funcs_by_offset[sym.st_value] = sym.st_size;
        }
      }
    });
    std::unordered_map<uint64_t, std::string_view> funcs_by_offset;
    std::multimap<std::string_view, Elf_Sym> syms;
    reader.VisitFunctionSymbols([&](Elf_Sym sym, const char* name) {
      // Exclude non-function or empty symbols.
      if (ELF32_ST_TYPE(sym.st_info) != STT_FUNC || sym.st_size == 0) {
        return;
      }

      // Exclude symbols at the same offset as a symbol in the set of
      // dynamic symbols.
      auto dyn_it = dyn_funcs_by_offset.find(sym.st_value);
      if (dyn_it != dyn_funcs_by_offset.end()) {
        CHECK(dyn_it->second >= sym.st_size);
        return;
      }

      // Exclude symbols with the same offset as a previous symbol.
      if (funcs_by_offset.contains(sym.st_value)) {
        const std::string_view& previous_name = funcs_by_offset[sym.st_value];

        // Find the previous symbol entry.
        auto it = syms.find(previous_name);
        while (it != syms.end() && it->second.st_value != sym.st_value) {
          ++it;
        }
        CHECK(it != syms.end());

        // When there is a duplicate, always choose the symbol with the
        // largest size.
        // In order to produce the same symbol table every time, if the
        // symbol has the same size choose the symbol with the shortest
        // name, or the symbol first according to ascii comparison.
        if (sym.st_size < it->second.st_size ||
            (sym.st_size == it->second.st_size &&
             (previous_name.size() < strlen(name) || previous_name.compare(name) <= 0))) {
          return;
        }
        syms.erase(it);
      }
      funcs_by_offset[sym.st_value] = name;
      syms.emplace(name, sym);
    });
    if (!syms.empty()) {
      symtab->Add(strtab->Write(kSortedSymbolName), nullptr, 0, 0, STB_GLOBAL, STT_NOTYPE);
    }
    for (auto& entry : syms) {
      std::string_view name = entry.first;
      const Elf_Sym& sym = entry.second;
      Elf_Word name_idx = strtab->Write(name);
      symtab->Add(name_idx, text, sym.st_value, sym.st_size, STB_GLOBAL, STT_FUNC);
    }
  }
  strtab->End();
  symtab->WriteCachedSection();

  auto* debug_frame = builder->GetDebugFrame();
  debug_frame->Start();
  {
    std::map<std::string_view, Elf_Addr> cie_dedup;
    std::unordered_map<const CIE*, Elf_Addr> new_cie_offset;
    std::deque<std::pair<const FDE*, const CIE*>> entries;
    // Read, de-duplicate and write CIE entries.  Read FDE entries.
    reader.VisitDebugFrame(
        [&](const CIE* cie) {
          std::string_view key(reinterpret_cast<const char*>(cie->data()), cie->size());
          auto it = cie_dedup.emplace(key, debug_frame->GetPosition());
          if (/* inserted */ it.second) {
            debug_frame->WriteFully(cie->data(), cie->size());
          }
          new_cie_offset[cie] = it.first->second;
        },
        [&](const FDE* fde, const CIE* cie) { entries.emplace_back(std::make_pair(fde, cie)); });
    // Sort FDE entries by opcodes to improve locality for compression (saves ~25%).
    std::stable_sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs) {
      constexpr size_t opcode_offset = sizeof(FDE);
      return std::lexicographical_compare(
          lhs.first->data() + opcode_offset, lhs.first->data() + lhs.first->size(),
          rhs.first->data() + opcode_offset, rhs.first->data() + rhs.first->size());
    });
    // Write all FDE entries while adjusting the CIE offsets to the new locations.
    for (const auto& entry : entries) {
      const FDE* fde = entry.first;
      const CIE* cie = entry.second;
      FDE new_header = *fde;
      new_header.cie_pointer = new_cie_offset[cie];
      debug_frame->WriteFully(&new_header, sizeof(FDE));
      debug_frame->WriteFully(fde->data() + sizeof(FDE), fde->size() - sizeof(FDE));
    }
  }
  debug_frame->End();

  builder->End();
  CHECK(builder->Good());

  XzCompress(ArrayRef<const uint8_t>(output_elf_data), output, 9 /*size*/, kBlockSize);
}

static int Main(int argc, char** argv) {
  // Check command like arguments.
  if (argc != 3) {
    printf("Usage: create_minidebuginfo ELF_FILE OUT_FILE\n");
    printf("  ELF_FILE: The path to an ELF file with full symbols (before being stripped).\n");
    printf("  OUT_FILE: The path for the generated mini-debug-info data (not an elf file).\n");
    return 1;
  }
  const char* input_filename = argv[1];
  const char* output_filename = argv[2];

  // Read input file.
  std::unique_ptr<File> input_file(OS::OpenFileForReading(input_filename));
  CHECK(input_file.get() != nullptr) << "Failed to open input file";
  std::vector<uint8_t> elf(input_file->GetLength());
  CHECK(input_file->ReadFully(elf.data(), elf.size())) << "Failed to read input file";

  // Write output file.
  std::vector<uint8_t> output;
  if (ElfDebugReader<ElfTypes32>::IsValidElfHeader(elf)) {
    WriteMinidebugInfo<ElfTypes32>(elf, &output);
  } else if (ElfDebugReader<ElfTypes64>::IsValidElfHeader(elf)) {
    WriteMinidebugInfo<ElfTypes64>(elf, &output);
  } else {
    LOG(FATAL) << "Invalid ELF file header " << input_filename;
  }
  std::unique_ptr<File> output_file(OS::CreateEmptyFile(output_filename));
  if (!output_file->WriteFully(output.data(), output.size()) || output_file->FlushClose() != 0) {
    LOG(FATAL) << "Failed to write " << output_filename;
  }
  return 0;
}

}  // namespace art

int main(int argc, char** argv) {
  return art::Main(argc, argv);
}
