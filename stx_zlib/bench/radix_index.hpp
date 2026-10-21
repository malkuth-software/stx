// Experimental capped critical-bit index. Included after the match-study
// primitives, inside its private namespace. The immutable input outlives a run.
// Keys contain 258 bytes, with virtual zero padding only at the end of input.
// This is a bounded index experiment, not yet a streaming-buffer implementation.
struct Radix final {
    using Link = std::uint16_t;
    static constexpr Link none = UINT16_MAX;
    static constexpr unsigned leaf_count = 32768;
    struct Branch final {
        std::array<Link, 2> children{none, none};
        Link parent = none;
        Link bit = 0;
        std::uint32_t newest = absent;
    };
    static_assert(sizeof(Branch) == 12);
    // Leaves occupy [0,32768); branches occupy [32768,65535). Leaf slots
    // are recycled by position modulo window, after explicit oldest removal.
    std::array<Branch, leaf_count - 1> branches{};
    std::array<std::uint8_t, leaf_count> live{};
    std::array<Link, leaf_count> parents{};
    // Exact two-byte directory removes the first sixteen discriminating bits
    // from each walk. It partitions one index; no workload classification.
#ifdef STX_MATCH_INDEX_RADIX_SINGLETON
    // Absolute entries identify singletons, or small-chain heads in the lazy
    // variant. High-bit entries refer to linked roots. Input is at most 16 MiB.
    using DirectoryEntry = std::uint32_t;
    static constexpr DirectoryEntry directory_empty = UINT32_MAX;
    static constexpr DirectoryEntry linked_entry = 0x80000000U;
#else
    using DirectoryEntry = Link;
    static constexpr DirectoryEntry directory_empty = none;
#endif
    std::array<DirectoryEntry, 65536> roots{};
    Link root = none;
    Link free_branch = none;
    unsigned allocated = 0;
    Carried carried{};
    unsigned adjacent_length = 0;

