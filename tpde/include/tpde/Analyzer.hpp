// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include "IRAdaptor.hpp"
#include "util/SmallBitSet.hpp"
#include "util/SmallVector.hpp"

namespace tpde {

template <IRAdaptor Adaptor>
struct Analyzer {
    // TODO(ts): maybe rename this to ValueTracker or smth since it not only
    // does the analysis but also manages and stores the value assignments and
    // block order

    // some forwards for the IR type defs
    using IRValueRef = typename Adaptor::IRValueRef;
    using IRBlockRef = typename Adaptor::IRBlockRef;
    using IRFuncRef  = typename Adaptor::IRFuncRef;

    static constexpr size_t SMALL_BLOCK_NUM = 64;

    /// Reference to the adaptor
    Adaptor *adaptor;

    /// An index into block_layout
    enum class BlockIndex : uint32_t {
    };
    /// The block layout, a BlockIndex is an index into this array
    util::SmallVector<IRBlockRef, SMALL_BLOCK_NUM> block_layout;

    /// Start the compilation of a new function and build the loop tree and
    /// liveness information
    void switch_func(IRFuncRef func);

    void build_loop_tree_and_block_layout();

    /// Builds a vector of block references in reverse post-order.
    void build_rpo_block_order(
        util::SmallVector<IRBlockRef, SMALL_BLOCK_NUM> &out) const noexcept;

    /// Creates a bitset of loop_heads and sets the parent loop of each block.
    /// The u32 in loop_parent is an index into block_rpo
    void identify_loops(
        const util::SmallVector<IRBlockRef, SMALL_BLOCK_NUM> &block_rpo,
        util::SmallVector<u32, SMALL_BLOCK_NUM>              &loop_parent,
        util::SmallBitSet<256> &loop_heads) const noexcept;

