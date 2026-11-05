#include "include/core/SkRefCnt.h"
#include "include/core/SkSpan.h"
#include "include/private/SkWeakRefCnt.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <sstream>
#include <span>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#include <variant>

namespace {
struct Events {
    std::atomic<unsigned int> destroyed{0};
    std::atomic<unsigned int> disposed{0};
};

class Probe : public SkRefCnt {
public:
    explicit Probe(Events& events) : events_(events) {}
    ~Probe() override {
        if (reset_owner != nullptr) (*reset_owner).reset();
        events_.destroyed.fetch_add(1, std::memory_order_relaxed);
    }
    sk_sp<Probe>* reset_owner{nullptr};
private:
    Events& events_;
};

class DerivedProbe final : public Probe {
public:
    explicit DerivedProbe(Events& events) : Probe(events) {}
};

class NVProbe final : public SkNVRefCnt<NVProbe> {
public:
    explicit NVProbe(Events& events) : events_(events) {}
    ~NVProbe() { events_.destroyed.fetch_add(1, std::memory_order_relaxed); }
private:
    Events& events_;
};

class WeakProbe final : public SkWeakRefCnt {
public:
    explicit WeakProbe(Events& events) : events_(events) {}
    ~WeakProbe() override { events_.destroyed.fetch_add(1, std::memory_order_relaxed); }
protected:
    void weak_dispose() const override {
        events_.disposed.fetch_add(1, std::memory_order_relaxed);
    }
private:
    Events& events_;
};

bool transfers() {
    Events events{};
    sk_sp<DerivedProbe> derived = sk_make_sp<DerivedProbe>(events);
    DerivedProbe* identity = derived.get();
    sk_sp<Probe> copy = derived;
    if ((*derived).unique()) return false;
    sk_sp<Probe> moved = std::move(derived);
    if (derived || moved.get() != identity) return false;
    copy = copy;
    if (copy != moved) return false;
    moved = std::move(moved);
    if (moved.get() != identity) return false;
    copy = nullptr;
    if (!(*moved).unique()) return false;
    Probe* transferred = moved.release();
    if (moved || transferred != identity) return false;
    moved.reset(transferred);
    sk_sp<Probe> retained = sk_ref_sp(static_cast<const Probe*>(identity));
    if ((*moved).unique()) return false;
    retained.reset();
    if (!(*moved).unique()) return false;
    std::ostringstream expected;
    std::ostringstream actual;
    expected << identity;
    actual << moved;
    if (actual.str() != expected.str()) return false;
    swap(moved, copy);
    if (moved || copy.get() != identity) return false;
    copy.reset();
    if (events.destroyed.load() != 1) return false;

    sk_sp<Probe> reentrant = sk_make_sp<Probe>(events);
    (*reentrant).reset_owner = &reentrant;
    // Releasing the old object resets and releases the already-published replacement.
    reentrant.reset(new Probe(events));
    if (reentrant || events.destroyed.load() != 3) return false;
    if (SkSafeRef(static_cast<Probe*>(nullptr)) != nullptr) return false;
    SkSafeUnref(static_cast<Probe*>(nullptr));
    return true;
}

void share_references(const NVProbe* borrowed) {
    for (unsigned int index = 0; index < 1024; ++index) {
        sk_sp<NVProbe> local = sk_ref_sp(borrowed);
        sk_sp<NVProbe> copy = local;
    }
}

bool concurrent_sharing() {
    Events events{};
    sk_sp<NVProbe> owner = sk_make_sp<NVProbe>(events);
    std::array<std::thread, 8> workers{};
    for (std::thread& worker : workers) worker = std::thread(share_references, owner.get());
    for (std::thread& worker : workers) worker.join();
    if (!(*owner).unique() || (*owner).refCntGreaterThan(1)) return false;
    sk_sp<NVProbe> second = owner;
    if (!(*owner).refCntGreaterThan(1) || (*owner).refCntGreaterThan(2)) return false;
    second.reset();
    owner.reset();
    return events.destroyed.load() == 1;
}

void attempt_weak_lock(WeakProbe* observer) {
    for (unsigned int index = 0; index < 1024; ++index) {
        if ((*observer).try_ref()) (*observer).unref();
    }
    (*observer).weak_unref();
}

struct WeakReference {
    explicit WeakReference(WeakProbe& probe) : target(probe) { target.weak_ref(); }
    ~WeakReference() { target.weak_unref(); }
    WeakReference(const WeakReference&) = delete;
    WeakReference& operator=(const WeakReference&) = delete;
    WeakProbe& target;
};

bool weak_lifetime_scope(Events& events) {
    sk_sp<WeakProbe> owner = sk_make_sp<WeakProbe>(events);
    WeakProbe* observer = owner.get();
    WeakReference weak(*observer);
    if (!(*observer).try_ref()) return false;
    (*observer).unref();
    std::array<std::thread, 8> workers{};
    for (std::thread& worker : workers) {
        (*observer).weak_ref();
        worker = std::thread(attempt_weak_lock, observer);
    }
    owner.reset();
    for (std::thread& worker : workers) worker.join();
    if (!(*observer).weak_expired() || (*observer).try_ref()) return false;
    if (events.disposed.load() != 1 || events.destroyed.load() != 0) return false;
    return true;
}

bool weak_lifetimes() {
    Events events{};
    if (!weak_lifetime_scope(events)) return false;
    return events.destroyed.load() == 1;
}

constexpr bool constexpr_view() {
    int values[3] = {3, 5, 7};
    SkSpan<int> view(values);
    return view.data()[1] == 5 && view.size() == 3 && view.size_bytes() == 3 * sizeof(int);
}
static_assert(constexpr_view());
#ifndef STX_SKIA_REFERENCE
struct NotAContainer { int value = 0; };
static_assert(!std::is_constructible_v<SkSpan<int>, NotAContainer&>);
static_assert(!std::is_convertible_v<NotAContainer&, SkSpan<int>>);
static_assert(!std::is_constructible_v<SkSpan<int>, const std::vector<int>&>);
static_assert(!std::is_constructible_v<SkSpan<int>, std::vector<float>&>);
static_assert(!std::is_constructible_v<SkSpan<int>, std::vector<bool>&>);
static_assert(std::is_constructible_v<SkSpan<const int>, const std::vector<int>&>);
static_assert(std::is_constructible_v<std::variant<NotAContainer, SkSpan<int>>, NotAContainer&>);
static_assert(!std::is_constructible_v<SkSpan<int>, std::vector<int>&&>);
static_assert(!std::is_constructible_v<SkSpan<const int>, std::vector<int>&&>);
static_assert(std::is_constructible_v<SkSpan<int>, std::vector<int>&>);
static_assert(std::is_constructible_v<SkSpan<const int>, std::span<const int>>);
#endif

bool views() {
    std::array<int, 33> values{};
    for (size_t index = 0; index < values.size(); ++index) values[index] = static_cast<int>(index);
    for (size_t length = 0; length <= values.size(); ++length) {
        const SkSpan<int> view(values.data(), length);
        for (size_t offset = 0; offset <= length; ++offset) {
            const SkSpan<int> tail = view.subspan(offset);
            if (tail.data() != values.data() + offset || tail.size() != length - offset) return false;
            for (size_t count = 0; count <= length - offset; ++count) {
                const SkSpan<int> part = view.subspan(offset, count);
                if (part.size() != count || part.data() != values.data() + offset) return false;
            }
            if (view.first(offset).size() != offset || view.last(offset).size() != offset) return false;
            if (view.last(offset).data() != values.data() + length - offset) return false;
        }
        if (length != 0 && (view.front() != 0 || view.back() != static_cast<int>(length - 1))) return false;
        size_t reverse_index = length;
        for (std::reverse_iterator<int*> iterator = view.rbegin(); iterator != view.rend(); ++iterator) {
            --reverse_index;
            if (*iterator != values[reverse_index]) return false;
        }
    }
    const SkSpan<int> empty;
    if (empty.begin() != nullptr || empty.end() != nullptr || !empty.first(0).empty()) return false;
    if (!empty.last(0).empty() || !empty.subspan(0).empty() || !empty.subspan(0, 0).empty()) return false;
    SkSpan<int> mutable_view(values);
    const SkSpan<const int> immutable = mutable_view;
    if (immutable.data() != values.data()) return false;
    const SkSpan<int> standard_view(std::span<int>(values.data(), values.size()));
    if (standard_view.data() != values.data()) return false;
    return true;
}
}

int main() {
    if (!transfers() || !concurrent_sharing() || !weak_lifetimes() || !views()) return 1;
    std::puts("strong/weak ownership, reentrant release, concurrent sharing and views: PASS");
    return 0;
}
