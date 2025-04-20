\page tpde-compiler-ref Compiler Reference

Content:
- base class hierarchy
- settings
- required functions to implement for user
- how to implement an instruction (very simple, then use the other bullet points to go more advanced)
- how to use Value(Part)Refs
- constants using ValRefSpecial
- materializing constants
- fusing instructions
- how to do branching/return
- how to do calls
- manual stack slot allocation
- custom var ref handling
- arch-specific:
  - calling conv
  - stack frame structure (ref to epilogue/prologue code)
- assembler reference (what unwind info is generated?, how to make object, how to do relocations [ex: globals?])
- at least mention (c++) exceptions
- can always use LLVM implementation as guidance since it uses most features

```
┌─────────┐         ┌────────────┐         ┌─────────┐
│IRAdaptor◄─ ─ ─ ─ ─┤CompilerBase├─ ─ ─ ─ ─►Assembler│
└─────────┘         └─────▲──────┘         └─────────┘
                          │                           
                          │                           
                  ┌───────┴────────┐                  
                  │UserCompilerBase│                  
                  └───────▲────────┘                  
                          │                           
                          │                           
                   ┌──────┴───────┐                   
                   │Compiler{Arch}│                   
                   └──────▲───────┘                   
                          │                           
                          │                           
                 ┌────────┴─────────┐                 
                 │UserCompiler{Arch}│                 
                 └──────────────────┘               
```

This is the reference for the `CompilerBase` and related classes. It is the main driver of compilation and the class you as the user will
in the end create an instance of to start compilation.

The reference starts by explaining the basic class hierarchy used in the compiler, ...

[TOC]

## Class Hierarchy
- Compiler uses static polymorphism, i.e. templates using CRTP to adapt the compiler to your IR and use your provided functions
- CompilerBase takes three template parameters
  - Adaptor class type
  - final derived type
  - config for Assembler type, some flags and architecture-specific information
- architecture-specific Compiler class (currently x64/arm64) which take 4 template parameters
  - Adaptor class type
  - final derived type
  - base compiler type (can be CompilerBase or a user-provided base class that inherits from CompilerBase that can be used for architecture-independent functionality)
  - config for CompilerBase config and some architecture-specific config
- final compiler class which inherits from the architecture-specific compiler class and provides the template types
- many funcs are called in the base classes using the derived class giving you the option to override or inject custom behavior

## Config
- type which provides typedefs and constants used to configure the compiler
- mostly internal or currently not useful (defined in CompilerConfig.hpp/CompilerX64.hpp/CompilerA64.hpp)
- e.g. provides typedef `Assembler` which tells the compiler which assembler to use, but currently only one assembler supported for each architecture
- most interesting option is `DEFAULT_VAR_REF_HANDLING` which tells the compiler whether or not it should setup
  the assignments for static stack allocs or whether you will do that. This is handy when implementing support for global values, see later

## Required functions to implement
- concept described in Compiler.hpp/CompilerX64.hpp/CompilerA64.hpp

### General

#### cur_func_may_emit_calls
```cpp
bool cur_func_may_emit_calls() const noexcept;
```
Can the compilation of any instruction in the current function emit calls? This may be used to optimize register allocation or stack frame setup. If you can cheaply provide the answer,
you should. Otherwise you currently should always return `true`.

#### try_force_fixed_assignment
```cpp
bool try_force_fixed_assignment(IRValueRef val) const noexcept;
```
Should the register allocator try to allocate a fixed register for `val` even if its heuristic tells it not to.
Mostly useful for debugging, you should return `false`.

#### val_parts
```cpp
struct ValParts {
    u32 count() const noexcept;
    u32 size_bytes(u32 part_idx) const noexcept;
    RegBank reg_bank(u32 part_idx) const noexcept;
};

ValParts val_parts(IRValueRef val) const noexcept;
```
This functions should return an object that provides information about how a value is divided into parts.
Each part corresponds to a register the value should be stored in.
For each value, you need to provide the number of parts and for each part its size in bytes and register bank.
The [RegBank](@ref RegBank) type is defined in [RegisterFile.hpp](@ref tpde/include/tpde/RegisterFile.hpp). The possible
banks are defined in the PlatformConfigs provided by the architecture-specific Compiler classes.

