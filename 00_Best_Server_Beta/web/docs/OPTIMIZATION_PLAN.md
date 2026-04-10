# 性能优化实施计划

## 优先级排序

### P0 - 立即执行（关键瓶颈）
1. **无锁队列** - 替换所有 std::queue + mutex
2. **内存池** - 替换标准分配器
3. **批量 I/O** - recvmmsg/sendmmsg

### P1 - 短期执行（1-2周）
4. **CPU 亲和性** - per-cpu 数据结构
5. **缓存优化** - 缓存行对齐
6. **引用计数优化** - 侵入式引用计数

### P2 - 中期执行（2-4周）
7. **io_uring 集成** - 异步 I/O
8. **NUMA 感知** - NUMA 节点分配
9. **协程优化** - 对象池

## 实施细节

### 任务 1: 无锁队列（预计 2-3 天）

#### 目标
- 替换 `std::queue<std::function<void()>>` + `std::mutex`
- 实现 MPMC（多生产者多消费者）无锁队列
- 缓存行对齐，避免 false sharing

#### 文件
- `include/best_server/core/lockfree_queue.hpp` (新文件)
- `src/core/lockfree_queue.cpp` (新文件)
- `include/best_server/core/task_queue.hpp` (修改)
- `src/core/task_queue.cpp` (修改)

#### 技术方案
```cpp
// 使用 Dmitry Vyukov 的 MPMC 队列算法
template<typename T>
class MPMCQueue {
    static constexpr size_t CACHE_LINE_SIZE = 64;
    
    struct Node {
        std::atomic<T*> data;
        std::atomic<size_t> sequence;
    };
    
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> write_pos_;
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> read_pos_;
    Node* buffer_;
    size_t capacity_;
};
```

#### 预期收益
- 减少 90% 的锁竞争
- 提升 2-3x 任务调度吞吐量

---

### 任务 2: 内存池（预计 3-4 天）

#### 目标
- 实现 Slab 分配器
- 对象大小分级（8, 16, 32, 64, 128, 256, 512, 1024 bytes）
- 线程本地缓存

#### 文件
- `include/best_server/memory/slab_allocator.hpp` (新文件)
- `src/memory/slab_allocator.cpp` (新文件)
- `include/best_server/memory/object_pool.hpp` (修改)
- `src/memory/object_pool.cpp` (修改)

#### 技术方案
```cpp
class SlabAllocator {
    struct Slab {
        void* free_list;
        size_t used;
        size_t capacity;
        Slab* next;
    };
    
    // 不同大小对象的 slab
    std::array<Slab*, 8> slabs_;
    
    // 线程本地缓存
    thread_local void* cache_[32];
    thread_local size_t cache_size_;
};
```

#### 预期收益
- 减少 50% 内存分配时间
- 减少 30% 内存碎片
- 提升 1.5-2x 分配性能

---

### 任务 3: 批量 I/O（预计 2-3 天）

#### 目标
- 使用 recvmmsg/sendmmsg
- 批量处理网络 I/O
- 减少系统调用次数

#### 文件
- `include/best_server/io/batch_io.hpp` (新文件)
- `src/io/batch_io.cpp` (新文件)
- `src/io/tcp_socket.cpp` (修改)

#### 技术方案
```cpp
class BatchIO {
    static constexpr size_t MAX_BATCH = 64;
    
    struct mmsghdr msgvec[MAX_BATCH];
    struct iovec iov[MAX_BATCH];
    
public:
    size_t recv_batch(int fd, ZeroCopyBuffer** buffers);
    size_t send_batch(int fd, const ZeroCopyBuffer** buffers);
};
```

#### 预期收益
- 减少 80% 系统调用
- 提升 2-3x I/O 吞吐量
- 降低 40% CPU 使用率

---

### 任务 4: CPU 亲和性（预计 2-3 天）

#### 目标
- per-cpu 数据结构
- CPU 核心绑定
- 避免跨核访问

#### 文件
- `include/best_server/core/per_cpu.hpp` (新文件)
- `src/core/per_cpu.cpp` (新文件)
- `include/best_server/core/scheduler.hpp` (修改)
- `src/core/scheduler.cpp` (修改)

#### 技术方案
```cpp
template<typename T>
class PerCPU {
    static constexpr size_t MAX_CPUS = 128;
    alignas(64) T data_[MAX_CPUS];
    
public:
    T& for_current_cpu() {
        return data_[sched_getcpu()];
    }
    
    T& for_cpu(size_t cpu) {
        return data_[cpu];
    }
};
```

#### 预期收益
- 减少 60% 缓存未命中
- 提升 1.5-2x 多核性能
- 更好的可扩展性

