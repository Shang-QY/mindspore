/**
 * Copyright 2021 Huawei Technologies Co., Ltd
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

#ifndef ACL_MAPPER_PRIMITIVE_MAPPER_H
#define ACL_MAPPER_PRIMITIVE_MAPPER_H

#include <string>
#include <memory>
#include "base/base.h"
#include "include/errorcode.h"
#include "ir/anf.h"

namespace mindspore {
namespace lite {
class PrimitiveMapper {
 public:
  explicit PrimitiveMapper(const std::string &name) : name_(name) {}

  virtual ~PrimitiveMapper() = default;

  virtual STATUS Mapper(const CNodePtr &cnode);

 protected:
  STATUS AttrAdjust(const PrimitivePtr &prim, const std::string &name);

  STATUS MoveAttrMap(const CNodePtr &cnode, const PrimitivePtr &dst_prim);

  STATUS GetValueNodeAndPrimFromCnode(const CNodePtr &cnode, ValueNodePtr *value_node, PrimitivePtr *prim_ptr);

  STATUS AdjustPoolAttr(int fmk_type, const std::string &src_prim_name, const PrimitivePtr &dst_prim);

  STATUS AddAttrToInput(const FuncGraphPtr &func_graph, const CNodePtr &cnode, const PrimitivePtr &dst_prim,
                        const std::string &attr_name, size_t flag);

  STATUS AddAttrForDynInputPrimitive(const CNodePtr &cnode, const std::string &attr_name);

 private:
  void AdjustCaffePoolAttr(const std::string &src_prim_name, const PrimitivePtr &dst_prim);

  void AdjustOnnxPoolAttr(const PrimitivePtr &dst_prim);

  std::string name_;
};

using PrimitiveMapperPtr = std::shared_ptr<PrimitiveMapper>;
}  // namespace lite
}  // namespace mindspore
#endif  // ACL_MAPPER_PRIMITIVE_MAPPER_H