#### val_ref_special
```cpp
using ValRefSpecial = /* optional special ValRef type */;

std::optional<ValRefSpecial> val_ref_special(IRValueRef val) noexcept;
```
With this function you have the possibility to return custom `ValueRef`s when you or the compiler calls `value_ref` with `val`.
This is useful to implement constants that should not be materialized in registers if not needed or otherwise do not need an assignment.
There is a default `ValRefSpecial` defined in `CompilerBase` but you can optionally override this typedef if you need to store additional information
for when individual parts of that value are accessed.

More about this later when explaining how to implement constants.

> [!warning]
> The `ValRefSpecial` type is used in a union so it must be a standard-layout struct and have `u8 mode` as its first member which has to have a value
> greater than 3.

#### val_part_ref_special
```cpp
ValuePartRef val_part_ref_special(ValRefSpecial& val_ref_special, u32 part_idx) noexcept;
```
When you return a `ValRefSpecial` in `val_ref_special` and a part of it is accessed, this function is called and you need to provide a `ValuePartRef`
for it.

More details about what `ValuePartRef`s can be found below.

#### define_func_idx
```cpp
void define_func_idx(IRFuncRef func, u32 idx) noexcept;
```
Internally, the compiler numbers functions and this callback gives you the chance to save that number if you need it later, e.g. to access the created
function symbols. This function is optional to implement.

#### setup_var_ref_assignments
```cpp
void setup_var_ref_assignments() noexcept;
```
This function is called when you set `DEFAULT_VAR_REF_HANDLING` to `false` in the config, before the prologue of the function is written,
asking you to setup assignments for all values which are marked to be "variable references". The details of this are explained further down below.

#### compile_inst
```cpp
bool compile_inst(IRInstRef inst, InstRange remaining_instructions) noexcept;
```
This function is called when the compiler iterates through blocks in the code generation pass and asks you to generate machine code for an instruction
and update the value assignments for the result values of the instruction.
The return value indicates whether compilation of the instruction failed.

How to implement this function is explained in the next section

### x86-64

#### cur_call_conv
TODO

### AArch64

## How to compile instructions

### very simple
- simple example: instruction without any operands, no result, no control flow
- each architecture-compiler provides `ASM*` macros to append instructions to the current function
- use assembler libraries fadec and disarm for encoding, instruction name for the macro derived from the naming in the libraries
```cpp
bool compile_first_inst() {
    ASM(AND32ri, AsmReg::AX, 0xFFFFFFFF);
    return true;
}
```
- registers given to the encoders do not encode their size, this is given in the instruction name

### defining results
- suppose we have instruction that simply returns a zero value
- have to get result [ValueRef](@ref ValueRef)
- [ValueRef](@ref ValueRef) is a RAII wrapper that manages the refcount for a value
- analyzer counts definition and each use (so you will have to retrieve a `ValueRef` for each definition or use as an operand)
- on destruction `ValueRef` will decrement refcount and free associated resources if the value becomes unused
- allows some manual refcounting if necessary (e.g. for fusing)
- when *defining* a value we use the [result_ref](@ref CompilerBase::result_ref) function to get the [ValueRef](@ref ValueRef)
- for each part that we use we can get a [ValuePartRef](@ref ValuePartRef) using [part](@ref ValueRef::part)
- allows to allocate a register when defining a value using [alloc_reg](@ref ValuePartRef::alloc_reg)
- NOTE: this *locks* the register for the `ValuePartRef` meaning it cannot be evicted until the `ValuePartRef` is destructed or you call [unlock](@ref ValuePartRef::unlock)
- when value defined, you have to call [set_modified](@ref ValuePartRef::set_modified) to tell the framework that the value needs to be spilled if the register is reused
```cpp
bool compile_zero_inst(IRInstRef inst) {
    IRValueRef res_val = /* IR-specific way to get result for instruction */;
    ValueRef res_ref = this->result_ref(res_val);
    ValuePartRef res_part = res_ref.part(0); // in this case, we assume the value is <= 64 bit
    AsmReg res_reg = res_part.alloc_reg(); // allocate a register for the part
    ASM(XOR32rr, res_reg, res_reg); // zero it
    res_part.set_modified(); // tell the framework the part is modified
    return true;
}
```