---

### 任务 5: 缓存优化（预计 2-3 天）

#### 目标
- 缓存行对齐
- 数据结构紧凑化
- 热点数据集中

#### 文件
- 多个头文件修改（添加 alignas）
- 数据结构优化

#### 技术方案
```cpp
// 使用缓存行对齐
alignas(64) std::atomic<uint64_t> counter_;

// 紧凑数据结构
struct CompactTimer {
    uint64_t expire_time;
    uint64_t id;
    TimerCallback* callback;  // 指针代替 shared_ptr
};
```

#### 预期收益
- 减少 40% 缓存未命中
- 提升 1.2-1.5x 性能

---

### 任务 6: 引用计数优化（预计 3-4 天）

#### 目标
- 侵入式引用计数
- 优化原子操作顺序
- 使用 unique_ptr 代替 shared_ptr

#### 文件
- `include/best_server/memory/intrusive_ptr.hpp` (新文件)
- 多个文件修改（替换 shared_ptr）

#### 技术方案
```cpp
template<typename T>
class IntrusivePtr {
    T* ptr_;
    
public:
    void add_ref() const {
        if (ptr_) {
            ptr_->ref_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    
    void release() const {
        if (ptr_) {
            if (ptr_->ref_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                delete ptr_;
            }
        }
    }
};
```

#### 预期收益
- 减少 30% 引用计数开销
- 减少 20% 内存占用
- 提升 1.1-1.3x 性能

---

### 任务 7: io_uring 集成（预计 4-5 天）

#### 目标
- 集成 Linux io_uring
- 异步 I/O 支持
- 减少系统调用

#### 文件
- `include/best_server/io/io_uring.hpp` (新文件)
- `src/io/io_uring.cpp` (新文件)
- 修改多个 I/O 相关文件

#### 技术方案
```cpp
class IoUring {
    io_uring ring_;
    
public:
    void submit_read(int fd, void* buf, size_t len);
    void submit_write(int fd, const void* buf, size_t len);
    void process_completions();
};
```

#### 预期收益
- 减少 70% 系统调用
- 提升 2-4x I/O 性能
- 更低延迟

---

### 任务 8: NUMA 感知（预计 3-4 天）

#### 目标
- NUMA 节点分配
- 本地内存访问
- 跨节点访问优化

#### 文件
- `include/best_server/memory/numa_allocator.hpp` (新文件)
- `src/memory/numa_allocator.cpp` (新文件)

#### 技术方案
```cpp
class NUMAAllocator {
    size_t num_nodes_;
    
public:
    void* allocate(size_t size, size_t node);
    void free(void* ptr);
    
    size_t get_node(void* ptr);
};
```

#### 预期收益
- NUMA 系统上提升 2-3x 性能
- 减少跨节点访问延迟

---

### 任务 9: 协程优化（预计 3-4 天）

#### 目标
- 自定义协程分配器
- 协程对象池
- 小任务内联

#### 文件
- `include/best_server/future/coroutine_pool.hpp` (新文件)
- `src/future/coroutine_pool.cpp` (新文件)

#### 技术方案
```cpp
class CoroutinePool {
    struct Frame {
        std::coroutine_handle<> handle;
        Frame* next;
    };
    
    std::atomic<Frame*> free_list_;
    
public:
    void* allocate(size_t size);
    void deallocate(void* ptr);
};
```

#### 预期收益
- 减少 40% 协程开销
- 提升 1.3-1.5x 小任务性能

---

## 性能验证

### 基准测试
- 每个优化前后对比
- 使用 perf/cachegrind 分析
- 压力测试验证

### 性能目标
| 阶段 | 吞吐量提升 | 延迟降低 | CPU 降低 |
|------|-----------|---------|---------|
| Phase 1 | 3-4x | 50% | 40% |
| Phase 2 | 5-6x | 70% | 50% |
| Phase 3 | 8-10x | 80% | 60% |

---

## 风险与缓解

### 风险
1. 无锁算法复杂度高，容易出现 bug
2. NUMA 支持依赖硬件
3. io_uring 需要 Linux 5.1+

### 缓解
1. 充分测试，使用压力测试
2. 提供编译选项，可选启用
3. 兼容旧内核，回退到 epoll

---

## 时间估算

- **Phase 1 (P0)**: 1 周
- **Phase 2 (P1)**: 1.5 周
- **Phase 3 (P2)**: 2 周

**总计**: 4.5 周

---

## 下一步

立即开始 P0 任务：
1. 创建无锁队列实现
2. 创建 Slab 分配器
3. 实现批量 I/O