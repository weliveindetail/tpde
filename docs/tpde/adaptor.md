\page tpde-adaptor IRAdaptor Reference

The IRAdaptor is a class that you as a user will need to implement such that the framework can access
information about the structure of your IR. The compiler implementation will be templated on the class type
and you will need to pass in a pointer to an instance of that class to the CompilerBase.

The class you provide will be typechecked against a concept which is mainly described in [IRAdaptor.hpp](@ref tpde/include/tpde/IRAdaptor.hpp).
This page provides similar content with a few more annotations.

# TODO: formulate requirements on the IR?

[TOC]

# Meta-concepts

### IRRange
```cpp
template<typename T, typename Value>
concept IRRange = /* ... */;
```
Basic range concept that is a little more lax than `std::range`. It requires a `begin` and `end` member function that return iterators
similar to `std::forward_iterator`. The iterators have to support non-equality comparison, dereferencing to a value of type `Value`
and incrementing through the pre-increment operator.

# IRAdaptor

### IRValueRef
```cpp
using IRValueRef = /* opaque reference type */;
```
To reference values in the IR, TPDE uses the `IRValueRef` type defined in the `IRAdaptor`.
As it might be stored in arrays or the like, it should not exceed the size of a pointer.

### IRInstRef
```cpp
using IRInstRef = /* opaque reference type */;
```
To reference instructions in the IR, TPDE uses the `IRInstRef` type defined in the `IRAdaptor`.
As it might be stored in arrays or the like, it should not exceed the size of a pointer.

Note that if your IR treats values the same as instructions, `IRInstRef` may be the same as `IRValueRef`.

### IRBlockRef
```cpp
using IRBlockRef = /* opaque reference type */;
```
To reference basic blocks in the IR, TPDE uses the `IRBlockRef` type defined in the `IRAdaptor`.
As it might be stored in arrays or the like, e.g. for the block layout, it should not exceed the size of a pointer.

### IRFuncRef
```cpp
using IRFuncRef = /* opaque reference type */;
```
To reference functions in the IR, TPDE uses the `IRFuncRef` type defined in the `IRAdaptor`.
As it might be stored in arrays or the like, it should not exceed the size of a pointer.

Note that this is the only reference type that is stored across the compilation of multiple functions and as such a `IRFuncRef` must reference the same
function for the whole compilation.

### INVALID_VALUE_REF
```cpp
static constexpr IRValueRef INVALID_VALUE_REF = /* some value */;
```
When initializing arrays of `IRValueRef`s or to generally signal a non-existant value, TPDE uses the `INVALID_VALUE_REF` value.

### INVALID_BLOCK_REF
```cpp
static constexpr IRBlockRef INVALID_BLOCK_REF = /* some value */;
```
When initializing arrays of `IRBlockRef`s or to generally signal a non-existant block, TPDE uses the `INVALID_BLOCK_REF` value.

### INVALID_FUNC_REF
```cpp
static constexpr IRFuncRef INVALID_FUNC_REF = /* some value */;
```
When initializing arrays of `IRFuncRef`s or to generally signal a non-existant function, TPDE uses the `INVALID_FUNC_REF` value.

### TPDE_PROVIDES_HIGHEST_VAL_IDX
```cpp
static constexpr bool TPDE_PROVIDES_HIGHEST_VAL_IDX = /* ... */;
```

TPDE keeps lookup arrays of values and uses the provided [TODO: value index]() to index them. To more efficiently allocate these arrays,
an adaptor can indicate whether it can provide the highest value index possible after [TODO: switch_func]() was called using `TPDE_PROVIDES_HIGHEST_VAL_IDX`.

### TPDE_LIVENESS_VISIT_ARGS
```cpp
static constexpr bool TPDE_LIVENESS_VISIT_ARGS = /* ... */;
```
Does the liveness analysis have to explicitly visit function arguments using [TODO: cur_args]() or do they show up as values in the entry block?

### func_count
```cpp
u32 func_count() const noexcept;
```
How many functions are referenced in the current compilation module. This includes functions where only the declarations are used, e.g. to
call them.

### funcs
```cpp
IRRange<IRFuncRef> funcs() const noexcept;
```
Returns an iterator over all functions in the current compilation module including functions without definitions or which shouldn't be compiled.
TODO: do the iterators need to be stable?

### funcs_to_compile
```cpp
IRRange<IRFuncRef> funcs_to_compile() const noexcept;
```
Returns an iterator over all functions which should be compiled.
TODO: do the iterators need to be stable?

### func_link_name
```cpp
std::string_view func_link_name(IRFuncRef func) const noexcept;
```
Returns the name of the function used in the object file (including mangling). The return type only needs to be convertible to an `std::string_view`.

<div class="section_buttons">
 
| Previous          |                              Next |
|:------------------|----------------------------------:|
| [Overview](@ref tpde-overview) | [Compiler Reference](@ref tpde-compiler-ref) |
 
</div>