- if value has multiple parts to zero, e.g. 128 bit int on 64 bit arch, simply get multiple parts
```cpp
bool compile_zero_inst128(IRInstRef inst) {
    IRValueRef res_val = /* IR-specific way to get result for instruction */;
    ValueRef res_ref = this->result_ref(res_val);
    // clear the lower 64 bits
    ValuePartRef res_part = res_ref.part(0);
    AsmReg res_reg = res_part.alloc_reg();
    ASM(XOR32rr, res_reg, res_reg);
    res_part.set_modified();
    // clear the upper 64 bits
    res_part = res_ref.part(1);
    res_reg = res_part.alloc_reg();
    ASM(XOR32rr, res_reg, res_reg);
    res_part.set_modified();
    return true;
}
```

- in case you only need part 0, you can get the `ValueRef` and `ValuePartRef` in an `std::pair` using [result_ref_single](@ref CompilerBase::result_ref_single)
```cpp
auto [res_ref, res_part] = this->result_ref_single(res_val);
```

### using operands
- assume we have a 64 bit add
- need to get lhs and rhs operand
- similar to result: get [ValueRef](@ref ValueRef) and then the necessary [ValuePartRef](@ref ValuePartRef)s
- when using as operand, use [val_ref](@ref CompilerBase::val_ref) function, or [val_ref_single](@ref CompilerBase::val_ref_single)
- to get register, use [load_to_reg](@ref ValuePartRef::load_to_reg) which will also lock the register and reload the value from the stack if necessary
```cpp
bool compile_add64(IRInstRef inst) {
    IRValueRef lhs_val = /* IR-specific way to get operand */;
    IRValueRef rhs_val = /* IR-specific way to get operand */;
    IRValueRef res_val = /* IR-specific way to get result */;
    auto [lhs_ref, lhs_part] = this->val_ref_single(lhs_val);
    auto [rhs_ref, rhs_part] = this->val_ref_single(rhs_val);
    auto [res_ref, res_part] = this->result_ref_single(res_val);

    // load and allocate registers
    AsmReg lhs_reg = lhs_part.load_to_reg();
    AsmReg rhs_reg = rhs_part.load_to_reg();
    AsmReg res_reg = res_part.alloc_reg();

    // encode instruction
    // lhs_reg might be different from res_reg so we have to move first since we cannot override the register of an operand
    ASM(MOV64rr, res_reg, lhs_reg);
    // then do the add
    ASM(ADD64rr, res_reg, rhs_reg);
    res_part.set_modified();
    return true;
}
```

### reusing operand registers
- previouse encoding inefficient
- if lhs is not used after the instruction, we could reuse register ("salvage" the register)
- to do this if possible we will use the [into_temporary](@ref ValuePartRef::into_temporary) function
- will give use an owning reference to the register if the value is dead after this instruction or make a copy into a new register
```cpp
bool compile_add64(IRInstRef inst) {
    IRValueRef lhs_val = /* IR-specific way to get operand */;
    IRValueRef rhs_val = /* IR-specific way to get operand */;
    IRValueRef res_val = /* IR-specific way to get result */;
    auto [lhs_ref, lhs_part] = this->val_ref_single(lhs_val);
    auto [rhs_ref, rhs_part] = this->val_ref_single(rhs_val);
    auto [res_ref, res_part] = this->result_ref_single(res_val);

    ValuePartRef tmp_part = lhs_part.into_temporary();

    // load and allocate registers
    AsmReg lhs_reg = tmp_part.cur_reg(); // tmp_part is guaranteed to already own a register
    AsmReg rhs_reg = rhs_part.load_to_reg();
    // do not allocate a register for the result

    // encode instruction
    // lhs_reg contains the value of lhs_part and will also hold the result
    ASM(ADD64rr, lhs_reg, rhs_reg);

    // transfer ownership of the register to res_part (note the std::move here)
    res_part.set_value(std::move(tmp_part));
    // no need to call set_modified, set_value will implicitly do that
    return true;
}
```
- note that in this case we do not need to get the result `ValueRef` before encoding the function and can transform it into a one-liner:
`this->result_ref(res_val).part(0).set_value(std::move(tmp_part))`

- to salvage either the lhs or rhs we can check whether we can actually salvage the register using [can_salvage](@ref ValuePartRef::can_salvage)
- then populate the ValuePartRef using either `lhs_part` or `rhs_part`

