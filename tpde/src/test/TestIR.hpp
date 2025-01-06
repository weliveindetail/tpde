// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "tpde/base.hpp"

namespace tpde::test {

struct TestIR {
  struct Value {
    enum class Type : u8 {
      normal,
      arg,
      phi,
      terminator,
    };

    enum class Op : u8 {
      none,
      any,
      add,
      sub,
      alloca,
      terminate,
      ret,
      br,
      condbr,
      jump,
      call,
    };

    struct OpInfo {
      std::string_view name;
      bool is_terminator;
      bool is_def;
      u32 op_count;
      u32 succ_count;
      u32 imm_count;
    };

    inline static constexpr OpInfo OP_INFOS[] = {
        // name       term   def    ops succ imm
        {   "<none>", false, false,   0,   0, 0},
        {      "any", false,  true, ~0u,   0, 0},
        {      "add", false,  true,   2,   0, 0},
        {      "sub", false,  true,   2,   0, 0},
        {   "alloca", false,  true,   0,   0, 2},
        {"terminate",  true, false,   0,   0, 0},
        {      "ret",  true, false,   1,   0, 0},
        {       "br",  true, false,   0,   1, 0},
        {   "condbr",  true, false,   1,   2, 0},
        {     "jump",  true, false,   0, ~0u, 0},
        {     "call", false,  true, ~0u,   0, 0},
    };

    std::string name;
    Type type;
    Op op = Op::none;
    bool force_fixed_assignment = false;
    /// For call only: called function
    u32 call_func_idx;
    /// Number of value operands
    u32 op_count;

    // op_count value operands first, then blocks, then constants
    u32 op_begin_idx, op_end_idx;

    Value(Type type, std::string_view name) : name(name), type(type) {}
  };

  struct Block {
    std::string name;
    u32 succ_begin_idx = 0, succ_end_idx = 0;
    u32 inst_begin_idx = 0, inst_end_idx = 0;
    bool has_sibling = false;
    u32 block_info = 0, block_info2 = 0;
  };

  struct Function {
    std::string name;
    bool declaration = false;
    bool local_only = false;
    bool has_call = false;
    u32 block_begin_idx = 0, block_end_idx = 0;
    u32 arg_begin_idx = 0, arg_end_idx = 0;
  };

  std::vector<Value> values;
  std::vector<u32> value_operands;
  std::vector<Block> blocks;
  std::vector<Function> functions;

  [[nodiscard]] bool parse_ir(std::string_view text) noexcept;

  void dump_debug() const noexcept;
  void print() const noexcept;
};

struct TestIRAdaptor {
  TestIR *ir;

  explicit TestIRAdaptor(TestIR *ir) : ir(ir) {}

  enum class IRValueRef : u32 {
  };

  /*constexpr friend bool operator<(const IRValueRef &lhs,
                                  const IRValueRef &rhs) {
      return static_cast<u32>(lhs) < static_cast<u32>(rhs);
  }*/

  enum class IRBlockRef : u32 {
  };
  enum class IRFuncRef : u32 {
  };

  static constexpr IRValueRef INVALID_VALUE_REF = static_cast<IRValueRef>(~0u);
  static constexpr IRBlockRef INVALID_BLOCK_REF = static_cast<IRBlockRef>(~0u);
  static constexpr IRFuncRef INVALID_FUNC_REF = static_cast<IRFuncRef>(~0u);

  static constexpr bool TPDE_PROVIDES_HIGHEST_VAL_IDX = true;
  u32 highest_local_val_idx;

  static constexpr bool TPDE_LIVENESS_VISIT_ARGS = true;

  [[nodiscard]] u32 func_count() const noexcept {
    return static_cast<u32>(ir->functions.size());
  }

  auto funcs() const noexcept {
    struct Range {
      struct Iter {
        u32 val;

        Iter &operator++() {
          ++val;
          return *this;
        }

        bool operator!=(const Iter &other) const noexcept {
          return other.val != val;
        }

        IRFuncRef operator*() const noexcept {
          return static_cast<IRFuncRef>(val);
        }
      };

      u32 func_count;

      [[nodiscard]] Iter begin() const noexcept { return Iter{0}; }

      [[nodiscard]] Iter end() const noexcept { return Iter{func_count}; }
    };

    return Range{.func_count = static_cast<u32>(ir->functions.size())};
  }

  [[nodiscard]] auto funcs_to_compile() const noexcept { return funcs(); }

