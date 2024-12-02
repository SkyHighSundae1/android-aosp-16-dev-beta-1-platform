/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "base/macros.h"
#include "load_store_analysis.h"

#include <array>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "base/scoped_arena_allocator.h"
#include "class_root.h"
#include "dex/dex_file_types.h"
#include "dex/method_reference.h"
#include "entrypoints/quick/quick_entrypoints_enum.h"
#include "gtest/gtest.h"
#include "handle.h"
#include "handle_scope.h"
#include "nodes.h"
#include "optimizing/data_type.h"
#include "optimizing_unit_test.h"
#include "scoped_thread_state_change.h"

namespace art HIDDEN {

class LoadStoreAnalysisTest : public CommonCompilerTest, public OptimizingUnitTestHelper {
 public:
  LoadStoreAnalysisTest() {
    use_boot_image_ = true;  // Make the Runtime creation cheaper.
  }
};

TEST_F(LoadStoreAnalysisTest, ArrayHeapLocations) {
  HBasicBlock* main = InitEntryMainExitGraphWithReturnVoid();

  // entry
  HInstruction* array = MakeParam(DataType::Type::kReference);
  HInstruction* index = MakeParam(DataType::Type::kInt32);
  HInstruction* c1 = graph_->GetIntConstant(1);
  HInstruction* c2 = graph_->GetIntConstant(2);
  HInstruction* c3 = graph_->GetIntConstant(3);

  // main
  HInstruction* array_get1 = MakeArrayGet(main, array, c1, DataType::Type::kInt32);
  HInstruction* array_get2 = MakeArrayGet(main, array, c2, DataType::Type::kInt32);
  HInstruction* array_set1 = MakeArraySet(main, array, c1, c3, DataType::Type::kInt32);
  HInstruction* array_set2 = MakeArraySet(main, array, index, c3, DataType::Type::kInt32);

  // Test HeapLocationCollector initialization.
  // Should be no heap locations, no operations on the heap.
  ScopedArenaAllocator allocator(graph_->GetArenaStack());
  HeapLocationCollector heap_location_collector(graph_, &allocator);
  ASSERT_EQ(heap_location_collector.GetNumberOfHeapLocations(), 0U);
  ASSERT_FALSE(heap_location_collector.HasHeapStores());

  // Test that after visiting the graph_, it must see following heap locations
  // array[c1], array[c2], array[index]; and it should see heap stores.
  heap_location_collector.VisitBasicBlock(main);
  ASSERT_EQ(heap_location_collector.GetNumberOfHeapLocations(), 3U);
  ASSERT_TRUE(heap_location_collector.HasHeapStores());

  // Test queries on HeapLocationCollector's ref info and index records.
  ReferenceInfo* ref = heap_location_collector.FindReferenceInfoOf(array);
  DataType::Type type = DataType::Type::kInt32;
  size_t field = HeapLocation::kInvalidFieldOffset;
  size_t vec = HeapLocation::kScalar;
  size_t class_def = HeapLocation::kDeclaringClassDefIndexForArrays;
  const bool is_vec_op = false;
  size_t loc1 = heap_location_collector.FindHeapLocationIndex(
      ref, type, field, c1, vec, class_def, is_vec_op);
  size_t loc2 = heap_location_collector.FindHeapLocationIndex(
      ref, type, field, c2, vec, class_def, is_vec_op);
  size_t loc3 = heap_location_collector.FindHeapLocationIndex(
      ref, type, field, index, vec, class_def, is_vec_op);
  // must find this reference info for array in HeapLocationCollector.
  ASSERT_TRUE(ref != nullptr);
  // must find these heap locations;
  // and array[1], array[2], array[3] should be different heap locations.
  ASSERT_TRUE(loc1 != HeapLocationCollector::kHeapLocationNotFound);
  ASSERT_TRUE(loc2 != HeapLocationCollector::kHeapLocationNotFound);
  ASSERT_TRUE(loc3 != HeapLocationCollector::kHeapLocationNotFound);
  ASSERT_TRUE(loc1 != loc2);
  ASSERT_TRUE(loc2 != loc3);
  ASSERT_TRUE(loc1 != loc3);

  // Test alias relationships after building aliasing matrix.
  // array[1] and array[2] clearly should not alias;
  // array[index] should alias with the others, because index is an unknow value.
  heap_location_collector.BuildAliasingMatrix();
  ASSERT_FALSE(heap_location_collector.MayAlias(loc1, loc2));
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc3));
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc3));

  EXPECT_TRUE(CheckGraph());
}

