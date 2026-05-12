# Performance Optimization Plan

## Phase 1: Low-hanging fruit (easy, high impact)

### 1. Fix `extendEdgePattern_final_new` redundant equivalent group work
**File:** `src/PMiner.h`, lines 631-649  
**Impact:** High (eliminates O(nbr_len × group_size) redundant work)  
**Risk:** Low  

**Problem:** The final-level function recomputes full neighborhood expansion for every member of equivalent groups, even though members already have their rows populated by `min_schedule` or `int_schedule`. The non-final version (`extendEdgePattern_new`, lines 522-532) correctly shares `RowSlice` via `shared_ptr` copy.

**Fix:** Replace the loop body with the sharing logic from the non-final version.

### 2. Remove redundant sort in `Graph::updata_batch`
**File:** `src/Graph.h`, line 361  
**Impact:** Medium (removes O(d log d) sort from the data-ready critical path)  
**Risk:** None  

**Problem:** `updata_batch` sorts every remote neighbor list on arrival. But the data originates from `local_adj`, which is already sorted per-vertex at graph load time (`build_adj()`, lines 180-182). The sender (`Request::workerLoop`) copies contiguous ranges without reordering. Data arrives pre-sorted.

**Fix:** Remove the `sort()` call.

### 3. Replace `rand()` with a fast local PRNG
**File:** `util/global.h`, line 271  
**Impact:** Low (removes global-lock contention on some `rand()` implementations)  
**Risk:** None  

**Problem:** `getRemoteData()` calls `rand() % candidate_node.size()`. On glibc, `rand()` uses a global lock. It's also a low-quality generator.

**Fix:** Replace with a simple xorshift32 or just pick the first candidate. The random selection is for load balancing; a deterministic round-robin or first-pick is equally effective and faster.

### 4. Add cache-line padding to hot atomics
**Files:** `src/PMiner.h`, `util/global.h`  
**Impact:** Low-Medium (reduces false sharing on atomics accessed by all worker threads)  
**Risk:** None  

**Problem:** `inFlightTasks_`, `finalAns` (PMiner), and `totalTaskCount` (global) are atomics written by all TBB worker threads. They likely share cache lines with adjacent data, causing cross-core invalidation.

**Fix:** Wrap each in `alignas(64)` or pad with a char array to fill the cache line.

### 5. Change `notify_all` to `notify_one` in Response worker
**File:** `src/Response.h`, line 123  
**Impact:** Low (reduces spurious wakeups)  
**Risk:** None  

**Problem:** `g_dataReadyCv.notify_all()` wakes ALL waiting threads when a single vertex's data arrives. Only one thread needs to proceed (typically the prefetch thread checking if a batch is ready).

**Fix:** Change to `notify_one()`.

### 6. TravSet: unroll the `test()` loop
**File:** `src/Task.h`, lines 6-14  
**Impact:** Low (micro-optimization in innermost loop)  
**Risk:** None  

**Problem:** `test()` does a for-loop linear scan for ≤4 elements. Branch predictor handles this well, but unrolling eliminates loop overhead entirely.

**Fix:** Hand-unroll the comparison.

## Phase 2: Structural improvements (medium effort, high impact)

### 7. CowSnapshot object pool
**File:** `src/snapshot.h`  
**Impact:** High  
**Risk:** Medium  

### 8. Eliminate double allocation in extendEdgePattern
**File:** `src/PMiner.h`  
**Impact:** Medium  
**Risk:** Medium  

### 9. Replace `tbb::concurrent_hash_map` with lock-free read structure
**File:** `src/Graph.h`  
**Impact:** Medium  
**Risk:** High  

### 10. Batch dequeue from level queues
**File:** `util/global.h`, `src/PMiner.h`  
**Impact:** Medium  
**Risk:** Medium  
