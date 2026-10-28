/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkRefCnt_DEFINED
#define SkRefCnt_DEFINED

#include "include/core/SkTypes.h"
#include "include/private/SkDebug.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <type_traits>
#include <utility>

/** \class SkRefCntBase

    SkRefCntBase is the base class for objects that may be shared by multiple
    objects. When an existing owner wants to share a reference, it calls ref().
    When an owner wants to release its reference, it calls unref(). When the
    shared object's reference count goes to zero as the result of an unref()
    call, its (virtual) destructor is called. It is an error for the
    destructor to be called explicitly (or via the object going out of scope on
    the stack or calling delete) if getRefCnt() > 1.
*/
class SK_API SkRefCntBase {
public:
    /** Default construct, initializing the reference count to 1.
    */
    SkRefCntBase() : fRefCnt(1) {}

    /** Destruct, asserting that the reference count is 1.
    */
    virtual ~SkRefCntBase() {
    #ifdef SK_DEBUG
        SkASSERTF(getRefCnt() == 1, "fRefCnt was %d", getRefCnt());
        // illegal value, to catch us if we reuse after delete
        fRefCnt.store(0, std::memory_order_relaxed);
    #endif
    }

    /** May return true if the caller is the only owner.
     *  Ensures that all previous owner's actions are complete.
     */
    bool unique() const {
        const int32_t count = fRefCnt.load(std::memory_order_acquire);
        if (count == 1) {
            // The acquire barrier is only really needed if we return true.  It
            // prevents code conditioned on the result of unique() from running
            // until previous owners are all totally done calling unref().
            return true;
        }
        return false;
    }

    /** Increment the reference count. Must be balanced by a call to unref().
    */
    void ref() const {
        SkASSERT(getRefCnt() > 0);
        // No barrier required.
        (void)fRefCnt.fetch_add(+1, std::memory_order_relaxed);
    }

    /** Decrement the reference count. If the reference count is 1 before the
        decrement, then delete the object. Note that if this is the case, then
        the object needs to have been allocated via new, and not on the stack.
    */
    void unref() const {
        SkASSERT(getRefCnt() > 0);
        // A release here acts in place of all releases we "should" have been doing in ref().
        const int32_t previous = fRefCnt.fetch_add(-1, std::memory_order_acq_rel);
        if (previous == 1) {
            // Like unique(), the acquire is only needed on success, to make sure
            // code in internal_dispose() doesn't happen before the decrement.
            internal_dispose();
        }
    }

private:

#ifdef SK_DEBUG
    /** Return the reference count. Use only for debugging. */
    int32_t getRefCnt() const {
        const int32_t count = fRefCnt.load(std::memory_order_relaxed);
        return count;
    }
#endif

    /**
     *  Called when the ref count goes to 0.
     */
    virtual void internal_dispose() const {
    #ifdef SK_DEBUG
        SkASSERT(0 == getRefCnt());
        fRefCnt.store(1, std::memory_order_relaxed);
    #endif
        delete this;
    }

    // The following friends are those which override internal_dispose()
    // and conditionally call SkRefCnt::internal_dispose().
    friend class SkWeakRefCnt;

    mutable std::atomic<int32_t> fRefCnt;

    SkRefCntBase(SkRefCntBase&&) = delete;
    SkRefCntBase(const SkRefCntBase&) = delete;
    SkRefCntBase& operator=(SkRefCntBase&&) = delete;
    SkRefCntBase& operator=(const SkRefCntBase&) = delete;
};

#ifdef SK_REF_CNT_MIXIN_INCLUDE
// It is the responsibility of the following include to define the type SkRefCnt.
// This SkRefCnt should normally derive from SkRefCntBase.
#include SK_REF_CNT_MIXIN_INCLUDE
#else
class SK_API SkRefCnt : public SkRefCntBase {
    // "#include SK_REF_CNT_MIXIN_INCLUDE" doesn't work with this build system.
    #if defined(SK_BUILD_FOR_GOOGLE3)
    public:
        void deref() const { unref(); }
    #endif
};
#endif

///////////////////////////////////////////////////////////////////////////////

/** Call (*obj).ref() and return obj. The obj must not be nullptr.
 */
template <typename T> static inline T* SkRef(T* obj) {
    SkASSERT(obj);
    (*obj).ref();
    return obj;
}

/** Check if the argument is non-null, and if so, call (*obj).ref() and return obj.
 */
template <typename T> static inline T* SkSafeRef(T* obj) {
    if (obj) {
        (*obj).ref();
    }
    return obj;
}

/** Check if the argument is non-null, and if so, call (*obj).unref()
 */
template <typename T> static inline void SkSafeUnref(T* obj) {
    if (obj) {
        (*obj).unref();
    }
}

///////////////////////////////////////////////////////////////////////////////