TEST_F(LoadStoreAnalysisTest, FieldHeapLocations) {
  HBasicBlock* main = InitEntryMainExitGraphWithReturnVoid();

  // entry
  HInstruction* object = MakeParam(DataType::Type::kReference);
  HInstruction* c1 = graph_->GetIntConstant(1);

  // main
  HInstanceFieldSet* set_field10 = MakeIFieldSet(main, object, c1, MemberOffset(10));
  HInstanceFieldGet* get_field10 =
      MakeIFieldGet(main, object, DataType::Type::kInt32, MemberOffset(10));
  HInstanceFieldGet* get_field20 =
      MakeIFieldGet(main, object, DataType::Type::kInt32, MemberOffset(20));

  // Test HeapLocationCollector initialization.
  // Should be no heap locations, no operations on the heap.
  ScopedArenaAllocator allocator(graph_->GetArenaStack());
  HeapLocationCollector heap_location_collector(graph_, &allocator);
  ASSERT_EQ(heap_location_collector.GetNumberOfHeapLocations(), 0U);
  ASSERT_FALSE(heap_location_collector.HasHeapStores());

  // Test that after visiting the graph, it must see following heap locations
  // object.field10, object.field20 and it should see heap stores.
  heap_location_collector.VisitBasicBlock(main);
  ASSERT_EQ(heap_location_collector.GetNumberOfHeapLocations(), 2U);
  ASSERT_TRUE(heap_location_collector.HasHeapStores());

  // Test queries on HeapLocationCollector's ref info and index records.
  ReferenceInfo* ref = heap_location_collector.FindReferenceInfoOf(object);
  size_t loc1 = heap_location_collector.GetFieldHeapLocation(object, &get_field10->GetFieldInfo());
  size_t loc2 = heap_location_collector.GetFieldHeapLocation(object, &get_field20->GetFieldInfo());
  // must find references info for object and in HeapLocationCollector.
  ASSERT_TRUE(ref != nullptr);
  // must find these heap locations.
  ASSERT_TRUE(loc1 != HeapLocationCollector::kHeapLocationNotFound);
  ASSERT_TRUE(loc2 != HeapLocationCollector::kHeapLocationNotFound);
  // different fields of same object.
  ASSERT_TRUE(loc1 != loc2);
  // accesses to different fields of the same object should not alias.
  ASSERT_FALSE(heap_location_collector.MayAlias(loc1, loc2));

  EXPECT_TRUE(CheckGraph());
}