### temporary registers
- sometimes temporary registers required for computing result and using `into_temporary` is not sufficient
- allocated and free'd using [ScratchReg](@ref ScratchReg)
- allocate using [alloc](@ref ScratchReg::alloc), [alloc_gp](@ref ScratchReg::alloc_gp) or a specific register using [alloc_specific](@ref ScratchReg::alloc_specific)
- use the `AsmReg` as desired
- register is free'd on desctruction of the `ScratchReg` or when calling [reset](@ref ScratchReg::reset)

## Constants using ValRefSpecial
- constants relatively common feature
- they might not have a local index assigned by the adaptor
- might want to handle them just as normal values for code that does not need to care
- every time `val_ref` is called it will call `val_ref_special` in your compiler
- check if the value is a constant and then return a `ValRefSpecial`
- suppose we use the default [ValRefSpecial](@ref CompilerBase::ValRefSpecial) from CompilerBase since we only support 64 bit constants
- then implement `val_ref_special` like this
```cpp
std::optional<ValRefSpecial> val_ref_special(IRValueRef value) noexcept {
    if (/* IR-specific way to check if value is not constant */) {
        return std::nullopt;
    }

    u64 constant = /* IR-specific way to get constant data from value */;
    return ValRefSpecial{.mode = 4, .const_data = constant};
}
```

- since constants are only 64 bit, only part 0 will be accessed for x86-64
- in `val_part_ref_special` we can then return a constant `ValuePartRef` which will handle materialization for us
```cpp
ValuePartRef val_part_ref_special(ValRefSpecial& val_ref, u32 part_idx) noexcept {
    assert(part_idx == 0);
    // ValuePartRef has a constructor that takes 64 bits of constant data, its actual size in bytes and the register bank for the constant
    // in our case the size will be 8 and the constant will be an integer
    return ValuePartRef{val_ref.const_data, 8, Config::GP_BANK};
```
- for constants larger than 64 bits, you will need to allocate them somewhere and then give the `ValuePartRef` a pointer to the constant data (e.g. for vector constants)
- if you want to store more information about constants, you will need to define your own `ValRefSpecial` struct
- LLVM implementation can be used as a reference

### optimizing constant operands
- might want to optimize instruction selection if an operand is a constant since many instructions can take a register or immediate operand
- doable by checking if a part is constant using [is_const](@ref ValuePartRef::is_const)
- then access the constant data using [const_data](@ref ValuePartRef::const_data) which returns a `std::span<u64>`
```cpp
bool compile_add64(IRInstRef inst) {
    IRValueRef lhs_val = /* IR-specific way to get operand */;
    IRValueRef rhs_val = /* IR-specific way to get operand */;
    IRValueRef res_val = /* IR-specific way to get result */;
    auto [lhs_ref, lhs_part] = this->val_ref_single(lhs_val);
    auto [rhs_ref, rhs_part] = this->val_ref_single(rhs_val);
    auto [res_ref, res_part] = this->result_ref_single(res_val);

    ValuePartRef tmp_part = lhs_part.into_temporary();

    // load and allocate registers
    AsmReg lhs_reg = tmp_part.cur_reg(); // tmp_part is guaranteed to already own a register
    if (rhs_part.is_const() // check if part is const
        && i64(rhs_part.const_data()[0]) == i64(i32(rhs_part.const_data()[0])) // check if it can be encoded as a 32 bit immediate in x86-64
        ) {
        // encode using immediate operand
        ASM(ADD64ri, lhs_reg, rhs_part.const_data()[0]);
    } else {
        // encode as before
        AsmReg rhs_reg = rhs_part.load_to_reg();
        ASM(ADD64rr, lhs_reg, rhs_reg);
    }

    res_part.set_value(std::move(tmp_part));
    return true;
}
```

- can also optimize if lhs is const and rhs is not
- before calling `into_temporary`
```cpp
if (lhs_part.is_const() && !rhs_part.is_const()) {
    std::swap(lhs_ref, rhs_ref);
    std::swap(lhs_part, rhs_part);
}
```

## Materializing constants
- sometimes need constant that is not a value for computation in register
- materializing constants might be tedious (AArch64)
- simple helper to do that if you have allocated a `ScratchReg`
```cpp
ScratchReg scratch{this};
AsmReg tmp_reg = scratch.alloc_gp();
u64 constant = /* some constant */;
this->materialize_constant(constant, Config::GP_BANK, /* size_bytes = */ 8, tmp_reg);
// constant is now in tmp_reg
```