  [[nodiscard]] std::string_view
      func_link_name(const IRFuncRef func) const noexcept {
    return ir->functions[static_cast<u32>(func)].name;
  }

  [[nodiscard]] bool func_extern(const IRFuncRef func) const noexcept {
    return ir->functions[static_cast<u32>(func)].declaration;
  }

  [[nodiscard]] bool func_only_local(const IRFuncRef func) const noexcept {
    return ir->functions[static_cast<u32>(func)].local_only;
  }

  [[nodiscard]] bool func_has_weak_linkage(const IRFuncRef) const noexcept {
    return false;
  }

  u32 cur_func;

  [[nodiscard]] bool cur_needs_unwind_info() const noexcept { return false; }

  [[nodiscard]] bool cur_is_vararg() const noexcept { return false; }

  [[nodiscard]] u32 cur_highest_val_idx() const noexcept {
    return highest_local_val_idx;
  }

  [[nodiscard]] IRFuncRef cur_personality_func() const noexcept {
    return INVALID_FUNC_REF;
  }

  [[nodiscard]] auto cur_args() const noexcept {
    struct Range {
      const u32 beg, last;

      struct Iter {
        u32 val;

        Iter &operator++() {
          ++val;
          return *this;
        }

        bool operator!=(const Iter &other) const noexcept {
          return other.val != val;
        }

        IRValueRef operator*() const noexcept {
          return static_cast<IRValueRef>(val);
        }
      };

      [[nodiscard]] Iter begin() const noexcept { return Iter{beg}; }

      [[nodiscard]] Iter end() const noexcept { return Iter{last}; }
    };

    const auto &func = ir->functions[cur_func];
    return Range{.beg = func.arg_begin_idx, .last = func.arg_end_idx};
  }

  [[nodiscard]] static bool cur_arg_is_byval(u32) noexcept { return false; }

  [[nodiscard]] static u32 cur_arg_byval_align(u32) noexcept { return 0; }

  [[nodiscard]] static u32 cur_arg_byval_size(u32) noexcept { return 0; }

  [[nodiscard]] static bool cur_arg_is_sret(u32) noexcept { return false; }

  [[nodiscard]] auto cur_static_allocas() const noexcept {
    struct Range {
      const TestIR *self;
      const u32 val_idx_beg, val_idx_end;

      struct Iter {
        const TestIR *self;
        u32 val, val_idx_end;

        Iter &operator++() noexcept {
          if (val == val_idx_end) {
            return *this;
          }

          while (val != val_idx_end) {
            ++val;

            if (self->values[val].op == TestIR::Value::Op::alloca) {
              break;
            }
          }
          return *this;
        }

        bool operator!=(const Iter &other) const noexcept {
          assert(other.self == self);
          assert(other.val_idx_end == val_idx_end);
          return other.val != val;
        }

        IRValueRef operator*() const noexcept {
          return static_cast<IRValueRef>(val);
        }
      };

      [[nodiscard]] Iter begin() const noexcept {
        return Iter{
            .self = self, .val = val_idx_beg, .val_idx_end = val_idx_end};
      }

      [[nodiscard]] Iter end() const noexcept {
        return Iter{
            .self = self, .val = val_idx_end, .val_idx_end = val_idx_end};
      }
    };

    const auto &entry = ir->blocks[ir->functions[cur_func].block_begin_idx];
    assert(ir->functions[cur_func].block_begin_idx !=
           ir->functions[cur_func].block_end_idx);

    u32 first_alloca = entry.inst_begin_idx;
    while (first_alloca != entry.inst_end_idx) {
      if (ir->values[first_alloca].op == TestIR::Value::Op::alloca) {
        break;
      }
      ++first_alloca;
    }
    return Range{ir, first_alloca, entry.inst_end_idx};
  }

  [[nodiscard]] static bool cur_has_dynamic_alloca() noexcept { return false; }

  [[nodiscard]] IRBlockRef cur_entry_block() const noexcept {
    const auto &func = ir->functions[cur_func];
    assert(ir->functions[cur_func].block_begin_idx !=
           ir->functions[cur_func].block_end_idx);

    return static_cast<IRBlockRef>(func.block_begin_idx);
  }

  [[nodiscard]] IRBlockRef
      block_sibling(const IRBlockRef block) const noexcept {
    if (ir->blocks[static_cast<u32>(block)].has_sibling) {
      return static_cast<IRBlockRef>(static_cast<u32>(block) + 1);
    } else {
      return INVALID_BLOCK_REF;
    }
  }