TEST_F(LoadStoreAnalysisTest, ArrayIndexAliasingTest) {
  HBasicBlock* body = InitEntryMainExitGraphWithReturnVoid();

  HInstruction* array = MakeParam(DataType::Type::kReference);
  HInstruction* index = MakeParam(DataType::Type::kInt32);
  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c1 = graph_->GetIntConstant(1);
  HInstruction* c_neg1 = graph_->GetIntConstant(-1);
  HInstruction* add0 = MakeBinOp<HAdd>(body, DataType::Type::kInt32, index, c0);
  HInstruction* add1 = MakeBinOp<HAdd>(body, DataType::Type::kInt32, index, c1);
  HInstruction* sub0 = MakeBinOp<HSub>(body, DataType::Type::kInt32, index, c0);
  HInstruction* sub1 = MakeBinOp<HSub>(body, DataType::Type::kInt32, index, c1);
  HInstruction* sub_neg1 = MakeBinOp<HSub>(body, DataType::Type::kInt32, index, c_neg1);
  HInstruction* rev_sub1 = MakeBinOp<HSub>(body, DataType::Type::kInt32, c1, index);
  // array[0] = c0
  HInstruction* arr_set1 = MakeArraySet(body, array, c0, c0, DataType::Type::kInt32);
  // array[1] = c0
  HInstruction* arr_set2 = MakeArraySet(body, array, c1, c0, DataType::Type::kInt32);
  // array[i+0] = c0
  HInstruction* arr_set3 = MakeArraySet(body, array, add0, c0, DataType::Type::kInt32);
  // array[i+1] = c0
  HInstruction* arr_set4 = MakeArraySet(body, array, add1, c0, DataType::Type::kInt32);
  // array[i-0] = c0
  HInstruction* arr_set5 = MakeArraySet(body, array, sub0, c0, DataType::Type::kInt32);
  // array[i-1] = c0
  HInstruction* arr_set6 = MakeArraySet(body, array, sub1, c0, DataType::Type::kInt32);
  // array[1-i] = c0
  HInstruction* arr_set7 = MakeArraySet(body, array, rev_sub1, c0, DataType::Type::kInt32);
  // array[i-(-1)] = c0
  HInstruction* arr_set8 = MakeArraySet(body, array, sub_neg1, c0, DataType::Type::kInt32);

  graph_->ComputeDominanceInformation();
  ScopedArenaAllocator allocator(graph_->GetArenaStack());
  LoadStoreAnalysis lsa(graph_, nullptr, &allocator);
  lsa.Run();
  const HeapLocationCollector& heap_location_collector = lsa.GetHeapLocationCollector();

  // LSA/HeapLocationCollector should see those ArrayGet instructions.
  ASSERT_EQ(heap_location_collector.GetNumberOfHeapLocations(), 8U);
  ASSERT_TRUE(heap_location_collector.HasHeapStores());

  // Test queries on HeapLocationCollector's aliasing matrix after load store analysis.
  size_t loc1 = HeapLocationCollector::kHeapLocationNotFound;
  size_t loc2 = HeapLocationCollector::kHeapLocationNotFound;

  // Test alias: array[0] and array[1]
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set1);
  loc2 = heap_location_collector.GetArrayHeapLocation(arr_set2);
  ASSERT_FALSE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[i+0] and array[i-0]
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set3);
  loc2 = heap_location_collector.GetArrayHeapLocation(arr_set5);
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[i+1] and array[i-1]
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set4);
  loc2 = heap_location_collector.GetArrayHeapLocation(arr_set6);
  ASSERT_FALSE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[i+1] and array[1-i]
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set4);
  loc2 = heap_location_collector.GetArrayHeapLocation(arr_set7);
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[i+1] and array[i-(-1)]
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set4);
  loc2 = heap_location_collector.GetArrayHeapLocation(arr_set8);
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc2));

  EXPECT_TRUE(CheckGraph());
}

