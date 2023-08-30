/**
 * Copyright 2020-2021 Huawei Technologies Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "frontend/parallel/pipeline_transformer/pipeline_transformer.h"
#include <set>
#include <vector>
#include <string>
#include <utility>
#include <algorithm>
#include <memory>
#include "mindspore/core/ops/sequence_ops.h"
#include "mindspore/core/ops/other_ops.h"
#include "mindspore/core/ops/nn_ops.h"
#include "mindspore/core/ops/array_ops.h"
#include "mindspore/core/ops/framework_ops.h"
#include "frontend/parallel/auto_parallel/graph_costmodel.h"
#include "frontend/parallel/ops_info/ops_utils.h"
#include "frontend/parallel/group_manager.h"
#include "frontend/parallel/parameter_manager.h"
#include "include/common/utils/parallel_context.h"
#include "frontend/parallel/step_parallel.h"
#include "frontend/parallel/node_check.h"
#include "frontend/parallel/graph_util/node_info.h"
#include "frontend/parallel/graph_util/pipeline_split_utils.h"
#include "frontend/parallel/step_parallel_utils.h"
#include "frontend/parallel/graph_util/graph_splitter.h"
#include "ir/anf.h"
#include "ir/graph_utils.h"
#include "include/common/utils/comm_manager.h"
#include "utils/ms_context.h"
#include "utils/tensor_construct_utils.h"
#include "mindspore/core/utils/parallel_node_check.h"

namespace mindspore {
namespace parallel {

const std::set<PrimitivePtr> WHITE_LIST = {prim::kPrimTupleGetItem, prim::kPrimMakeTuple, prim::kPrimCast};

bool IsInWhiteList(const CNodePtr &cnode) {
  for (auto prim = WHITE_LIST.cbegin(); prim != WHITE_LIST.cend(); ++prim) {
    if (IsPrimitiveCNode(cnode, *prim)) {
      return true;
    }
  }
  return false;
}

static AbstractBasePtr GetRealAbstract(const AnfNodePtr &node) {
  if (IsPrimitiveCNode(node, prim::kPrimDepend)) {
    auto &input = node->cast<CNodePtr>()->input(1);
    MS_EXCEPTION_IF_NULL(input);
    return input->abstract();
  }
  return node->abstract();
}

TensorInfo PipelineTransformer::GetTensorInfo(const std::pair<OperatorInfoPtr, int> &op_info_pair, bool is_param) {
  if (is_param) {
    auto inputs_tensor_info = op_info_pair.first->inputs_tensor_info();
    return inputs_tensor_info.at(IntToSize(op_info_pair.second));
  } else {
    auto outputs_tensor_info = op_info_pair.first->outputs_tensor_info();
    return outputs_tensor_info.at(IntToSize(op_info_pair.second));
  }
}

static void SeparateParamBorder(const std::vector<AnfNodePtr> &nodes, bool send, std::vector<AnfNodePtr> *const params,
                                std::vector<AnfNodePtr> *const borders) {
  std::vector<AnfNodePtr> real_comm_ops;
  if (send) {
    (void)std::transform(nodes.begin(), nodes.end(), std::back_inserter(real_comm_ops), [](const AnfNodePtr &n) {
      const auto &cnode = n->cast<CNodePtr>();
      MS_EXCEPTION_IF_NULL(cnode);
      const auto &real = cnode->input(INDEX_TWO)->cast<CNodePtr>();
      MS_EXCEPTION_IF_NULL(real);
      return real;
    });
  } else {
    real_comm_ops = nodes;
  }
  for (auto &node : real_comm_ops) {
    const auto &cnode = node->cast<CNodePtr>();
    MS_EXCEPTION_IF_NULL(cnode);
    if (cnode->HasPrimalAttr(PIPELINE_PARAM)) {
      (*params).push_back(node);
    } else {
      (*borders).push_back(node);
    }
  }
}

bool PipelineTransformer::MainGraph() {
  if (!is_train_) {
    main_graph_ = root_;
    return true;
  }
  bool find_main_graph = false;
  for (auto &fg : manager_->func_graphs()) {
    for (auto &node : fg->nodes()) {
      if (IsPrimitiveCNode(node, prim::kPrimVirtualDataset)) {
        main_graph_ = fg;
        main_graph_->set_flag(MAIN_GRAPH, true);
        virtual_dataset_ = node;
        find_main_graph = true;
        break;
      }
    }
    if (find_main_graph) {
      break;
    }
  }
  if (!find_main_graph) {
    MS_LOG(WARNING) << "Can't find main graph, possible reason is can't find virtual dataset.";
    return false;
  }
  const auto &cell_reuse_env = common::GetEnv("MS_DEV_CELL_REUSE");
  enable_share_cell_ = cell_reuse_env == "1" || cell_reuse_env == "2";
  if (!enable_share_cell_) {
    return true;
  }
  auto value_nodes = main_graph_->value_nodes();
  for (auto value_pair = value_nodes.cbegin(); value_pair != value_nodes.cend(); ++value_pair) {
    auto node = (*value_pair).first;
    if (!IsValueNode<FuncGraph>(node)) {
      continue;
    }
    shared_cell_ = GetValueNode<FuncGraphPtr>(node);
    auto node_users = manager_->node_users()[node];
    for (auto &node_user : node_users) {
      auto user = node_user.first;
      if (user->func_graph() == main_graph_) {
        shared_cell_users_.push_back(user);
      }
    }
    return true;
  }
  if (shared_cell_ == nullptr) {
    MS_LOG(WARNING) << "Can't find share graph, but the environment MS_DEV_CELL_REUSE is set.";
    return false;
  }
  return true;
}

ValuePtr PipelineTransformer::SetMicroBatch(const AnfNodePtr &node, int64_t micro_size, size_t batch_axis) const {
  if (!IsPrimitiveCNode(node, prim::kPrimStridedSlice)) {
    MS_LOG(EXCEPTION) << "Can't find MicroBatch information.";
  }
  auto cnode = node->cast<CNodePtr>();
  auto value = GetValueNode(cnode->input(2));
  MS_EXCEPTION_IF_NULL(value);
  auto tuple = GetValue<std::vector<int64_t>>(value);
  auto input_tmp = GetNodeShape(cnode->input(1));
  auto input_shape = input_tmp.at(0);
  auto slice_batch_size = input_shape.at(batch_axis);
  if (slice_batch_size == 0) {
    MS_LOG(EXCEPTION) << "slice_batch_size should be a positive integer, but got " << slice_batch_size;
  }
  int64_t micro = tuple.at(batch_axis) * micro_size / slice_batch_size;
  cnode->AddPrimalAttr(MICRO, MakeValue(micro));
  cnode->AddPrimalAttr(PIPELINE_BEGIN, MakeValue(micro));
  int64_t seg = 0;
  cnode->AddPrimalAttr(SEGMENT, MakeValue(seg));
  return MakeValue(micro);
}

AnfNodePtr PipelineTransformer::GetArgumentsByParameter(const AnfNodePtr &parameter) {
  auto fg = parameter->func_graph();
  if (fg == root_) {
    return parameter;
  }
  auto parameters = fg->parameters();
  auto iter = std::find(parameters.begin(), parameters.end(), parameter);
  if (iter != parameters.end()) {
    auto pos = std::distance(parameters.begin(), iter);
    auto fg_used_map = fg->func_graph_cnodes_index();
    for (auto &cur_fg_use : fg_used_map) {
      if (cur_fg_use.first->second != 0) {
        continue;
      }
      auto cur_fg = cur_fg_use.first->first->cast<CNodePtr>();
      auto argument = cur_fg->input(pos + 1);
      if (argument->isa<Parameter>()) {
        return GetArgumentsByParameter(argument);
      }
    }
  }
  return nullptr;
}

bool PipelineTransformer::NeedGrad(const CNodePtr &cnode) {
  for (auto &input : cnode->inputs()) {
    auto temp = input;
    while (IsPrimitiveCNode(temp, prim::kPrimLoad) || IsPrimitiveCNode(temp, prim::kPrimCast) ||
           IsPrimitiveCNode(temp, prim::kPrimDepend)) {
      auto input_cnode = temp->cast<CNodePtr>();
      MS_EXCEPTION_IF_NULL(input_cnode);
      temp = input_cnode->input(1);
    }
    if (temp->isa<Parameter>()) {
      auto argument = GetArgumentsByParameter(temp);
      if (!argument || !GetRealKernelNode(argument, -1, nullptr).first->isa<Parameter>()) {
        continue;
      }
      if (ParameterRequireGrad(argument)) {
        return true;
      }
    }
  }
  return false;
}

bool PipelineTransformer::LabelParameterStart(const FuncGraphPtr &graph) {
  auto orders = graph->GetOrderedCnodes();
  for (auto node = orders.cbegin(); node != orders.cend(); ++node) {
    auto cnode = (*node)->cast<CNodePtr>();
    MS_EXCEPTION_IF_NULL(cnode);
    auto stage_info = cnode->user_data<NodeStageInfo>();
    if (stage_info != nullptr && stage_info->stage() > 0) {
      continue;
    }
    if (IsValueNode<FuncGraph>(cnode->input(0))) {
      auto sub_graph = GetValueNode<FuncGraphPtr>(cnode->input(0));
      if (LabelParameterStart(sub_graph)) {
        return true;
      } else {
        continue;
      }
    }
    if (!IsPipelineCareNode(cnode)) {
      continue;
    }
    if (NeedGrad(cnode)) {
      auto prim = GetCNodePrimitive(cnode);
      if (enable_share_cell_) {
        (void)prim->AddAttr(PARAMETER_START_SHARE_CELL, MakeValue(0));
      } else {
        (void)prim->AddAttr(PARAMETER_START, MakeValue(0));
      }
      return true;
    }
  }
  return false;
}

size_t PipelineTransformer::GetBatchAxisForInput(const AnfNodeIndexSet &input_node_users) const {
  Shapes inputs_tuple;
  for (const auto &input_node_user : input_node_users) {
    auto node = input_node_user.first;
    if (!IsPrimitiveCNode(node, prim::kPrimStridedSlice)) {
      MS_LOG(EXCEPTION) << "Can't find MicroBatch information.";
    }
    auto cnode = node->cast<CNodePtr>();
    auto value = GetValueNode(cnode->input(2));
    MS_EXCEPTION_IF_NULL(value);
    auto tuple = GetValue<std::vector<int64_t>>(value);
    inputs_tuple.push_back(tuple);
  }
  size_t batch_axis = 0;
  size_t batch_axis_count = 0;
  size_t input_dim = inputs_tuple.at(0).size();
  size_t micro_num = inputs_tuple.size();
  for (size_t axis = 0; axis < input_dim; ++axis) {
    for (size_t i = 1; i < micro_num; ++i) {
      if (inputs_tuple[i][axis] != inputs_tuple[i - 1][axis]) {
        batch_axis = axis;
        ++batch_axis_count;
        break;
      }
    }
  }
  if (batch_axis_count != kSizeOne) {
    MS_LOG(EXCEPTION)
      << "For pipeline parallelism, micro_size partitioning of the input along a certain dimension is and "
      << "is only allowed, but it is found that " << batch_axis_count << " to be partitioned.";
  }
  return batch_axis;
}

void PipelineTransformer::LabelMicroBatch() {
  if (!is_train_) {
    return;
  }
  auto graph = enable_share_cell_ ? shared_cell_ : main_graph_;
  MS_EXCEPTION_IF_NULL(graph);
  if (!LabelParameterStart(graph)) {
    MS_LOG(EXCEPTION) << "Stage 0 should has at least 1 parameter. but got none.";
  }
  MS_EXCEPTION_IF_NULL(virtual_dataset_);
  auto node_user_map = manager_->node_users();
  auto node_users = node_user_map[virtual_dataset_];
  for (auto &node_user : node_users) {
    if (IsPrimitiveCNode(node_user.first, prim::kPrimTupleGetItem)) {
      auto data_users = manager_->node_users()[node_user.first];
      auto node_first = data_users.front().first;
      if (!IsPrimitiveCNode(node_first, prim::kPrimStridedSlice)) {
        data_users.clear();
        data_users = node_user_map[node_first];
      }
      auto micro_size = int64_t(data_users.size());
      micro_size_ = micro_size;
      auto batch_axis = GetBatchAxisForInput(data_users);
      MS_LOG(INFO) << "For the "
                   << GetSerialNumberString(
                        GetValue<int64_t>(GetValueNode(node_user.first->cast<CNodePtr>()->input(kIndex2))))
                   << "input, batch axis is " << batch_axis << ", micro size is : " << micro_size;
      for (auto &data_user : data_users) {
        auto micro = SetMicroBatch(data_user.first, micro_size, batch_axis);
        SetStridedSliceStrategy(data_user.first);
        auto cnode = data_user.first->cast<CNodePtr>();
        BroadCastMicroBatch(cnode, &node_user_map, micro, 0);
      }
    }
  }
}

void PipelineTransformer::CreateForwardGroup() {
  std::vector<int64_t> rank_list = g_device_manager->GetDeviceListBetweenStage();
  auto dev_list = g_device_manager->CreateDeviceListByRankList(rank_list);
  Group g;
  if (g_device_manager->CreateGroup(rank_list, &g) != SUCCESS) {
    MS_LOG(EXCEPTION) << "Create forward communication group between all pipeline stages failed, the rank_list is: "
                      << rank_list;
  }
  auto g_back_name = g.name() + BACKWARD;
  Group g_back;
  if (g_device_manager->CreateGroup(g_back_name, dev_list, &g_back) != SUCCESS) {
    MS_LOG(EXCEPTION) << "Create backward communication group between all pipeline stages failed, the rank_list is: "
                      << rank_list;
  }
  group_.push_back(g.name());
  group_.push_back(g_back.name());
}

void PipelineTransformer::LabelGenMaskFusion() {
  auto fgs = manager_->func_graphs();
  int64_t fusion_id = 0;
  for (auto fg = fgs.cbegin(); fg != fgs.cend(); ++fg) {
    if (*fg == root_ || *fg == main_graph_) {
      continue;
    }
    auto stage = (*fg)->stage();
    if (stage != -1 && stage != stage_) {
      continue;
    }
    auto nodes = (*fg)->nodes();
    for (auto node = nodes.cbegin(); node != nodes.cend(); ++node) {
      if (!IsPrimitiveCNode(*node, prim::kPrimDropoutGenMask) && !IsPrimitiveCNode(*node, prim::kPrimDropoutDoMaskV3) &&
          !IsPrimitiveCNode(*node, prim::kPrimDropout)) {
        continue;
      }
      auto cnode = (*node)->cast<CNodePtr>();
      MS_EXCEPTION_IF_NULL(cnode);
      cnode->AddPrimalAttr(kAttrFusion, MakeValue(fusion_id));
      fusion_id += 1;
    }
  }
}

void PipelineTransformer::Coloring() {
  auto need_coloring = true;
  std::set<int64_t> stage_set;
  if (!IsTraining(manager_)) {
    is_train_ = false;
  }
  while (need_coloring) {
    need_coloring = false;
    for (auto &fg : manager_->func_graphs()) {
      if (fg == root_ && is_train_) {
        continue;
      }
      auto value_nodes = fg->value_nodes();
      for (auto value_pair = value_nodes.cbegin(); value_pair != value_nodes.cend(); ++value_pair) {
        auto node = (*value_pair).first;
        if (!IsValueNode<FuncGraph>(node)) {
          continue;
        }
        auto graph = GetValueNode<FuncGraphPtr>(node);
        if (graph->stage() == -1) {
          continue;
        }
        (void)stage_set.insert(graph->stage());
        auto node_users = manager_->node_users()[node];
        for (auto &user_pair : node_users) {
          auto user_node = user_pair.first->cast<CNodePtr>();
          user_node->set_user_data<NodeStageInfo>(std::make_shared<NodeStageInfo>(graph->stage()));
          auto user_node_graph = user_node->func_graph();
          if (graph->stage() == stage_ && user_node_graph->stage() == -1) {
            user_node_graph->set_stage(graph->stage());
            need_coloring = true;
          }
        }
      }
    }
  }
  MS_EXCEPTION_IF_NULL(g_device_manager);
  auto stage_num = g_device_manager->stage_num();
  if (SizeToLong(stage_set.size()) != stage_num) {
    MS_LOG(EXCEPTION) << "Stage num is " << stage_num << " is not equal to stage used: " << stage_set.size();
  }
}

void PipelineTransformer::BroadCastColoring() {
  auto need_coloring = true;
  while (need_coloring) {
    need_coloring = false;
    auto all_nodes = enable_share_cell_ ? shared_cell_->nodes() : main_graph_->nodes();
    auto node_users = manager_->node_users();
    for (auto node = all_nodes.cbegin(); node != all_nodes.cend(); ++node) {
      auto stage_info = (*node)->user_data<NodeStageInfo>();
      if (!(*node)->isa<CNode>() || stage_info == nullptr || stage_info->stage() == -1 ||
          IsPrimitiveCNode(*node, prim::kPrimUpdateState)) {
        continue;
      }
      auto stage = stage_info->stage();
      for (auto &user_pair : node_users[*node]) {
        auto user_node = user_pair.first->cast<CNodePtr>();
        auto user_stage_info = user_node->user_data<NodeStageInfo>();
        if (user_stage_info == nullptr) {
          user_node->set_user_data<NodeStageInfo>(std::make_shared<NodeStageInfo>(stage));
          need_coloring = true;
          continue;
        }
        auto user_node_stage = user_stage_info->stage();
        if (stage > user_node_stage) {
          if (IsValueNode<FuncGraph>(user_node->input(0))) {
            MS_LOG(EXCEPTION) << "The stage setting is incorrect. PreNode's stage:" << stage
                              << " is larger than NextNode's stage:" << user_node_stage;
          }
          user_node->set_user_data<NodeStageInfo>(std::make_shared<NodeStageInfo>(stage));
          need_coloring = true;
        }
      }
    }
  }
}

std::vector<AnfNodePtr> PipelineTransformer::GetLoadNodeByParam(const AnfNodePtr &param) const {
  std::vector<AnfNodePtr> load_vec = {param};
  auto node_users = manager_->node_users()[param];
  for (auto &param_user : node_users) {
    if (IsPrimitiveCNode(param_user.first, prim::kPrimLoad)) {
      auto graph = param_user.first->func_graph();
      // exclude opt graphs
      if (graph == root_ || (graph->stage() == -1 && graph != main_graph_)) {
        continue;
      }
      (void)load_vec.emplace_back(param_user.first);
    }
  }
  return load_vec;
}

bool PipelineTransformer::IsPipelineCareNode(const CNodePtr &cnode) const {
  MS_EXCEPTION_IF_NULL(cnode);
  auto prim = GetValueNode<PrimitivePtr>(cnode->input(0));
  if (!prim) {
    return false;
  }
  if (IsInWhiteList(cnode)) {
    return false;
  }
  if (IsInParallelBlackList(prim)) {
    MS_LOG(INFO) << "PipelineSplit don't care node:" << prim->name();
    return false;
  }
  return true;
}

CNodePtr PipelineTransformer::GraphOutNode(const AnfNodePtr &node, int tuple_index) {
  auto cnode = node->cast<CNodePtr>();
  MS_EXCEPTION_IF_NULL(cnode);
  if (IsPrimitiveCNode(node, prim::kPrimDepend)) {
    return GraphOutNode(cnode->input(1), tuple_index);
  }
  if (IsPrimitiveCNode(node, prim::kPrimMakeTuple)) {
    return cnode->input(IntToSize(tuple_index) + 1)->cast<CNodePtr>();
  }
  return cnode;
}

OperatorInfoPtr PipelineTransformer::CreateOpInfo(const CNodePtr &cnode, int tuple_index = 0) {
  MS_EXCEPTION_IF_NULL(cnode);
  auto temp_node = cnode;
  if (IsValueNode<FuncGraph>(cnode->input(0))) {
    auto output = GetValueNode<FuncGraphPtr>(cnode->input(0))->output();
    MS_EXCEPTION_IF_NULL(output);
    temp_node = GraphOutNode(output, tuple_index);
  }
  if (!IsPipelineCareNode(temp_node)) {
    MS_LOG(EXCEPTION) << "Node: " << temp_node->DebugString() << " is not a Pipeline Care Node.";
  }
  if (IsPrimitiveCNode(temp_node, prim::kPrimVirtualDataset)) {
    SetVirtualDatasetStrategy(temp_node);
  }

  auto prim = GetValueNode<PrimitivePtr>(temp_node->input(0));
  MS_EXCEPTION_IF_NULL(prim);
  if (prim->name() == RESHAPE) {
    MS_LOG(EXCEPTION) << "Reshape op can't be a border. node:" << temp_node->DebugString();
  }
  auto attrs = prim->attrs();
  auto op_info = CreateOperatorInfo(temp_node);

  StrategyPtr in_strategy = nullptr, out_strategy = nullptr;
  if (!StrategyFound(attrs)) {
    in_strategy = GenerateBatchParallelStrategy(op_info, prim);
  } else {
    in_strategy = ExtractStrategy(attrs[IN_STRATEGY]);
    out_strategy = ExtractStrategy(attrs[OUT_STRATEGY]);
  }
  MS_EXCEPTION_IF_NULL(in_strategy);
  if (op_info->Init(in_strategy, out_strategy) == FAILED) {
    MS_LOG(EXCEPTION) << "operator: " << prim->name() << " init failed.";
  }
  return op_info;
}

std::pair<OperatorInfoPtr, int> PipelineTransformer::GetOpInfo(const AnfNodePtr &node) {
  MS_EXCEPTION_IF_NULL(node);
  auto cnode = node->cast<CNodePtr>();
  MS_EXCEPTION_IF_NULL(cnode);
  // Handle Cast and TupleGetitem situation
  int tensor_info_index = 0;
  OperatorInfoPtr op_info;
  if (IsPrimitiveCNode(node, prim::kPrimReceive)) {
    op_info = node->user_data<OperatorInfo>();
  } else {
    if (IsPrimitiveCNode(node, prim::kPrimCast)) {
      cnode = cnode->input(1)->cast<CNodePtr>();
    } else if (IsPrimitiveCNode(cnode, prim::kPrimTupleGetItem)) {
      tensor_info_index = LongToInt(GetTupleGetItemIndex(cnode));
      cnode = cnode->input(1)->cast<CNodePtr>();
    }
    // Create OperatorInfo to get slice_shape for send/recv
    MS_EXCEPTION_IF_NULL(cnode);
    if (cnode->has_user_data<OperatorInfo>()) {
      op_info = cnode->user_data<OperatorInfo>();
    } else {
      op_info = CreateOpInfo(cnode, tensor_info_index);
    }
  }
  return std::make_pair(op_info, tensor_info_index);
}

AnfNodeIndexSet GetActualOpUsers(const AnfNodePtr &node, NodeUsersMap *node_users_map) {
  AnfNodeIndexSet users;
  auto user_pairs = (*node_users_map)[node];
  for (const auto &user_pair : user_pairs) {
    const auto user = user_pair.first;
    const auto &cuser = user->cast<CNodePtr>();
    MS_EXCEPTION_IF_NULL(cuser);
    const auto &input = cuser->input(0);
    MS_EXCEPTION_IF_NULL(input);
    AnfNodePtr temp_node = nullptr;
    if (IsValueNode<FuncGraph>(input)) {
      auto graph = GetValueNode<FuncGraphPtr>(input);
      MS_EXCEPTION_IF_NULL(graph);
      auto temp_params = graph->parameters();
      auto index = user_pair.second;
      if (temp_params.size() < IntToSize(index)) {
        MS_LOG(EXCEPTION) << "parameter: " << temp_node->DebugString() << " out of graph: " << graph->ToString()
                          << "'s range.";
      }
      temp_node = temp_params[IntToSize(index - 1)];
    } else if (IsPrimitiveCNode(cuser, prim::kPrimLoad) || IsPrimitiveCNode(cuser, prim::kPrimCast)) {
      temp_node = cuser;
    }
    if (temp_node) {
      const auto &temp_users = GetActualOpUsers(temp_node, node_users_map);
      (void)(users.insert(temp_users.begin(), temp_users.end()));
    } else {
      (void)(users.insert(user_pair));
    }
  }
  return users;
}

std::pair<OperatorInfoPtr, int> PipelineTransformer::GetParameterPair(const AnfNodePtr &node) {
  MS_EXCEPTION_IF_NULL(node);
  auto node_users_map = manager_->node_users();
  const auto &node_users = GetActualOpUsers(node, &node_users_map);
  for (auto &node_user : node_users) {
    auto user = node_user.first->cast<CNodePtr>();
    MS_EXCEPTION_IF_NULL(user);
    auto user_graph = user->func_graph();
    MS_EXCEPTION_IF_NULL(user_graph);
    if (user_graph->stage() == -1) {
      continue;
    }
    auto index = node_user.second;
    if (!IsPipelineCareNode(user)) {
      continue;
    }
    OperatorInfoPtr op_info;
    if (user->has_user_data<OperatorInfo>()) {
      op_info = user->user_data<OperatorInfo>();
    } else {
      op_info = CreateOpInfo(user);
    }
    return std::make_pair(op_info, index - 1);
  }
  return std::make_pair(nullptr, 0);
}

AnfNodeIndexSet PipelineTransformer::GetParameterLoadUsers(const AnfNodePtr &node,
                                                           const NodeUsersMap &node_users_map) const {
  AnfNodeIndexSet users;
  if (node_users_map.find(node) == node_users_map.end()) {
    return users;
  }
  auto loads = GetLoadNodeByParam(node);
  for (auto &load : loads) {
    auto iter = node_users_map.find(load);
    if (iter == node_users_map.end()) {
      continue;
    }
    const auto &temp_users = iter->second;
    for (const auto &user : temp_users) {
      auto cuser = user.first->cast<CNodePtr>();
      MS_EXCEPTION_IF_NULL(cuser);
      const auto &input = cuser->input(0);
      MS_EXCEPTION_IF_NULL(input);
      if (enable_share_cell_ && IsValueNode<FuncGraph>(input) && GetValueNode<FuncGraphPtr>(input) == shared_cell_) {
        auto index = user.second;
        auto pos = index - 1;
        const auto &share_cell_params = shared_cell_->parameters();
        const auto &param = share_cell_params.at(pos);
        const auto &param_iter = node_users_map.find(param);
        if (param_iter == node_users_map.end()) {
          continue;
        }
        const auto &param_users = param_iter->second;
        users.insert(param_users.begin(), param_users.end());
      } else {
        users.insert(user);
      }
    }
  }
  return users;
}

std::pair<std::vector<AnfNodePtr>, std::vector<AnfNodePtr>> PipelineTransformer::HandleSharedParameter() {
  auto parameters = root_->parameters();
  std::vector<AnfNodePtr> sends = {};
  std::vector<AnfNodePtr> recvs = {};
  for (auto &parameter : parameters) {
    auto parameter_stage = parameter_color_map_[parameter];
    if (parameter_stage.size() <= 1) {
      continue;
    }
    const auto &node_users_map = manager_->node_users();
    auto users = GetParameterLoadUsers(parameter, node_users_map);
    for (auto &user : users) {
      auto node = user.first;
      auto cnode = node->cast<CNodePtr>();
      auto graph = node->func_graph();
      if (IsValueNode<FuncGraph>(cnode->input(0))) {
        graph = GetValueNode<FuncGraphPtr>(cnode->input(0));
      }
      if (graph == root_ || graph->stage() == -1 || parameter_stage.count(stage_) == 0) {
        continue;
      }
      auto micro = cnode->GetPrimalAttr(MICRO);
      if (!micro) {
        MS_LOG(INFO) << "parameter: " << parameter->ToString() << " doesn't have micro batch";
        micro = MakeValue(int64_t(0));
      }
      if (stage_ == *parameter_stage.begin()) {
        auto user_stage = graph->stage();
        auto stage_info = node->user_data<NodeStageInfo>();
        if (stage_info) {
          user_stage = stage_info->stage();
        }
        if (graph->stage() == stage_ || user_stage == -1) {
          continue;
        }
        if (Reuse(parameter, user_stage, sends, DEST_RANK)) {
          continue;
        }
        auto send_out = InsertSend(parameter, user_stage, stage_, micro);
        sends.push_back(send_out.depend);
      } else {
        auto receive = Reuse(parameter, *parameter_stage.begin(), recvs, SRC_RANK);
        if (receive) {
          manager_->SetEdge(node, user.second, receive);
        } else {
          AnfNodePtr recv;
          auto fg = enable_share_cell_ ? shared_cell_ : main_graph_;
          recv = InsertReceive(fg, parameter, node, user.second, stage_, *parameter_stage.begin(), micro, parameter);
          (void)(recvs.push_back(recv));
        }
      }
    }
  }
  return std::make_pair(sends, recvs);
}

bool PipelineTransformer::GetStageByArgument(const CNodePtr &node, size_t index,
                                             const std::vector<AnfNodePtr> &parameters,
                                             const NodeUsersMap &node_users_map,
                                             std::set<int64_t> *const parameter_stage) {
  if (!enable_share_cell_) {
    return false;
  }
  if (index < 1) {
    return false;
  }
  const auto &input = node->input(0);
  if (!IsValueNode<FuncGraph>(input)) {
    return false;
  }
  if (GetValueNode<FuncGraphPtr>(input) != shared_cell_) {
    return false;
  }
  auto pos = index - 1;
  const auto &param = parameters.at(pos);
  MS_EXCEPTION_IF_NULL(param);
  const auto &iter = node_users_map.find(param);
  if (iter == node_users_map.end()) {
    return true;
  }
  const auto &users = (*iter).second;
  for (auto &user : users) {
    auto user_cnode = user.first->cast<CNodePtr>();
    MS_EXCEPTION_IF_NULL(user_cnode);
    auto stage_info = user_cnode->user_data<NodeStageInfo>();
    if (stage_info != nullptr && stage_info->stage() != -1) {
      (void)((*parameter_stage).insert(stage_info->stage()));
    } else {
      auto graph = user_cnode->func_graph();
      MS_EXCEPTION_IF_NULL(graph);
      if (graph != root_ && graph != main_graph_ && graph->stage() != -1) {
        (void)((*parameter_stage).insert(graph->stage()));
      }
    }
  }
  return true;
}

void PipelineTransformer::ParameterColoring() {
  auto parameters = root_->parameters();
  auto &node_users_map = manager_->node_users();
  const auto &share_cell_parameters = shared_cell_->parameters();
  for (auto &parameter : parameters) {
    auto loads = GetLoadNodeByParam(parameter);
    std::set<int64_t> parameter_stage;
    for (auto &load : loads) {
      auto load_users = node_users_map[load];
      for (auto &load_user : load_users) {
        auto user_cnode = load_user.first->cast<CNodePtr>();
        MS_EXCEPTION_IF_NULL(user_cnode);
        if (GetStageByArgument(user_cnode, load_user.second, share_cell_parameters, node_users_map, &parameter_stage)) {
          continue;
        }
        auto stage_info = user_cnode->user_data<NodeStageInfo>();
        if (stage_info != nullptr && stage_info->stage() != -1) {
          (void)parameter_stage.insert(stage_info->stage());
          continue;
        } else {
          auto graph = user_cnode->func_graph();
          MS_EXCEPTION_IF_NULL(graph);
          if (graph != root_ && graph != main_graph_ && graph->stage() != -1) {
            (void)parameter_stage.insert(graph->stage());
            continue;
          }
        }
      }
    }
    auto param_info = parameter->cast<ParameterPtr>()->param_info();
    if (!param_info) {
      parameter_color_map_[parameter] = parameter_stage;
      continue;
    }
    MS_EXCEPTION_IF_NULL(param_info);
    auto requires_grad = param_info->requires_grad();
    if (!parameter_stage.empty() && *parameter_stage.begin() == stage_ && !virtual_param_ && requires_grad) {
      virtual_param_ = parameter;
    }
    parameter_color_map_[parameter] = parameter_stage;
  }
}

void PipelineTransformer::RemoveMonadNode() {
  auto all_nodes = DeepScopedGraphSearch(main_graph_->get_return());
  auto node_users_map = manager_->node_users();
  for (auto &node : all_nodes) {
    if (!IsPrimitiveCNode(node, prim::kPrimUpdateState)) {
      continue;
    }
    auto cnode = node->cast<CNodePtr>();
    MS_EXCEPTION_IF_NULL(cnode);
    auto stage_info = cnode->user_data<NodeStageInfo>();
    if (stage_info == nullptr) {
      continue;
    }
    auto stage = stage_info->stage();
    if (stage != stage_ && stage != -1) {
      auto node_users = node_users_map[node];
      for (auto &user_node : node_users) {
        auto u_node = NewValueNode(kUMonad);
        manager_->SetEdge(user_node.first, user_node.second, u_node);
      }
    }
  }
}

static ValueListPtr GetShapeValue(const Shape &shape) {
  std::vector<ValuePtr> element;
  (void)std::transform(shape.begin(), shape.end(), std::back_inserter(element),
                       [](int elem) { return MakeValue(elem); });
  return std::make_shared<ValueList>(element);
}

std::pair<ValueListPtr, TypePtr> GetShapeType(const AnfNodePtr &node, const Shape &shape, size_t index) {
  TypePtr type;
  auto cnode = node->cast<CNodePtr>();
  if (cnode != nullptr && IsValueNode<FuncGraph>(cnode->input(0))) {
    auto graph = GetValueNode<FuncGraphPtr>(cnode->input(0));
    auto graph_output = graph->output();
    type = graph_output->Type();
  } else {
    type = node->Type();
  }
  MS_EXCEPTION_IF_NULL(type);

  std::vector<ValuePtr> element;
  (void)std::transform(shape.begin(), shape.end(), std::back_inserter(element),
                       [](int elem) { return MakeValue(elem); });
  TensorTypePtr tensor_type;
  if (type->isa<mindspore::TensorType>()) {
    tensor_type = type->cast<mindspore::TensorTypePtr>();
  } else if (type->isa<Tuple>()) {
    auto tuple_type = type->cast<TuplePtr>();
    MS_EXCEPTION_IF_NULL(tuple_type);
    tensor_type = tuple_type->elements().at(index)->cast<TensorTypePtr>();
  }
  MS_EXCEPTION_IF_NULL(tensor_type);
  auto dtype = tensor_type->element();
  MS_EXCEPTION_IF_NULL(dtype);
  auto shape_list = GetShapeValue(shape);
  return std::make_pair(shape_list, dtype);
}

AnfNodePtr PipelineTransformer::FindPipelineCareNode(const AnfNodePtr &node) const {
  MS_EXCEPTION_IF_NULL(node);
  auto real_node = GetRealKernelNode(node, -1).first;
  if (!real_node->isa<CNode>()) {
    return real_node;
  }
  auto cnode = real_node->cast<CNodePtr>();
  MS_EXCEPTION_IF_NULL(cnode);
  if (IsInWhiteList(cnode)) {
    return cnode->cast<AnfNodePtr>();
  }
  if (!IsPipelineCareNode(cnode)) {
    MS_LOG(EXCEPTION) << "Only PipelineSplit cared node can be a border."
                      << " border node: " << cnode->DebugString();
  }
  return cnode->cast<AnfNodePtr>();
}

SendAttr PipelineTransformer::InsertSend(const AnfNodePtr &parameter, int64_t user_node_stage, int64_t node_stage,
                                         const ValuePtr &value) {
  auto dest_rank = global_rank_ + (user_node_stage - node_stage) * per_stage_rank_num_;
  int64_t send_tag = send_tag_map_[dest_rank];
  send_tag_map_[dest_rank]++;
  Attr attr_tag = std::make_pair(SR_TAG, MakeValue(send_tag));
  Attr attr_rank = std::make_pair(DEST_RANK, MakeValue(user_node_stage));
  Attr attr_group = std::make_pair(GROUP, MakeValue(group_[0]));
  Attr attr_group_back = std::make_pair(GROUP_BACK, MakeValue(group_[1]));
  OperatorAttrs attrs = {attr_tag, attr_rank, attr_group, attr_group_back};
  auto send_op = CreateOpInstance(attrs, SEND, SEND);
  auto send_node = NewValueNode(send_op);
  auto prim = GetValueNode<PrimitivePtr>(send_node);
  AnfNodePtr care_node;
  bool is_param = true;
  auto op_info_pair = GetOpInfoPair(parameter, parameter, &care_node, &is_param);
  auto tensor_info = GetTensorInfo(op_info_pair, is_param);
  auto index = op_info_pair.second;
  auto op_info = op_info_pair.first;
  auto slice_shape = tensor_info.slice_shape();
  auto shape_type_pair = GetShapeType(parameter, slice_shape, 0);
  prim->set_attr(SHAPE, shape_type_pair.first);
  prim->set_attr(DTYPE, shape_type_pair.second);
  std::vector<AnfNodePtr> send_input = {send_node, parameter};
  auto graph = enable_share_cell_ ? shared_cell_ : main_graph_;
  CNodePtr send = graph->NewCNode(send_input);
  if (!is_param) {
    send->AddPrimalAttr(PIPELINE_END, value);
  } else {
    send->AddPrimalAttr(PIPELINE_PARAM, value);
    send->set_user_data<OperatorInfo>(op_info);
    send->AddPrimalAttr(PARAM_INDEX, MakeValue(index));
    auto param = care_node ? care_node : parameter;
    send->set_user_data<AnfNode>(INPUT_PARAM, param);
  }
  send->AddPrimalAttr(MICRO, value);
  send->AddPrimalAttr(DEST_RANK, MakeValue(user_node_stage));
  OperatorAttrs depend_attrs;
  auto depend_op = CreateOpInstance(depend_attrs, DEPEND, DEPEND);
  std::vector<AnfNodePtr> depend_input = {NewValueNode(depend_op), parameter, send};
  CNodePtr depend = graph->NewCNode(depend_input);
  auto abstract = parameter->abstract();
  if (care_node) {
    abstract = care_node->abstract();
  }
  depend->set_abstract(abstract);
  send->set_abstract(abstract);
  SendAttr send_out = {shape_type_pair.first, shape_type_pair.second, depend};

  // for FetchSends
  send->set_user_data<int64_t>(DEST_RANK, std::make_shared<int64_t>(dest_rank));
  send->set_user_data<int64_t>(USER_NODE_STAGE, std::make_shared<int64_t>(user_node_stage));
  return send_out;
}

AnfNodePtr PipelineTransformer::InsertReceive(const FuncGraphPtr &graph, const AnfNodePtr &node,
                                              const AnfNodePtr &use_node, int index, int64_t user_node_stage,
                                              int64_t node_stage, const ValuePtr &value,
                                              const AnfNodePtr &graph_param) {
  auto src_rank = global_rank_ - (user_node_stage - node_stage) * per_stage_rank_num_;
  int64_t recv_tag = recv_tag_map_[src_rank];
  recv_tag_map_[src_rank]++;
  Attr attr_tag = std::make_pair(SR_TAG, MakeValue(recv_tag));
  Attr attr_rank = std::make_pair(SRC_RANK, MakeValue(node_stage));
  bool is_param = true;
  AnfNodePtr care_node;
  auto op_info_pair = GetOpInfoPair(node, graph_param, &care_node, &is_param);
  auto tensor_info = GetTensorInfo(op_info_pair, is_param);
  auto tensor_layout = tensor_info.tensor_layout();
  Shape slice_shape = tensor_info.slice_shape();
  auto shape_type_pair = GetShapeType(node, slice_shape, 0);
  Attr attr_shape = std::make_pair(SHAPE, shape_type_pair.first);
  Attr attr_dtype = std::make_pair(DTYPE, shape_type_pair.second);
  Attr attr_group = std::make_pair(GROUP, MakeValue(group_[0]));
  Attr attr_group_back = std::make_pair(GROUP_BACK, MakeValue(group_[1]));
  OperatorAttrs attrs = {attr_tag, attr_rank, attr_shape, attr_dtype, attr_group, attr_group_back};
  auto recv_op = CreateOpInstance(attrs, RECEIVE, RECEIVE);
  std::vector<AnfNodePtr> recv_input;
  if (node->isa<Parameter>()) {
    recv_input = {NewValueNode(recv_op), node};
  } else {
    recv_input = {NewValueNode(recv_op), virtual_param_};
  }
  auto recv = graph->NewCNode(recv_input);
  if (is_param) {
    recv->set_user_data<AnfNode>(PIPELINE_PARAM, node);
    recv->AddPrimalAttr(PIPELINE_PARAM, value);
    auto param = care_node ? care_node : node;
    recv->set_user_data<AnfNode>(INPUT_PARAM, param);
  } else {
    recv->AddPrimalAttr(PIPELINE_BEGIN, value);
  }
  recv->AddPrimalAttr(MICRO, value);
  recv->AddPrimalAttr(SRC_RANK, MakeValue(node_stage));
  auto node_abstract = node->abstract();
  if (node->isa<CNode>()) {
    auto cnode = node->cast<CNodePtr>();
    MS_EXCEPTION_IF_NULL(cnode);
    if (IsValueNode<FuncGraph>(cnode->input(0))) {
      auto output = GetValueNode<FuncGraphPtr>(cnode->input(0))->output();
      MS_EXCEPTION_IF_NULL(output);
      node_abstract = output->abstract();
    }
  }
  MS_EXCEPTION_IF_NULL(node_abstract);
  recv->set_abstract(node_abstract);
  if (node->isa<Parameter>()) {
    BaseShapePtr parallel_shape = std::make_shared<abstract::Shape>(slice_shape);
    auto abstract_clone = node->abstract()->Clone();
    MS_EXCEPTION_IF_NULL(abstract_clone);
    abstract_clone->set_shape(parallel_shape);
    node->set_abstract(abstract_clone);
    node->set_user_data<TensorLayout>(std::make_shared<TensorLayout>(tensor_layout));
    auto actual_param = RefParameterToActualParameter(node);
    if (actual_param) {
      actual_param->set_user_data<TensorLayout>(std::make_shared<TensorLayout>(tensor_layout));
      auto actual_param_abstract = actual_param->abstract()->Clone();
      actual_param_abstract->set_shape(parallel_shape);
      actual_param->set_abstract(actual_param_abstract);
    }
  }
  recv->set_user_data<TensorLayout>(std::make_shared<TensorLayout>(tensor_layout));
  recv->set_user_data<OperatorInfo>(op_info_pair.first);

  // for FetchRecvs
  recv->set_user_data<int64_t>(SRC_RANK, std::make_shared<int64_t>(src_rank));
  recv->set_user_data<int64_t>(NODE_STAGE, std::make_shared<int64_t>(node_stage));
  recv->set_user_data<Type>(SLICE_DTYPE, shape_type_pair.second);
  recv->set_user_data<Shape>(SLICE_SHAPE, std::make_shared<Shape>(slice_shape));

  manager_->SetEdge(use_node, index, recv);
  return recv;
}

AnfNodePtr PipelineTransformer::Reuse(const AnfNodePtr &node, int64_t stage, const std::vector<AnfNodePtr> &out_input,
                                      const std::string &tag) const {
  for (auto &input : out_input) {
    auto cnode = input->cast<CNodePtr>();
    if (!cnode) {
      continue;
    }
    if (IsPrimitiveCNode(cnode, prim::kPrimDepend)) {
      cnode = cnode->input(2)->cast<CNodePtr>();
    }
    if (cnode->input(1) == node) {
      auto dest_rank_send = GetValue<int64_t>(cnode->GetPrimalAttr(tag));
      if (dest_rank_send == stage) {
        return input;
      }
    }
  }
  return nullptr;
}

AnfNodePtr PipelineTransformer::ActualOp(const AnfNodePtr &node) {
  // skip some virtual op like:Depend, Load, Cast
  if (IsPrimitiveCNode(node, prim::kPrimDepend) || IsPrimitiveCNode(node, prim::kPrimCast) ||
      IsPrimitiveCNode(node, prim::kPrimLoad)) {
    auto cnode = node->cast<CNodePtr>();
    MS_EXCEPTION_IF_NULL(cnode);
    return ActualOp(cnode->input(1));
  }
  return node;
}

bool PipelineTransformer::IsParameterGraph(const AnfNodePtr &node) const {
  // ParameterGraph: graph which return a parameter
  MS_EXCEPTION_IF_NULL(node);
  CNodePtr call_node = nullptr;
  auto real_kernel = GetRealKernelNode(node, -1, &call_node).first;
  if (call_node != nullptr && real_kernel->isa<Parameter>()) {
    return true;
  }
  return false;
}

AnfNodePtr PipelineTransformer::HandleParameterGraph(const AnfNodePtr &node, const AnfNodePtr &use_node, int64_t stage,
                                                     int64_t user_stage, const ValuePtr &micro, size_t pos,
                                                     const std::vector<AnfNodePtr> &ops) {
  CNodePtr call_node = nullptr;
  auto argument = GetRealKernelNode(node, -1, &call_node).first;

  auto use_cnode = use_node->cast<CNodePtr>();
  MS_EXCEPTION_IF_NULL(use_cnode);
  if (!IsValueNode<FuncGraph>(use_cnode->input(0))) {
    MS_LOG(EXCEPTION) << "Parameter must be used by a graph, but got: " << use_cnode->DebugString();
  }
  auto use_graph = GetValueNode<FuncGraphPtr>(use_cnode->input(0));
  auto use_parameter_list = use_graph->parameters();
  auto parameter = use_parameter_list.at(pos - 1);
  // insert receive
  if (stage_ == user_stage) {
    auto recv = Reuse(argument, stage, ops, SRC_RANK);
    if (recv) {
      manager_->SetEdge(use_node, SizeToInt(pos), recv);
      return nullptr;
    }
    auto root_param = argument;
    if (argument->isa<Parameter>() && argument->func_graph() != root_) {
      root_param = GetArgumentsByParameter(argument);
    }
    (void)parameter_color_map_[root_param].insert(user_stage);
    auto graph = enable_share_cell_ ? shared_cell_ : main_graph_;
    return InsertReceive(graph, argument, use_node, SizeToInt(pos), user_stage, stage, micro, parameter);
  }
  // insert send
  if (Reuse(argument, user_stage, ops, DEST_RANK)) {
    return nullptr;
  }
  auto send_out = InsertSend(argument, user_stage, stage_, micro);
  send_out.depend->set_user_data<Type>(DTYPE, send_out.type);
  send_out.depend->set_user_data<ValueList>(SHAPE, send_out.shape);
  return send_out.depend;
}

void PipelineTransformer::CutBorderForNode(const FuncGraphPtr &graph, const AnfNodePtr &node,
                                           std::vector<AnfNodePtr> *send_ops, std::vector<AnfNodePtr> *receive_ops) {
  auto stage_info = node->user_data<NodeStageInfo>();
  auto node_users = manager_->node_users()[node];
  AnfNodePtr receive = nullptr;
  for (auto &user_pair : node_users) {
    auto user_node = user_pair.first;
    auto node_stage = stage_info->stage();
    auto user_stage_info = user_node->user_data<NodeStageInfo>();
    if (user_stage_info == nullptr) {
      continue;
    }
    auto user_node_stage = user_stage_info->stage();
    if (node_stage != stage_ && user_node_stage != stage_) {
      continue;
    }
    auto micro = user_node->cast<CNodePtr>()->GetPrimalAttr(MICRO);
    if (!micro) {
      MS_LOG(INFO) << "Can't find micro_batch information, use micro(0)";
      micro = MakeValue(int64_t(0));
    }
    if (node_stage < user_node_stage) {
      if (node_stage == stage_) {
        if (IsParameterGraph(node)) {
          auto send_depend = HandleParameterGraph(node, user_node, node_stage, user_node_stage, micro,
                                                  IntToSize(user_pair.second), *send_ops);
          if (!send_depend) {
            continue;
          }
          (void)send_ops->insert(send_ops->cbegin(), send_depend);
          continue;
        }
        if (Reuse(node, user_node_stage, *send_ops, DEST_RANK)) {
          continue;
        }
        auto send_out = InsertSend(node, user_node_stage, node_stage, micro);
        MS_EXCEPTION_IF_NULL(send_out.depend);
        send_ops->push_back(send_out.depend);
        send_out.depend->set_user_data<Type>(DTYPE, send_out.type);
        send_out.depend->set_user_data<ValueList>(SHAPE, send_out.shape);
      } else {
        if (!receive) {
          if (IsParameterGraph(node)) {
            receive = HandleParameterGraph(node, user_node, node_stage, user_node_stage, micro,
                                           IntToSize(user_pair.second), *receive_ops);
            if (!receive) {
              continue;
            }
            receive_ops->push_back(receive);
          } else {
            receive = InsertReceive(graph, node, user_node, user_pair.second, user_node_stage, node_stage, micro, node);
            receive_ops->push_back(receive);
          }
        } else {
          manager_->SetEdge(user_node, user_pair.second, receive);
        }
      }
      continue;
    }
    if (node_stage > user_node_stage) {
      MS_LOG(EXCEPTION) << "node_stage: " << node_stage << " must be smaller than user_node_stage: " << user_node_stage;
    }
  }
}

std::pair<std::vector<AnfNodePtr>, std::vector<AnfNodePtr>> PipelineTransformer::CutBorder(const FuncGraphPtr &graph) {
  std::vector<AnfNodePtr> send_ops;
  std::vector<AnfNodePtr> receive_ops;
  auto ret = graph->get_return();
  MS_EXCEPTION_IF_NULL(ret);
  std::vector<AnfNodePtr> all_nodes = DeepScopedGraphSearch(ret);
  std::reverse(all_nodes.begin(), all_nodes.end());
  auto stage_num = g_device_manager->stage_num();
  if (is_train_ && (stage_num > micro_size_)) {
    MS_LOG(EXCEPTION) << "MicroBatch size: " << micro_size_ << " can't less than stage num: " << stage_num;
  }
  for (auto &node : all_nodes) {
    auto stage_info = node->user_data<NodeStageInfo>();
    if (!node->isa<CNode>() || stage_info == nullptr || stage_info->stage() == -1 ||
        IsPrimitiveCNode(node, prim::kPrimUpdateState)) {
      continue;
    }
    // Modify for lizard cyclomatic complexity.
    CutBorderForNode(graph, node, &send_ops, &receive_ops);
  }
  RemoveMonadNode();
  return std::make_pair(send_ops, receive_ops);
}

tensor::TensorPtr PipelineTransformer::CreateZeroseOutput(const AnfNodePtr &node, size_t index) {
  auto out_shapes = GetNodeShape(node);
  auto out_shape_type = GetShapeType(node, out_shapes.at(index), index);
  auto zero_tensor = TensorConstructUtils::CreateZerosTensor(out_shape_type.second, out_shapes.at(index));
  return zero_tensor;
}

AnfNodePtr PipelineTransformer::GetZeroOutputs(const FuncGraphPtr &graph) {
  // first: out node  second: getitem index
  auto real_kernel = GetRealKernelNode(graph->output(), -1);
  auto real_out = real_kernel.first;
  MS_EXCEPTION_IF_NULL(real_out);
  std::vector<AnfNodePtr> out_tuple_inputs = {NewValueNode(prim::kPrimMakeTuple)};
  if (IsPrimitiveCNode(real_out, prim::kPrimMakeTuple)) {
    auto real_out_cnode = real_out->cast<CNodePtr>();
    for (size_t i = 1; i < real_out_cnode->inputs().size(); ++i) {
      auto each_out_shapes = GetNodeShape(real_out_cnode->input(i));
      // In case: tuple's input is also a tuple
      if (each_out_shapes.size() > 1) {
        auto temp_tuple = CreateTupleZeroTensor(real_out_cnode->input(i), each_out_shapes.size());
        (void)out_tuple_inputs.emplace_back(temp_tuple);
        continue;
      }
      (void)out_tuple_inputs.emplace_back(NewValueNode(CreateZeroseOutput(real_out_cnode->input(i), 0)));
    }
  }
  if (out_tuple_inputs.size() > INDEX_ONE) {
    auto out_tuple = main_graph_->NewCNode(out_tuple_inputs);
    return out_tuple;
  } else {
    auto real_out_shapes = GetNodeShape(real_out);
    AnfNodePtr out_tensor;
    // In case: op has multioutput
    if (real_out_shapes.size() > 1 && real_kernel.second == -1) {
      out_tensor = CreateTupleZeroTensor(real_out, real_out_shapes.size());
    } else {
      out_tensor = NewValueNode(CreateZeroseOutput(real_out, 0));
    }
    return out_tensor;
  }
  return nullptr;
}

std::pair<OperatorInfoPtr, int> PipelineTransformer::GetOpInfoPair(const AnfNodePtr &node,
                                                                   const AnfNodePtr &graph_param, AnfNodePtr *care_node,
                                                                   bool *is_param) {
  if (node->isa<Parameter>()) {
    return GetParameterPair(graph_param);
  } else {
    *care_node = FindPipelineCareNode(node);
    if ((*care_node)->isa<Parameter>()) {
      return GetParameterPair(*care_node);
    } else {
      *is_param = false;
      return GetOpInfo(*care_node);
    }
  }
}

void PipelineTransformer::SetNodeAbstract(const std::vector<AnfNodePtr> &nodes) {
  AbstractBasePtr abs;
  if (nodes.size() == 1) {
    auto cnode = nodes.front()->cast<CNodePtr>();
    MS_EXCEPTION_IF_NULL(cnode);
    abs = GetRealAbstract(cnode->input(INDEX_ONE));
  } else {
    AbstractBasePtrList abstract_list;
    abstract_list.resize(nodes.size());
    (void)std::transform(nodes.begin(), nodes.end(), abstract_list.begin(), [](const AnfNodePtr &node) {
      auto cnode = node->cast<CNodePtr>();
      MS_EXCEPTION_IF_NULL(cnode);
      return GetRealAbstract(cnode->input(INDEX_ONE));
    });
    abs = std::make_shared<abstract::AbstractTuple>(abstract_list);
  }
  for (auto &user : shared_cell_users_) {
    user->set_abstract(abs);
  }
}

AnfNodePtr PipelineTransformer::GenNewSendFromOld(const AnfNodePtr &node, const AnfNodePtr &input,
                                                  const ValuePtr &value) {
  const auto &old = node->cast<CNodePtr>();
  MS_EXCEPTION_IF_NULL(old);
  auto old_is_pipeline_param = old->HasPrimalAttr(PIPELINE_PARAM);
  auto dest_rank = old->user_data<int64_t>(DEST_RANK);
  auto send_tag = send_tag_map_[*dest_rank];
  send_tag_map_[*dest_rank]++;
  Attr attr_tag = std::make_pair(SR_TAG, MakeValue(send_tag));
  Attr attr_rank = std::make_pair(DEST_RANK, MakeValue(*(old->user_data<int64_t>(USER_NODE_STAGE))));
  Attr attr_group = std::make_pair(GROUP, MakeValue(group_[0]));
  Attr attr_group_back = std::make_pair(GROUP_BACK, MakeValue(group_[1]));
  OperatorAttrs attrs = {attr_tag, attr_rank, attr_group, attr_group_back};
  auto send_op = CreateOpInstance(attrs, SEND, SEND);
  AnfNodePtr send_node = NewValueNode(send_op);
  std::vector<AnfNodePtr> send_input{send_node, input};
  auto send = main_graph_->NewCNode(send_input);
  AnfNodePtr care_node;
  bool is_param = true;
  auto op_info_pair = GetOpInfoPair(input, input, &care_node, &is_param);
  auto tensor_info = GetTensorInfo(op_info_pair, is_param);
  auto op_info = op_info_pair.first;
  auto index = op_info_pair.second;
  auto slice_shape = tensor_info.slice_shape();
  auto shape_type_pair = GetShapeType(input, slice_shape, 0);
  auto prim = GetValueNode<PrimitivePtr>(send_node);
  prim->set_attr(SHAPE, shape_type_pair.first);
  prim->set_attr(DTYPE, shape_type_pair.second);
  if (!is_param) {
    if (old_is_pipeline_param) {
      MS_LOG(EXCEPTION) << "The old send is pipeline_param, but new send is not pipeline_param.";
    }
    send->AddPrimalAttr(PIPELINE_END, value);
  } else {
    if (!old_is_pipeline_param) {
      MS_LOG(EXCEPTION) << "The old send is not pipeline_param, but new send is pipeline_param.";
    }
    send->AddPrimalAttr(PARAM_INDEX, MakeValue(index));
    send->AddPrimalAttr(PIPELINE_PARAM, value);
    send->set_user_data<OperatorInfo>(op_info);
  }
  send->AddPrimalAttr(MICRO, value);

  OperatorAttrs depend_attrs;
  auto depend_op = CreateOpInstance(depend_attrs, DEPEND, DEPEND);
  std::vector<AnfNodePtr> depend_input = {NewValueNode(depend_op), send_input[INDEX_ONE], send};
  auto depend = main_graph_->NewCNode(depend_input);
  auto abstract = input->abstract();
  if (care_node) {
    abstract = care_node->abstract();
  }
  depend->set_abstract(abstract);
  send->set_abstract(abstract);
  depend->set_user_data<ValueList>(SHAPE, shape_type_pair.first);
  depend->set_user_data<Type>(DTYPE, shape_type_pair.second);
  return depend;
}

std::vector<AnfNodePtr> PipelineTransformer::FetchSend(const AnfNodePtr &node, bool pipeline_param,
                                                       bool single_pipeline_end, size_t end_index) {
  std::vector<AnfNodePtr> depends;
  AnfNodePtr send_input;
  if (pipeline_param) {
    auto param = node->user_data<AnfNode>(INPUT_PARAM);
    MS_EXCEPTION_IF_NULL(param);
    auto params = shared_cell_->parameters();
    auto iter = std::find(params.begin(), params.end(), param);
    if (iter != params.end()) {
      auto input_pos = std::distance(params.begin(), iter) + 1;
      auto &front = shared_cell_users_.front();
      MS_EXCEPTION_IF_NULL(front);
      const auto &user = front->cast<CNodePtr>();
      MS_EXCEPTION_IF_NULL(user);
      send_input = user->input(input_pos);
    } else {
      const auto &cnode = node->cast<CNodePtr>();
      MS_EXCEPTION_IF_NULL(cnode);
      send_input = cnode->input(INDEX_ONE);
    }
    MS_EXCEPTION_IF_NULL(send_input);
    auto value = MakeValue(int64_t(0));
    (void)(depends.emplace_back(GenNewSendFromOld(node, send_input, value)));
    return depends;
  }
  for (auto &user : shared_cell_users_) {
    auto cuser = user->cast<CNodePtr>();
    MS_EXCEPTION_IF_NULL(cuser);
    auto value = cuser->GetPrimalAttr(MICRO);
    MS_EXCEPTION_IF_NULL(value);
    send_input = single_pipeline_end ? user : CreateTupleGetItemNode(main_graph_, user, end_index);
    (void)(depends.emplace_back(GenNewSendFromOld(node, send_input, value)));
  }
  return depends;
}

void PipelineTransformer::HandleGraphOutputs(const std::vector<AnfNodePtr> &nodes) {
  std::vector<AnfNodePtr> pipeline_params;
  std::vector<AnfNodePtr> pipeline_ends;
  SeparateParamBorder(nodes, true, &pipeline_params, &pipeline_ends);
  std::vector<AnfNodePtr> sends;
  SetNodeAbstract(pipeline_ends);
  size_t ends_size = pipeline_ends.size();
  bool single_pipeline_end = ends_size == 1;
  if (single_pipeline_end) {
    auto &depend = pipeline_ends.front();
    const auto &cdepend = depend->cast<CNodePtr>();
    MS_EXCEPTION_IF_NULL(cdepend);
    (void)manager_->Replace(shared_cell_->output(), cdepend->input(INDEX_ONE));
  } else {
    std::vector<AnfNodePtr> rets;
    (void)std::transform(pipeline_ends.begin(), pipeline_ends.end(), std::back_inserter(rets),
                         [](const AnfNodePtr &depend) {
                           const auto &cdepend = depend->cast<CNodePtr>();
                           MS_EXCEPTION_IF_NULL(cdepend);
                           return cdepend->input(INDEX_ONE);
                         });
    auto out = CreateMakeTupleNode(shared_cell_, rets);
    (void)manager_->Replace(shared_cell_->output(), out);
  }
  for (auto &node : pipeline_params) {
    auto params = FetchSend(node, true, false, 0);
    (void)std::copy(params.begin(), params.end(), std::back_inserter(sends));
  }
  for (size_t i = 0; i < ends_size; i++) {
    auto node = pipeline_ends[i];
    auto ends = FetchSend(node, false, single_pipeline_end, i);
    (void)std::copy(ends.begin(), ends.end(), std::back_inserter(sends));
  }
  auto make_tuple = CreateMakeTupleNode(main_graph_, sends);
  auto zero_outputs = GetZeroOutputs(main_graph_);
  std::vector<AnfNodePtr> out = {NewValueNode(prim::kPrimDepend), zero_outputs, make_tuple};
  auto out_node = main_graph_->NewCNode(out);
  (void)manager_->Replace(main_graph_->output(), out_node);
}

AnfNodePtr PipelineTransformer::GenNewRecvFromOld(const AnfNodePtr &node, const AnfNodePtr &input,
                                                  const ValuePtr &value) {
  auto cnode = node->cast<CNodePtr>();
  MS_EXCEPTION_IF_NULL(cnode);
  auto src_rank = *(cnode->user_data<int64_t>(SRC_RANK));
  auto recv_tag = recv_tag_map_[src_rank];
  recv_tag_map_[src_rank]++;
  auto dtype = node->user_data<Type>(SLICE_DTYPE);
  auto slice_shape = *(cnode->user_data<Shape>(SLICE_SHAPE));
  auto shape = GetShapeValue(slice_shape);
  Attr attr_tag = std::make_pair(SR_TAG, MakeValue(recv_tag));
  Attr attr_rank = std::make_pair(SRC_RANK, MakeValue(*(node->user_data<int64_t>(NODE_STAGE))));
  Attr attr_shape = std::make_pair(SHAPE, shape);
  Attr attr_dtype = std::make_pair(DTYPE, dtype);
  Attr attr_group = std::make_pair(GROUP, MakeValue(group_[0]));
  Attr attr_group_back = std::make_pair(GROUP_BACK, MakeValue(group_[1]));
  OperatorAttrs attrs = {attr_tag, attr_rank, attr_shape, attr_dtype, attr_group, attr_group_back};

  auto recv_op = CreateOpInstance(attrs, RECEIVE, RECEIVE);
  std::vector<AnfNodePtr> recv_input = {NewValueNode(recv_op), input};
  auto recv = main_graph_->NewCNode(recv_input);
  auto tensor_layout = node->user_data<TensorLayout>();
  if (cnode->HasPrimalAttr(PIPELINE_PARAM)) {
    auto abstract_clone = node->abstract()->Clone();
    MS_EXCEPTION_IF_NULL(abstract_clone);
    recv->set_user_data<AnfNode>(PIPELINE_PARAM, recv_input[INDEX_ONE]);
    recv->AddPrimalAttr(PIPELINE_PARAM, value);
    recv_input[INDEX_ONE]->set_abstract(abstract_clone);
    recv_input[INDEX_ONE]->set_user_data<TensorLayout>(std::make_shared<TensorLayout>(*tensor_layout));
  } else {
    recv->AddPrimalAttr(PIPELINE_BEGIN, value);
  }
  auto abstract_clone = node->abstract()->Clone();
  MS_EXCEPTION_IF_NULL(abstract_clone);
  recv->set_abstract(abstract_clone);

  recv->AddPrimalAttr(MICRO, value);
  recv->set_user_data<TensorLayout>(std::make_shared<TensorLayout>(*tensor_layout));
  recv->set_user_data<OperatorInfo>(node->user_data<OperatorInfo>());
  return recv;
}

std::vector<AnfNodePtr> PipelineTransformer::FetchRecv(const AnfNodePtr &node, bool pipeline_param) {
  std::vector<AnfNodePtr> recvs;
  AnfNodePtr recv_input;
  AnfNodePtr recv;
  if (pipeline_param) {
    auto value = MakeValue(int64_t(0));
    auto param = node->user_data<AnfNode>(INPUT_PARAM);
    MS_EXCEPTION_IF_NULL(param);
    auto params = shared_cell_->parameters();
    auto iter = std::find(params.begin(), params.end(), param);
    if (iter != params.end()) {
      auto pos = std::distance(params.begin(), iter);
      auto input_pos = pos + 1;
      auto &front = shared_cell_users_.front();
      MS_EXCEPTION_IF_NULL(front);
      const auto &user = front->cast<CNodePtr>();
      MS_EXCEPTION_IF_NULL(user);
      recv_input = user->input(input_pos);
      recv = GenNewRecvFromOld(node, recv_input, value);
      for (auto &share_user : shared_cell_users_) {
        manager_->SetEdge(share_user, input_pos, recv);
      }
      node->set_user_data<bool>(ORIGIN_INPUT_IS_PARAM, std::make_shared<bool>(true));
    } else {
      const auto &cnode = node->cast<CNodePtr>();
      MS_EXCEPTION_IF_NULL(cnode);
      recv_input = cnode->input(INDEX_ONE);
      recv = GenNewRecvFromOld(node, recv_input, value);
    }
    (void)(recvs.emplace_back(recv));
    return recvs;
  }
  for (auto &user : shared_cell_users_) {
    auto cuser = user->cast<CNodePtr>();
    MS_EXCEPTION_IF_NULL(cuser);
    auto value = cuser->GetPrimalAttr(MICRO);
    MS_EXCEPTION_IF_NULL(value);
    recv = GenNewRecvFromOld(node, virtual_param_, value);
    (void)(recvs.emplace_back(recv));
  }
  return recvs;
}

void PipelineTransformer::ResetSharedCellParamAndArgu(
  const std::vector<std::vector<AnfNodePtr>> &pipeline_begins_fetched,
  const std::vector<AnfNodePtr> &newly_added_params, const std::vector<AnfNodePtr> &reserved_inputs) {
  // set shared_cell_ parameters, and call_input
  auto params = shared_cell_->parameters();
  auto ret = shared_cell_->get_return();
  MS_EXCEPTION_IF_NULL(ret);
  std::vector<AnfNodePtr> searched_params;
  std::vector<AnfNodePtr> all_nodes = DeepScopedGraphSearch(ret);
  for (auto &node : all_nodes) {
    if (node->isa<Parameter>()) {
      searched_params.push_back(node);
    }
  }
  std::set<size_t> reserved_param_index;
  std::vector<AnfNodePtr> new_params;
  std::vector<AnfNodePtr> monad_params;
  // set shared_cell_ parameters
  for (size_t i = 0; i < params.size(); i++) {
    auto param = params[i];
    if (std::find(searched_params.begin(), searched_params.end(), param) == searched_params.end()) {
      continue;
    }
    if (HasAbstractMonad(param)) {
      monad_params.push_back(param);
    } else {
      new_params.push_back(param);
    }
    (void)(reserved_param_index.insert(i));
  }
  (void)(new_params.insert(new_params.end(), newly_added_params.begin(), newly_added_params.end()));
  (void)(new_params.insert(new_params.end(), monad_params.begin(), monad_params.end()));
  MS_LOG(DEBUG) << "The shared cell origin params size is " << params.size() << ", new params size is "
                << new_params.size();
  manager_->SetParameters(shared_cell_, new_params);
  // set call inputs
  size_t user_index = 0;
  for (auto &user : shared_cell_users_) {
    auto cuser = user->cast<CNodePtr>();
    MS_EXCEPTION_IF_NULL(cuser);
    const auto &old_inputs = cuser->inputs();
    std::vector<AnfNodePtr> new_inputs{old_inputs.front()};
    std::vector<AnfNodePtr> monad_inputs;
    for (size_t i = 1; i < old_inputs.size(); i++) {
      if (reserved_param_index.find(i - 1) == reserved_param_index.end()) {
        continue;
      }
      auto old_input = old_inputs[i];
      if (HasAbstractMonad(old_input)) {
        monad_inputs.push_back(old_input);
      } else {
        new_inputs.push_back(old_input);
      }
    }
    auto newly_added_inputs = reserved_inputs;
    auto begins = pipeline_begins_fetched.at(user_index);
    (void)(newly_added_inputs.insert(newly_added_inputs.end(), begins.begin(), begins.end()));
    (void)(newly_added_inputs.insert(newly_added_inputs.end(), monad_inputs.begin(), monad_inputs.end()));
    (void)(new_inputs.insert(new_inputs.end(), newly_added_inputs.begin(), newly_added_inputs.end()));
    auto new_call = main_graph_->NewCNode(new_inputs);
    new_call->set_attrs(cuser->attrs());
    new_call->set_primal_attrs(cuser->primal_attrs());
    new_call->set_abstract(cuser->abstract());
    (void)manager_->Replace(user, new_call);
    user_index++;
  }
}

void PipelineTransformer::HandleGraphInputs(const std::vector<AnfNodePtr> &recv_ops) {
  std::vector<AnfNodePtr> pipeline_params;
  std::vector<AnfNodePtr> pipeline_begins;
  SeparateParamBorder(recv_ops, false, &pipeline_params, &pipeline_begins);

  // reserved inputs
  std::vector<AnfNodePtr> reserved_inputs;
  // pipeline_param whose input is a parameter
  std::vector<AnfNodePtr> pipeline_params_with_param_input;
  std::vector<AnfNodePtr> need_link_to_new_param;

  for (auto &node : pipeline_params) {
    auto recvs = FetchRecv(node, true);
    auto cnode = node->cast<CNodePtr>();
    MS_EXCEPTION_IF_NULL(cnode);
    if (cnode->has_user_data(ORIGIN_INPUT_IS_PARAM)) {
      pipeline_params_with_param_input.push_back(node);
    } else {
      (void)(reserved_inputs.insert(reserved_inputs.end(), recvs.begin(), recvs.end()));
      need_link_to_new_param.push_back(node);
    }
  }
  (void)(need_link_to_new_param.insert(need_link_to_new_param.end(), pipeline_begins.begin(), pipeline_begins.end()));

  size_t begin_size = pipeline_begins.size();
  // The 0th dimension corresponds to shared_cell users
  // The first dimension corresponds to recvs
  // user0: recv0_0, recv0_1
  // user1: recv1_0, recv1_1
  size_t shared_cell_users_size = shared_cell_users_.size();
  std::vector<std::vector<AnfNodePtr>> pipeline_begins_fetched(shared_cell_users_size, std::vector<AnfNodePtr>());
  for (size_t i = 0; i < begin_size; i++) {
    auto node = pipeline_begins[i];
    auto begins = FetchRecv(node, false);
    for (size_t j = 0; j < shared_cell_users_size; j++) {
      pipeline_begins_fetched[j].push_back(begins.at(j));
    }
  }
  auto &node_users_map = manager_->node_users();
  // relink pipeline_param_with_param_input's users to its input
  for (const auto &param : pipeline_params_with_param_input) {
    const auto &users = node_users_map[param];
    auto input = param->user_data<AnfNode>(INPUT_PARAM);
    MS_EXCEPTION_IF_NULL(input);
    for (const auto &user : users) {
      manager_->SetEdge(user.first, user.second, input);
    }
  }

  std::vector<AnfNodePtr> newly_added_params;
  // relink pipeline_param_without_param_input and pipeline_begins's users to new parameter
  for (const auto &node : need_link_to_new_param) {
    auto param = std::make_shared<Parameter>(shared_cell_);
    param->set_abstract(node->abstract()->Clone());
    newly_added_params.push_back(param);
    const auto &users = node_users_map[node];
    for (const auto &user : users) {
      manager_->SetEdge(user.first, user.second, param);
    }
  }
  ResetSharedCellParamAndArgu(pipeline_begins_fetched, newly_added_params, reserved_inputs);
}

AnfNodePtr PipelineTransformer::CreateTupleZeroTensor(const AnfNodePtr &node, size_t index) {
  std::vector<AnfNodePtr> temp_tuple_inputs = {NewValueNode(prim::kPrimMakeTuple)};
  auto out_shapes = GetNodeShape(node);
  for (size_t ele = 0; ele < out_shapes.size(); ++ele) {
    temp_tuple_inputs.emplace_back(NewValueNode(CreateZeroseOutput(node, ele)));
  }
  auto temp_tuple = main_graph_->NewCNode(temp_tuple_inputs);
  return temp_tuple;
}

void PipelineTransformer::CutGraph() {
  CreateForwardGroup();
  auto send_recv_shared_param = HandleSharedParameter();
  auto graph = enable_share_cell_ ? shared_cell_ : main_graph_;
  MS_EXCEPTION_IF_NULL(graph);
  auto send_recv_cut_border = CutBorder(graph);
  std::vector<AnfNodePtr> send_ops;
  (void)(send_ops.insert(send_ops.end(), send_recv_shared_param.first.begin(), send_recv_shared_param.first.end()));
  (void)(send_ops.insert(send_ops.end(), send_recv_cut_border.first.begin(), send_recv_cut_border.first.end()));
  if (IsLastStage() && !enable_share_cell_) {
    return;
  }
  if (send_ops.empty() && !is_train_) {
    return;
  }
  if (!send_ops.empty()) {
    type_ptr_ = send_ops.back()->user_data<Type>(DTYPE);
    shape_ = send_ops.back()->user_data<ValueList>(SHAPE);
  }
  if (!enable_share_cell_) {
    auto make_tuple = CreateMakeTupleNode(main_graph_, send_ops);
    auto zero_outputs = GetZeroOutputs(main_graph_);
    std::vector<AnfNodePtr> out = {NewValueNode(prim::kPrimDepend), zero_outputs, make_tuple};
    auto out_node = main_graph_->NewCNode(out);
    (void)manager_->Replace(main_graph_->output(), out_node);
    return;
  }
  send_tag_map_.clear();
  recv_tag_map_.clear();
  if (!IsLastStage()) {
    HandleGraphOutputs(send_ops);
  }
  std::vector<AnfNodePtr> recv_ops;
  (void)(recv_ops.insert(recv_ops.end(), send_recv_shared_param.second.begin(), send_recv_shared_param.second.end()));
  (void)(recv_ops.insert(recv_ops.end(), send_recv_cut_border.second.begin(), send_recv_cut_border.second.end()));
  HandleGraphInputs(recv_ops);
}

void PipelineTransformer::ElimGraphStage() {
  for (auto &fg : manager_->func_graphs()) {
    fg->set_stage(-1);
    fg->set_segment(-1);
  }
}

void PipelineTransformer::RedundancyNode(const AnfNodePtr &node,
                                         mindspore::HashMap<CNodePtr, std::vector<AnfNodePtr>> *make_tuple_map) {
  auto node_users = manager_->node_users()[node];
  for (auto &node_user_pair : node_users) {
    auto cnode = node_user_pair.first->cast<CNodePtr>();
    // node->UpdateState, replaced node wiht U.
    auto fg = cnode->func_graph();
    MS_EXCEPTION_IF_NULL(fg);
    if (fg->stage() != -1 && fg != main_graph_) {
      continue;
    }
    if (IsPrimitiveCNode(cnode, prim::kPrimUpdateState)) {
      auto u_node = NewValueNode(kUMonad);
      manager_->SetEdge(cnode, node_user_pair.second, u_node);
      continue;
    }
    // node->make_tuple, record with a map, Unified deleted later.
    if (IsPrimitiveCNode(cnode, prim::kPrimMakeTuple)) {
      if (make_tuple_map->find(cnode) == (*make_tuple_map).end()) {
        (*make_tuple_map)[cnode] = {node};
      } else {
        (*make_tuple_map)[cnode].push_back(node);
      }
    } else {
      RedundancyNode(node_user_pair.first, make_tuple_map);
    }
  }
}

bool PipelineTransformer::IsRedundancyParameter(const AnfNodePtr &parameter,
                                                const std::vector<AnfNodePtr> &non_cloned_parameters) {
  // RedundancyParameter: other stage's parameters included corresponding cloned parameters.
  auto param_ptr = parameter->cast<ParameterPtr>();
  MS_EXCEPTION_IF_NULL(param_ptr);
  if (!param_ptr->has_default()) {
    return false;
  }
  std::set<int64_t> stage_set;
  if (!ParameterIsCloned(parameter)) {
    stage_set = parameter_color_map_.at(parameter);
  } else {
    auto parameters = root_->parameters();
    auto param_name = param_ptr->name();
    auto non_clone_name = param_name.substr(param_name.find_first_of('.') + 1);
    for (auto &param : non_cloned_parameters) {
      auto non_cloned_param = param->cast<ParameterPtr>();
      if (non_clone_name != non_cloned_param->name()) {
        continue;
      }
      stage_set = parameter_color_map_.at(param);
      break;
    }
  }
  if (stage_set.empty()) {
    return false;
  }
  return stage_set.count(stage_) == 0;
}

void PipelineTransformer::ElimParameter() {
  auto parameters = root_->parameters();
  mindspore::HashMap<CNodePtr, std::vector<AnfNodePtr>> make_tuple_map;
  std::vector<AnfNodePtr> non_cloned_parameters;
  for (auto &parameter : parameters) {
    if (ParameterIsCloned(parameter)) {
      continue;
    }
    non_cloned_parameters.push_back(parameter);
  }
  for (auto &parameter : parameters) {
    if (!IsRedundancyParameter(parameter, non_cloned_parameters)) {
      continue;
    }
    MS_LOG(INFO) << "Parameter:" << parameter->DebugString() << " is Redundancy.";
    RedundancyNode(parameter, &make_tuple_map);
  }
  for (auto &temp : make_tuple_map) {
    auto make_tuple = temp.first;
    auto fg = make_tuple->func_graph();
    MS_EXCEPTION_IF_NULL(fg);
    auto remove_vector = temp.second;
    if (remove_vector.empty()) {
      continue;
    }
    auto make_tuple_inputs = make_tuple->inputs();
    std::vector<AnfNodePtr> new_inputs;
    for (auto &input : make_tuple_inputs) {
      if (std::find(remove_vector.begin(), remove_vector.end(), input) == remove_vector.end()) {
        new_inputs.push_back(input);
      }
    }
    auto new_make_tuple = fg->NewCNode(new_inputs);
    (void)manager_->Replace(make_tuple, new_make_tuple);
  }
}

void PipelineTransformer::ModifyParameterList() {
  ElimParameter();
  auto parameters = root_->parameters();
  std::vector<AnfNodePtr> parameter_list;
  for (auto &parameter : parameters) {
    auto param = parameter->cast<ParameterPtr>();
    MS_EXCEPTION_IF_NULL(param);
    if (!manager_->node_users()[parameter].empty() || !param->has_default()) {
      parameter_list.push_back(parameter);
    }
  }
  auto del_num = parameters.size() - parameter_list.size();
  root_->set_fv_param_count(root_->fv_param_count() - del_num);
  manager_->SetParameters(root_, parameter_list);
}
}  // namespace parallel
}  // namespace mindspore
