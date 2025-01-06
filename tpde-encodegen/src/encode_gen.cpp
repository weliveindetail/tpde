// SPDX-FileCopyrightText: 2024 Tobias Kamm <tobias.kamm@tum.de>
// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary

#include "encode_gen.hpp"


#include <format>
#include <iostream>
#include <string>
#include <unordered_set>

#include <llvm/CodeGen/AsmPrinter.h>
#include <llvm/CodeGen/LivePhysRegs.h>
#include <llvm/CodeGen/MachineConstantPool.h>
#include <llvm/CodeGen/MachineRegisterInfo.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/MC/MCAsmBackend.h>
#include <llvm/MC/MCCodeEmitter.h>
#include <llvm/MC/MCContext.h>
#include <llvm/MC/MCInstrInfo.h>
#include <llvm/MC/MCObjectWriter.h>
#include <llvm/MC/MCStreamer.h>
#include <llvm/MC/SectionKind.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Target/TargetLoweringObjectFile.h>
#include <llvm/Target/TargetMachine.h>

#include "Target.hpp"
#include "arm64/Target.hpp"
#include "encode_gen.hpp"
#include "x64/Target.hpp"

using namespace tpde_encgen;

namespace {

struct ValueInfo {
  enum TYPE {
    SCRATCHREG,
    ASM_OPERAND,
    REG_ALIAS
  };

  TYPE ty;
  std::string operand_name;
  unsigned alias_reg_id;
  unsigned alias_size;
  // llvm::MachineRegisterInfo::reg_instr_nodbg_iterator cur_def_use_it;
  // registers aliased to this value
  std::unordered_set<unsigned> aliased_regs = {};

  bool is_dead = false;
  // TODO(ts): this has questionable implications when we modify them in if
  // statements as we would need to consolidate their state across the
  // different branches so for now we can simply move this to the runtime
  // scratch register state
  // bool scratch_allocated = false, scratch_destroyed = false;
};

static unsigned reg_size_bytes(llvm::MachineFunction *func,
                               llvm::Register reg) {
  const auto &target_reg_info = func->getSubtarget().getRegisterInfo();
  const auto &mach_reg_info = func->getRegInfo();
  return target_reg_info->getRegSizeInBits(reg, mach_reg_info) / 8;
}

struct GenerationState {
  llvm::MachineFunction *func;
  EncodingTarget *target;
  std::vector<std::string> param_names{};
  std::unordered_set<unsigned> used_regs{};
  llvm::DenseMap<unsigned, ValueInfo> value_map{};
  std::unordered_map<std::string, unsigned> operand_ref_counts{};
  unsigned cur_inst_id = 0;
  int num_ret_regs = -1;
  bool is_first_block = false;
  std::vector<unsigned> return_regs = {};
  std::string one_bb_return_assignments = {};
  std::unordered_set<unsigned> const_pool_indices_used = {};

  // Conditions on which fixed registers depend
  std::vector<std::string> asm_operand_early_conds;
  unsigned num_conds = 0;

  llvm::DenseMap<unsigned, std::vector<std::string>> fixed_reg_conds{};
  // TODO(ts): we could precompute the conditions to get even better
  // knowledge about this
  std::unordered_set<unsigned> maybe_fixed_regs{};

  template <typename... T>
  void fmt_line(std::string &buf,
                unsigned indent,
                std::format_string<T...> fmt,
                T &&...args) {
    std::format_to(std::back_inserter(buf), "{:>{}}", "", indent);
    std::format_to(std::back_inserter(buf), fmt, std::forward<T>(args)...);
    buf += '\n';
  }

  void remove_reg_alias(std::string &buf, unsigned reg, unsigned aliased_reg);
  /// This function will update all registers that alias this register to
  /// point to the target of this register (either an AsmOperand or another
  /// register)
  ///
  /// \note reg must be a REGISTER_ALIAS or ASM_OPERAND
  void redirect_aliased_regs_to_parent(unsigned reg);
  unsigned resolve_reg_alias(std::string &buf, unsigned indent, unsigned);

