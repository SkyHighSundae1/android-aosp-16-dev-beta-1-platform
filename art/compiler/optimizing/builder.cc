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

#include "builder.h"

#include "art_field-inl.h"
#include "base/arena_bit_vector.h"
#include "base/bit_vector-inl.h"
#include "base/logging.h"
#include "block_builder.h"
#include "code_generator.h"
#include "data_type-inl.h"
#include "driver/compiler_options.h"
#include "driver/dex_compilation_unit.h"
#include "instruction_builder.h"
#include "mirror/class_loader.h"
#include "mirror/dex_cache.h"
#include "nodes.h"
#include "optimizing_compiler_stats.h"
#include "ssa_builder.h"
#include "thread.h"

namespace art HIDDEN {

HGraphBuilder::HGraphBuilder(HGraph* graph,
                             const CodeItemDebugInfoAccessor& accessor,
                             const DexCompilationUnit* dex_compilation_unit,
                             const DexCompilationUnit* outer_compilation_unit,
                             CodeGenerator* code_generator,
                             OptimizingCompilerStats* compiler_stats)
    : graph_(graph),
      dex_file_(&graph->GetDexFile()),
      code_item_accessor_(accessor),
      dex_compilation_unit_(dex_compilation_unit),
      outer_compilation_unit_(outer_compilation_unit),
      code_generator_(code_generator),
      compilation_stats_(compiler_stats),
      return_type_(DataType::FromShorty(dex_compilation_unit_->GetShorty()[0])) {}

HGraphBuilder::HGraphBuilder(HGraph* graph,
                             const DexCompilationUnit* dex_compilation_unit,
                             const CodeItemDebugInfoAccessor& accessor,
                             DataType::Type return_type)
    : graph_(graph),
      dex_file_(&graph->GetDexFile()),
      code_item_accessor_(accessor),
      dex_compilation_unit_(dex_compilation_unit),
      outer_compilation_unit_(nullptr),
      code_generator_(nullptr),
      compilation_stats_(nullptr),
      return_type_(return_type) {}

bool HGraphBuilder::SkipCompilation() {
  if (code_generator_ == nullptr) {
    // Note that the codegen is null when unit testing.
    return false;
  }

  const CompilerOptions& compiler_options = code_generator_->GetCompilerOptions();
  CompilerFilter::Filter compiler_filter = compiler_options.GetCompilerFilter();
  if (compiler_filter == CompilerFilter::kEverything) {
    return false;
  }

  const uint32_t code_units = code_item_accessor_.InsnsSizeInCodeUnits();
  if (compiler_options.IsHugeMethod(code_units)) {
    VLOG(compiler) << "Skip compilation of huge method "
                   << dex_file_->PrettyMethod(dex_compilation_unit_->GetDexMethodIndex())
                   << ": " << code_units << " code units";
    MaybeRecordStat(compilation_stats_, MethodCompilationStat::kNotCompiledHugeMethod);
    return true;
  }

  return false;
}

GraphAnalysisResult HGraphBuilder::BuildGraph() {
  DCHECK(code_item_accessor_.HasCodeItem());
  DCHECK(graph_->GetBlocks().empty());

  graph_->SetNumberOfVRegs(code_item_accessor_.RegistersSize());
  graph_->SetNumberOfInVRegs(code_item_accessor_.InsSize());

  // Use ScopedArenaAllocator for all local allocations.
  ScopedArenaAllocator local_allocator(graph_->GetArenaStack());
  HBasicBlockBuilder block_builder(graph_, dex_file_, code_item_accessor_, &local_allocator);
  SsaBuilder ssa_builder(graph_,
                         dex_compilation_unit_->GetClassLoader(),
                         dex_compilation_unit_->GetDexCache(),
                         &local_allocator);
  HInstructionBuilder instruction_builder(graph_,
                                          &block_builder,
                                          &ssa_builder,
                                          dex_file_,
                                          code_item_accessor_,
                                          return_type_,
                                          dex_compilation_unit_,
                                          outer_compilation_unit_,
                                          code_generator_,
                                          compilation_stats_,
                                          &local_allocator);

  // 1) Create basic blocks and link them together. Basic blocks are left
  //    unpopulated with the exception of synthetic blocks, e.g. HTryBoundaries.
  if (!block_builder.Build()) {
    return kAnalysisInvalidBytecode;
  }

  // 2) Decide whether to skip compiling this method based on e.g. the compiler filter and method's
  // code size.
  if (SkipCompilation()) {
    return kAnalysisSkipped;
  }

  // 3) Build the dominator tree and fill in loop and try/catch metadata.
  GraphAnalysisResult result = graph_->BuildDominatorTree();
  if (result != kAnalysisSuccess) {
    return result;
  }

  // 4) Populate basic blocks with instructions.
  if (!instruction_builder.Build()) {
    return kAnalysisInvalidBytecode;
  }

  // 5) Type the graph and eliminate dead/redundant phis.
  return ssa_builder.BuildSsa();
}

void HGraphBuilder::BuildIntrinsicGraph(ArtMethod* method) {
  DCHECK(!code_item_accessor_.HasCodeItem());
  DCHECK(graph_->GetBlocks().empty());

  // Determine the number of arguments and associated vregs.
  uint32_t method_idx = dex_compilation_unit_->GetDexMethodIndex();
  const char* shorty = dex_file_->GetMethodShorty(dex_file_->GetMethodId(method_idx));
  size_t num_args = strlen(shorty + 1);
  size_t num_wide_args = std::count(shorty + 1, shorty + 1 + num_args, 'J') +
                         std::count(shorty + 1, shorty + 1 + num_args, 'D');
  size_t num_arg_vregs = num_args + num_wide_args + (dex_compilation_unit_->IsStatic() ? 0u : 1u);

  // For simplicity, reserve 2 vregs (the maximum) for return value regardless of the return type.
  size_t return_vregs = 2u;
  graph_->SetNumberOfVRegs(return_vregs + num_arg_vregs);
  graph_->SetNumberOfInVRegs(num_arg_vregs);

  // Use ScopedArenaAllocator for all local allocations.
  ScopedArenaAllocator local_allocator(graph_->GetArenaStack());
  HBasicBlockBuilder block_builder(graph_,
                                   dex_file_,
                                   CodeItemDebugInfoAccessor(),
                                   &local_allocator);
  SsaBuilder ssa_builder(graph_,
                         dex_compilation_unit_->GetClassLoader(),
                         dex_compilation_unit_->GetDexCache(),
                         &local_allocator);
  HInstructionBuilder instruction_builder(graph_,
                                          &block_builder,
                                          &ssa_builder,
                                          dex_file_,
                                          CodeItemDebugInfoAccessor(),
                                          return_type_,
                                          dex_compilation_unit_,
                                          outer_compilation_unit_,
                                          code_generator_,
                                          compilation_stats_,
                                          &local_allocator);

  // 1) Create basic blocks for the intrinsic and link them together.
  block_builder.BuildIntrinsic();

  // 2) Build the trivial dominator tree.
  GraphAnalysisResult bdt_result = graph_->BuildDominatorTree();
  DCHECK_EQ(bdt_result, kAnalysisSuccess);

  // 3) Populate basic blocks with instructions for the intrinsic.
  instruction_builder.BuildIntrinsic(method);

  // 4) Type the graph (no dead/redundant phis to eliminate).
  GraphAnalysisResult build_ssa_result = ssa_builder.BuildSsa();
  DCHECK_EQ(build_ssa_result, kAnalysisSuccess);
}

}  // namespace art
