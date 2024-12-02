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

#include "nodes.h"

#include "base/arena_allocator.h"
#include "base/macros.h"
#include "optimizing_unit_test.h"

#include "gtest/gtest.h"

namespace art HIDDEN {

class NodeTest : public OptimizingUnitTest {};

/**
 * Test that we can clear loop and dominator information in either order.
 * Code is:
 * while (true) {
 *   if (foobar) { break; }
 *   if (baz) { xyz; } else { abc; }
 * }
 * dosomething();
 */
TEST_F(NodeTest, ClearLoopThenDominanceInformation) {
  CreateGraph();
  AdjacencyListGraph alg(graph_,
                         GetAllocator(),
                         "entry",
                         "exit",
                         {{"entry", "loop_pre_header"},

                          {"loop_pre_header", "loop_header"},
                          {"loop_header", "critical_break"},
                          {"loop_header", "loop_body"},
                          {"loop_body", "loop_if_left"},
                          {"loop_body", "loop_if_right"},
                          {"loop_if_left", "loop_merge"},
                          {"loop_if_right", "loop_merge"},
                          {"loop_merge", "loop_header"},

                          {"critical_break", "breturn"},
                          {"breturn", "exit"}});
  graph_->ClearDominanceInformation();
  graph_->BuildDominatorTree();

  // Test
  EXPECT_TRUE(
      std::all_of(graph_->GetBlocks().begin(), graph_->GetBlocks().end(), [&](HBasicBlock* b) {
        return b == graph_->GetEntryBlock() || b == nullptr || b->GetDominator() != nullptr;
      }));
  EXPECT_TRUE(
      std::any_of(graph_->GetBlocks().begin(), graph_->GetBlocks().end(), [&](HBasicBlock* b) {
        return b != nullptr && b->GetLoopInformation() != nullptr;
      }));

  // Clear
  graph_->ClearLoopInformation();
  graph_->ClearDominanceInformation();

  // Test
  EXPECT_TRUE(
      std::none_of(graph_->GetBlocks().begin(), graph_->GetBlocks().end(), [&](HBasicBlock* b) {
        return b != nullptr && b->GetDominator() != nullptr;
      }));
  EXPECT_TRUE(
      std::all_of(graph_->GetBlocks().begin(), graph_->GetBlocks().end(), [&](HBasicBlock* b) {
        return b == nullptr || b->GetLoopInformation() == nullptr;
      }));
}

/**
 * Test that we can clear loop and dominator information in either order.
 * Code is:
 * while (true) {
 *   if (foobar) { break; }
 *   if (baz) { xyz; } else { abc; }
 * }
 * dosomething();
 */
TEST_F(NodeTest, ClearDominanceThenLoopInformation) {
  CreateGraph();
  AdjacencyListGraph alg(graph_,
                         GetAllocator(),
                         "entry",
                         "exit",
                         {{"entry", "loop_pre_header"},

                          {"loop_pre_header", "loop_header"},
                          {"loop_header", "critical_break"},
                          {"loop_header", "loop_body"},
                          {"loop_body", "loop_if_left"},
                          {"loop_body", "loop_if_right"},
                          {"loop_if_left", "loop_merge"},
                          {"loop_if_right", "loop_merge"},
                          {"loop_merge", "loop_header"},

                          {"critical_break", "breturn"},
                          {"breturn", "exit"}});
  graph_->ClearDominanceInformation();
  graph_->BuildDominatorTree();

  // Test
  EXPECT_TRUE(
      std::all_of(graph_->GetBlocks().begin(), graph_->GetBlocks().end(), [&](HBasicBlock* b) {
        return b == graph_->GetEntryBlock() || b == nullptr || b->GetDominator() != nullptr;
      }));
  EXPECT_TRUE(
      std::any_of(graph_->GetBlocks().begin(), graph_->GetBlocks().end(), [&](HBasicBlock* b) {
        return b != nullptr && b->GetLoopInformation() != nullptr;
      }));

  // Clear
  graph_->ClearDominanceInformation();
  graph_->ClearLoopInformation();

  // Test
  EXPECT_TRUE(
      std::none_of(graph_->GetBlocks().begin(), graph_->GetBlocks().end(), [&](HBasicBlock* b) {
        return b != nullptr && b->GetDominator() != nullptr;
      }));
  EXPECT_TRUE(
      std::all_of(graph_->GetBlocks().begin(), graph_->GetBlocks().end(), [&](HBasicBlock* b) {
        return b == nullptr || b->GetLoopInformation() == nullptr;
      }));
}

/**
 * Test that removing instruction from the graph removes itself from user lists
 * and environment lists.
 */
TEST_F(NodeTest, RemoveInstruction) {
  HBasicBlock* main = InitEntryMainExitGraphWithReturnVoid();

  HInstruction* parameter = MakeParam(DataType::Type::kReference);

  HInstruction* null_check = MakeNullCheck(main, parameter, /*env=*/ {parameter});

  ASSERT_TRUE(parameter->HasEnvironmentUses());
  ASSERT_TRUE(parameter->HasUses());

  main->RemoveInstruction(null_check);

  ASSERT_FALSE(parameter->HasEnvironmentUses());
  ASSERT_FALSE(parameter->HasUses());
}

/**
 * Test that inserting an instruction in the graph updates user lists.
 */
TEST_F(NodeTest, InsertInstruction) {
  HGraph* graph = CreateGraph();
  HBasicBlock* entry = new (GetAllocator()) HBasicBlock(graph);
  graph->AddBlock(entry);
  graph->SetEntryBlock(entry);
  HInstruction* parameter1 = MakeParam(DataType::Type::kReference);
  HInstruction* parameter2 = MakeParam(DataType::Type::kReference);
  MakeExit(entry);

  ASSERT_FALSE(parameter1->HasUses());

  HInstruction* to_insert = new (GetAllocator()) HNullCheck(parameter1, 0);
  entry->InsertInstructionBefore(to_insert, parameter2);

  ASSERT_TRUE(parameter1->HasUses());
  ASSERT_TRUE(parameter1->GetUses().HasExactlyOneElement());
}

/**
 * Test that adding an instruction in the graph updates user lists.
 */
TEST_F(NodeTest, AddInstruction) {
  HGraph* graph = CreateGraph();
  HBasicBlock* entry = new (GetAllocator()) HBasicBlock(graph);
  graph->AddBlock(entry);
  graph->SetEntryBlock(entry);
  HInstruction* parameter = MakeParam(DataType::Type::kReference);

  ASSERT_FALSE(parameter->HasUses());

  MakeNullCheck(entry, parameter);

  ASSERT_TRUE(parameter->HasUses());
  ASSERT_TRUE(parameter->GetUses().HasExactlyOneElement());
}

TEST_F(NodeTest, ParentEnvironment) {
  HGraph* graph = CreateGraph();
  HBasicBlock* entry = new (GetAllocator()) HBasicBlock(graph);
  graph->AddBlock(entry);
  graph->SetEntryBlock(entry);
  HInstruction* parameter1 = MakeParam(DataType::Type::kReference);
  HInstruction* with_environment = MakeNullCheck(entry, parameter1, /*env=*/ {parameter1});
  MakeExit(entry);

  ASSERT_TRUE(parameter1->HasUses());
  ASSERT_TRUE(parameter1->GetUses().HasExactlyOneElement());

  ASSERT_TRUE(parameter1->HasEnvironmentUses());
  ASSERT_TRUE(parameter1->GetEnvUses().HasExactlyOneElement());

  HEnvironment* parent1 = HEnvironment::Create(
      GetAllocator(),
      /*number_of_vregs=*/ 1,
      graph->GetArtMethod(),
      /*dex_pc=*/ 0,
      /*holder=*/ nullptr);
  parent1->CopyFrom(ArrayRef<HInstruction* const>(&parameter1, 1u));

  ASSERT_EQ(parameter1->GetEnvUses().SizeSlow(), 2u);

  HEnvironment* parent2 = HEnvironment::Create(
      GetAllocator(),
      /*number_of_vregs=*/ 1,
      graph->GetArtMethod(),
      /*dex_pc=*/ 0,
      /*holder=*/ nullptr);
  parent2->CopyFrom(ArrayRef<HInstruction* const>(&parameter1, 1u));
  parent1->SetAndCopyParentChain(GetAllocator(), parent2);

  // One use for parent2, and one other use for the new parent of parent1.
  ASSERT_EQ(parameter1->GetEnvUses().SizeSlow(), 4u);

  // We have copied the parent chain. So we now have two more uses.
  with_environment->GetEnvironment()->SetAndCopyParentChain(GetAllocator(), parent1);
  ASSERT_EQ(parameter1->GetEnvUses().SizeSlow(), 6u);
}

}  // namespace art