  [[nodiscard]] auto block_succs(const IRBlockRef block) const noexcept {
    struct Range {
      const u32 *beg, *last;

      struct Iter {
        const u32 *val;

        Iter &operator++() {
          ++val;
          return *this;
        }

        bool operator!=(const Iter &other) const noexcept {
          return other.val != val;
        }

        IRBlockRef operator*() const noexcept {
          return static_cast<IRBlockRef>(*val);
        }
      };

      [[nodiscard]] Iter begin() const noexcept { return Iter{beg}; }

      [[nodiscard]] Iter end() const noexcept { return Iter{last}; }
    };

    const auto &info = ir->blocks[static_cast<u32>(block)];
    const auto *data = ir->value_operands.data();
    return Range{data + info.succ_begin_idx, data + info.succ_end_idx};
  }

  struct BlockInstRange {
    const u32 beg, last;

    struct Iter {
      u32 val;

      Iter &operator++() {
        ++val;
        return *this;
      }

      bool operator!=(const Iter &other) const noexcept {
        return other.val != val;
      }

      IRValueRef operator*() const noexcept {
        return static_cast<IRValueRef>(val);
      }
    };

    [[nodiscard]] Iter begin() const noexcept { return Iter{beg}; }

    [[nodiscard]] Iter end() const noexcept { return Iter{last}; }
  };

  using IRInstIter = BlockInstRange::Iter;

  [[nodiscard]] auto block_values(const IRBlockRef block) const noexcept {
    const auto &info = ir->blocks[static_cast<u32>(block)];
    return BlockInstRange{info.inst_begin_idx, info.inst_end_idx};
  }

  [[nodiscard]] auto block_phis(const IRBlockRef block) const noexcept {
    struct Range {
      const TestIR *self;
      const u32 val_idx_beg, val_idx_end;

      struct Iter {
        const TestIR *self;
        u32 val, val_idx_end;

        Iter &operator++() noexcept {
          if (val == val_idx_end) {
            return *this;
          }

          while (val != val_idx_end) {
            ++val;

            if (self->values[val].type == TestIR::Value::Type::phi) {
              break;
            } else {
              val = val_idx_end;
              break;
            }
          }
          return *this;
        }

        bool operator!=(const Iter &other) const noexcept {
          assert(other.self == self);
          assert(other.val_idx_end == val_idx_end);
          return other.val != val;
        }

        IRValueRef operator*() const noexcept {
          return static_cast<IRValueRef>(val);
        }
      };

      [[nodiscard]] Iter begin() const noexcept {
        return Iter{
            .self = self, .val = val_idx_beg, .val_idx_end = val_idx_end};
      }

      [[nodiscard]] Iter end() const noexcept {
        return Iter{
            .self = self, .val = val_idx_end, .val_idx_end = val_idx_end};
      }
    };

    const auto &info = ir->blocks[static_cast<u32>(block)];
    if (info.inst_begin_idx == info.inst_end_idx ||
        ir->values[info.inst_begin_idx].type != TestIR::Value::Type::phi) {
      return Range{ir, info.inst_end_idx, info.inst_end_idx};
    } else {
      return Range{ir, info.inst_begin_idx, info.inst_end_idx};
    }
  }

  [[nodiscard]] u32 block_info(IRBlockRef block) const noexcept {
    return ir->blocks[static_cast<u32>(block)].block_info;
  }

  void block_set_info(IRBlockRef block, u32 value) noexcept {
    ir->blocks[static_cast<u32>(block)].block_info = value;
  }

  [[nodiscard]] u32 block_info2(IRBlockRef block) const noexcept {
    return ir->blocks[static_cast<u32>(block)].block_info2;
  }

  void block_set_info2(IRBlockRef block, u32 value) noexcept {
    ir->blocks[static_cast<u32>(block)].block_info2 = value;
  }

  [[nodiscard]] std::string_view
      block_fmt_ref(IRBlockRef block) const noexcept {
    return ir->blocks[static_cast<u32>(block)].name;
  }

  [[nodiscard]] std::string_view value_fmt_ref(IRValueRef val) const noexcept {
    return ir->values[static_cast<u32>(val)].name;
  }

  [[nodiscard]] u32 val_local_idx(IRValueRef val) {
    assert(static_cast<u32>(val) >= ir->functions[cur_func].arg_begin_idx);
    return static_cast<u32>(val) - ir->functions[cur_func].arg_begin_idx;
  }