// This is a variant of SkRefCnt that's Not Virtual, so weighs 4 bytes instead of 8 or 16.
// There's only benefit to using this if the deriving class does not otherwise need a vtable.
template <typename Derived>
class SkNVRefCnt {
public:
    SkNVRefCnt() : fRefCnt(1) {}
    ~SkNVRefCnt() {
    #ifdef SK_DEBUG
        const int32_t rc = fRefCnt.load(std::memory_order_relaxed);
        SkASSERTF(rc == 1, "NVRefCnt was %d", rc);
    #endif
    }

    // Implementation is pretty much the same as SkRefCntBase. All required barriers are the same:
    //   - unique() needs acquire when it returns true, and no barrier if it returns false;
    //   - ref() doesn't need any barrier;
    //   - unref() needs a release barrier, and an acquire if it's going to call delete.

    bool unique() const {
        const int32_t count = fRefCnt.load(std::memory_order_acquire);
        const bool sole_owner = count == 1;
        return sole_owner;
    }
    void ref() const { (void)fRefCnt.fetch_add(+1, std::memory_order_relaxed); }
    void unref() const {
        const int32_t previous = fRefCnt.fetch_add(-1, std::memory_order_acq_rel);
        if (previous == 1) {
            // restore the 1 for our destructor's assert
            SkDEBUGCODE(fRefCnt.store(1, std::memory_order_relaxed));
            const Derived* owner = static_cast<const Derived*>(this);
            delete owner;
        }
    }
    void  deref() const { unref(); }

    // This must be used with caution. It is only valid to call this when 'threadIsolatedTestCnt'
    // refs are known to be isolated to the current thread. That is, it is known that there are at
    // least 'threadIsolatedTestCnt' refs for which no other thread may make a balancing unref()
    // call. Assuming the contract is followed, if this returns false then no other thread has
    // ownership of this. If it returns true then another thread *may* have ownership.
    bool refCntGreaterThan(int32_t threadIsolatedTestCnt) const {
        const int32_t cnt = fRefCnt.load(std::memory_order_acquire);
        // If this fails then the above contract has been violated.
        SkASSERT(cnt >= threadIsolatedTestCnt);
        const bool shared = cnt > threadIsolatedTestCnt;
        return shared;
    }

private:
    mutable std::atomic<int32_t> fRefCnt;

    SkNVRefCnt(SkNVRefCnt&&) = delete;
    SkNVRefCnt(const SkNVRefCnt&) = delete;
    SkNVRefCnt& operator=(SkNVRefCnt&&) = delete;
    SkNVRefCnt& operator=(const SkNVRefCnt&) = delete;
};

///////////////////////////////////////////////////////////////////////////////////////////////////

/**
 *  Shared pointer class to wrap classes that support a ref()/unref() interface.
 *
 *  This can be used for classes inheriting from SkRefCnt, but it also works for other
 *  classes that match the interface, but have different internal choices: e.g. the hosted class
 *  may have its ref/unref be thread-safe, but that is not assumed/imposed by sk_sp.
 *
 *  Declared with the trivial_abi attribute where supported so that sk_sp and types containing it
 *  may be considered as trivially relocatable by the compiler so that destroying-move operations
 *  i.e. move constructor followed by destructor can be optimized to memcpy.
 */