  bool can_salvage_operand(const llvm::MachineOperand &op);
};

bool handle_move(std::string &buf,
                 GenerationState &state,
                 llvm::MachineOperand &src_op,
                 llvm::MachineOperand &dst_op);
bool handle_terminator(std::string &buf,
                       GenerationState &state,
                       llvm::MachineInstr *inst);

static void materialize_aliased_regs(GenerationState &state,
                                     std::string &buf,
                                     unsigned indent,
                                     unsigned break_reg,
                                     unsigned reg_size);

bool const_to_bytes(const llvm::DataLayout &dl,
                    const llvm::Constant *constant,
                    std::vector<uint8_t> &bytes,
                    const unsigned off) {
  const auto alloc_size = dl.getTypeAllocSize(constant->getType());
  // can't do this since for floats -0 is special
  // if (constant->isZeroValue()) {
  //    return true;
  //}

  if (auto *CI = llvm::dyn_cast<llvm::ConstantInt>(constant); CI) {
    // TODO: endianess?
    llvm::StoreIntToMemory(CI->getValue(), bytes.data() + off, alloc_size);
    return true;
  }
  if (auto *CF = llvm::dyn_cast<llvm::ConstantFP>(constant); CF) {
    // TODO: endianess?
    llvm::StoreIntToMemory(
        CF->getValue().bitcastToAPInt(), bytes.data() + off, alloc_size);
    return true;
  }
  if (auto *CDS = llvm::dyn_cast<llvm::ConstantDataSequential>(constant); CDS) {
    auto d = CDS->getRawDataValues();
    assert(d.size() <= alloc_size);
    std::copy(d.bytes_begin(), d.bytes_end(), bytes.begin() + off);
    return true;
  }
  if (auto *CA = llvm::dyn_cast<llvm::ConstantArray>(constant); CA) {
    const auto num_elements = CA->getType()->getNumElements();
    const auto element_size =
        dl.getTypeAllocSize(CA->getType()->getElementType());
    for (auto i = 0u; i < num_elements; ++i) {
      const_to_bytes(
          dl, CA->getAggregateElement(i), bytes, off + i * element_size);
    }
    return true;
  }
  if (auto *CA = llvm::dyn_cast<llvm::ConstantAggregate>(constant); CA) {
    const auto num_elements = CA->getType()->getStructNumElements();
    auto &ctx = constant->getContext();

    auto *ty = CA->getType();
    auto c0 = llvm::ConstantInt::get(ctx, llvm::APInt(32, 0, false));

    for (auto i = 0u; i < num_elements; ++i) {
      auto idx = llvm::ConstantInt::get(ctx, llvm::APInt(32, i, false));
      auto agg_off = dl.getIndexedOffsetInType(ty, {c0, idx});
      const_to_bytes(dl, CA->getAggregateElement(i), bytes, off + agg_off);
    }
    return true;
  }
  if (auto *ud = llvm::dyn_cast<llvm::UndefValue>(constant); ud) {
    // just leave it at 0
    return true;
  }

  std::cerr << "ERROR: Encountered unsupported constant in constant pool\n";
  return false;
}

bool generate_cp_entry_sym(GenerationState &state,
                           std::string &buf,
                           unsigned indent,
                           std::string_view sym_name,
                           unsigned cp_idx) {
  state.const_pool_indices_used.insert(cp_idx);

  const llvm::MachineConstantPoolEntry &cp_entry =
      state.func->getConstantPool()->getConstants()[cp_idx];

  if (cp_entry.isMachineConstantPoolEntry()) {
    std::cerr << "ERROR: encountered MachineConstantPoolEntry\n";
    return false;
  }

  if (cp_entry.needsRelocation()) {
    std::cerr
        << "ERROR: encountered constant pool entry that needs relocation\n";
    return false;
  }

  const auto *constant = cp_entry.Val.ConstVal;
  const auto size = cp_entry.getSizeInBytes(state.func->getDataLayout());
  // const auto sec_kind =
  // cp_entry.getSectionKind(&state.func->getDataLayout());
  const auto align = cp_entry.getAlign().value();

  std::vector<uint8_t> bytes{};
  bytes.resize(size);

  if (!const_to_bytes(state.func->getDataLayout(), constant, bytes, 0)) {
    std::cerr << "ERROR: could not convert constant to bytes\n";
    return false;
  }

  std::string bytes_str = "{";
  for (const auto byte : bytes) {
    if (bytes_str.size() != 1) {
      bytes_str += ", ";
    }
    bytes_str += std::format("0x{:X}", byte);
  }
  bytes_str += "}";

  state.fmt_line(buf,
                 indent,
                 "SymRef &{} = this->sym_{}_cp{};",
                 sym_name,
                 state.func->getName().str(),
                 cp_idx);
  state.fmt_line(buf,
                 indent,
                 "if ({} == Assembler::INVALID_SYM_REF) [[unlikely]] {{",
                 sym_name);
  // TODO(ts): make this static so it does not get stack allocated?
  state.fmt_line(buf,
                 indent + 4,
                 "const std::array<u8, {}> data = {};",
                 bytes.size(),
                 bytes_str);
  state.fmt_line(buf,
                 indent + 4,
                 "auto sec = derived()->assembler.get_data_section(true);",
                 sym_name,
                 align);
  state.fmt_line(buf,
                 indent + 4,
                 "{} = derived()->assembler.sym_def_data(sec, \"\", data, {}, "
                 "Assembler::SymBinding::LOCAL);",
                 sym_name,
                 align);
  state.fmt_line(buf, indent, "}}");

  return true;
}

bool generate_inst(std::string &buf,
                   GenerationState &state,
                   llvm::MachineInstr *inst) {
  (void)buf;
  (void)state;

  // check if this is a move
  if (auto move_ops = state.target->is_move(*inst)) {
    // move without side-effects, e.g. zero-extension
    auto &dst_op = inst->getOperand(move_ops->first);
    auto &src_op = inst->getOperand(move_ops->second);
    return handle_move(buf, state, src_op, dst_op);
  }

  if (inst->isTerminator()) {
    return handle_terminator(buf, state, inst);
  }

  if (inst->isPseudo()) {
    if (inst->isKill()) {
      assert(inst->getOperand(0).isReg() && inst->getOperand(1).isReg());
      if (state.target->reg_id_from_mc_reg(inst->getOperand(0).getReg()) !=
          state.target->reg_id_from_mc_reg(inst->getOperand(1).getReg())) {
        std::cerr << "ERROR: Found KILL instruction with different "
                     "source and destination register\n";
        assert(0);
        return false;
      }
      state.fmt_line(buf, 4, "// KILL is a no-op");
      return true;
    } else {
      std::string buf{};
      llvm::raw_string_ostream os(buf);
      inst->print(os);
      std::cerr << std::format("ERROR: Encountered unknown instruction '{}'\n",
                               buf);
      assert(0);
      return false;
    }
  }

  if (state.target->inst_should_be_skipped(*inst)) {
    state.fmt_line(buf, 4, "// Skipping");
    return true;
  }

#if 0
    auto       &tm      = state.func->getTarget();
    const auto &target  = tm.getTarget();
    auto        context = llvm::MCContext{tm.getTargetTriple(),
                                   tm.getMCAsmInfo(),
                                   tm.getMCRegisterInfo(),
                                   tm.getMCSubtargetInfo(),
                                   nullptr,
                                   &tm.Options.MCOptions};
    tm.getObjFileLowering()->Initialize(context, tm);
    auto emitter = state.func->getTarget().getTarget().createMCCodeEmitter(
        *tm.getMCInstrInfo(), context);

    llvm::SmallVector<char, 32> inst_buf = {};

    struct EmitterWrapper : llvm::MCCodeEmitter {
        virtual ~EmitterWrapper() = default;

        llvm::MCCodeEmitter         *emitter;
        llvm::SmallVector<char, 32> *inst_buf;

        EmitterWrapper(llvm::MCCodeEmitter         *emitter,
                       llvm::SmallVector<char, 32> *buf)
            : emitter(emitter), inst_buf(buf) {}

        void reset() override { emitter->reset(); }

        void
            encodeInstruction(const llvm::MCInst                   &Inst,
                              llvm::SmallVectorImpl<char>          &CB,
                              llvm::SmallVectorImpl<llvm::MCFixup> &Fixups,
                              const llvm::MCSubtargetInfo &STI) const override {
            const auto size = CB.size();
            emitter->encodeInstruction(Inst, CB, Fixups, STI);
            const auto enc_len = CB.size() - size;
            std::cerr << std::format("Encoded inst with len {}: ", enc_len);
            for (unsigned i = 0; i < enc_len; ++i) {
                std::cerr << std::format("{:X} ", CB[size + i]);
            }
            std::cerr << "\n";

            inst_buf->append(CB.begin() + size, CB.begin() + CB.size());
        }
    };

    auto obj_streamer =
        std::unique_ptr<llvm::MCStreamer>(target.createMCObjectStreamer(
            tm.getTargetTriple(),
            context,
            nullptr,
            nullptr,
            std::make_unique<EmitterWrapper>(emitter, &inst_buf),
            state.func->getSubtarget(),
            false,
            false,
            true));
    // TOOD(ts): get tm from main? why is is not const?
    auto *asm_printer = target.createAsmPrinter(
        *const_cast<llvm::LLVMTargetMachine *>(&tm), std::move(obj_streamer));
    asm_printer->SetupMachineFunction(*state.func);

    asm_printer->emitInstruction(inst);
    delete asm_printer;
    delete emitter;

    std::format_to(std::back_inserter(buf), "    // encoded as ");
    for (char c : inst_buf) {
        std::format_to(std::back_inserter(buf), "{:X}", c);
    }
    std::format_to(std::back_inserter(buf), "\n");
#endif

  // we need to check all definitions for existing aliases so
  // that those do not get overwritten since they refer to the old value
  const auto is_input = [inst, &state](unsigned reg) {
    for (const auto &input : inst->all_uses()) {
      if (!input.isReg() || !input.getReg().isValid()) {
        continue;
      }

      if (state.target->reg_id_from_mc_reg(input.getReg()) == reg) {
        return true;
      }
    }
    return false;
  };

  for (const auto &def : inst->all_defs()) {
    if (!def.isReg() || !def.getReg().isValid()) {
      continue;
    }

    const auto id = state.target->reg_id_from_mc_reg(def.getReg());
    if (!state.value_map.contains(id)) {
      continue;
    }

    if (state.value_map[id].aliased_regs.empty()) {
      continue;
    }
    // should not be an AsmOperand or alias anymore
    assert(state.value_map[id].ty == ValueInfo::SCRATCHREG);

    // need to generate copies
    state.fmt_line(buf,
                   4,
                   "// {} is a def but still has active aliases left which "
                   "need to be preserved",
                   state.target->reg_name_lower(id));
    if (!state.maybe_fixed_regs.contains(id) && !is_input(id)) {
      // we can simply emit a scratchreg move
      int new_target = -1;
      for (auto alias_reg : state.value_map[id].aliased_regs) {
        if (new_target != -1) {
          state.fmt_line(buf,
                         4,
                         "// {} will be redirected to {}",
                         state.target->reg_name_lower(alias_reg),
                         state.target->reg_name_lower(new_target));
          state.value_map[new_target].aliased_regs.insert(alias_reg);
          state.value_map[alias_reg].alias_reg_id = new_target;
          continue;
        }

        assert(state.value_map.contains(alias_reg));
        assert(state.value_map[alias_reg].ty == ValueInfo::REG_ALIAS);
        const auto id_name = state.target->reg_name_lower(id);
        const auto alias_name = state.target->reg_name_lower(alias_reg);
        state.fmt_line(buf,
                       4,
                       "// since {} is not an input or fixed we can "
                       "move the scratchreg to {}",
                       id_name,
                       alias_name);
        state.fmt_line(
            buf, 4, "scratch_{} = std::move(scratch_{});", alias_name, id_name);
        new_target = alias_reg;

        state.value_map[alias_reg].ty = ValueInfo::SCRATCHREG;
      }
    } else {
      unsigned max_alias_size = 0;
      for (auto alias_reg : state.value_map[id].aliased_regs) {
        assert(state.value_map.contains(alias_reg));
        max_alias_size =
            std::max(max_alias_size, state.value_map[alias_reg].alias_size);
      }
      materialize_aliased_regs(state, buf, 4, id, max_alias_size);
    }

    state.value_map[id].aliased_regs.clear();
  }

  const llvm::MCInstrDesc &llvm_inst_desc =
      state.func->getTarget().getMCInstrInfo()->get(inst->getOpcode());

// TODO(ts): check preferred encodings
#if 0
    const auto get_use = [&](unsigned idx) {
        for (auto &use : inst->uses()) {
            if (idx != 0) {
                --idx;
                continue;
            }
            return use;
        }
        // should never happen
        assert(0);
        exit(1);
    };
#endif
#if 0
    const auto get_def = [&](unsigned idx) {
        for (auto &def : inst->all_defs()) {
            if (idx != 0) {
                --idx;
                continue;
            }
            return def;
        }
        // should never happen
        assert(0);
        exit(1);
    };
#endif

  llvm::SmallVector<MICandidate> candidates;
  state.target->get_inst_candidates(*inst, candidates);
  assert(candidates.size() > 0);

  if (candidates.size() > 1) {
    state.fmt_line(buf, 4, "do {{");
  }
  std::string parent_conds = "0";
  for (const auto &cand : candidates) {
    std::string if_cond = "";
    llvm::SmallVector<std::pair<unsigned, std::string>, 2> repl_ops{};
    llvm::SmallVector<std::string> use_ops;
    llvm::SmallVector<std::string> ops;

    auto wrote_initial_bracket = false;
    if (!cand.conds.empty()) {
      auto skip = false;
      for (auto &cond_entry : cand.conds) {
        llvm::MachineOperand &cond_op = inst->getOperand(cond_entry.op_idx);
        auto reg_id = state.target->reg_id_from_mc_reg(cond_op.getReg());
        auto value_map_it = state.value_map.find(reg_id);
        if (value_map_it == state.value_map.end() ||
            value_map_it->second.ty != ValueInfo::ASM_OPERAND) {
          skip = true;
          break;
        }
        const auto &op_name = value_map_it->second.operand_name;

        std::string opt = std::format("{}({}{}{})",
                                      cond_entry.func,
                                      op_name,
                                      cond_entry.args.size() ? ", " : "",
                                      cond_entry.args);
        auto cond = std::format("cond{}", state.num_conds++);
        std::string cond_decl = std::format("auto {} = {};", cond, opt);

        if (!wrote_initial_bracket) {
          state.fmt_line(buf, 4, "{{");
          wrote_initial_bracket = true;
        }

        // Extremely ugly. We want to execute checks which avoid
        // requiring fixed registers at the top. For now pretend this is
        // only relevant when a condition allows replacing an implicit
        // operands. So essentially, this works exactly for shifts on
        // x86 and nothing else.
        // TODO: proper solution? Or explicitly special case x86 shifts?
        if (cond_entry.op_idx >= inst->getNumExplicitOperands()) {
          state.asm_operand_early_conds.push_back(std::move(cond_decl));
          parent_conds += "||" + cond;
        } else {
          state.fmt_line(buf, 4, "{}", cond_decl);
        }
        if (if_cond.empty()) {
          if_cond = cond;
        } else {
          if_cond += std::format(" && {}", cond);
        }
        repl_ops.push_back(std::make_pair(cond_entry.op_idx, cond));
      }
      if (skip) {
        continue;
      }
    } else {
      state.fmt_line(buf, 4, "{{");
    }

    // allocate mapping for destinations
    std::vector<std::pair<bool, const llvm::MachineOperand *>> defs_allocated{};
    for (const auto &def : inst->all_defs()) {
      const auto reg = def.getReg();
      if (state.target->reg_should_be_ignored(reg)) {
        continue;
      }

      const auto reg_id = state.target->reg_id_from_mc_reg(reg);
      if (!state.value_map.contains(reg_id)) {
        state.value_map[reg_id] =
            ValueInfo{.ty = ValueInfo::SCRATCHREG, .is_dead = false};
      }
      defs_allocated.emplace_back(def.isImplicit(), &def);
    }

    const auto def_idx = [inst, &state](const llvm::MachineOperand *op) {
      unsigned idx = 0;
      for (const auto &def : inst->all_defs()) {
        assert(def.isReg() && def.getReg().isPhysical());
        if (state.target->reg_should_be_ignored(def.getReg())) {
          continue;
        }
        if (&def == op) {
          return idx;
        }
        ++idx;
      }
      assert(0);
      exit(1);
    };

    const auto inst_id = state.cur_inst_id;

    // Map from register ID their operand to prevent loading the same
    // register twice.
    llvm::DenseMap<unsigned, std::string> allocated_regs;

    std::vector<unsigned> implicit_ops_handled{};
    unsigned implicit_uses = 0; //, explicit_uses = 0;

    state.fmt_line(buf, 4, "if ({}) {{", !if_cond.empty() ? if_cond : "1");
    unsigned op_cnt = 0;
    for (const llvm::MachineOperand &use : inst->operands()) {
      // llvm::errs() << use.isCPI() << " " << use << "\n";
      unsigned op_idx = op_cnt++;
      if (use.isReg() && use.isUse() && use.isImplicit()) {
        implicit_ops_handled.push_back(use.getOperandNo());
        if (implicit_uses >= llvm_inst_desc.NumImplicitUses) {
          state.fmt_line(buf, 8, "// operand exceeds NumImplicitUses, ignore");
          continue;
        }
        implicit_uses++;
      }
      if (use.isCPI()) {
        assert(use.getOffset() == 0);
        auto var_name = std::format("op{}_sym", use.getOperandNo());
        use_ops.push_back(var_name);
        generate_cp_entry_sym(state, buf, 8, var_name, use.getIndex());
        continue;
      }

      if (!use.isReg() || !use.isUse()) {
        continue;
      }
      if (!use.getReg().isValid()) {
        use_ops.push_back("");
        continue;
      }
      if (auto it = std::find_if(repl_ops.begin(),
                                 repl_ops.end(),
                                 [&](const auto &entry) {
                                   return entry.first == use.getOperandNo();
                                 });
          it != repl_ops.end()) {
        use_ops.push_back(std::format("(*{})", it->second));
        continue;
      }

      // TODO: is this correct?
      // We must still count implicit uses.
      if (state.target->reg_should_be_ignored(use.getReg())) {
        if (!use.isImplicit()) {
          use_ops.push_back("");
        }
        continue;
      }

      assert(!use.isTied() ||
             (!use.isImplicit() && "implicit tied use not implemented"));

      auto orig_reg_id = state.target->reg_id_from_mc_reg(use.getReg());

      // TODO(ae): I don't think this is correct. But how to handle
      // XOR32rr undef tied-def eax, undef eax?
      if (!use.isTied()) {
        auto [it, inserted] = allocated_regs.try_emplace(orig_reg_id, "");
        if (!inserted) {
          if (!it->second.empty()) {
            use_ops.push_back(it->second);
          }
          continue;
        }
      }

      if (use.isUndef()) {
        if (use.isTied()) {
          state.fmt_line(buf, 8, "// undef tied");
          // TODO: assertions
        } else {
          state.fmt_line(buf, 8, "// undef allocate scratch");
          state.fmt_line(buf,
                         8,
                         "AsmReg inst{}_op{} = "
                         "scratch_{}.alloc({});\n",
                         inst_id,
                         op_idx,
                         state.target->reg_name_lower(orig_reg_id),
                         state.target->reg_bank(orig_reg_id));
          use_ops.push_back(std::format("inst{}_op{}", inst_id, op_idx));
          allocated_regs[orig_reg_id] = use_ops.back();
        }
        continue;
      }

      auto resolved_reg_id = orig_reg_id;
      while (state.value_map[resolved_reg_id].ty == ValueInfo::REG_ALIAS) {
        resolved_reg_id = state.value_map[resolved_reg_id].alias_reg_id;
        assert(state.value_map.contains(resolved_reg_id));
      }

      // TODO(ts): need to think about this
      assert(!use.isEarlyClobber());

      const auto &reg_info = state.value_map[resolved_reg_id];

      if (use.isImplicit()) {
        // notify the outer code that we need the register
        // argument as a fixed register
        state.fmt_line(
            buf, 8, "// fixing reg {} for cond ({})", orig_reg_id, if_cond);
        state.fixed_reg_conds[orig_reg_id].emplace_back(std::format(
            "!({})&&{}", parent_conds, if_cond.empty() ? "1" : if_cond));

        auto dst = std::format("scratch_{}.cur_reg",
                               state.target->reg_name_lower(orig_reg_id));
        std::string copy_src;
        if (reg_info.ty == ValueInfo::ASM_OPERAND) {
          // TODO(ts): try to salvage the register if possible
          // register should be allocated if we come into here
          copy_src = std::format("op{}_tmp", use.getOperandNo());
          state.fmt_line(buf,
                         8,
                         "AsmReg op{}_tmp = derived()->gval_as_reg({});",
                         use.getOperandNo(),
                         reg_info.operand_name);
        } else if (orig_reg_id != resolved_reg_id) {
          // Copy alias into implicit (fixed) register
          copy_src = std::format("scratch_{}.cur_reg",
                                 state.target->reg_name_lower(resolved_reg_id));
        }

        if (!copy_src.empty()) {
          state.target->generate_copy(buf,
                                      8,
                                      state.target->reg_bank(orig_reg_id),
                                      dst,
                                      copy_src,
                                      reg_size_bytes(state.func, use.getReg()));
        }
        continue;
      }

      if (!use.isTied()) {
        auto op_name = std::format("op{}", use.getOperandNo());
        if (reg_info.ty == ValueInfo::SCRATCHREG) {
          // TODO(ts): if this reg is also used as a def-reg
          // should we mark it as allocated here?
          state.fmt_line(buf,
                         8,
                         "AsmReg {} = scratch_{}.cur_reg;",
                         op_name,
                         state.target->reg_name_lower(resolved_reg_id));
          use_ops.push_back(std::move(op_name));
          allocated_regs[orig_reg_id] = use_ops.back();
          continue;
        }

        auto handled = false;
        if (state.can_salvage_operand(use)) {
          const auto op_bank = state.target->reg_bank(resolved_reg_id);
          for (auto &[allocated, def] : defs_allocated) {
            if (allocated) {
              continue;
            }
            const auto def_reg_id =
                state.target->reg_id_from_mc_reg(def->getReg());

            // if (def_reg_id != orig_reg_id && def_is_tied(*def)) {
            //     continue;
            // }

            const auto def_bank = state.target->reg_bank(def_reg_id);

            if (def_bank != op_bank) {
              continue;
            }

            const auto def_reg_name = state.target->reg_name_lower(def_reg_id);
            state.fmt_line(buf,
                           8,
                           "AsmReg {} = derived()->gval_as_reg_reuse({}, "
                           "scratch_{});",
                           op_name,
                           reg_info.operand_name,
                           def_reg_name);
            handled = true;
            break;
          }
        }
        if (!handled) {
          state.fmt_line(buf,
                         8,
                         "AsmReg {} = derived()->gval_as_reg({});",
                         op_name,
                         reg_info.operand_name);
        }

        use_ops.push_back(std::move(op_name));
        allocated_regs[orig_reg_id] = use_ops.back();
        continue;
      }

      // Only explicit tied-def are left here, they need no use_ops entry
      if (reg_info.ty == ValueInfo::SCRATCHREG) {
        const auto &def_reg =
            inst->getOperand(inst->findTiedOperandIdx(use.getOperandNo()));
        assert(def_reg.isReg() && def_reg.getReg().isPhysical());
        const auto def_resolved_reg_id =
            state.target->reg_id_from_mc_reg(def_reg.getReg());
        assert(state.target->reg_bank(resolved_reg_id) ==
               state.target->reg_bank(def_resolved_reg_id));

        if (resolved_reg_id == def_resolved_reg_id) {
          std::format_to(std::back_inserter(buf),
                         "{:>{}}// operand {}({}) is the "
                         "same as its tied "
                         "destination\n",
                         "",
                         8,
                         op_idx,
                         state.target->reg_name_lower(resolved_reg_id));
          // TODO(ts): in this case, the scratch should
          // already be allocated, no?
          // Yes.
          state.fmt_line(buf,
                         8,
                         "assert(scratch_{}.cur_reg.valid());",
                         state.target->reg_name_lower(resolved_reg_id));
          defs_allocated[def_idx(&def_reg)].first = true;
        } else if (state.can_salvage_operand(use)) {
          assert(false && "untested");
          std::format_to(std::back_inserter(buf),
                         "{:>{}}// operand {}({}) can be salvaged\n",
                         "",
                         8,
                         op_idx,
                         state.target->reg_name_lower(resolved_reg_id));
          std::format_to(std::back_inserter(buf),
                         "{:>{}}scratch_{} = "
                         "std::move(scratch_{});\n",
                         "",
                         8,
                         state.target->reg_name_lower(def_resolved_reg_id),
                         state.target->reg_name_lower(resolved_reg_id));
          std::format_to(std::back_inserter(buf),
                         "{:>{}}"
                         "scratch_{}.alloc({});\n",
                         "",
                         8,
                         state.target->reg_name_lower(def_resolved_reg_id),
                         state.target->reg_bank(def_resolved_reg_id));
          defs_allocated[def_idx(&def_reg)].first = true;
        } else {
          state.fmt_line(buf,
                         8,
                         "// operand {} has some references",
                         state.target->reg_name_lower(resolved_reg_id));
          state.fmt_line(buf,
                         8,
                         "AsmReg reg_{0} = scratch_{0}.alloc({1});",
                         state.target->reg_name_lower(def_resolved_reg_id),
                         state.target->reg_bank(def_resolved_reg_id));
          state.target->generate_copy(
              buf,
              8,
              state.target->reg_bank(def_resolved_reg_id),
              std::format("reg_{}",
                          state.target->reg_name_lower(def_resolved_reg_id)),
              std::format("scratch_{}.cur_reg",
                          state.target->reg_name_lower(resolved_reg_id)),
              reg_size_bytes(state.func, use.getReg()));
        }
      } else if (reg_info.ty == ValueInfo::ASM_OPERAND) {
        // try to salvage
        // TODO(ts): if the destination is used as a fixed
        // reg at some point we cannot simply salvage into
        // it i think so we need some extra helpers for that
        const auto &def_reg =
            inst->getOperand(inst->findTiedOperandIdx(use.getOperandNo()));
        assert(def_reg.isReg() && def_reg.getReg().isPhysical());
        const auto def_resolved_reg_id =
            state.target->reg_id_from_mc_reg(def_reg.getReg());
        assert(state.target->reg_bank(resolved_reg_id) ==
               state.target->reg_bank(def_resolved_reg_id));

        if (state.can_salvage_operand(use)) {
          std::format_to(std::back_inserter(buf),
                         "{:>{}}// operand {}({}) is tied so try "
                         "to salvage or materialize\n",
                         "",
                         8,
                         op_idx,
                         reg_info.operand_name);
          std::format_to(std::back_inserter(buf),
                         "{:>{}}try_salvage_or_materialize({}, "
                         "scratch_{}, {}, {});\n",
                         "",
                         8,
                         reg_info.operand_name,
                         state.target->reg_name_lower(def_resolved_reg_id),
                         state.target->reg_bank(resolved_reg_id),
                         reg_size_bytes(state.func, use.getReg()));
          defs_allocated[def_idx(&def_reg)].first = true;
        } else {
          if (!state.value_map[def_resolved_reg_id].aliased_regs.empty()) {
            if (state.value_map[def_resolved_reg_id].ty !=
                ValueInfo::SCRATCHREG) {
              state.fmt_line(buf,
                             8,
                             "// {} has aliases left so need "
                             "to redirect them",
                             state.target->reg_name_lower(def_resolved_reg_id));

              state.redirect_aliased_regs_to_parent(def_resolved_reg_id);
            } else {
              state.fmt_line(buf,
                             8,
                             "// {} has aliases left so need "
                             "to materialize "
                             "the copy before overwriting",
                             state.target->reg_name_lower(def_resolved_reg_id));
              materialize_aliased_regs(
                  state,
                  buf,
                  8,
                  def_resolved_reg_id,
                  reg_size_bytes(state.func, use.getReg()));
            }
          }
          assert(state.value_map[def_resolved_reg_id].aliased_regs.empty());
          std::format_to(std::back_inserter(buf),
                         "{:>{}}AsmReg inst{}_op{} = "
                         "scratch_{}.alloc({});\n",
                         "",
                         8,
                         inst_id,
                         op_idx,
                         state.target->reg_name_lower(def_resolved_reg_id),
                         state.target->reg_bank(def_resolved_reg_id));
          std::format_to(std::back_inserter(buf),
                         "{:>{}}AsmReg inst{}_op{}_tmp = "
                         "derived()->gval_as_reg({});\n",
                         "",
                         8,
                         inst_id,
                         op_idx,
                         reg_info.operand_name);
          state.target->generate_copy(
              buf,
              8,
              state.target->reg_bank(def_resolved_reg_id),
              std::format("inst{}_op{}", inst_id, op_idx),
              std::format("inst{}_op{}_tmp", inst_id, op_idx),
              reg_size_bytes(state.func, use.getReg()));
          defs_allocated[def_idx(&def_reg)].first = true;
        }
      }
    }

    // TODO(ae): this code is dead right now, was it important?
#if 0
        // TODO(ts): factor this out a bit with the code above
        // need to make sure implicit register uses are also properly moved
        for (auto &op : inst->all_uses()) {
            if (!op.isReg() || !op.isImplicit()) {
                continue;
            }
            if (std::find(implicit_ops_handled.begin(),
                          implicit_ops_handled.end(),
                          op.getOperandNo())
                != implicit_ops_handled.end()) {
                continue;
            }

            assert(op.isReg());
            if (state.target->reg_should_be_ignored(op.getReg())) {
                continue;
            }

            const auto reg_id = state.target->reg_id_from_mc_reg(op.getReg());
            assert(state.value_map.contains(reg_id));

            state.fmt_line(buf,
                           8,
                           "// Handling implicit operand {}",
                           state.target->reg_name_lower(reg_id));

            const auto resolved_reg_id =
                state.resolve_reg_alias(buf, 8, reg_id);

            const auto &info = state.value_map[resolved_reg_id];
            if (info.ty == ValueInfo::SCRATCHREG && reg_id == resolved_reg_id) {
                state.fmt_line(buf,
                               8,
                               "// Value is already in register, no need to copy");
                continue;
            }

            if (info.ty == ValueInfo::SCRATCHREG) {
                state.fmt_line(buf,
                               8,
                               "// Need to break alias from {} to {} and copy the "
                               "value",
                               state.target->reg_name_lower(reg_id),
                               state.target->reg_name_lower(resolved_reg_id));
                state.target->generate_copy(
                    buf,
                    8,
                    state.target->reg_bank(reg_id),
                    std::format("scratch_{}.cur_reg",
                                state.target->reg_name_lower(reg_id)),
                    std::format("scratch_{}.cur_reg",
                                state.target->reg_name_lower(resolved_reg_id)),
                    reg_size_bytes(state.func, op.getReg()));
                assert(state.value_map[reg_id].ty == ValueInfo::REG_ALIAS);
                // TODO(ts): allow this function to bubble up these copies
                // so we can take advantage of them if they happen in all
                // branches
                if (if_cond.empty()) {
                    state.remove_reg_alias(
                        buf, reg_id, state.value_map[reg_id].alias_reg_id);
                }
                continue;
            }

            assert(info.ty == ValueInfo::ASM_OPERAND);
            state.fmt_line(buf,
                           8,
                           "// Need to break alias from {} to operand {} and "
                           "copy the value",
                           state.target->reg_name_lower(reg_id),
                           info.operand_name);
            // TODO(ts): add helper that directly gets the value into the register
            // if it is in memory for example
            state.fmt_line(buf,
                           8,
                           "AsmReg inst{}_op{}_tmp = {}.as_reg(this);",
                           inst_id,
                           op.getOperandNo(),
                           info.operand_name);
            state.target->generate_copy(
                buf,
                8,
                state.target->reg_bank(reg_id),
                std::format("scratch_{}.cur_reg",
                            state.target->reg_name_lower(reg_id)),
                std::format("inst{}_op{}_tmp", inst_id, op.getOperandNo()),
                reg_size_bytes(state.func, op.getReg()));

            // TODO(ts): propagate upwards, see above
            if (if_cond.empty()) {
                if (state.value_map[reg_id].ty == ValueInfo::REG_ALIAS) {
                    state.remove_reg_alias(
                        buf, reg_id, state.value_map[reg_id].alias_reg_id);
                } else {
                    assert(state.value_map[reg_id].ty == ValueInfo::ASM_OPERAND);
                    state.value_map[reg_id].ty = ValueInfo::SCRATCHREG;
                }
            }
        }
#endif

    // allocate destinations if it did not yet happen
    // TODO(ts): we rely on the fact that the order when encoding
    // corresponds to llvms ordering of defs/uses which might not always
    // be the case


    unsigned imp_defs = 0;
    for (const llvm::MachineOperand &def : inst->all_defs()) {
      assert(def.isReg() && def.getReg().isPhysical());
      const auto reg = def.getReg();

      // Ignore exceeding implicit defs
      if (def.isImplicit() && imp_defs++ >= llvm_inst_desc.NumImplicitDefs) {
        continue;
      }
      if (state.target->reg_should_be_ignored(reg)) {
        if (!def.isImplicit()) {
          ops.push_back("");
        }
        continue;
      }

      const auto reg_id = state.target->reg_id_from_mc_reg(reg);
      if (def.isImplicit()) {
        // notify the outer code that the register is needed as a
        // fixed register
        state.fixed_reg_conds[reg_id].emplace_back(if_cond);
      }

      if (!defs_allocated[def_idx(&def)].first) {
        std::format_to(std::back_inserter(buf),
                       "        // def {} has not been allocated yet\n",
                       state.target->reg_name_lower(reg_id));
        std::format_to(std::back_inserter(buf),
                       "        scratch_{}.alloc({});\n",
                       state.target->reg_name_lower(reg_id),
                       state.target->reg_bank(reg_id));
      }

      if (def.isImplicit()) {
        // implicit defs do not show up in the argument list
        continue;
      }
      ops.push_back(std::format("scratch_{}.cur_reg",
                                state.target->reg_name_lower(reg_id)));
    }

    ops.append(use_ops);

    llvm::raw_string_ostream os(buf);
    cand.generator(os, *inst, ops);
    if (candidates.size() > 1) {
      state.fmt_line(buf, 8, "break;");
    }
    state.fmt_line(buf, 4, "}}");
    state.fmt_line(buf, 4, "}}");
  }
  if (candidates.size() > 1) {
    state.fmt_line(buf, 4, "}} while (false);");
  }

  // TODO(ts): check killed operands
  for (auto &use : inst->all_uses()) {
    if (use.isImm() || (use.isReg() && !use.getReg().isValid())) {
      continue;
    }

    assert(use.isReg() && use.getReg().isPhysical());
    const auto reg = use.getReg();

    if (state.target->reg_should_be_ignored(reg)) {
      continue;
    }

    if (!use.isKill()) {
      continue;
    }

    const auto reg_id = state.target->reg_id_from_mc_reg(reg);
    std::format_to(std::back_inserter(buf),
                   "    // argument {} is killed and marked as dead\n",
                   state.target->reg_name_lower(reg_id));

    assert(state.value_map.contains(reg_id));
    auto &reg_info = state.value_map[reg_id];
    if (reg_info.ty == ValueInfo::REG_ALIAS) {
      state.remove_reg_alias(buf, reg_id, reg_info.alias_reg_id);
    } else if (reg_info.ty == ValueInfo::ASM_OPERAND) {
      assert(state.operand_ref_counts[reg_info.operand_name] > 0);
      state.operand_ref_counts[reg_info.operand_name] -= 1;
      // TODO(ae): shouldn't the ref count be always zero when killed?
      // assert(state.operand_ref_counts[reg_info.operand_name] == 0);
      if (state.operand_ref_counts[reg_info.operand_name] == 0) {
        state.fmt_line(buf, 4, "{}.reset();", reg_info.operand_name);
      }
    }

    reg_info.ty = ValueInfo::SCRATCHREG;
    reg_info.is_dead = true;
  }

  for (auto &def : inst->all_defs()) {
    assert(def.isReg() && def.getReg().isPhysical());
    const auto reg = def.getReg();
    if (state.target->reg_should_be_ignored(reg)) {
      continue;
    }

    // after an instruction all registers are in ScratchRegs
    const auto reg_id = state.target->reg_id_from_mc_reg(reg);
    assert(state.value_map.contains(reg_id));
    auto &reg_info = state.value_map[reg_id];
    if (reg_info.ty == ValueInfo::REG_ALIAS) {
      state.remove_reg_alias(buf, reg_id, reg_info.alias_reg_id);
    } else if (reg_info.ty == ValueInfo::ASM_OPERAND) {
      assert(state.operand_ref_counts[reg_info.operand_name] > 0);
      state.operand_ref_counts[reg_info.operand_name] -= 1;
    }

    reg_info.ty = ValueInfo::SCRATCHREG;
    reg_info.is_dead = def.isDead();

    state.fmt_line(buf,
                   4,
                   "// result {} is marked as {}",
                   state.target->reg_name_lower(reg_id),
                   reg_info.is_dead ? "dead" : "alive");
  }

  return true;
}

void GenerationState::remove_reg_alias(std::string &buf,
                                       unsigned reg,
                                       unsigned aliased_reg) {
  assert(value_map.contains(reg));
  assert(value_map.contains(aliased_reg));
  std::format_to(std::back_inserter(buf),
                 "    // removing alias from {} to {}\n",
                 target->reg_name_lower(reg),
                 target->reg_name_lower(aliased_reg));

  assert(value_map[reg].ty != ValueInfo::ASM_OPERAND);
  value_map[reg].ty = ValueInfo::SCRATCHREG;
  auto &info = value_map[aliased_reg];
  info.aliased_regs.erase(reg);

  if (info.ty == ValueInfo::REG_ALIAS && info.aliased_regs.empty() &&
      info.is_dead) {
    remove_reg_alias(buf, aliased_reg, info.alias_reg_id);
  }
}

void GenerationState::redirect_aliased_regs_to_parent(const unsigned reg) {
  assert(value_map.contains(reg));
  auto &info = value_map[reg];
  assert(info.ty == ValueInfo::ASM_OPERAND || info.ty == ValueInfo::REG_ALIAS);
  for (auto alias_reg : info.aliased_regs) {
    assert(value_map.contains(alias_reg));
    auto &alias_info = value_map[alias_reg];

    assert(alias_info.ty == ValueInfo::REG_ALIAS);
    alias_info.ty = info.ty;
    alias_info.alias_reg_id = info.alias_reg_id;
    alias_info.operand_name = info.operand_name;

    if (info.ty == ValueInfo::ASM_OPERAND) {
      operand_ref_counts[info.operand_name] += 1;
    }

    if (info.ty == ValueInfo::REG_ALIAS) {
      assert(value_map.contains(info.alias_reg_id));
      value_map[info.alias_reg_id].aliased_regs.insert(alias_reg);
    }
  }

  info.aliased_regs.clear();
}

unsigned GenerationState::resolve_reg_alias(std::string &buf,
                                            unsigned indent,
                                            unsigned reg) {
  assert(value_map.contains(reg));
  const auto &info = value_map[reg];
  if (info.ty == ValueInfo::REG_ALIAS) {
    fmt_line(buf,
             indent,
             "// {} is an alias for {}",
             target->reg_name_lower(reg),
             target->reg_name_lower(info.alias_reg_id));
    return resolve_reg_alias(buf, indent, info.alias_reg_id);
  }

  return reg;
}

bool GenerationState::can_salvage_operand(const llvm::MachineOperand &op) {
  if (!is_first_block) {
    return false;
  }

  if (!op.isReg()) {
    return false;
  }

  if (!op.isKill()) {
    return false;
  }

  const auto reg_id = target->reg_id_from_mc_reg(op.getReg());
  assert(value_map.contains(reg_id));
  const auto &info = value_map[reg_id];

  if (!info.aliased_regs.empty()) {
    return false;
  }

  if (info.ty == ValueInfo::ASM_OPERAND) {
    assert(operand_ref_counts.contains(info.operand_name));
    if (operand_ref_counts[info.operand_name] > 1) {
      return false;
    }
  }

  if (info.ty != ValueInfo::ASM_OPERAND && maybe_fixed_regs.contains(reg_id)) {
    // we can salvage if the register maps to an operand
    // since the salvaging will not overwrite the scratchreg in this case
    // TODO(ts): optimize the try_salvaging logic to give notice about the
    // fixed status of a register so that it can still salvage if it is not
    // fixed to not have to move
    return false;
  }

  if (info.ty != ValueInfo::REG_ALIAS) {
    return true;
  }

  [[maybe_unused]] auto old_reg = reg_id;
  auto cur_reg = info.alias_reg_id;
  while (true) {
    assert(value_map.contains(cur_reg));
    const auto &alias_info = value_map[cur_reg];
    if (!alias_info.is_dead || maybe_fixed_regs.contains(cur_reg)) {
      return false;
    }

    if (alias_info.aliased_regs.size() > 1) {
      return false;
    }
    assert(alias_info.aliased_regs.size() == 1);
    assert(alias_info.aliased_regs.contains(old_reg));

    if (alias_info.ty != ValueInfo::REG_ALIAS) {
      return true;
    }

    old_reg = cur_reg;
    cur_reg = alias_info.alias_reg_id;
  }
}

static void materialize_aliased_regs(GenerationState &state,
                                     std::string &buf,
                                     unsigned indent,
                                     unsigned break_reg,
                                     unsigned reg_size) {
  // TODO(ts): if we would know the value is not fixed we could
  // instead std::move the scratchregs
  auto &info = state.value_map[break_reg];
  int copied_dst = -1;
  auto dst_name = state.target->reg_name_lower(break_reg);
  for (auto reg : info.aliased_regs) {
    assert(reg != break_reg);
    const auto reg_name = state.target->reg_name_lower(reg);
    assert(state.value_map[reg].ty == ValueInfo::REG_ALIAS);
    assert(!state.value_map[reg].is_dead);
    if (copied_dst != -1) {
      // just alias to the reg we already copied to
      state.fmt_line(buf,
                     indent,
                     "// aliasing {} to {}",
                     reg_name,
                     state.target->reg_name_lower(copied_dst));
      state.value_map[reg].alias_reg_id = copied_dst;
      state.value_map[copied_dst].aliased_regs.insert(reg);
      continue;
    }

    state.fmt_line(buf,
                   indent,
                   "// {} is still aliased to the value of {} so "
                   "we emit an explicit copy",
                   reg_name,
                   dst_name);
    state.fmt_line(buf,
                   indent,
                   "scratch_{}.alloc({});",
                   reg_name,
                   state.target->reg_bank(reg));
    state.target->generate_copy(buf,
                                indent,
                                state.target->reg_bank(reg),
                                std::format("scratch_{}.cur_reg", reg_name),
                                std::format("scratch_{}.cur_reg", dst_name),
                                reg_size);
    copied_dst = reg;
    state.value_map[reg].ty = ValueInfo::SCRATCHREG;
  }
  info.aliased_regs.clear();
}

bool handle_move(std::string &buf,
                 GenerationState &state,
                 llvm::MachineOperand &src_op,
                 llvm::MachineOperand &dst_op) {
  assert(src_op.getType() == llvm::MachineOperand::MO_Register);
  const auto src_reg = src_op.getReg();
  assert(src_reg.isPhysical());

  const auto dst_reg = dst_op.getReg();
  assert(dst_reg.isPhysical());

  const auto src_id = state.target->reg_id_from_mc_reg(src_reg);
  const auto dst_id = state.target->reg_id_from_mc_reg(dst_reg);
  const auto src_name = state.target->reg_name_lower(src_id);
  const auto dst_name = state.target->reg_name_lower(dst_id);

  assert(state.value_map.contains(src_id));
  if (state.value_map[src_id].is_dead) {
    std::cerr << std::format("ERROR: dead register {} used in move\n",
                             src_name);
    return false;
  }

  // TODO(ts): src_id == dst_id?
  if (src_reg == dst_reg) {
    // this is a no-op
    buf += std::format("    // no-op\n");
    return true;
  }

  // This is a register move without any intended side-effects,
  // e.g. zero-extension so we can alias it for now
  buf += std::format("    // aliasing {} to {}\n", dst_name, src_name);

  if (state.value_map.contains(dst_id)) {
    auto &info = state.value_map[dst_id];
    if (info.ty == ValueInfo::REG_ALIAS || info.ty == ValueInfo::ASM_OPERAND) {
      state.fmt_line(buf,
                     4,
                     "// {} is an alias or operand, redirecting all aliases",
                     dst_name);
      state.redirect_aliased_regs_to_parent(dst_id);

      if (info.ty == ValueInfo::REG_ALIAS) {
        state.remove_reg_alias(buf, dst_id, info.alias_reg_id);
      } else {
        assert(info.ty == ValueInfo::ASM_OPERAND);
        assert(state.operand_ref_counts[info.operand_name] > 0);
        state.operand_ref_counts[info.operand_name] -= 1;
      }
    } else if (info.ty == ValueInfo::SCRATCHREG && !info.aliased_regs.empty()) {
      // need to emit a copy to preserve the original value
      materialize_aliased_regs(
          state, buf, 4, dst_id, reg_size_bytes(state.func, dst_reg));
    }
  }

  auto resolved_id = state.resolve_reg_alias(buf, 4, src_id);
  assert(state.value_map.contains(resolved_id));
  const auto &resolved_info = state.value_map[resolved_id];

  // TODO(ts): check for dead moves?
  auto &dst_info = state.value_map[dst_id];
  if (resolved_info.ty == ValueInfo::ASM_OPERAND) {
    state.fmt_line(buf,
                   4,
                   "// {} is an alias for operand {}",
                   dst_name,
                   resolved_info.operand_name);
    dst_info.ty = ValueInfo::ASM_OPERAND;
    dst_info.operand_name = resolved_info.operand_name;
    state.operand_ref_counts[resolved_info.operand_name] += 1;
  } else {
    state.value_map[resolved_id].aliased_regs.insert(dst_id);
    dst_info.ty = ValueInfo::REG_ALIAS;
    dst_info.alias_reg_id = resolved_id;
  }
  dst_info.is_dead = false;
  dst_info.alias_size = reg_size_bytes(state.func, dst_reg);

  // TODO(ts): cannot do this when we are outside of the entry-block
  if (src_op.isKill()) {
#if 0
                // no need to alias, we can simply move the register
                // TODO(ts): we cannot do this if dst_id is used as a fixed register somewhere
                // as we free up the allocation then

                // TODO(ts): do we want this as the optimizer might not optimize out the move
                buf += std::format("    // source {} is killed so move to {}\n", src_name, dst_name);
                buf += std::format("    scratch_{} = std::move(scratch_{})\n", dst_name, src_name);

                state.value_map[src_id].was_destroyed = true;
                state.value_map[src_id].is_dead = true;
#else
    buf += std::format(
        "    // source {} is killed, all aliases redirected and marked as "
        "dead\n",
        src_name);

    auto &reg_info = state.value_map[src_id];
    if (reg_info.ty == ValueInfo::REG_ALIAS ||
        reg_info.ty == ValueInfo::ASM_OPERAND) {
      state.redirect_aliased_regs_to_parent(src_id);
    }
    if (reg_info.ty == ValueInfo::REG_ALIAS) {
      state.remove_reg_alias(buf, src_id, reg_info.alias_reg_id);
    } else if (reg_info.ty == ValueInfo::ASM_OPERAND) {
      assert(state.operand_ref_counts[reg_info.operand_name] > 0);
      state.operand_ref_counts[reg_info.operand_name] -= 1;
    }

    reg_info.ty = ValueInfo::SCRATCHREG;
    reg_info.is_dead = true;
#endif
  }

  return true;
}

bool handle_end_of_block(GenerationState &state,
                         std::string &buf,
                         llvm::MachineBasicBlock *block) {
  const auto collect_liveouts =
      [block, &state, &buf](std::unordered_map<unsigned, unsigned> &liveouts) {
        for (auto &reg : block->liveouts()) {
          if (state.target->reg_should_be_ignored(reg.PhysReg)) {
            continue;
          }

          const auto reg_id = state.target->reg_id_from_mc_reg(reg.PhysReg);
          const auto reg_size = reg_size_bytes(state.func, reg.PhysReg);
          if (!liveouts.contains(reg_id)) {
            state.fmt_line(buf,
                           4,
                           "// {} is live-out",
                           state.target->reg_name_lower(reg_id));
            liveouts.emplace(reg_id, reg_size);
          } else {
            if (liveouts[reg_id] < reg_size) {
              liveouts[reg_id] = reg_size;
            }
          }
        }
      };

  // TODO(ts): in the first block, allocate all live outgoing scratchregs
  // and set all others to be dead (and maybe to an invalid type?)
  if (block->isEntryBlock()) {
    // <reg, size>
    std::unordered_map<unsigned, unsigned> func_used_regs{};
    // collect all liveouts with size information
    collect_liveouts(func_used_regs);

    // need to collect all registers that may be used in the function
    // and allocate them
    for (auto it = ++block->getIterator(); it != state.func->end(); ++it) {
      for (const auto &inst : *it) {
        for (const auto &op : inst.operands()) {
          if (!op.isReg() || op.isImplicit()) {
            // implicit defs/uses should be handled through the
            // fixed allocation
            continue;
          }

          if (state.target->reg_should_be_ignored(op.getReg()) ||
              !op.getReg().isValid()) {
            continue;
          }

          const auto reg_id = state.target->reg_id_from_mc_reg(op.getReg());
          if (func_used_regs.contains(reg_id)) {
            continue;
          }

          // reg_id is not a liveout of the entry block so we
          // don't need to copy anything into it but the scratch
          // for it needs to be allocated
          func_used_regs[reg_id] = 0;
          state.fmt_line(buf,
                         4,
                         "// {} is used in the function later on",
                         state.target->reg_name_lower(reg_id));
        }
      }
    }

    // allocate all used registers and resolve aliases if needed
    auto asm_ops_seen = std::unordered_set<std::string>{};
    for (auto [reg, size] : func_used_regs) {
      state.fmt_line(
          buf, 4, "// Handling register {}", state.target->reg_name_lower(reg));

      if (!state.value_map.contains(reg)) {
        assert(size == 0);
        state.fmt_line(buf,
                       4,
                       "// {} is not live-out and needs to be allocated",
                       state.target->reg_name_lower(reg));
        state.fmt_line(buf,
                       4,
                       "scratch_{}.alloc({});",
                       state.target->reg_name_lower(reg),
                       state.target->reg_bank(reg));

        state.value_map[reg] =
            ValueInfo{.ty = ValueInfo::SCRATCHREG, .is_dead = true};
        continue;
      }

      auto *info = &state.value_map[reg];
      if (size == 0) {
        if (info->ty == ValueInfo::SCRATCHREG) {
          state.fmt_line(buf,
                         4,
                         "// {} is not live-out but is already allocated",
                         state.target->reg_name_lower(reg));
          continue;
        }

        state.fmt_line(buf,
                       4,
                       "// {} is not live-out but is currently "
                       "aliased so needs to be allocated",
                       state.target->reg_name_lower(reg));
        // we do not need to copy anything since it is not a liveout
        // of the entry block

        if (info->ty == ValueInfo::REG_ALIAS ||
            info->ty == ValueInfo::ASM_OPERAND) {
          if (info->ty == ValueInfo::ASM_OPERAND) {
            assert(!asm_ops_seen.contains(info->operand_name));
            asm_ops_seen.insert(info->operand_name);
            assert(state.operand_ref_counts[info->operand_name] > 0);
            state.operand_ref_counts[info->operand_name] -= 1;
          }

          // make sure regs depending on this reg get updated to
          // alias this register's alias since we do not copy
          // anything
          state.redirect_aliased_regs_to_parent(reg);

          if (info->ty == ValueInfo::REG_ALIAS) {
            assert(state.value_map.contains(info->alias_reg_id));
            state.value_map[info->alias_reg_id].aliased_regs.erase(reg);
          }
        }

        state.fmt_line(buf,
                       4,
                       "scratch_{}.alloc({});",
                       state.target->reg_name_lower(reg),
                       state.target->reg_bank(reg));
        info->ty = ValueInfo::SCRATCHREG;
        info->is_dead = true;
        continue;
      }

      assert(size > 0);
      if (info->ty == ValueInfo::SCRATCHREG) {
        state.fmt_line(buf,
                       4,
                       "// {} is already allocated",
                       state.target->reg_name_lower(reg));
        continue;
      }

      if (info->ty == ValueInfo::ASM_OPERAND) {
        assert(!asm_ops_seen.contains(info->operand_name));
        asm_ops_seen.insert(info->operand_name);
        assert(state.operand_ref_counts[info->operand_name] > 0);
        state.operand_ref_counts[info->operand_name] -= 1;

        state.fmt_line(buf,
                       4,
                       "// {} is mapped to operand {}, materializing it",
                       state.target->reg_name_lower(reg),
                       info->operand_name);
        // TODO(ts): we need to change the semantics of
        // try_salvage_or_materialize to not salvage if the scratch
        // register is already allocated, otherwise this will screw
        // up codegen with fixed regs
        state.fmt_line(buf,
                       4,
                       "try_salvage_or_materialize({}, "
                       "scratch_{}, {}, {});",
                       info->operand_name,
                       state.target->reg_name_lower(reg),
                       state.target->reg_bank(reg),
                       size);
      } else {
        assert(info->ty == ValueInfo::REG_ALIAS);
        const auto aliased_reg_id = state.resolve_reg_alias(buf, 4, reg);
        assert(state.value_map.contains(aliased_reg_id));
        const auto &alias_info = state.value_map[aliased_reg_id];
        if (alias_info.ty == ValueInfo::ASM_OPERAND) {
          state.fmt_line(buf,
                         4,
                         "// {} maps to operand {}",
                         state.target->reg_name_lower(aliased_reg_id),
                         alias_info.operand_name);
        }


        state.fmt_line(buf,
                       4,
                       "// copying {} into {} to resolve alias",
                       state.target->reg_name_lower(info->alias_reg_id),
                       state.target->reg_name_lower(reg));
        state.redirect_aliased_regs_to_parent(reg);
        state.remove_reg_alias(buf, reg, info->alias_reg_id);

        state.fmt_line(buf,
                       4,
                       "scratch_{}.alloc({});",
                       state.target->reg_name_lower(reg),
                       state.target->reg_bank(reg));
        std::string src_name;
        if (alias_info.ty == ValueInfo::ASM_OPERAND) {
          assert(!asm_ops_seen.contains(alias_info.operand_name));
          asm_ops_seen.insert(alias_info.operand_name);
          assert(state.operand_ref_counts[info->operand_name] > 0);
          state.operand_ref_counts[info->operand_name] -= 1;

          // TODO(ts): salvaging if possible
          src_name = std::format("inst{}_tmp_{}",
                                 state.cur_inst_id,
                                 state.target->reg_name_lower(aliased_reg_id));
          state.fmt_line(buf,
                         4,
                         "AsmReg {} = derived()->gval_as_reg({});",
                         src_name,
                         alias_info.operand_name);
        } else {
          src_name = std::format("scratch_{}.cur_reg",
                                 state.target->reg_name_lower(aliased_reg_id));
        }

        state.target->generate_copy(
            buf,
            4,
            state.target->reg_bank(reg),
            std::format("scratch_{}.cur_reg",
                        state.target->reg_name_lower(reg)),
            src_name,
            size);
      }

      info->ty = ValueInfo::SCRATCHREG;
      info->is_dead = false;
    }

    // reset the state of any other regs that will not be used anymore
    for (auto &[reg, info] : state.value_map) {
      if (func_used_regs.contains(reg)) {
        continue;
      }

      state.fmt_line(buf,
                     4,
                     "// Resetting the state of {} as it is unused "
                     "for the rest of the function",
                     state.target->reg_name_lower(reg));
      assert(info.aliased_regs.empty());

      if (info.ty == ValueInfo::REG_ALIAS) {
        state.redirect_aliased_regs_to_parent(reg);
        state.remove_reg_alias(buf, reg, info.alias_reg_id);
      } else if (info.ty == ValueInfo::ASM_OPERAND) {
        state.redirect_aliased_regs_to_parent(reg);
        assert(state.operand_ref_counts[info.operand_name] > 0);
        state.operand_ref_counts[info.operand_name] -= 1;
      }
      info.ty = ValueInfo::SCRATCHREG;
      info.is_dead = true;
    }
  } else {
    // in the other blocks we just need to make sure all register
    // aliases are resolved at the end
    std::unordered_map<unsigned, unsigned> block_liveouts{};
    collect_liveouts(block_liveouts);

    for (auto &[reg, info] : state.value_map) {
      const auto reg_name = state.target->reg_name_lower(reg);
      if (!block_liveouts.contains(reg)) {
        if (info.ty != ValueInfo::SCRATCHREG) {
          state.fmt_line(buf,
                         4,
                         "// {} is not live-out, resetting its "
                         "state to be a ScratchReg",
                         reg_name);

          state.redirect_aliased_regs_to_parent(reg);
          if (info.ty == ValueInfo::REG_ALIAS) {
            state.remove_reg_alias(buf, reg, info.alias_reg_id);
          }
          info.ty = ValueInfo::SCRATCHREG;
        }
        info.is_dead = true;
        continue;
      }

      if (info.ty == ValueInfo::SCRATCHREG) {
        state.fmt_line(buf,
                       4,
                       "// {} is live-out but already in its correct place",
                       reg_name);
        continue;
      }

      state.fmt_line(
          buf, 4, "// {} is live-out and an alias, need to resolve", reg_name);
      if (info.ty == ValueInfo::ASM_OPERAND) {
        std::cerr << "Encountered AsmOperand at branch in "
                     "non-entry block\n";
        exit(1);
      }

      assert(info.ty == ValueInfo::REG_ALIAS);
      const auto aliased_reg_id = state.resolve_reg_alias(buf, 4, reg);


      state.redirect_aliased_regs_to_parent(reg);
      state.remove_reg_alias(buf, reg, info.alias_reg_id);

      state.target->generate_copy(
          buf,
          4,
          state.target->reg_bank(reg),
          std::format("scratch_{}.cur_reg", reg_name),
          std::format("scratch_{}.cur_reg",
                      state.target->reg_name_lower(aliased_reg_id)),
          block_liveouts[reg]);

      info.ty = ValueInfo::SCRATCHREG;
      info.is_dead = false;
    }
  }

  return true;
}

bool handle_terminator(std::string &buf,
                       GenerationState &state,
                       llvm::MachineInstr *inst) {
  if (inst->isReturn()) {
    // record the number of return regs
    {
      unsigned num_ret_regs = 0;
      std::vector<unsigned> ret_regs = {};
      for (const auto &mach_op : inst->uses()) {
        assert(mach_op.getType() == llvm::MachineOperand::MO_Register);
        if (mach_op.isUndef()) { // LR on AArch64
          continue;
        }
        const auto reg = mach_op.getReg();
        assert(reg.isPhysical());
        const auto reg_id = state.target->reg_id_from_mc_reg(reg);
        if (state.num_ret_regs == -1) {
          ret_regs.push_back(reg_id);
        } else {
          if (state.return_regs.size() <= num_ret_regs ||
              state.return_regs[num_ret_regs] != reg_id) {
            std::cerr << std::format(
                "ERROR: Found mismatching return with register "
                "{}({})",
                reg_id,
                state.target->reg_name_lower(reg_id));
            return false;
          }
        }
        ++num_ret_regs;
      }
      if (state.num_ret_regs == -1) {
        state.num_ret_regs = static_cast<int>(num_ret_regs);
        state.return_regs = std::move(ret_regs);
      }
    }

    if (state.func->size() > 1) {
      // need to resolve aliases for ret regs (all values should be
      // scratchregs by now) and then write the return stub for the end of
      // the function if we haven't done so

      const auto ret_op_for_idx = [inst](unsigned idx) {
        for (auto &op : inst->explicit_uses()) {
          if (!idx) {
            return op;
          }
          --idx;
        }
        std::cerr << "ERROR: Encountered OOB return register operand index\n";
        exit(1);
      };

      for (int idx = 0; idx < state.num_ret_regs; ++idx) {
        const auto reg = state.return_regs[idx];
        assert(state.value_map.contains(reg));

        state.fmt_line(buf,
                       4,
                       "// handling return for {}",
                       state.target->reg_name_lower(reg));
        auto cur_reg = reg;
        auto *info = &state.value_map[cur_reg];
        if (info->ty != ValueInfo::REG_ALIAS) {
          assert(info->ty == ValueInfo::SCRATCHREG);
          state.fmt_line(buf, 4, "// value already in register, nothing to do");
          continue;
        }

        while (info->ty == ValueInfo::REG_ALIAS) {
          buf += std::format("    // {} is an alias for {}\n",
                             state.target->reg_name_lower(cur_reg),
                             state.target->reg_name_lower(info->alias_reg_id));
          cur_reg = info->alias_reg_id;
          info = &state.value_map[cur_reg];
        }

        assert(info->ty == ValueInfo::SCRATCHREG);
        const auto &op = ret_op_for_idx(idx);
        assert(op.isReg());
        state.target->generate_copy(
            buf,
            4,
            state.target->reg_bank(cur_reg),
            std::format("scratch_{}.cur_reg",
                        state.target->reg_name_lower(reg)),
            std::format("scratch_{}.cur_reg",
                        state.target->reg_name_lower(cur_reg)),
            reg_size_bytes(state.func, op.getReg()));
      }

      // jump to convergence point
      if (inst->getParent()->getNextNode() == nullptr) {
        state.fmt_line(buf,
                       4,
                       "// Omitting jump to convergence as this is the "
                       "last block");
      } else {
        state.fmt_line(buf,
                       4,
                       "// Jumping to convergence point at the end of "
                       "the encoding function");
        state.fmt_line(buf,
                       4,
                       "derived()->generate_raw_jump(CompilerX64::Jump::"
                       "jmp, ret_converge_label);\n");
      }

      // remove all aliases and reset them to scratch regs so other blocks
      // don't think that values are aliased
      for (auto &[reg, info] : state.value_map) {
        if (info.ty != ValueInfo::REG_ALIAS) {
          // for now, we assume that if there are multiple blocks, the
          // first block will materialize everything into scratch
          // registers at the end of it
          assert(info.ty != ValueInfo::ASM_OPERAND);
          continue;
        }
        state.fmt_line(buf,
                       4,
                       "// Resetting alias status for {}",
                       state.target->reg_name_lower(reg));
        info.ty = ValueInfo::SCRATCHREG;
        assert(state.value_map.contains(info.alias_reg_id));
        state.value_map[info.alias_reg_id].aliased_regs.erase(reg);
      }

      return true;
    }

    // if there is only one block, we can return from the encoding function

    auto &ret_buf = state.one_bb_return_assignments;
    auto asm_ops_seen = std::unordered_set<std::string>{};
    for (int idx = 0; idx < state.num_ret_regs; ++idx) {
      const auto reg = state.return_regs[idx];
      state.fmt_line(ret_buf,
                     4,
                     "// returning reg {} as result_{}",
                     state.target->reg_name_lower(reg),
                     idx);
      assert(state.value_map.contains(reg));
      auto cur_reg = reg;
      auto *info = &state.value_map[reg];
      while (info->ty == ValueInfo::REG_ALIAS) {
        state.fmt_line(ret_buf,
                       4,
                       "// {} is an alias for {}",
                       state.target->reg_name_lower(cur_reg),
                       state.target->reg_name_lower(info->alias_reg_id));
        cur_reg = info->alias_reg_id;
        info = &state.value_map[cur_reg];
      }

      if (info->ty == ValueInfo::ASM_OPERAND) {
        assert(!asm_ops_seen.contains(info->operand_name));
        asm_ops_seen.insert(info->operand_name);
        state.fmt_line(ret_buf,
                       4,
                       "// {} is an alias for {}",
                       state.target->reg_name_lower(cur_reg),
                       info->operand_name);

        unsigned tmp_idx = idx;
        for (auto &op : inst->explicit_uses()) {
          if (tmp_idx > 0) {
            --tmp_idx;
            continue;
          }

          state.fmt_line(ret_buf,
                         4,
                         "try_salvage_or_materialize({}, "
                         "result_{}, {}, {});",
                         info->operand_name,
                         idx,
                         state.target->reg_bank(cur_reg),
                         reg_size_bytes(state.func, op.getReg()));
        }
      } else {
        assert(info->ty == ValueInfo::SCRATCHREG);
        state.fmt_line(ret_buf,
                       4,
                       "result_{} = std::move(scratch_{});",
                       idx,
                       state.target->reg_name_lower(cur_reg));
        // no more housekeeping needed
      }
    }

    return true;
  } else if (inst->isBranch()) {
    state.fmt_line(buf, 4, "// Preparing jump to other block");

    if (!handle_end_of_block(state, buf, inst->getParent())) {
      return false;
    }
    // at last, jump to the target basic block

    std::string jump_code{};
    if (inst->isIndirectBranch()) {
      assert(0);
      std::cerr << "ERROR: encountered indirect branch\n";
      return false;
    }

    if (inst->isUnconditionalBranch()) {
      jump_code = "jmp";
    } else {
      // just assume the first immediate is the condition code
      for (const auto &op : inst->explicit_uses()) {
        if (!op.isImm()) {
          continue;
        }
        assert(op.getImm() >= 0);
        jump_code = state.target->jump_code(op.getImm());
      }

      if (jump_code.empty()) {
        assert(0);
        std::cerr << "ERROR: encountered jump without known condition code\n";
        return false;
      }
    }
    llvm::MachineBasicBlock *target = nullptr;
    for (const auto &op : inst->explicit_uses()) {
      if (!op.isMBB()) {
        continue;
      }

      if (target != nullptr) {
        assert(0);
        std::cerr << "ERROR: multiple block targets for branch\n";
        return false;
      }
      target = op.getMBB();
    }
    if (target == nullptr) {
      assert(0);
      std::cerr << "ERROR: could not find block target for branch\n";
      return false;
    }

    state.fmt_line(buf,
                   4,
                   "derived()->generate_raw_jump(Derived::Jump::{}, "
                   "block{}_label);",
                   jump_code,
                   target->getNumber());

    buf += '\n';
    buf += '\n';
    return true;
  }

  std::string tmp;
  llvm::raw_string_ostream os(tmp);
  inst->print(os);

  std::cerr << std::format("ERROR: Encountered unknown terminator {}\n", tmp);
  return false;
}

bool encode_prepass(llvm::MachineFunction *func, GenerationState &state) {
  for (auto bb_it = func->begin(); bb_it != func->end(); ++bb_it) {
    for (auto inst_it = bb_it->begin(); inst_it != bb_it->end(); ++inst_it) {
      llvm::MachineInstr &inst = *inst_it;
      if (inst.isDebugInstr() || inst.getFlag(llvm::MachineInstr::FrameSetup) ||
          inst.getFlag(llvm::MachineInstr::FrameDestroy) ||
          inst.isCFIInstruction()) {
        continue;
      }

      if (inst.isTerminator() || inst.isPseudo() ||
          state.target->inst_should_be_skipped(inst)) {
        continue;
      }

      const llvm::MCInstrDesc &MCID =
          state.func->getTarget().getMCInstrInfo()->get(inst.getOpcode());
      for (auto reg : MCID.implicit_defs()) {
        if (state.target->reg_should_be_ignored(reg)) {
          continue;
        }
        state.maybe_fixed_regs.insert(state.target->reg_id_from_mc_reg(reg));
      }
      for (auto reg : MCID.implicit_uses()) {
        if (state.target->reg_should_be_ignored(reg)) {
          continue;
        }
        state.maybe_fixed_regs.insert(state.target->reg_id_from_mc_reg(reg));
      }
    }
  }

  return true;
}

} // namespace