TEST_F(LoadStoreAnalysisTest, ArrayAliasingTest) {
  constexpr size_t vlen1 = kDefaultTestVectorSizeInBytes;
  constexpr size_t vlen2 = vlen1 / 2;

  HBasicBlock* main = InitEntryMainExitGraphWithReturnVoid();

  HInstruction* array = MakeParam(DataType::Type::kReference);
  HInstruction* index = MakeParam(DataType::Type::kInt32);
  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c1 = graph_->GetIntConstant(1);
  HInstruction* c6 = graph_->GetIntConstant(6);
  HInstruction* c8 = graph_->GetIntConstant(8);

  HInstruction* arr_set_0 = MakeArraySet(main, array, c0, c0, DataType::Type::kInt32);
  HInstruction* arr_set_1 = MakeArraySet(main, array, c1, c0, DataType::Type::kInt32);
  HInstruction* arr_set_i = MakeArraySet(main, array, index, c0, DataType::Type::kInt32);

  HVecOperation* v1 = new (GetAllocator()) HVecReplicateScalar(GetAllocator(),
                                                               c1,
                                                               DataType::Type::kInt32,
                                                               vlen1,
                                                               kNoDexPc);
  AddOrInsertInstruction(main, v1);
  HVecOperation* v2 = new (GetAllocator()) HVecReplicateScalar(GetAllocator(),
                                                               c1,
                                                               DataType::Type::kInt32,
                                                               vlen2,
                                                               kNoDexPc);
  AddOrInsertInstruction(main, v2);
  HInstruction* i_add6 = MakeBinOp<HAdd>(main, DataType::Type::kInt32, index, c6);
  HInstruction* i_add8 = MakeBinOp<HAdd>(main, DataType::Type::kInt32, index, c8);

  HInstruction* vstore_0 =
      MakeVecStore(main, array, c0, v1, DataType::Type::kInt32, vlen1);
  HInstruction* vstore_1 =
      MakeVecStore(main, array, c1, v1, DataType::Type::kInt32, vlen1);
  HInstruction* vstore_8 =
      MakeVecStore(main, array, c8, v1, DataType::Type::kInt32, vlen1);
  HInstruction* vstore_i =
      MakeVecStore(main, array, index, v1, DataType::Type::kInt32, vlen1);
  HInstruction* vstore_i_add6 =
      MakeVecStore(main, array, i_add6, v1, DataType::Type::kInt32, vlen1);
  HInstruction* vstore_i_add8 =
      MakeVecStore(main, array, i_add8, v1, DataType::Type::kInt32, vlen1);
  HInstruction* vstore_i_add6_vlen2 =
      MakeVecStore(main, array, i_add6, v2, DataType::Type::kInt32, vlen2);

  graph_->BuildDominatorTree();
  ScopedArenaAllocator allocator(graph_->GetArenaStack());
  LoadStoreAnalysis lsa(graph_, nullptr, &allocator);
  lsa.Run();
  const HeapLocationCollector& heap_location_collector = lsa.GetHeapLocationCollector();

  // LSA/HeapLocationCollector should see those instructions.
  ASSERT_EQ(heap_location_collector.GetNumberOfHeapLocations(), 10U);
  ASSERT_TRUE(heap_location_collector.HasHeapStores());

  // Test queries on HeapLocationCollector's aliasing matrix after load store analysis.
  size_t loc1, loc2;

  // Test alias: array[0] and array[0,1,2,3]
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set_0);
  loc2 = heap_location_collector.GetArrayHeapLocation(vstore_0);
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[0] and array[1,2,3,4]
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set_0);
  loc2 = heap_location_collector.GetArrayHeapLocation(vstore_1);
  ASSERT_FALSE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[0] and array[8,9,10,11]
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set_0);
  loc2 = heap_location_collector.GetArrayHeapLocation(vstore_8);
  ASSERT_FALSE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[1] and array[8,9,10,11]
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set_1);
  loc2 = heap_location_collector.GetArrayHeapLocation(vstore_8);
  ASSERT_FALSE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[1] and array[0,1,2,3]
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set_1);
  loc2 = heap_location_collector.GetArrayHeapLocation(vstore_0);
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[0,1,2,3] and array[8,9,10,11]
  loc1 = heap_location_collector.GetArrayHeapLocation(vstore_0);
  loc2 = heap_location_collector.GetArrayHeapLocation(vstore_8);
  ASSERT_FALSE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[0,1,2,3] and array[1,2,3,4]
  loc1 = heap_location_collector.GetArrayHeapLocation(vstore_0);
  loc2 = heap_location_collector.GetArrayHeapLocation(vstore_1);
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[0] and array[i,i+1,i+2,i+3]
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set_0);
  loc2 = heap_location_collector.GetArrayHeapLocation(vstore_i);
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[i] and array[0,1,2,3]
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set_i);
  loc2 = heap_location_collector.GetArrayHeapLocation(vstore_0);
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[i] and array[i,i+1,i+2,i+3]
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set_i);
  loc2 = heap_location_collector.GetArrayHeapLocation(vstore_i);
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[i] and array[i+8,i+9,i+10,i+11]
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set_i);
  loc2 = heap_location_collector.GetArrayHeapLocation(vstore_i_add8);
  ASSERT_FALSE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[i+6,i+7,i+8,i+9] and array[i+8,i+9,i+10,i+11]
  // Test partial overlap.
  loc1 = heap_location_collector.GetArrayHeapLocation(vstore_i_add6);
  loc2 = heap_location_collector.GetArrayHeapLocation(vstore_i_add8);
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[i+6,i+7] and array[i,i+1,i+2,i+3]
  // Test different vector lengths.
  loc1 = heap_location_collector.GetArrayHeapLocation(vstore_i_add6_vlen2);
  loc2 = heap_location_collector.GetArrayHeapLocation(vstore_i);
  ASSERT_FALSE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[i+6,i+7] and array[i+8,i+9,i+10,i+11]
  loc1 = heap_location_collector.GetArrayHeapLocation(vstore_i_add6_vlen2);
  loc2 = heap_location_collector.GetArrayHeapLocation(vstore_i_add8);
  ASSERT_FALSE(heap_location_collector.MayAlias(loc1, loc2));
}