    /// Resets the analyzer state
    void reset() {}
};

template <IRAdaptor Adaptor>
void Analyzer<Adaptor>::build_rpo_block_order(
    util::SmallVector<IRBlockRef, SMALL_BLOCK_NUM> &out) const noexcept {
    out.clear();

    // implement the RPO generation using a simple stack that also walks in RPO.
    // This will however generate a bit of an unatural RPO since
    //
    // A:
    //  - B:
    //    - D
    //    - E
    //  - C:
    //    - F
    //    - G
    //
    // will be generated as A C G F B E D
    // which is not very nice for loops so we push the children and then reverse
    // them
    util::SmallVector<IRBlockRef, SMALL_BLOCK_NUM / 2> stack;
    stack.push_back(adaptor->cur_entry_block());

    while (!stack.empty()) {
        const auto cur_node = stack.back();
        stack.pop_back();

        // visit the current node
        adaptor->block_set_info(cur_node, out.size());
        out.push_back(cur_node);

        const auto start_idx = stack.size();
        // push the successors onto the stack to visit them
        for (const auto succ : adaptor->block_succs(cur_node)) {
            stack.push_back(succ);
        }
        // Reverse the pushed nodes to get a more natural ordering
        // TODO(ts): should we just reverse everything at the end?
        // maybe measure the perf or/and ask the adaptor whether it can
        // efficiently give us a reverse iteration over the successors and then
        // we can just not do any of that
        const auto len = stack.size() - start_idx;
        if (len <= 1) {
            continue;
        }

        if (len == 2) {
            std::swap(stack[start_idx], stack[start_idx + 1]);
            continue;
        }

        std::reverse(stack.begin() + start_idx, stack.end());
    }

#ifdef TPDE_LOGGING
    TPDE_LOG_TRACE("Finished building RPO for blocks:");
    for (u32 i = 0; i < out.size(); ++i) {
        TPDE_LOG_TRACE("Index {}: {}", i, adaptor->block_fmt_ref(out[i]));
    }
#endif
}

template <IRAdaptor Adaptor>
void Analyzer<Adaptor>::identify_loops(
    const util::SmallVector<IRBlockRef, SMALL_BLOCK_NUM> &block_rpo,
    util::SmallVector<u32, SMALL_BLOCK_NUM>              &loop_parent,
    util::SmallBitSet<256> &loop_heads) const noexcept {
    loop_parent.clear();
    loop_parent.resize(block_rpo.size());
    loop_heads.clear();
    loop_heads.resize(block_rpo.size());

    // Implement the modified algorithm from Wei et al.: A New Algorithm for
    // Identifying Loops in Decompilation
    // in a non-recursive form

    // TODO(ts): ask the adaptor if it can store this information for us?
    // then we could save the allocation here
    struct BlockInfo {
        bool traversed;
        bool self_loop;
        u32  dfsp_pos;
        u32  iloop_header;
    };

    util::SmallVector<BlockInfo, SMALL_BLOCK_NUM> block_infos;
    block_infos.resize(block_rpo.size());

    struct TravState {
        u32                    block_idx; // b0 from the paper
        u32                    dfsp_pos;
        u32                    nh;
        u8                     state = 0;
        decltype(adaptor->block_succs(adaptor->cur_entry_block())
                     .begin()) succ_it;
        decltype(adaptor->block_succs(adaptor->cur_entry_block()).end()) end_it;
    };

    // The algorithm will reach the depth of the CFG so the small vector needs
    // to be relatively big
    // TODO(ts); maybe use the recursive form for small CFGs since that may be
    // faster or use stack switching since the non-recursive version is really
    // ugly
    util::SmallVector<TravState, SMALL_BLOCK_NUM> trav_state;

    trav_state.push_back(TravState{.block_idx = 0, .dfsp_pos = 1});

    const auto tag_lhead = [&block_infos](u32 b, u32 h) {
        if (b == h || h == 0) {
            return;
        }

        auto cur1 = b, cur2 = h;
        while (block_infos[cur1].iloop_header != 0) {
            const auto ih = block_infos[cur1].iloop_header;
            if (ih == cur2) {
                return;
            }
            if (block_infos[ih].dfsp_pos < block_infos[cur2].dfsp_pos) {
                block_infos[cur1].iloop_header = cur2;
                cur1                           = cur2;
                cur2                           = ih;
            } else {
                cur1 = ih;
            }
        }
        block_infos[cur1].iloop_header = cur2;
    };

    // TODO(ts): this can be optimized and more condensed so that the state
    // variable becomes unnecessary
    while (!trav_state.empty()) {
        auto      &state     = trav_state.back();
        const auto block_idx = state.block_idx;
        switch (state.state) {
        case 0: {
            // entry
            block_infos[block_idx].traversed = true;
            block_infos[block_idx].dfsp_pos  = state.dfsp_pos;

            // TODO(ts): somehow make sure that the iterators can live when
            // succs is destroyed?
            auto succs    = adaptor->block_succs(block_rpo[block_idx]);
            state.succ_it = succs.begin();
            state.end_it  = succs.end();
            state.state   = 1;
        }
            [[fallthrough]];
        case 1: {
        loop_inner:
            auto cont = false;
            while (state.succ_it != state.end_it) {
                const auto succ_idx = adaptor->block_info(*state.succ_it);
                if (succ_idx == block_idx) {
                    block_infos[block_idx].self_loop = true;
                }

                if (block_infos[block_idx].traversed) {
                    ++state.succ_it;
                    continue;
                }

                // recurse
                cont = true;
                trav_state.push_back(TravState{.block_idx = succ_idx,
                                               .dfsp_pos = state.dfsp_pos + 1});
                state.state = 2;
                break;
            }

            if (cont) {
                continue;
            }

            block_infos[block_idx].dfsp_pos = 0;
            trav_state.pop_back();
            if (trav_state.empty()) {
                break;
            }
            trav_state.back().nh = block_infos[block_idx].iloop_header;
            continue;
        }
        case 2: {
            const auto succ_idx = adaptor->block_info(*state.succ_it);
            const auto nh       = state.nh;
            tag_lhead(block_idx, nh);
            if (block_infos[succ_idx].dfsp_pos > 0) {
                tag_lhead(block_idx, succ_idx);
            } else if (block_infos[succ_idx].iloop_header != 0) {
                auto h_idx = block_infos[succ_idx].iloop_header;
                if (block_infos[h_idx].dfsp_pos > 0) {
                    tag_lhead(block_idx, h_idx);
                } else {
                    while (block_infos[h_idx].iloop_header != 0) {
                        h_idx = block_infos[h_idx].iloop_header;
                        if (block_infos[h_idx].dfsp_pos > 0) {
                            tag_lhead(block_idx, h_idx);
                            break;
                        }
                    }
                }
            }

            ++state.succ_it;
            state.state = 1;
            goto loop_inner;
        }
        }
    }

    for (u32 i = 0; i < block_rpo.size(); ++i) {
        auto &info = block_infos[i];
        if (info.iloop_header != 0) {
            loop_parent[i] = info.iloop_header;
            loop_heads.mark_set(info.iloop_header);
        }
        if (info.self_loop) {
            loop_heads.mark_set(i);
        }
    }
}

} // namespace tpde
