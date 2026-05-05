#pragma once
#include <atomic>
#include <new>
#include <memory>
#include <vector>
#include <cstring> 
/* 一行候选的连续存储 */
// struct RowSlice {
//     unsigned int* data;      // ← 改为 unsigned int
//     unsigned int  size;      // ← 改为 unsigned int
//     RowSlice() : data(nullptr), size(0) {}
//     RowSlice(unsigned int* p, size_t len) : data(p), size(len) {}
// };

// struct RowSlice {
//     std::shared_ptr<unsigned int[]> data;  // 共享数组
//     size_t length;                         // 当前长度

//     // 默认构造函数
//     RowSlice() : length(0) {}

//     // 从原始指针构造（转移所有权）
//     explicit RowSlice(std::shared_ptr<unsigned int[]> ptr, size_t len)
//         : data(std::move(ptr)), length(len) {}

//     // 构造一个拷贝（可选）
//     RowSlice clone() const {
//         if (!data || length == 0) return RowSlice();

//         auto new_data = std::make_shared<unsigned int[]>(length);
//         std::memcpy(new_data.get(), data.get(), length * sizeof(unsigned int));
//         return RowSlice(new_data, length);
//     }

//     // 获取 raw 指针（用于遍历）
//     const unsigned int* get() const { return data ? data.get() : nullptr; }
//     unsigned int* get() { return data ? data.get() : nullptr; }

//     bool empty() const { return length == 0 || !data; }
// };
//template<typename T>
struct SingleUnsigned {
    unsigned int value;
};

inline std::shared_ptr<unsigned int> make_single_shared(unsigned int val) {
    auto sp = std::make_shared<SingleUnsigned>();
    sp->value = val;
    return std::shared_ptr<unsigned int>(sp, &sp->value);
}

std::shared_ptr<unsigned int> make_array_shared(size_t n) {
    return std::shared_ptr<unsigned int>(new unsigned int[n], std::default_delete<unsigned int[]>());
}

struct RowSlice {
    std::shared_ptr<unsigned int> data;  // 注意：这里不是 T[]
    size_t length;

    RowSlice() : length(0) {}

    RowSlice(std::shared_ptr<unsigned int> ptr, size_t len)
        : data(std::move(ptr)), length(len) {}

    const unsigned int* get() const { return data.get(); }
    unsigned int* get() { return data.get(); }

    bool empty() const { return !data || length == 0; }

    RowSlice clone() const {
        if (empty()) return RowSlice();

        auto new_data = make_array_shared(length);
        std::memcpy(new_data.get(), data.get(), length * sizeof(unsigned int));
        return RowSlice(new_data, length);
    }
};

// struct alignas(64) CowSnapshot {
//     std::atomic<int> ref{1};
//     unsigned int    numRow;              // 行数
//     RowSlice        rows[];               // 柔性数组（Flexible Array Member）

//     // 获取整个结构体大小（用于 malloc）
//     static size_t total_size(unsigned int n) {
//         return sizeof(CowSnapshot) + n * sizeof(RowSlice);
//     }

//     // 创建新快照（静态工厂函数）
//     static CowSnapshot* create(int n) {
//         void* mem = ::operator new(total_size(n));  // 或 malloc
//         CowSnapshot* snap = new (mem) CowSnapshot();
//         snap->numRow = n;
//         // 注意：rows[i] 需要后续赋值
//         return snap;
//     }

//     // 销毁（仅当 ref == 0 时调用 release 触发）
//     void destroy() {
//         this->~CowSnapshot();           // 析构（当前为空）
//         ::operator delete(this);       // 匹配 operator new
//     }

//     // 引用计数操作
//     void acquire() {
//         ref.fetch_add(1, std::memory_order_relaxed);
//     }

//     void release() {
//         int old = ref.fetch_sub(1, std::memory_order_acq_rel);
//         if (old == 1) {
//             // 可选：清零内存页（帮助 OS 回收）
//             madvise(this, total_size(numRow), MADV_DONTNEED);
//             destroy();
//         }
//     }

// private:
//     // 私有构造函数（禁止栈上创建）
//     CowSnapshot() = default;
// };

struct alignas(64) CowSnapshot {
    std::atomic<int> ref{1};
    unsigned int numRow;          // 行数，构造后不可变

    /* 返回 rows 数组首地址 */
    RowSlice*       rows()       { return reinterpret_cast<RowSlice*>(this + 1); }
    const RowSlice* rows() const { return reinterpret_cast<const RowSlice*>(this + 1); }

    /* 计算总字节数 */
    static size_t total_size(unsigned int n) {
        return sizeof(CowSnapshot) + n * sizeof(RowSlice);
    }

    /* 工厂：一次性分配 + 逐个 placement-new */
    static CowSnapshot* create(unsigned int n) {
        void* mem = ::operator new(total_size(n));
        CowSnapshot* snap = new (mem) CowSnapshot(n);
        RowSlice* r = snap->rows();
        for (unsigned int i = 0; i < n; ++i) new (r + i) RowSlice();
        return snap;
    }

    /* 释放：先逐个析构，再释放整块内存 */
    void destroy() {
        RowSlice* r = rows();
        for (unsigned int i = 0; i < numRow; ++i) r[i].~RowSlice();
        this->~CowSnapshot();
        ::operator delete(this);
    }

    /* 引用计数减到 0 时销毁 */
    void release() {
        if (ref.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            destroy();
        }
    }

    void acquire() { ref.fetch_add(1, std::memory_order_relaxed); }

private:
    explicit CowSnapshot(unsigned int n) : numRow(n) {}
    ~CowSnapshot() = default;   // 空析构，真正的清理在 destroy()

    /* 禁止栈对象及数组 new/delete */
    void* operator new(size_t) = delete;
    void* operator new[](size_t) = delete;
    void* operator new(size_t, void* p) { return p; }
    void operator delete(void*, void*) {}
};