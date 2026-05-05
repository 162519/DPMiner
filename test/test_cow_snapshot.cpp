// test.cpp
#include "/home/caohaoshuang/pjj/distributed_graph_query/PMiner-undirect/src/snapshot.h"   // 把前面贴的头文件内容放这里
#include <iostream>
#include <iomanip>

void print_row(const RowSlice& r, const char* tag) {
    std::cout << tag << "  length=" << r.length
              << "  use_count=" << r.data.use_count()
              << "  data={ ";
    const unsigned int* p = r.get();
    for (size_t i = 0; i < r.length; ++i) std::cout << p[i] << ' ';
    std::cout << "}\n";
}

void test(){
    constexpr unsigned int kRows = 3;

    /* 1. 构造源数据 */
    RowSlice src[3];
    for (unsigned int i = 0; i < kRows; ++i) {
        auto sp = make_array_shared(i + 2);
        for (size_t j = 0; j < i + 2; ++j) sp.get()[j] = (i + 1) * 10 + j;
        src[i] = RowSlice(sp, i + 2);
    }

    /* 2. 目标快照：未构造的裸内存 */
    CowSnapshot* dst1 = CowSnapshot::create(kRows);
    std::uninitialized_copy(std::begin(src), std::end(src), dst1->rows());
    std::cout << "----- after uninitialized_copy -----\n";
    for (unsigned int i = 0; i < kRows; ++i) {
        std::cout << "dst1 row[" << i << "] use_count="
                  << dst1->rows()[i].data.use_count() << "  data={ ";
        for (size_t j = 0; j < dst1->rows()[i].length; ++j)
            std::cout << dst1->rows()[i].get()[j] << ' ';
        std::cout << "}\n";
    }

    /* 3. 再建一个已构造的快照，用 std::copy */
    CowSnapshot* dst2 = CowSnapshot::create(kRows);
    /* 先默认构造 */
    RowSlice* r2 = dst2->rows();
    for (unsigned int i = 0; i < kRows; ++i) new (r2 + i) RowSlice();
    /* 再拷贝赋值 */
    std::copy(std::begin(src), std::end(src), dst2->rows());
    std::cout << "\n----- after std::copy -----\n";
    for (unsigned int i = 0; i < kRows; ++i) {
        std::cout << "dst2 row[" << i << "] use_count="
                  << dst2->rows()[i].data.use_count() << "  data={ ";
        for (size_t j = 0; j < dst2->rows()[i].length; ++j)
            std::cout << dst2->rows()[i].get()[j] << ' ';
        std::cout << "}\n";
    }

    /* 4. 清理 */
    dst1->destroy();
    dst2->destroy();
}

int main() {
    /* ---------- 1. RowSlice 共享/克隆测试 ---------- */
    std::cout << "----- RowSlice test -----\n";
    auto sp = make_array_shared(4);          // 创建底层数组
    unsigned int* raw = sp.get();
    for (unsigned int i = 0; i < 4; ++i) raw[i] = i + 10;

    RowSlice r1(sp, 4);                      // 第一个切片
    print_row(r1, "r1");

    RowSlice r2 = r1;                        // 共享同一个 shared_ptr
    print_row(r2, "r2");

    RowSlice r3 = r1.clone();                // 深拷贝
    print_row(r3, "r3(copy)");

    r3.get()[0] = 999;                       // 修改副本
    print_row(r1, "r1(unchanged)");
    print_row(r3, "r3(modified)");

    /* ---------- 2. CowSnapshot 测试 ---------- */
    std::cout << "\n----- CowSnapshot test -----\n";
    constexpr unsigned int kRows = 3;
    CowSnapshot* snap = CowSnapshot::create(kRows);

    /* 给每一行赋不同数据 */
    for (unsigned int i = 0; i < kRows; ++i) {
        auto row_sp = make_array_shared(i + 2);
        for (unsigned int j = 0; j < i + 2; ++j) row_sp.get()[j] = (i + 1) * 10 + j;
        snap->rows()[i] = RowSlice(row_sp, i + 2);
    }

    /* 打印快照内容 */
    std::cout << "snapshot use_count=" << snap->ref.load()
              << "  numRow=" << snap->numRow << '\n';
    for (unsigned int i = 0; i < snap->numRow; ++i) {
        std::ostringstream os;
        os << "  row[" << i << "]";
        print_row(snap->rows()[i], os.str().c_str());
    }

    /* 模拟多线程/多引用 */
    snap->acquire();
    std::cout << "\nafter acquire, ref=" << snap->ref.load() << '\n';

    /* 释放两次，第二次会真正销毁并 madvise */
    snap->release();
    std::cout << "after 1st release, ref=" << snap->ref.load() << '\n';
    snap->release();   // 这里会触发 ~CowSnapshot 和 madvise
    std::cout << "after 2nd release, snapshot destroyed\n";
    test();
    return 0;
}