## Fusing instructions
- sometimes it is beneficial to fuse adjacent IR instructions, e.g. load with zero extend
- TPDE only supports fusing instructions which have not been compiled yet (forward fusing)
- adaptor needs to implement bookkeeping for which instructions have been fused
- `compile_inst` gets an `InstRange` as a parameter which can be used to iterate over the following instructions
- check for the specific instruction we want to match, then mark it as fused
- example: load i8 with sx
```cpp
bool compile_loadi8(IRInstRef inst, InstRange remaining) {
    IRValueRef ptr_val = /* IR-specific */;
    IRValueRef res_val = /* IR-specific */;

    auto [ptr_ref, ptr_part] = this->val_ref_single(ptr_val)
    AsmReg ptr_reg = ptr_part.load_to_reg();

    if (remaining.from != remaining.to // is there any instruction left?
        && analyzer.liveness_info(u32(this->adaptor->val_local_idx(res_val))).ref_count <= 2 // does the current load only have one user? (definition counts as a use here)
        && /* IR-specific; *remaining.from yields an IRInstRef */ // is the following instruction a sign extension?
        ) {
        res_val = /* IR-specific way to get result value ref from *remaining.from */;
        auto [res_ref, res_part] = this->result_ref_single(res_val);

        AsmReg res_reg = res_part.alloc_reg();
        ASM(MOVSXr64m8, res_reg, FE_MEM(/* base = */ ptr_reg, /* scale = */ 0, /* index_reg = */ FE_NOREG, /* displacement = */ 0));
        res_part.set_modified();
        // mark *remaining.from as fused here
    } else {
        // use regular MOVZX
        res_val = /* IR-specific way to get result value ref from *remaining.from */;
        auto [res_ref, res_part] = this->result_ref_single(res_val);

        AsmReg res_reg = res_part.alloc_reg();
        ASM(MOVZXr32m8, res_reg, FE_MEM(/* base = */ ptr_reg, /* scale = */ 0, /* index_reg = */ FE_NOREG, /* displacement = */ 0));
        res_part.set_modified();
    }
    return true;
}
```

## Return
- you only need to move result values into registers
- i.e. get the result register for the current calling convention, move the values there
- afterwards, call `gen_func_epilog` from the architecture compiler and then [release_regs_after_return](@ref CompilerBase::release_regs_after_return)
- WARNING: you *always* need to call `release_regs_after_return` if you compile an instruction that terminates a basic block and does not branch to another block
```cpp
bool compile_ret(IRInstRef inst) {
    if (/* IR-specific way to check if return should return a value */) {
        IRValueRef ret_val = /* IR-specific way to get return value */;
        // we assume that we only ever return a single integer register
        auto [ret_ref, ret_part] = this->result_ref_single(ret_val);
        // get the current calling convention
        x64::CallingConv call_conv = this->cur_calling_convention();
        // move the value into the result register
        ret_part.reload_into_specific_fixed(call_conv.ret_regs_gp()[0]);
    }

    // generate the epilogue
    this->gen_func_epilog();
    // make sure the framework knows this instruction terminates the block
    this->release_regs_after_return();
    return true;
}
```

## Branches
- branching mostly handled by the compiler
- use `generate_branch_to_block` from the architecture compiler to emit an (un)conditional branch, arguments are architecture-specific
- before calling it, need to call [spill_before_branch](@ref CompilerBase::spill_before_branch) so that values which live across blocks
  can be spilled
- after all branches are generated, call [release_spilled_regs](@ref CompilerBase::release_spilled_regs) so the register allocator can free values which can no longer be assumed to be in registers
  after the branch

### Unconditional branch
- need to get `IRBlockRef` to jump to
- straightforward
```cpp
bool compile_br(IRInstRef inst) {
    IRBlockRef target = /* IR-specific way to get target block ref */;

    const auto spilled = this->spill_before_branch();
    this->generate_branch_to_block(Jump::jmp, target, /* needs_split = */ false, /* last_inst = */ true);
    this->release_spilled_regs(spilled);

    return true;
}
```
- split only relevant to conditional branches

