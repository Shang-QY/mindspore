/**
 * This is the C++ adaptation and derivative work of Myia (https://github.com/mila-iqia/myia/).
 *
 * Copyright 2020-2022 Huawei Technologies Co., Ltd
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

#ifndef MINDSPORE_CCSRC_FRONTEND_OPERATOR_COMPOSITE_MULTITYPE_FUNCGRAPH_H_
#define MINDSPORE_CCSRC_FRONTEND_OPERATOR_COMPOSITE_MULTITYPE_FUNCGRAPH_H_

#include <vector>
#include <string>
#include <tuple>
#include <utility>
#include <map>
#include <set>
#include <memory>
#include <algorithm>
#include "pipeline/jit/static_analysis/static_analysis.h"
#include "utils/misc.h"
#include "ir/dtype.h"
#include "ir/meta_func_graph.h"

namespace mindspore {
namespace prim {
class MultitypeFuncGraph : public MetaFuncGraph {
 public:
  explicit MultitypeFuncGraph(const std::string &name, const std::string &doc_url);
  ~MultitypeFuncGraph() override = default;
  MS_DECLARE_PARENT(MultitypeFuncGraph, MetaFuncGraph)

  using specialize_fn = FuncGraph *(*)(TypePtrList);
  // Register a method which specialize based on types vectors.
  virtual void Register(const TypePtrList &types, specialize_fn s_fn);
  virtual void Register(const TypePtrList &types, const py::function &py_fn);
  virtual void PyRegister(const py::tuple &tuple, const py::function &py_fn);

  FuncGraphPtr GenerateFromTypes(const TypePtrList &types) override;
  size_t GetPyFnCacheSize() const { return fn_cache_py_.size(); }
  const TypeListMap<py::function> &GetPyFunctions() const { return fn_cache_py_; }

 private:
  const std::tuple<py::function, bool, size_t> SignMatch(const TypePtrList &types);
  const std::string PrintMatchFailLog(const TypeListMap<py::function>, const TypePtrList &types, size_t match_max_idx);
  TypeListMap<specialize_fn> fn_cache_;
  TypeListMap<py::function> fn_cache_py_;
  std::string doc_url_;
};
using MultitypeFuncGraphPtr = std::shared_ptr<MultitypeFuncGraph>;
}  // namespace prim
}  // namespace mindspore

#endif  // MINDSPORE_CCSRC_FRONTEND_OPERATOR_COMPOSITE_H_