TEST_F(LoadStoreAnalysisTest, ArrayIndexCalculationOverflowTest) {
  HBasicBlock* main = InitEntryMainExitGraphWithReturnVoid();

  HInstruction* array = MakeParam(DataType::Type::kReference);
  HInstruction* index = MakeParam(DataType::Type::kInt32);

  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c_0x80000000 = graph_->GetIntConstant(0x80000000);
  HInstruction* c_0x10 = graph_->GetIntConstant(0x10);
  HInstruction* c_0xFFFFFFF0 = graph_->GetIntConstant(0xFFFFFFF0);
  HInstruction* c_0x7FFFFFFF = graph_->GetIntConstant(0x7FFFFFFF);
  HInstruction* c_0x80000001 = graph_->GetIntConstant(0x80000001);

  // `index+0x80000000` and `index-0x80000000` array indices MAY alias.
  HInstruction* add_0x80000000 = MakeBinOp<HAdd>(main, DataType::Type::kInt32, index, c_0x80000000);
  HInstruction* sub_0x80000000 = MakeBinOp<HSub>(main, DataType::Type::kInt32, index, c_0x80000000);
  HInstruction* arr_set_1 = MakeArraySet(main, array, add_0x80000000, c0, DataType::Type::kInt32);
  HInstruction* arr_set_2 = MakeArraySet(main, array, sub_0x80000000, c0, DataType::Type::kInt32);

  // `index+0x10` and `index-0xFFFFFFF0` array indices MAY alias.
  HInstruction* add_0x10 = MakeBinOp<HAdd>(main, DataType::Type::kInt32, index, c_0x10);
  HInstruction* sub_0xFFFFFFF0 = MakeBinOp<HSub>(main, DataType::Type::kInt32, index, c_0xFFFFFFF0);
  HInstruction* arr_set_3 = MakeArraySet(main, array, add_0x10, c0, DataType::Type::kInt32);
  HInstruction* arr_set_4 = MakeArraySet(main, array, sub_0xFFFFFFF0, c0, DataType::Type::kInt32);

  // `index+0x7FFFFFFF` and `index-0x80000001` array indices MAY alias.
  HInstruction* add_0x7FFFFFFF = MakeBinOp<HAdd>(main, DataType::Type::kInt32, index, c_0x7FFFFFFF);
  HInstruction* sub_0x80000001 = MakeBinOp<HSub>(main, DataType::Type::kInt32, index, c_0x80000001);
  HInstruction* arr_set_5 = MakeArraySet(main, array, add_0x7FFFFFFF, c0, DataType::Type::kInt32);
  HInstruction* arr_set_6 = MakeArraySet(main, array, sub_0x80000001, c0, DataType::Type::kInt32);

  // `index+0` and `index-0` array indices MAY alias.
  HInstruction* add_0 = MakeBinOp<HAdd>(main, DataType::Type::kInt32, index, c0);
  HInstruction* sub_0 = MakeBinOp<HSub>(main, DataType::Type::kInt32, index, c0);
  HInstruction* arr_set_7 = MakeArraySet(main, array, add_0, c0, DataType::Type::kInt32);
  HInstruction* arr_set_8 = MakeArraySet(main, array, sub_0, c0, DataType::Type::kInt32);

  graph_->BuildDominatorTree();
  ScopedArenaAllocator allocator(graph_->GetArenaStack());
  LoadStoreAnalysis lsa(graph_, nullptr, &allocator);
  lsa.Run();
  const HeapLocationCollector& heap_location_collector = lsa.GetHeapLocationCollector();

  // LSA/HeapLocationCollector should see those ArrayGet instructions.
  ASSERT_EQ(heap_location_collector.GetNumberOfHeapLocations(), 8U);
  ASSERT_TRUE(heap_location_collector.HasHeapStores());

  // Test queries on HeapLocationCollector's aliasing matrix after load store analysis.
  size_t loc1 = HeapLocationCollector::kHeapLocationNotFound;
  size_t loc2 = HeapLocationCollector::kHeapLocationNotFound;

  // Test alias: array[i+0x80000000] and array[i-0x80000000]
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set_1);
  loc2 = heap_location_collector.GetArrayHeapLocation(arr_set_2);
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[i+0x10] and array[i-0xFFFFFFF0]
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set_3);
  loc2 = heap_location_collector.GetArrayHeapLocation(arr_set_4);
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[i+0x7FFFFFFF] and array[i-0x80000001]
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set_5);
  loc2 = heap_location_collector.GetArrayHeapLocation(arr_set_6);
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[i+0] and array[i-0]
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set_7);
  loc2 = heap_location_collector.GetArrayHeapLocation(arr_set_8);
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc2));

  // Should not alias:
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set_2);
  loc2 = heap_location_collector.GetArrayHeapLocation(arr_set_6);
  ASSERT_FALSE(heap_location_collector.MayAlias(loc1, loc2));

  // Should not alias:
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set_7);
  loc2 = heap_location_collector.GetArrayHeapLocation(arr_set_2);
  ASSERT_FALSE(heap_location_collector.MayAlias(loc1, loc2));
}

