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
#ifndef MINDSPORE_CCSRC_PIPELINE_JIT_PI_GRAPH_CAPTURE_LOCAL_LIVENESS_H
#define MINDSPORE_CCSRC_PIPELINE_JIT_PI_GRAPH_CAPTURE_LOCAL_LIVENESS_H

#define _GLIBCXX_ASSERTIONS 1

#include <vector>
#include "pipeline/jit/pi/utils/bitmap.h"

namespace mindspore {
namespace jit {
namespace graph {

class Instr;
class Block;
class CFG;

class Liveness {
 public:
  Liveness(const CFG *cfg) : cfg_(cfg) {}

  void Init();
  BitMap CollectAlive(int start_bci) const;

  static void BuildRW(const Instr &instr, BitMap *read, BitMap *write);

 private:
  void Propagate(Block *cur, std::vector<Block *> *list);
  const CFG *const cfg_;

  std::vector<BitMap> read_;
  std::vector<BitMap> write_;
  std::vector<BitMap> alive_;
  std::vector<BitMap> alive_effect_;
};

}  // namespace graph
}  // namespace jit
}  // namespace mindspore

#endif