  [[nodiscard]] auto val_operands(IRValueRef val) {
    struct Range {
      const u32 *beg, *last;

      struct Iter {
        const u32 *val;

        Iter &operator++() {
          ++val;
          return *this;
        }

        bool operator!=(const Iter &other) const noexcept {
          return other.val != val;
        }

        IRValueRef operator*() const noexcept {
          return static_cast<IRValueRef>(*val);
        }
      };

      [[nodiscard]] Iter begin() const noexcept { return Iter{beg}; }

      [[nodiscard]] Iter end() const noexcept { return Iter{last}; }
    };

    const auto &info = ir->values[static_cast<u32>(val)];
    const auto *data = ir->value_operands.data();
    return Range(data + info.op_begin_idx,
                 data + info.op_begin_idx + info.op_count);
  }

  [[nodiscard]] bool
      val_ignore_in_liveness_analysis(IRValueRef value) const noexcept {
    return ir->values[static_cast<u32>(value)].op == TestIR::Value::Op::alloca;
  }

  [[nodiscard]] bool val_produces_result(IRValueRef value) const noexcept {
    const auto &info = ir->values[static_cast<u32>(value)];
    return TestIR::Value::OP_INFOS[static_cast<u32>(info.op)].is_def;
  }

  static bool val_fused(IRValueRef) noexcept { return false; }

  [[nodiscard]] bool val_is_arg(IRValueRef value) const noexcept {
    return ir->values[static_cast<u32>(value)].type == TestIR::Value::Type::arg;
  }

  [[nodiscard]] bool val_is_phi(IRValueRef value) const noexcept {
    return ir->values[static_cast<u32>(value)].type == TestIR::Value::Type::phi;
  }

  [[nodiscard]] auto val_as_phi(IRValueRef value) const noexcept {
    struct PHIRef {
      const u32 *op_begin, *block_begin;

      [[nodiscard]] u32 incoming_count() const noexcept {
        return block_begin - op_begin;
      }

      [[nodiscard]] IRValueRef
          incoming_val_for_slot(const u32 slot) const noexcept {
        assert(slot < incoming_count());
        return static_cast<IRValueRef>(op_begin[slot]);
      }

      [[nodiscard]] IRBlockRef
          incoming_block_for_slot(const u32 slot) const noexcept {
        assert(slot < incoming_count());
        return static_cast<IRBlockRef>(block_begin[slot]);
      }

      [[nodiscard]] IRValueRef
          incoming_val_for_block(const IRBlockRef block_ref) const noexcept {
        const auto block = static_cast<u32>(block_ref);
        for (auto *op = op_begin; op < block_begin; ++op) {
          if (block_begin[op - op_begin] == block) {
            return static_cast<IRValueRef>(*op);
          }
        }

        return INVALID_VALUE_REF;
      }
    };

    const auto val_idx = static_cast<u32>(value);
    assert(ir->values[val_idx].type == TestIR::Value::Type::phi);
    const auto &info = ir->values[val_idx];
    const auto *data = ir->value_operands.data();
    return PHIRef{data + info.op_begin_idx,
                  data + info.op_begin_idx + info.op_count};
  }

  [[nodiscard]] u32 val_alloca_size(IRValueRef value) const noexcept {
    const auto val_idx = static_cast<u32>(value);
    assert(ir->values[val_idx].op == TestIR::Value::Op::alloca);
    const auto *data = ir->value_operands.data();
    return data[ir->values[val_idx].op_begin_idx];
  }

  [[nodiscard]] u32 val_alloca_align(IRValueRef value) const noexcept {
    const auto val_idx = static_cast<u32>(value);
    assert(ir->values[val_idx].op == TestIR::Value::Op::alloca);
    const auto *data = ir->value_operands.data();
    return data[ir->values[val_idx].op_begin_idx + 1];
  }

  void start_compile() const noexcept {}

  void end_compile() const noexcept {}

  bool switch_func(IRFuncRef func) noexcept {
    cur_func = static_cast<u32>(func);

    highest_local_val_idx = 0;
    const auto &info = ir->functions[cur_func];
    assert(info.block_begin_idx != info.block_end_idx);

    highest_local_val_idx =
        ir->blocks[info.block_end_idx - 1].inst_end_idx - info.arg_begin_idx;
    if (highest_local_val_idx > 0) {
      --highest_local_val_idx;
    }
    return true;
  }

  void reset() { highest_local_val_idx = 0; }
};

} // namespace tpde::test