namespace tpde_encgen {
bool create_encode_function(llvm::MachineFunction *func,
                            std::string_view name,
                            std::string &decl_lines,
                            std::string &sym_lines,
                            std::string &impl_lines) {
  std::string write_buf{};

  // update dead/kill flags since they might not be very accurate anymore
  // NOTE: we assume that the MBB liveins are accurate though
  for (auto &MBB : *func) {
    llvm::recomputeLivenessFlags(MBB);
  }

  // write MachineIR as comment at the start of the function
  {
    std::string tmp;
    llvm::raw_string_ostream os(tmp);
    func->print(os);

    auto view = std::string_view{tmp};
    while (!view.empty()) {
      write_buf += "    // ";

      const auto pos = view.find('\n');
      if (pos == std::string_view::npos) {
        write_buf += view;
      } else {
        write_buf += view.substr(0, pos);
        view = view.substr(pos + 1);
      }
      write_buf += '\n';
    }
    write_buf += '\n';
  }

  // TODO(ts): check for the target feature flags of the function
  // and if they are not default, emit a check at the beginning for it

  // TODO(ts): parameterize
  std::unique_ptr<EncodingTarget> target;
  switch (func->getTarget().getTargetTriple().getArch()) {
  case llvm::Triple::x86_64:
    target = std::make_unique<x64::EncodingTargetX64>(func);
    break;
  case llvm::Triple::aarch64:
    target = std::make_unique<arm64::EncodingTargetArm64>(func);
    break;
  default: assert(false && "unsupported target"); abort();
  }
  GenerationState state{
      .func = func, .target = target.get(), .is_first_block = true};

  if (!encode_prepass(func, state)) {
    std::cerr << "Prepass failed\n";
    return false;
  }

// TODO(ts): this can't work if a IR value corresponds to multiple registers
// so we should parse debug info but that does not always exist
#if 0
    // try to get the names for the IR arguments
    auto &llvm_func = func->getFunction();
    for (llvm::Argument &arg : llvm_func.args()) {
        if (arg.hasName() && !arg.getName().empty()) {
            if (std::find(state.param_names.begin(),
                          state.param_names.end(),
                          arg.getName())
                != state.param_names.end()) {
                std::cerr << std::format(
                    "ERROR: argument '{}' already exists in function '{}'!\n",
                    arg.getName().str(),
                    llvm_func.getName().str());
                return false;
            }
            state.param_names.push_back(arg.getName().str());
        } else {
            auto name = std::format("param_{}", state.param_names.size());
            if (std::find(
                    state.param_names.begin(), state.param_names.end(), name)
                != state.param_names.end()) {
                std::cerr << std::format(
                    "ERROR: argument '{}' already exists in function '{}'!\n",
                    name,
                    llvm_func.getName().str());
                return false;
            }
            state.param_names.push_back(std::move(name));
        }
    }
#endif

  auto &mach_reg_info = func->getRegInfo();
  // const auto &target_reg_info = func->getSubtarget().getRegisterInfo();


  // map inputs
  {
    unsigned idx = 0;
    for (auto it = mach_reg_info.livein_begin(),
              end = mach_reg_info.livein_end();
         it != end;
         ++it) {
      auto name = std::format("param_{}", state.param_names.size());
      state.param_names.push_back(std::move(name));

      const auto reg_id = target->reg_id_from_mc_reg(it->first);
      assert(!state.used_regs.contains(reg_id));
      state.used_regs.insert(reg_id);
      state.value_map[reg_id] = ValueInfo{
          .ty = ValueInfo::ASM_OPERAND,
          .operand_name = state.param_names[idx++],
          // .cur_def_use_it =
          //    mach_reg_info.reg_instr_nodbg_begin(it->first)
      };

      state.operand_ref_counts[state.param_names.back()] = 1;

      std::format_to(std::back_inserter(write_buf),
                     "    // Mapping {} to {}\n",
                     target->reg_name_lower(reg_id),
                     state.value_map[reg_id].operand_name);
    }
  }

  std::string write_buf_inner{};
  {
    const auto multiple_bbs = func->size() > 1;

    if (multiple_bbs) {
      write_buf += "\n    // Creating label for convergence point at the "
                   "end of the function\n";
      write_buf += "    Label ret_converge_label = "
                   "derived()->assembler.label_create();\n";
      write_buf += "    // Creating labels for blocks that are jump targets\n";
    }

    for (auto bb_it = func->begin(); bb_it != func->end(); ++bb_it) {
      if (multiple_bbs && !bb_it->pred_empty()) {
        std::format_to(std::back_inserter(write_buf),
                       "    Label block{}_label = "
                       "derived()->assembler.label_create();\n",
                       bb_it->getNumber());

        std::format_to(std::back_inserter(write_buf_inner),
                       "    // Starting encoding of block {}\n",
                       bb_it->getNumber());
        std::format_to(std::back_inserter(write_buf_inner),
                       "    derived()->assembler.label_place(block{}_label);\n",
                       bb_it->getNumber());

        std::vector<unsigned> live_in_regs{};
        for (auto &livein : bb_it->liveins()) {
          if (state.target->reg_should_be_ignored(livein.PhysReg)) {
            continue;
          }

          const auto reg_id = state.target->reg_id_from_mc_reg(livein.PhysReg);
          assert(state.value_map.contains(reg_id));
          state.fmt_line(write_buf_inner,
                         4,
                         "// Marking {} as live",
                         state.target->reg_name_lower(reg_id));
          state.value_map[reg_id].is_dead = false;

          if (std::find(live_in_regs.begin(), live_in_regs.end(), reg_id) ==
              live_in_regs.end()) {
            live_in_regs.push_back(reg_id);
          }
        }

        for (auto &[reg, info] : state.value_map) {
          if (std::find(live_in_regs.begin(), live_in_regs.end(), reg) !=
              live_in_regs.end()) {
            continue;
          }

          info.is_dead = true;
        }
      }

      for (auto inst_it = bb_it->begin(); inst_it != bb_it->end(); ++inst_it) {
        llvm::MachineInstr *inst = &(*inst_it);
        if (inst->isDebugInstr() ||
            inst->getFlag(llvm::MachineInstr::FrameSetup) ||
            inst->getFlag(llvm::MachineInstr::FrameDestroy) ||
            inst->isCFIInstruction()) {
          continue;
        }


        std::string tmp;
        llvm::raw_string_ostream os(tmp);
        inst->print(os, true, false, true, true);
        write_buf_inner += "\n\n    // ";
        write_buf_inner += tmp;
        write_buf_inner += "\n";

        if (!generate_inst(write_buf_inner, state, inst)) {
          std::cerr << std::format(
              "ERROR: Failed to generate code for instruction {}\n", tmp);
          return false;
        }
        ++state.cur_inst_id;
      }
      if (bb_it->empty() || !bb_it->back().isTerminator()) {
        if (!handle_end_of_block(state, write_buf_inner, &*bb_it)) {
          return false;
        }
      }
      state.is_first_block = false;
    }


    if (multiple_bbs) {
      // separate block labels from ScratchRegs
      write_buf += '\n';

      // write the moves into the result register at the end of the
      // function
      write_buf_inner += '\n';
      state.fmt_line(write_buf_inner,
                     4,
                     "// Placing the convergence point for registers here");
      state.fmt_line(write_buf_inner,
                     4,
                     "derived()->assembler.label_place(ret_converge_label);");

      assert(state.num_ret_regs >= 0);
      for (int idx = 0; idx < state.num_ret_regs; ++idx) {
        const auto reg = state.return_regs[idx];
        assert(state.value_map.contains(reg));
        assert(state.value_map[reg].ty == ValueInfo::SCRATCHREG);
        state.fmt_line(state.one_bb_return_assignments,
                       4,
                       "result_{} = std::move(scratch_{});",
                       idx,
                       state.target->reg_name_lower(reg));
      }
    }
  }

  // create ScratchRegs
  for (const auto &[reg, info] : state.value_map) {
    std::format_to(std::back_inserter(write_buf),
                   "    ScratchReg scratch_{}{{derived()}};\n",
                   state.target->reg_name_lower(reg));
  }

  // create param conditions
  for (const auto &cond : state.asm_operand_early_conds) {
    std::format_to(std::back_inserter(write_buf), "    {};\n", cond);
  }

  // handle fixed registers
  std::string fixed_reg_param_names{};
  for (const auto &name : state.param_names) {
    if (!fixed_reg_param_names.empty()) {
      fixed_reg_param_names += ", ";
    }
    fixed_reg_param_names += '&';
    fixed_reg_param_names += name;
  }

  for (const auto &[reg, fix_conds] : state.fixed_reg_conds) {
    const auto reg_name_lower = state.target->reg_name_lower(reg);
    auto reg_name_upper = std::string{reg_name_lower};
    std::transform(reg_name_upper.begin(),
                   reg_name_upper.end(),
                   reg_name_upper.begin(),
                   toupper);

    state.fmt_line(write_buf,
                   4,
                   "FixedRegBackup reg_backup_{} = {{.scratch = "
                   "ScratchReg{{derived()}}}};",
                   reg_name_lower);

    auto if_written = false;
    if (!std::any_of(fix_conds.begin(), fix_conds.end(), [](const auto &e) {
          return e.empty();
        })) {
      // no use is unconditional, so we emit an if to not always fix the
      // register
      if_written = true;
      auto if_conds = std::string{};
      for (auto i = 0u; i < fix_conds.size(); ++i) {
        if (std::find(fix_conds.begin(), fix_conds.begin() + i, fix_conds[i]) !=
            fix_conds.begin() + i) {
          continue;
        }
        if (!if_conds.empty()) {
          if_conds += " || ";
        }
        if_conds += '(';
        if_conds += fix_conds[i];
        if_conds += ')';
      }
      write_buf += "    if (";
      write_buf += if_conds;
      write_buf += ") {\n";
    }

    state.fmt_line(write_buf,
                   if_written ? 8 : 4,
                   "scratch_alloc_specific(AsmReg::{}, scratch_{}, {{{}}}, "
                   "reg_backup_{});",
                   reg_name_upper,
                   reg_name_lower,
                   fixed_reg_param_names,
                   reg_name_lower);

    if (if_written) {
      write_buf += "    }\n";
    }

    const auto is_ret_reg =
        std::find(state.return_regs.begin(), state.return_regs.end(), reg) !=
        state.return_regs.end();
    state.fmt_line(write_buf_inner,
                   4,
                   "scratch_check_fixed_backup(scratch_{}, reg_backup_{}, {});",
                   reg_name_lower,
                   reg_name_lower,
                   is_ret_reg ? "true" : "false");
  }

  // assignments for the results need to be after the check for fixed reg
  // backups
  write_buf_inner += state.one_bb_return_assignments;

  // TODO
  // for now, always return true
  state.fmt_line(write_buf_inner, 4, "return true;");

  std::string func_decl{};
  // finish up function header
  {
    std::format_to(
        std::back_inserter(impl_lines),
        "template <typename Adaptor,\n"
        "          typename Derived,\n"
        "          template <typename, typename, typename>\n"
        "          class BaseTy,\n"
        "          typename Config>"
        "bool EncodeCompiler<Adaptor, Derived, BaseTy, Config>::encode_{}(",
        name);
    std::format_to(std::back_inserter(decl_lines), "    bool encode_{}(", name);
  }

  auto first = true;
  for (auto &param : state.param_names) {
    if (!first) {
      std::format_to(std::back_inserter(func_decl), ", ");
    } else {
      first = false;
    }
    std::format_to(
        std::back_inserter(func_decl), "GenericValuePart &&{}", param);
  }

  if (state.num_ret_regs == -1) {
    // TODO(ts): might mean no return
    std::cerr
        << "ERROR: number of return registers not set at end of function\n";
    return false;
  }

  for (int i = 0; i < state.num_ret_regs; ++i) {
    if (!first) {
      std::format_to(std::back_inserter(func_decl), ", ");
    } else {
      first = false;
    }
    std::format_to(std::back_inserter(func_decl), "ScratchReg &result_{}", i);
  }

  func_decl += ')';

  decl_lines += func_decl;
  decl_lines += ";\n";

  impl_lines += func_decl;
  impl_lines += " {\n";

  impl_lines += write_buf;
  impl_lines += write_buf_inner;

  impl_lines += "\n}\n\n";

  for (const auto cp_idx : state.const_pool_indices_used) {
    sym_lines +=
        std::format("    SymRef sym_{}_cp{} = Assembler::INVALID_SYM_REF;\n",
                    state.func->getName().str(),
                    cp_idx);
  }

  return true;
}
} // namespace tpde_encgen