TEST_F(LoadStoreAnalysisTest, TestHuntOriginalRef) {
  HBasicBlock* main = InitEntryMainExitGraphWithReturnVoid();

  // Different ways where orignal array reference are transformed & passed to ArrayGet.
  // ParameterValue --> ArrayGet
  // ParameterValue --> BoundType --> ArrayGet
  // ParameterValue --> BoundType --> NullCheck --> ArrayGet
  // ParameterValue --> BoundType --> NullCheck --> IntermediateAddress --> ArrayGet
  HInstruction* c1 = graph_->GetIntConstant(1);
  HInstruction* array = MakeParam(DataType::Type::kReference);

  HInstruction* array_get1 = MakeArrayGet(main, array, c1, DataType::Type::kInt32);

  HInstruction* bound_type = new (GetAllocator()) HBoundType(array);
  AddOrInsertInstruction(main, bound_type);
  HInstruction* array_get2 = MakeArrayGet(main, bound_type, c1, DataType::Type::kInt32);

  HInstruction* null_check = MakeNullCheck(main, bound_type);
  HInstruction* array_get3 = MakeArrayGet(main, null_check, c1, DataType::Type::kInt32);

  HInstruction* inter_addr = new (GetAllocator()) HIntermediateAddress(null_check, c1, 0);
  AddOrInsertInstruction(main, inter_addr);
  HInstruction* array_get4 = MakeArrayGet(main, inter_addr, c1, DataType::Type::kInt32);

  ScopedArenaAllocator allocator(graph_->GetArenaStack());
  HeapLocationCollector heap_location_collector(graph_, &allocator);
  heap_location_collector.VisitBasicBlock(main);

  // Test that the HeapLocationCollector should be able to tell
  // that there is only ONE array location, no matter how many
  // times the original reference has been transformed by BoundType,
  // NullCheck, IntermediateAddress, etc.
  ASSERT_EQ(heap_location_collector.GetNumberOfHeapLocations(), 1U);
  size_t loc1 = heap_location_collector.GetArrayHeapLocation(array_get1);
  size_t loc2 = heap_location_collector.GetArrayHeapLocation(array_get2);
  size_t loc3 = heap_location_collector.GetArrayHeapLocation(array_get3);
  size_t loc4 = heap_location_collector.GetArrayHeapLocation(array_get4);
  ASSERT_TRUE(loc1 != HeapLocationCollector::kHeapLocationNotFound);
  ASSERT_EQ(loc1, loc2);
  ASSERT_EQ(loc1, loc3);
  ASSERT_EQ(loc1, loc4);
}