    void reset() {
        root = none;
        free_branch = none;
        allocated = 0;
        live.fill(0);
        roots.fill(directory_empty);
        carried = {};
        adjacent_length = 0;
    }
    void set_parent(Link link, Link value) {
        if (link < leaf_count) parents[link] = value;
        else branches[link - leaf_count].parent = value;
    }
    static std::uint32_t source_position(Link leaf, std::uint32_t query_position) {
        // Existing tree leaves are less than one window behind the query.
        // When publishing the newly inserted root, distance zero is intentional.
        const unsigned distance = (query_position - leaf) & (window - 1);
        const std::uint32_t source = query_position - distance;
        return source;
    }
    std::uint32_t newest(Link link, std::uint32_t query_position) const {
        if (link < leaf_count) return source_position(link, query_position);
        return branches[link - leaf_count].newest;
    }
    template<bool Count>
    void store_root(unsigned slot, std::uint32_t position, Work& work) {
#ifdef STX_MATCH_INDEX_RADIX_SINGLETON
        if (root == none) roots[slot] = directory_empty;
        else if (root < leaf_count && parents[root] == none) {
            roots[slot] = source_position(root, position);
            live[root] = 0;
            if constexpr (Count) ++work.writes;
        } else roots[slot] = linked_entry | root;
#else
        (void)position;
        roots[slot] = root;
#endif
        if constexpr (Count) ++work.writes;
    }
    template<bool Count>
    void finalize_previous(std::uint32_t position, Work& work) {
        if (position == 0) return;
        const Link previous = static_cast<Link>((position - 1) & (window - 1));
        if (live[previous] == 0) return;
        Link ancestor = parents[previous];
        while (ancestor != none && ancestor >= leaf_count) {
            Branch& branch = branches[ancestor - leaf_count];
            branch.newest = position - 1;
            ancestor = branch.parent;
            if constexpr (Count) { ++work.visits; ++work.writes; }
        }
    }
    static unsigned byte(std::span<const std::uint8_t> input,
        std::uint32_t position, unsigned offset) {
        const std::size_t index = static_cast<std::size_t>(position) + offset;
        if (index < input.size()) return input[index];
        return 0;
    }
    static unsigned direction(std::span<const std::uint8_t> input,
        std::uint32_t position, unsigned bit) {
        const unsigned value = byte(input, position, bit / 8);
        const unsigned result = (value >> (7 - bit % 8)) & 1U;
        return result;
    }
    static unsigned directory(std::span<const std::uint8_t> input, std::uint32_t position) {
        const unsigned result = (static_cast<unsigned>(input[position]) << 8) | input[position + 1];
        return result;
    }
    template<bool Count>
    static unsigned different_bit(std::span<const std::uint8_t> input,
        std::uint32_t position, std::uint32_t source, unsigned length,
        unsigned maximum, Work& work) {
        unsigned different = length;
        while (different < 258 && different >= maximum) {
            if constexpr (Count) ++work.compared;
            if (byte(input, position, different) != byte(input, source, different)) break;
            ++different;
        }
        if (different == 258) return 258 * 8;
        const unsigned difference = byte(input, position, different) ^ byte(input, source, different);
        const unsigned bit = different * 8 + static_cast<unsigned>(std::countl_zero(difference)) - 24;
        return bit;
    }
    template<bool Count>
    void replace(Link old_link, Link replacement, Link ancestor, Work& work) {
        if (ancestor == none) root = replacement;
        else {
            Branch& branch = branches[ancestor - leaf_count];
            const unsigned side = branch.children[0] == old_link ? 0U : 1U;
            branch.children[side] = replacement;
        }
        if (replacement != none) set_parent(replacement, ancestor);
        if constexpr (Count) work.writes += replacement == none ? 1 : 2;
    }
    template<bool Count>
    void erase_oldest(std::uint32_t position, Work& work) {
        const Link leaf = static_cast<Link>(position & (window - 1));
        // An identical, newer key may already have displaced this source.
        if (live[leaf] == 0) return;
        const Link ancestor = parents[leaf];
#if defined(STX_MATCH_INDEX_RADIX_SMALL) && !defined(STX_MATCH_INDEX_RADIX_LAZY)
        if (ancestor < leaf_count || ancestor == none) {
            // A directory holding at most four leaves uses the parent field
            // as its next link. Internal-parent links occupy the other half.
            if (root == leaf) root = ancestor;
            else {
                Link preceding = root;
                while (parents[preceding] != leaf) {
                    preceding = parents[preceding];
                    if constexpr (Count) ++work.visits;
                }
                parents[preceding] = ancestor;
            }
#ifdef STX_MATCH_INDEX_RADIX_SINGLETON
            live[leaf] = 0;
            if constexpr (Count) ++work.writes;
#endif
            // Ordinary linked insertion overwrites this leaf immediately.
            if constexpr (Count) { ++work.root_expirations; ++work.writes; }
            return;
        }
#endif
        if (ancestor == none) {
            root = none;
            if constexpr (Count) ++work.root_expirations;
        }
        else {
            if constexpr (Count) ++work.branch_expirations;
            Branch& branch = branches[ancestor - leaf_count];
            const Link sibling = branch.children[branch.children[0] == leaf ? 1 : 0];
            replace<Count>(ancestor, sibling, branch.parent, work);
            branch.children[0] = free_branch;
            free_branch = ancestor;
            if constexpr (Count) work.writes += 2;
        }
        // The globally oldest leaf cannot be the newest surviving source in
        // any ancestor. No upward maximum repair is required after contraction.
        // Its ring slot is overwritten by advance before the next operation.
#ifdef STX_MATCH_INDEX_RADIX_SINGLETON
        live[leaf] = 0;
        if constexpr (Count) ++work.writes;
#endif
    }
#ifdef STX_MATCH_INDEX_RADIX_SMALL
    template<bool Count>
    void promote(std::span<const std::uint8_t> input, std::uint32_t query_position, Work& work) {
        std::array<Link, 4> leaves{};
        Link cursor = root;
        for (unsigned index = 0; index < leaves.size(); ++index) {
            leaves[index] = cursor;
#ifdef STX_MATCH_INDEX_RADIX_LAZY
            live[cursor] = 1;
            if constexpr (Count) ++work.writes;
#endif
            cursor = parents[cursor];
        }
        root = leaves[3];
        parents[root] = none;
        for (unsigned index = 3; index != 0; --index) {
            const Link added = leaves[index - 1];
            const std::uint32_t position = source_position(added, query_position);
            const unsigned maximum = static_cast<unsigned>(std::min<std::size_t>(258, input.size() - position));
            Link leaf = root;
            while (leaf >= leaf_count) {
                const Branch& branch = branches[leaf - leaf_count];
                leaf = branch.children[direction(input, position, branch.bit)];
                if constexpr (Count) { ++work.visits; ++work.compared; }
            }
            const std::uint32_t source = source_position(leaf, query_position);
            const unsigned length = extend<Count>(input, position, source, 2, maximum, work);
            const unsigned bit = different_bit<Count>(input, position, source, length, maximum, work);
            require(bit != 258 * 8);
            Link subtree = leaf;
            Link ancestor = parents[leaf];
            while (ancestor != none && branches[ancestor - leaf_count].bit >= bit) {
                subtree = ancestor;
                ancestor = branches[ancestor - leaf_count].parent;
                if constexpr (Count) ++work.visits;
            }
            Link inserted = free_branch;
            if (inserted == none) {
                require(allocated < branches.size());
                inserted = static_cast<Link>(leaf_count + allocated);
                ++allocated;
            } else free_branch = branches[inserted - leaf_count].children[0];
            Branch& branch = branches[inserted - leaf_count];
            const unsigned side = direction(input, position, bit);
            branch.children[side] = added;
            branch.children[1 - side] = subtree;
            branch.bit = static_cast<Link>(bit);
            branch.newest = position;
            replace<Count>(subtree, inserted, ancestor, work);
            set_parent(subtree, inserted);
            parents[added] = inserted;
            if constexpr (Count) { ++work.branch_insertions; work.writes += 6; }
            while (ancestor != none) {
                Branch& enclosing = branches[ancestor - leaf_count];
                enclosing.newest = position;
                ancestor = enclosing.parent;
                if constexpr (Count) { ++work.visits; ++work.writes; }
            }
        }
    }
    template<bool Count>
#ifdef STX_MATCH_INDEX_RADIX_INLINE
    [[gnu::always_inline]]
#endif
    bool insert_small(std::span<const std::uint8_t> input, std::uint32_t position,
        unsigned maximum, bool query, Match& best, Work& work) {
        if (root != none && root >= leaf_count) return false;
        finalize_previous<Count>(position, work);
        adjacent_length = 0;
        unsigned count = 0;
        Link preceding = none;
        Link cursor = root;
        while (cursor != none) {
            const std::uint32_t source = source_position(cursor, position);
            unsigned known = 2;
            unsigned length = 2;
            if (input[position + 2] != input[source + 2]) {
                if constexpr (Count) ++work.compared;
            } else {
                const unsigned shift = position - carried.position;
                if constexpr (share_prefixes) {
                    if (shift < carried.length && source == carried.source + shift) known = std::max(known, carried.length - shift);
                }
                length = extend<Count>(input, position, source, known, maximum, work);
            }
            if constexpr (Count) { ++work.visits; work.reused += known; }
            if (query && length >= 3 && length > best.length) best = Match{length, position - source};
            const bool duplicate = length == maximum && different_bit<Count>(input, position, source, length, maximum, work) == 258 * 8;
            if (duplicate) {
                if constexpr (share_prefixes) carried = Carried{position, source, length};
                const Link following = parents[cursor];
                if (preceding == none) root = following;
                else parents[preceding] = following;
                live[cursor] = 0;
                if constexpr (Count) { ++work.duplicate_insertions; work.writes += 2; }
                break;
            }
            preceding = cursor;
            cursor = parents[cursor];
            ++count;
        }
        if (count == 4) {
            promote<Count>(input, position, work);
            return false;
        }
        const Link added = static_cast<Link>(position & (window - 1));
        parents[added] = root;
        live[added] = 1;
        root = added;
        if constexpr (Count) { ++work.root_insertions; work.writes += 3; }
        return true;
    }
#endif
#ifdef STX_MATCH_INDEX_RADIX_SINGLETON
    template<bool Count>
    [[gnu::always_inline]]
    Match insert_singleton(std::span<const std::uint8_t> input, std::uint32_t position,
        unsigned slot, unsigned maximum, bool query, Work& work) {
        Match best{};
        finalize_previous<Count>(position, work);
        adjacent_length = 0;
        const std::uint32_t source = roots[slot];
        roots[slot] = position;
        if constexpr (Count) { ++work.root_insertions; ++work.writes; }
        if (source == directory_empty || position - source > window) return best;
        unsigned known = 2;
        unsigned length = 2;
        if (input[position + 2] != input[source + 2]) {
            if constexpr (Count) ++work.compared;
        } else {
            const unsigned shift = position - carried.position;
            if constexpr (share_prefixes) {
                if (shift < carried.length && source == carried.source + shift) known = std::max(known, carried.length - shift);
            }
            length = extend<Count>(input, position, source, known, maximum, work);
        }
        if constexpr (Count) { ++work.visits; work.reused += known; }
        if (query && length >= 3) best = Match{length, position - source};
        if (position - source == window) return best;
        if (length == maximum && different_bit<Count>(input, position, source, length, maximum, work) == 258 * 8) {
            if constexpr (share_prefixes) carried = Carried{position, source, length};
            if constexpr (Count) ++work.duplicate_insertions;
            return best;
        }
        const Link old_leaf = static_cast<Link>(source & (window - 1));
        const Link new_leaf = static_cast<Link>(position & (window - 1));
        live[old_leaf] = 1;
        parents[old_leaf] = none;
        live[new_leaf] = 1;
        parents[new_leaf] = old_leaf;
        roots[slot] = linked_entry | new_leaf;
        if constexpr (Count) work.writes += 5;
        return best;
    }
#endif
#ifdef STX_MATCH_INDEX_RADIX_LAZY
    // Small groups are ordinary newest-first chains. An absolute head and
    // strictly decreasing source positions make expiration implicit. Only
    // tree leaves participate in the explicit removal ring.
    template<bool Count>
    [[gnu::always_inline]]
    bool insert_lazy_small(std::span<const std::uint8_t> input, std::uint32_t position,
        unsigned slot, unsigned maximum, bool query, Match& best, Work& work) {
        finalize_previous<Count>(position, work);
        adjacent_length = 0;
        std::uint32_t source = roots[slot];
        // Shift a certified full distance-one match. Only its new final byte
        // remains unknown. Replacing this exact key preserves all older links.
        if (position != 0 && source == position - 1 && maximum == 258 &&
            carried.position == source && carried.source + 1 == source && carried.length == 258) {
            if constexpr (Count) ++work.compared;
            if (input[position + 257] == input[source + 257]) {
                const Link added = static_cast<Link>(position & (window - 1));
                const Link replaced = static_cast<Link>(source & (window - 1));
                parents[added] = parents[replaced];
                roots[slot] = position;
                carried = Carried{position, source, 258};
                if constexpr (Count) { ++work.duplicate_insertions; work.writes += 2; work.reused += 257; }
                if (query) best = Match{258, 1};
                return true;
            }
        }
        unsigned count = 0;
        Link preceding = none;
        Link first = none;
        if (source != directory_empty && position - source <= window) {
            first = static_cast<Link>(source & (window - 1));
        }
        while (source != directory_empty && position - source <= window) {
            const Link cursor = static_cast<Link>(source & (window - 1));
            const Link following = parents[cursor];
            unsigned known = 2;
            unsigned length = 2;
            if (input[position + 2] != input[source + 2]) {
                if constexpr (Count) ++work.compared;
            } else {
                const unsigned shift = position - carried.position;
                if constexpr (share_prefixes) {
                    if (shift < carried.length && source == carried.source + shift) known = std::max(known, carried.length - shift);
                }
                length = extend<Count>(input, position, source, known, maximum, work);
            }
            if constexpr (Count) { ++work.visits; work.reused += known; }
            if (query && length >= 3 && length > best.length) best = Match{length, position - source};
            const bool duplicate = length == maximum && different_bit<Count>(input, position, source, length, maximum, work) == 258 * 8;
            if (position - source == window) {
                if (preceding == none) first = none;
                else parents[preceding] = none;
                if constexpr (Count) ++work.writes;
                break;
            }
            if (duplicate) {
                if constexpr (share_prefixes) carried = Carried{position, source, length};
                if (preceding == none) first = following;
                else parents[preceding] = following;
                if constexpr (Count) { ++work.duplicate_insertions; ++work.writes; }
                break;
            }
            preceding = cursor;
            ++count;
            if (following == none) break;
            unsigned distance = (source - following) & (window - 1);
            if (distance == 0) distance = window;
            if (distance > source || position - source + distance > window) {
                parents[cursor] = none;
                if constexpr (Count) ++work.writes;
                break;
            }
            source -= distance;
        }
        if (count == 4) {
            root = first;
            promote<Count>(input, position, work);
            return false;
        }
        const Link added = static_cast<Link>(position & (window - 1));
        parents[added] = first;
        roots[slot] = position;
        if constexpr (Count) { ++work.root_insertions; work.writes += 2; }
        return true;
    }
#endif
    template<bool Count>
#ifdef STX_MATCH_INDEX_RADIX_LAZY
    [[gnu::noinline]]
#endif
    bool expire_linked(std::span<const std::uint8_t> input, std::uint32_t position,
        unsigned current_directory, Work& work) {
        const unsigned old_directory = directory(input, position - window);
        const bool oldest_can_match = old_directory == current_directory;
        root = static_cast<Link>(roots[old_directory]);
        const Link old_root = root;
        erase_oldest<Count>(position - window, work);
        if (root != old_root) store_root<Count>(old_directory, position, work);
        return oldest_can_match;
    }
    template<bool Count>
#ifdef STX_MATCH_INDEX_RADIX_INLINE
    [[gnu::always_inline]]
#endif
    Match advance(std::span<const std::uint8_t> input, std::uint32_t position,
        bool query, Work& work) {
        Match best{};
        const unsigned maximum = static_cast<unsigned>(std::min<std::size_t>(258, input.size() - position));
        if (maximum < 3) return best;
        // Exclude exactly one still-legal source before recycling its slot.
        // Query it separately below, after nearer sources have settled ties.
        const Link new_leaf = static_cast<Link>(position & (window - 1));
        const unsigned current_directory = directory(input, position);
        bool oldest_can_match = false;
        if (position >= window && live[new_leaf] != 0) {
            oldest_can_match = expire_linked<Count>(input, position, current_directory, work);
        }
#ifdef STX_MATCH_INDEX_RADIX_SINGLETON
        const DirectoryEntry entry = roots[current_directory];
        if (entry == directory_empty || (entry & linked_entry) == 0) {
#ifdef STX_MATCH_INDEX_RADIX_LAZY
            if (!insert_lazy_small<Count>(input, position, current_directory, maximum, query, best, work)) {
                const Match result = advance_tree<Count>(input, position, query, oldest_can_match, best, work);
                return result;
            }
#else
            best = insert_singleton<Count>(input, position, current_directory, maximum, query, work);
#endif
            if (query && oldest_can_match && best.length < maximum) {
                if constexpr (Count) { ++work.visits; ++work.compared; }
                if (input[position + best.length] == input[position - window + best.length]) {
                    const unsigned length = extend<Count>(input, position, position - window, 0, maximum, work);
                    if (length >= 3 && length > best.length) best = Match{length, window};
                }
            }
            return best;
        }
#endif
        root = static_cast<Link>(roots[current_directory]);
#if defined(STX_MATCH_INDEX_RADIX_SMALL) && !defined(STX_MATCH_INDEX_RADIX_LAZY)
        if (insert_small<Count>(input, position, maximum, query, best, work)) {
            store_root<Count>(current_directory, position, work);
            if (query && oldest_can_match && best.length < maximum) {
                if constexpr (Count) { ++work.visits; ++work.compared; }
                if (input[position + best.length] == input[position - window + best.length]) {
                    const unsigned length = extend<Count>(input, position, position - window, 0, maximum, work);
                    if (length >= 3 && length > best.length) best = Match{length, window};
                }
            }
            return best;
        }
#endif
        const Match result = advance_tree<Count>(input, position, query, oldest_can_match, best, work);
        return result;
    }
    // Keep the substantial tree kernel out of the common singleton/list loop.
    // This prevents its live temporaries from determining the hot loop's frame.
    template<bool Count>
    [[gnu::noinline]]
    Match advance_tree(std::span<const std::uint8_t> input, std::uint32_t position,
        bool query, bool oldest_can_match, Match best, Work& work) {
        const unsigned maximum = static_cast<unsigned>(std::min<std::size_t>(258, input.size() - position));
        const unsigned current_directory = directory(input, position);
        const Link new_leaf = static_cast<Link>(position & (window - 1));
        Link start = root;
#ifdef STX_MATCH_INDEX_RADIX_FINGER
        unsigned adjacent_bit = 0;
        if (position != 0) {
            const unsigned known_adjacent = adjacent_length > 0 ? adjacent_length - 1 : 0;
            if constexpr (Count) work.reused += known_adjacent;
            if (known_adjacent == 0 && input[position] != input[position - 1]) {
                adjacent_length = 0;
                if constexpr (Count) ++work.compared;
                if constexpr (Count) ++work.predictor_rejections;
            } else adjacent_length = extend<Count>(input, position, position - 1, known_adjacent, maximum, work);
            adjacent_bit = different_bit<Count>(input, position, position - 1, adjacent_length, maximum, work);
            const Link previous_leaf = static_cast<Link>((position - 1) & (window - 1));
            if (adjacent_bit == 258 * 8) {
                if constexpr (Count) ++work.duplicate_insertions;
                // The previous leaf is the exact key and wins every query tie.
                // Its ancestors stay on the implicit newest-source path.
                replace<Count>(previous_leaf, new_leaf, parents[previous_leaf], work);
                live[previous_leaf] = 0;
                live[new_leaf] = 1;
                store_root<Count>(current_directory, position, work);
                if constexpr (Count) work.writes += 2;
                if constexpr (share_prefixes) carried = Carried{position, position - 1, adjacent_length};
                if (query) return Match{maximum, 1};
                return best;
            }
            Link departed = previous_leaf;
            Link ancestor = live[previous_leaf] != 0 ? parents[previous_leaf] : none;
#ifdef STX_MATCH_INDEX_RADIX_SMALL
            if (ancestor < leaf_count) ancestor = none;
#endif
            // Every ancestor of the last inserted leaf has an implicit newest
            // source of position-1. Finalize only the path being departed.
            // Shared ancestors remain implicit for the new current source.
            while (ancestor != none && branches[ancestor - leaf_count].bit >= adjacent_bit) {
                Branch& branch = branches[ancestor - leaf_count];
                branch.newest = position - 1;
                departed = ancestor;
                ancestor = branch.parent;
                if constexpr (Count) { ++work.visits; ++work.writes; }
            }
            if (adjacent_length >= 2) {
                start = departed;
                // A first difference before this subtree's first split proves
                // its entire key group ties the adjacent source. Insert above
                // the subtree, using that source as the representative.
                if (start >= leaf_count && branches[start - leaf_count].bit > adjacent_bit) start = previous_leaf;
            }
            if (query && adjacent_length >= 3) best = Match{adjacent_length, 1};
        }
#endif
        if (root == none) {
            if constexpr (Count) ++work.root_insertions;
            root = new_leaf;
            live[new_leaf] = 1;
            parents[new_leaf] = none;
            if constexpr (Count) work.writes += 3;
        } else {
            Link leaf = start;
            while (leaf >= leaf_count) {
                const Branch& branch = branches[leaf - leaf_count];
                const unsigned side = direction(input, position, branch.bit);
                leaf = branch.children[side];
                if constexpr (Count) { ++work.visits; ++work.compared; }
            }
            if constexpr (Count) ++work.visits;
            const std::uint32_t source = source_position(leaf, position);
            const unsigned shift = position - carried.position;
            unsigned known = 2;
#ifdef STX_MATCH_INDEX_RADIX_FINGER
            if (source == position - 1) known = adjacent_length;
#endif
            if constexpr (share_prefixes) {
                if (shift < carried.length && source == carried.source + shift) known = std::max(known, carried.length - shift);
            }
            if constexpr (Count) work.reused += known;
            const unsigned length = extend<Count>(input, position, source, known, maximum, work);
            if constexpr (share_prefixes) carried = Carried{position, source, length};
            // Blind critical-bit descent finds a leaf with maximal common
            // prefix. Ascend through splits inside/after the mismatch byte:
            // this entire subtree shares the winning whole-byte prefix.
            if (query && length >= 3 && length > best.length) {
                Link group = leaf;
                Link ancestor = parents[leaf];
                while (ancestor != none && branches[ancestor - leaf_count].bit >= length * 8) {
                    group = ancestor;
                    ancestor = branches[ancestor - leaf_count].parent;
                    if constexpr (Count) ++work.visits;
                }
                best = Match{length, position - newest(group, position)};
            }
            const unsigned bit = different_bit<Count>(input, position, source, length, maximum, work);
            Link ancestor = parents[leaf];
            if (bit == 258 * 8) {
                if constexpr (Count) ++work.duplicate_insertions;
                replace<Count>(leaf, new_leaf, ancestor, work);
                live[leaf] = 0;
                if constexpr (Count) ++work.writes;
            } else {
                if constexpr (Count) ++work.branch_insertions;
                Link subtree = leaf;
                while (ancestor != none && branches[ancestor - leaf_count].bit >= bit) {
                    subtree = ancestor;
                    ancestor = branches[ancestor - leaf_count].parent;
                    if constexpr (Count) ++work.visits;
                }
                Link inserted = free_branch;
                if (inserted == none) {
                    require(allocated < branches.size());
                    inserted = static_cast<Link>(leaf_count + allocated);
                    ++allocated;
                } else free_branch = branches[inserted - leaf_count].children[0];
                Branch& branch = branches[inserted - leaf_count];
                const unsigned side = direction(input, position, bit);
                branch.children[side] = new_leaf;
                branch.children[1 - side] = subtree;
                branch.bit = static_cast<Link>(bit);
                branch.newest = position;
                replace<Count>(subtree, inserted, ancestor, work);
                set_parent(subtree, inserted);
                parents[new_leaf] = inserted;
                if constexpr (Count) work.writes += 6;
            }
            live[new_leaf] = 1;
            if constexpr (Count) ++work.writes;
#ifndef STX_MATCH_INDEX_RADIX_FINGER
            while (ancestor != none) {
                Branch& branch = branches[ancestor - leaf_count];
                branch.newest = position;
                ancestor = branch.parent;
                if constexpr (Count) { ++work.visits; ++work.writes; }
            }
#endif
        }
        store_root<Count>(current_directory, position, work);
        if (query && oldest_can_match && best.length < maximum) {
            if constexpr (Count) ++work.visits;
            if constexpr (Count) ++work.compared;
            if (input[position + best.length] == input[position - window + best.length]) {
                const unsigned length = extend<Count>(input, position, position - window, 0, maximum, work);
                if (length >= 3 && length > best.length) best = Match{length, window};
            }
        }
        return best;
    }
};