### Conditional branch
- a bit more involved but can also be simple
- suppose jump to false block only when lower 32 bit 0, otherwise true block
- need to ask framework whether branch to block needs to be split
```cpp
bool compile_condbr(IRInstRef inst) {
    IRValueRef cond_val = /* IR-specific way to get condition */;
    IRBlockRef true_target = /* IR-specific way to get block ref */;
    IRBlockRef false_target = /* IR-specific way to get block ref */;

    {
        auto [cond_ref, cond_part] = this->val_ref_single(cond_val);
        ASM(CMP32ri, cond_part.load_to_reg(), 0);
    }

    const bool true_needs_split = this->branch_needs_split(true_target);
    
    // let the framework spill
    const auto spilled = this->spill_before_branch();

    // actually generate the branches
    this->generate_branch_to_block(Jump::jne, true_target, true_needs_split, /* last_inst = */ false);
    // unconditional branch since the condition must be false at this point
    // note that the last branch in a block never needs to be split
    this->generate_branch_to_block(Jump::jmp, false_target, /* needs_split = */ false, /* last_inst = */ true);

    // framework register handling
    this->release_spilled_regs(spilled);
    return true;
}
```

- want to optimize this, since we want to avoid branch to block that comes directly after
- a bit more involved but code can be easily reused
```cpp
bool compile_condbr(IRInstRef inst) {
    IRValueRef cond_val = /* IR-specific way to get condition */;
    IRBlockRef true_target = /* IR-specific way to get block ref */;
    IRBlockRef false_target = /* IR-specific way to get block ref */;

    {
        auto [cond_ref, cond_part] = this->val_ref_single(cond_val);
        ASM(CMP32ri, cond_part.load_to_reg(), 0);
    }

    const bool true_needs_split = this->branch_needs_split(true_target);
    const bool false_needs_split = this->branch_needs_split(false_target);
    const IRBlockRef next_block = this->analyzer.block_ref(this->next_block());
    
    // let the framework spill
    const auto spilled = this->spill_before_branch();

    if (next_block == true_target || (next_block != false_target && true_needs_split)) {
        // if the following block is the true target or if we have to always emit a branch but a branch to the true block
        // is heavy (i.e. needs to be split) then we want to first jump to the false block
        this->generate_branch_to_block(this->invert_jump(Jump::jne), false_target, false_needs_split, /* last_inst = */ false);
        // if the next block is the true_target, then the jump will not be emitted
        this->generate_branch_to_block(Jump::jmp, true_target, /* needs_split = */ false, /* last_inst = */ true);
    } else {
        // try to elide the branch to the false_target
        this->generate_branch_to_block(Jump::jne, true_target, true_needs_split, /* last_inst = */ false);
        this->generate_branch_to_block(Jump::jmp, false_target, /* needs_split = */ false, /* last_inst = */ true);
    } 

    // framework register handling
    this->release_spilled_regs(spilled);
    return true;
}
```

## Emitting function calls
- helper function `generate_call` in architecture compiler
- API subject to change since it is suboptimal atm
- three types of target:
  - direct call to symbol
  - indirect call to ptr in temporary register in ScratchReg
  - indirect call to ptr in ValuePartRef
- arguments given by their IRValueRef + flags for sign/zero-extension or passed as byval
- results returned in [ValuePart](@ref ValuePart)s
- supports calling vararg functions

```cpp
bool compile_call_direct(IRInstRef inst) {
    IRValueRef res_val = /* IR-specific way to get result value */;
    u32 target_idx = /* implementation-specific way to get index passed to define_func_idx for the target function */;
    SymRef target_sym = this->func_syms[target_idx];

    tpde::SmallVector<CallArg, 4> args{};
    for (IRValueRef call_arg : /* IR-specific */) {
        // no extensions, no byval arguments
        args.push_back(CallArg{call_arg});
    }

    // assume only one result register
    ValuePart res_part{};
    CallingConv call_conv = /* IR-specific way to decide which calling convention to use */;
    this->generate_call(target_sym, args, std::span{&res_part, 1}, call_conv, /* variable_args = */ false);

    // assign result
    this->result_ref(res_val).part(0).set_value(std::move(res_part));
    return true;
}
```

- other ways to get symbols, you can create your own, e.g. LLVM back-end does this
- calling convention is architecture-specific, explained later

## Manual stack allocations



<div class="section_buttons">
 
| Previous          |                              Next |
|:------------------|----------------------------------:|
| [IRAdaptor Reference](@ref tpde-adaptor) | [EncodeGen Reference](@ref tpde-encodegen) |
 
</div>