// // IF_BLOCK
// obj = new Obj();
// if (parameter_value) {
//   // LEFT
//   call_func(obj);
// } else {
//   // RIGHT
//   obj.f0 = 0;
//   call_func2(obj);
// }
// // RETURN_BLOCK
// obj.f0;
TEST_F(LoadStoreAnalysisTest, TotalEscape) {
  HBasicBlock* return_block = InitEntryMainExitGraphWithReturnVoid();
  auto [if_block, left, right] = CreateDiamondPattern(return_block);

  HInstruction* bool_value = MakeParam(DataType::Type::kBool);
  HInstruction* c0 = graph_->GetIntConstant(0);

  HInstruction* cls = MakeLoadClass(if_block);
  HInstruction* new_inst = MakeNewInstance(if_block, cls);
  MakeIf(if_block, bool_value);

  HInstruction* call_left = MakeInvokeStatic(left, DataType::Type::kVoid, {new_inst});

  HInstruction* call_right = MakeInvokeStatic(right, DataType::Type::kVoid, {new_inst});
  HInstruction* write_right = MakeIFieldSet(right, new_inst, c0, MemberOffset(32));

  HInstruction* read_final =
      MakeIFieldGet(return_block, new_inst, DataType::Type::kInt32, MemberOffset(32));

  graph_->ComputeDominanceInformation();
  ScopedArenaAllocator allocator(graph_->GetArenaStack());
  LoadStoreAnalysis lsa(graph_, nullptr, &allocator);
  lsa.Run();

  const HeapLocationCollector& heap_location_collector = lsa.GetHeapLocationCollector();
  ReferenceInfo* info = heap_location_collector.FindReferenceInfoOf(new_inst);
  ASSERT_FALSE(info->IsSingleton());
}

// // MAIN
// obj = new Obj();
// obj.foo = 0;
// return obj;
TEST_F(LoadStoreAnalysisTest, TotalEscape2) {
  HBasicBlock* main = InitEntryMainExitGraph();

  HInstruction* c0 = graph_->GetIntConstant(0);

  HInstruction* cls = MakeLoadClass(main);
  HInstruction* new_inst = MakeNewInstance(main, cls);
  HInstruction* write_start = MakeIFieldSet(main, new_inst, c0, MemberOffset(32));
  MakeReturn(main, new_inst);

  graph_->ComputeDominanceInformation();
  ScopedArenaAllocator allocator(graph_->GetArenaStack());
  LoadStoreAnalysis lsa(graph_, nullptr, &allocator);
  lsa.Run();

  const HeapLocationCollector& heap_location_collector = lsa.GetHeapLocationCollector();
  ReferenceInfo* info = heap_location_collector.FindReferenceInfoOf(new_inst);
  ASSERT_TRUE(info->IsSingletonAndNonRemovable());
}

// // TOP
// obj = new Obj();
// if (parameter_value) {
//   // HIGH_LEFT
//   call_func(obj);
// } else {
//   // HIGH_RIGHT
//   obj.f0 = 1;
// }
// // MID
// obj.f0 *= 2;
// if (parameter_value2) {
//   // LOW_LEFT
//   call_func(obj);
// } else {
//   // LOW_RIGHT
//   obj.f0 = 1;
// }
// // BOTTOM
// obj.f0
TEST_F(LoadStoreAnalysisTest, DoubleDiamondEscape) {
  HBasicBlock* bottom = InitEntryMainExitGraphWithReturnVoid();
  auto [mid, low_left, low_right] = CreateDiamondPattern(bottom);
  auto [top, high_left, high_right] = CreateDiamondPattern(mid);

  HInstruction* bool_value1 = MakeParam(DataType::Type::kBool);
  HInstruction* bool_value2 = MakeParam(DataType::Type::kBool);
  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c2 = graph_->GetIntConstant(2);

  HInstruction* cls = MakeLoadClass(top);
  HInstruction* new_inst = MakeNewInstance(top, cls);
  MakeIf(top, bool_value1);

  HInstruction* call_left = MakeInvokeStatic(high_left, DataType::Type::kVoid, {new_inst});

  HInstruction* write_right = MakeIFieldSet(high_right, new_inst, c0, MemberOffset(32));

  HInstruction* read_mid = MakeIFieldGet(mid, new_inst, DataType::Type::kInt32, MemberOffset(32));
  HInstruction* mul_mid = MakeBinOp<HMul>(mid, DataType::Type::kInt32, read_mid, c2);
  HInstruction* write_mid = MakeIFieldSet(mid, new_inst, mul_mid, MemberOffset(32));
  MakeIf(mid, bool_value2);

  HInstruction* call_low_left = MakeInvokeStatic(low_left, DataType::Type::kVoid, {new_inst});

  HInstruction* write_low_right = MakeIFieldSet(low_right, new_inst, c0, MemberOffset(32));

  HInstruction* read_final =
      MakeIFieldGet(bottom, new_inst, DataType::Type::kInt32, MemberOffset(32));

  graph_->ComputeDominanceInformation();
  ScopedArenaAllocator allocator(graph_->GetArenaStack());
  LoadStoreAnalysis lsa(graph_, nullptr, &allocator);
  lsa.Run();

  const HeapLocationCollector& heap_location_collector = lsa.GetHeapLocationCollector();
  ReferenceInfo* info = heap_location_collector.FindReferenceInfoOf(new_inst);
  ASSERT_FALSE(info->IsSingleton());
}