template <typename T> class SK_TRIVIAL_ABI sk_sp {
public:
    using element_type = T;

    constexpr sk_sp() : fPtr(nullptr) {}
    constexpr sk_sp(std::nullptr_t) : fPtr(nullptr) {}

    /**
     *  Shares the underlying object by calling ref(), so that both the argument and the newly
     *  created sk_sp both have a reference to it.
     */
    sk_sp(const sk_sp<T>& that) : fPtr(SkSafeRef(that.get())) {}
    template <typename U,
              typename = typename std::enable_if<std::is_convertible<U*, T*>::value>::type>
    sk_sp(const sk_sp<U>& that) : fPtr(SkSafeRef(that.get())) {}

    /**
     *  Move the underlying object from the argument to the newly created sk_sp. Afterwards only
     *  the new sk_sp will have a reference to the object, and the argument will point to null.
     *  No call to ref() or unref() will be made.
     */
    sk_sp(sk_sp<T>&& that) : fPtr(that.release()) {}
    template <typename U,
              typename = typename std::enable_if<std::is_convertible<U*, T*>::value>::type>
    sk_sp(sk_sp<U>&& that) : fPtr(that.release()) {}

    /**
     *  Adopt the bare pointer into the newly created sk_sp.
     *  No call to ref() or unref() will be made.
     */
    explicit sk_sp(T* obj) : fPtr(obj) {}

    /**
     *  Calls unref() on the underlying object pointer.
     */
    ~sk_sp() {
        SkSafeUnref(fPtr);
        SkDEBUGCODE(fPtr = nullptr);
    }

    sk_sp<T>& operator=(std::nullptr_t) {
        reset();
        return *this;
    }

    /**
     *  Shares the underlying object referenced by the argument by calling ref() on it. If this
     *  sk_sp previously had a reference to an object (i.e. not null) it will call unref() on that
     *  object.
     */
    sk_sp<T>& operator=(const sk_sp<T>& that) {
        if (this != &that) {
            T* retained = SkSafeRef(that.get());
            reset(retained);
        }
        return *this;
    }
    template <typename U,
              typename = typename std::enable_if<std::is_convertible<U*, T*>::value>::type>
    sk_sp<T>& operator=(const sk_sp<U>& that) {
        T* retained = SkSafeRef(that.get());
        reset(retained);
        return *this;
    }

    /**
     *  Move the underlying object from the argument to the sk_sp. If the sk_sp previously held
     *  a reference to another object, unref() will be called on that object. No call to ref()
     *  will be made.
     */
    sk_sp<T>& operator=(sk_sp<T>&& that) {
        T* transferred = that.release();
        reset(transferred);
        return *this;
    }
    template <typename U,
              typename = typename std::enable_if<std::is_convertible<U*, T*>::value>::type>
    sk_sp<T>& operator=(sk_sp<U>&& that) {
        T* transferred = that.release();
        reset(transferred);
        return *this;
    }

    T& operator*() const {
        SkASSERT(fPtr != nullptr);
        return *fPtr;
    }

    explicit operator bool() const {
        const bool present = fPtr != nullptr;
        return present;
    }

    T* get() const { return fPtr; }
    T* operator->() const { return fPtr; }

    /**
     *  Adopt the new bare pointer, and call unref() on any previously held object (if not null).
     *  No call to ref() will be made.
     */
    void reset(T* ptr = nullptr) {
        // Release can reenter this owner or destroy it. Publish the replacement
        // before releasing the old reference, and do not touch this afterward.
        // http://wg21.cmeerw.net/lwg/issue998
        // http://wg21.cmeerw.net/lwg/issue2262
        T* oldPtr = fPtr;
        fPtr = ptr;
        SkSafeUnref(oldPtr);
    }

    /**
     *  Return the bare pointer, and set the internal object pointer to nullptr.
     *  The caller must assume ownership of the object, and manage its reference count directly.
     *  No call to unref() will be made.
     */
    [[nodiscard]] T* release() {
        T* ptr = fPtr;
        fPtr = nullptr;
        return ptr;
    }

    void swap(sk_sp<T>& that) {
        using std::swap;
        swap(fPtr, that.fPtr);
    }

    using sk_is_trivially_relocatable = std::true_type;

private:
    T*  fPtr;
};

template <typename T> inline void swap(sk_sp<T>& a, sk_sp<T>& b) {
    a.swap(b);
}

template <typename T, typename U> inline bool operator==(const sk_sp<T>& a, const sk_sp<U>& b) {
    const bool same = a.get() == b.get();
    return same;
}
template <typename T> inline bool operator==(const sk_sp<T>& a, std::nullptr_t) {
    const bool empty = !a;
    return empty;
}
template <typename T> inline bool operator==(std::nullptr_t, const sk_sp<T>& b) {
    const bool empty = !b;
    return empty;
}

template <typename T, typename U> inline bool operator!=(const sk_sp<T>& a, const sk_sp<U>& b) {
    const bool different = a.get() != b.get();
    return different;
}
template <typename T> inline bool operator!=(const sk_sp<T>& a, std::nullptr_t) {
    const bool present = static_cast<bool>(a);
    return present;
}
template <typename T> inline bool operator!=(std::nullptr_t, const sk_sp<T>& b) {
    const bool present = static_cast<bool>(b);
    return present;
}

template <typename C, typename CT, typename T>
std::basic_ostream<C, CT>& operator<<(std::basic_ostream<C, CT>& os, const sk_sp<T>& sp) {
    os << sp.get();
    return os;
}

template <typename T> sk_sp(T*) -> sk_sp<T>;

template <typename T, typename... Args>
sk_sp<T> sk_make_sp(Args&&... args) {
    sk_sp<T> owner(new T(std::forward<Args>(args)...));
    return owner;
}

/*
 *  Returns a sk_sp wrapping the provided ptr AND calls ref on it (if not null).
 *
 *  This is different than the semantics of the constructor for sk_sp, which just wraps the ptr,
 *  effectively "adopting" it.
 */
template <typename T> sk_sp<T> sk_ref_sp(T* obj) {
    T* retained = SkSafeRef(obj);
    sk_sp<T> owner(retained);
    return owner;
}

template <typename T> sk_sp<T> sk_ref_sp(const T* obj) {
    const T* retained = SkSafeRef(obj);
    sk_sp<T> owner(const_cast<T*>(retained));
    return owner;
}

#endif
