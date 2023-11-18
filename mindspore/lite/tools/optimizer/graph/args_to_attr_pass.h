/**
 * Copyright 2023 Huawei Technologies Co., Ltd
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

#ifndef MINDSPORE_LITE_TOOLS_OPTIMIZER_GRAPH_ARGS_TO_ATTR_PASS_H_
#define MINDSPORE_LITE_TOOLS_OPTIMIZER_GRAPH_ARGS_TO_ATTR_PASS_H_

#include <map>
#include <memory>
#include <string>
#include <vector>
#include "include/backend/optimizer/pass.h"

namespace mindspore {
namespace opt {
class ArgsToAttrPass : public Pass {
 public:
  explicit ArgsToAttrPass(const std::string &name = "InferShapePass") : Pass(name) {}
  ~ArgsToAttrPass() override = default;
  bool Run(const FuncGraphPtr &func_graph) override;
};
}  // namespace opt
}  // namespace mindspore
#endif  // MINDSPORE_LITE_TOOLS_OPTIMIZER_GRAPH_ARGS_TO_ATTR_PASS_H_