// // START
// Obj new_inst = new Obj();
// new_inst.foo = 12;
// Obj obj;
// Obj out;
// if (param1) {
//   // LEFT_START
//   if (param2) {
//     // LEFT_LEFT
//     obj = new_inst;
//   } else {
//     // LEFT_RIGHT
//     obj = obj_param;
//   }
//   // LEFT_MERGE
//   // technically the phi is enough to cause an escape but might as well be
//   // thorough.
//   // obj = phi[new_inst, param]
//   escape(obj);
//   out = obj;
// } else {
//   // RIGHT
//   out = obj_param;
// }
// // BRETURN
// // Can't do anything with this since we don't have good tracking for the heap-locations
// // out = phi[param, phi[new_inst, param]]
// return out.foo
TEST_F(LoadStoreAnalysisTest, PartialPhiPropagation1) {
  HBasicBlock* breturn = InitEntryMainExitGraph();
  auto [start, left_merge, right] = CreateDiamondPattern(breturn);
  auto [left, left_left, left_right] = CreateDiamondPattern(left_merge);
  EnsurePredecessorOrder(breturn, {left_merge, right});
  EnsurePredecessorOrder(left_merge, {left_left, left_right});
  HInstruction* param1 = MakeParam(DataType::Type::kBool);
  HInstruction* param2 = MakeParam(DataType::Type::kBool);
  HInstruction* obj_param = MakeParam(DataType::Type::kReference);
  HInstruction* c12 = graph_->GetIntConstant(12);

  HInstruction* cls = MakeLoadClass(start);
  HInstruction* new_inst = MakeNewInstance(start, cls);
  HInstruction* store = MakeIFieldSet(start, new_inst, c12, MemberOffset(32));
  MakeIf(start, param1);

  MakeIf(left, param2);

  HPhi* left_phi = MakePhi(left_merge, {obj_param, new_inst});
  HInstruction* call_left = MakeInvokeStatic(left_merge, DataType::Type::kVoid, {left_phi});
  MakeGoto(left_merge);
  left_phi->SetCanBeNull(true);

  HPhi* return_phi = MakePhi(breturn, {left_phi, obj_param});
  HInstruction* read_exit =
      MakeIFieldGet(breturn, return_phi, DataType::Type::kReference, MemberOffset(32));
  MakeReturn(breturn, read_exit);

  graph_->ClearDominanceInformation();
  graph_->BuildDominatorTree();

  ScopedArenaAllocator allocator(graph_->GetArenaStack());
  LoadStoreAnalysis lsa(graph_, nullptr, &allocator);
  lsa.Run();

  const HeapLocationCollector& heap_location_collector = lsa.GetHeapLocationCollector();
  ReferenceInfo* info = heap_location_collector.FindReferenceInfoOf(new_inst);
  ASSERT_FALSE(info->IsSingleton());
}
}  // namespace art
