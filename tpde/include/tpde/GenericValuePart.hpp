// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <array>
#include <cstring>
#include <span>

namespace tpde {

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
struct CompilerBase<Adaptor, Derived, Config>::GenericValuePart {
    struct Expr {
        std::variant<AsmReg, ScratchReg> base;
        std::variant<AsmReg, ScratchReg> index;
        i64 scale;
        i64 disp;

        explicit Expr() : base{AsmReg::make_invalid()}, scale{0}, disp{0} {}

        explicit Expr(AsmReg base, i64 disp = 0)
            : base(base), scale(0), disp(disp) {}

        explicit Expr(ScratchReg &&base, i64 disp = 0)
            : base(std::move(base)), scale(0), disp(disp) {}

        AsmReg base_reg() const noexcept {
            if (std::holds_alternative<AsmReg>(base)) {
                return std::get<AsmReg>(base);
            }
            return std::get<ScratchReg>(base).cur_reg;
        }

        [[nodiscard]] bool has_base() const noexcept {
            if (std::holds_alternative<AsmReg>(base)) {
                return std::get<AsmReg>(base).valid();
            }
            return true;
        }

        AsmReg index_reg() const noexcept {
            assert(scale != 0 && "index_reg() called on invalid index");
            assert((scale != 1 || has_base())
                   && "Expr with unscaled index must have base");
            if (std::holds_alternative<AsmReg>(index)) {
                return std::get<AsmReg>(index);
            }
            return std::get<ScratchReg>(index).cur_reg;
        }

        [[nodiscard]] bool has_index() const noexcept { return scale != 0; }
    };

    struct Immediate {
        union {
            u64 const_u64;
            std::array<u8, 64> const_bytes;
        };

        u32 bank, size;
    };

    // TODO(ts): evaluate the use of std::variant
    // TODO(ts): I don't like the ValuePartRefs but we also don't want to
    // force all the operands into registers at the start of the encoding...
    std::variant<std::monostate,
                 ValuePartRef,
                 ValuePartRef *,
                 ScratchReg,
                 Expr,
                 Immediate>
        state;

    GenericValuePart() = default;

    GenericValuePart(GenericValuePart &) = delete;

    GenericValuePart(GenericValuePart &&other) noexcept {
        state = std::move(other.state);
        other.state = std::monostate{};
    }

    GenericValuePart &operator=(const GenericValuePart &) noexcept = delete;

    GenericValuePart &operator=(GenericValuePart &&other) noexcept {
        if (this == &other) {
            return *this;
        }
        state = std::move(other.state);
        other.state = std::monostate{};
        return *this;
    }

    // reg can't be overwritten
    GenericValuePart(AsmReg reg) noexcept : state{Expr(reg)} {}

    // no salvaging
    GenericValuePart(const ScratchReg &reg) noexcept {
        assert(!reg.cur_reg.invalid());
        state = Expr(reg.cur_reg);
    }

    // salvaging
    GenericValuePart(ScratchReg &&reg) noexcept {
        assert(!reg.cur_reg.invalid());
        state = std::move(reg);
    }

    // no salvaging
    GenericValuePart(ValuePartRef &ref) noexcept {
        if (ref.is_const) {
            state = Immediate{.const_bytes = ref.state.c.const_data,
                              .bank = ref.state.c.bank,
                              .size = ref.state.c.size};
            return;
        }
        // TODO(ts): check if it is a variable_ref/frame_ptr and then
        // turning it into an Address?
        state = &ref;
    }

    // salvaging
    GenericValuePart(ValuePartRef &&ref) noexcept {
        if (ref.is_const) {
            state = Immediate{.const_bytes = ref.state.c.const_data,
                              .bank = ref.state.c.bank,
                              .size = ref.state.c.size};
            return;
        }
        state = std::move(ref);
    }

    GenericValuePart(Expr expr) noexcept {
        ScratchReg *base_scratch = std::get_if<ScratchReg>(&expr.base);
        if (base_scratch && !expr.has_index() && expr.disp == 0) {
            state = std::move(*base_scratch);
        } else {
            state = std::move(expr);
        }
    }

    GenericValuePart(Immediate imm) noexcept { state = imm; }

    [[nodiscard]] bool is_expr() const noexcept {
        return std::holds_alternative<Expr>(state);
    }

    [[nodiscard]] Immediate &imm() noexcept {
        return std::get<Immediate>(state);
    }

    [[nodiscard]] ValuePartRef &val_ref() noexcept {
        return std::get<ValuePartRef>(state);
    }

    void reset() noexcept;
};
} // namespace tpde
