// TaskTypes.h  或粘在你现有头文件里
#pragma once
#include <atomic>
#include"snapshot.h"

struct TravSet {
    unsigned int buf[4];
    uint8_t  len = 0;
    void push(unsigned int v) { buf[len++] = v; }
    bool test(unsigned int v) const {
        for (uint8_t i = 0; i < len; ++i) if (buf[i] == v) return true;
        return false;
    }
    TravSet clone() const { return *this; }
};


// /* 深拷贝被修改的行，其余复用父指针 */
// inline CowSnapshot* cow_fork(BufferArena& arena,
//                              const CowSnapshot* parent,
//                              const std::vector<uint32_t>& dirtyRows,
//                              const std::vector<RowSlice>& newRows)
// {
//     uint32_t n = parent->numRow;
//     size_t need = sizeof(CowSnapshot) + n * sizeof(RowSlice);
//     auto ptr = arena.allocate(need, alignof(CowSnapshot));
//     auto snap = new (ptr.ptr) CowSnapshot;
//     snap->numRow = n;
//     // 先复制父行指针
//     for (uint32_t i = 0; i < n; ++i) snap->rows[i] = parent->rows[i];
//     // 深拷贝被改行
//     for (size_t j = 0; j < dirtyRows.size(); ++j) {
//         uint32_t r = dirtyRows[j];
//         uint32_t bytes = newRows[j].size * sizeof(uint32_t);
//         auto buf = arena.allocate(bytes, alignof(uint32_t));
//         memcpy(buf.ptr, newRows[j].data, bytes);
//         snap->rows[r] = { (uint32_t*)buf.ptr, newRows[j].size };
//     }
//     return snap;
// }

/* Task 控制块（48 B）*/
struct Task {
    CowSnapshot* snapshot = nullptr;
    TravSet      trav;
    unsigned int centerIdx = 0;

    Task() = default;

    Task(CowSnapshot* snap, const TravSet& tset, unsigned int idx)
        : snapshot(snap), trav(tset), centerIdx(idx)
    {
    }
};