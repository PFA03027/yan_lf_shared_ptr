# yan_lf_shared_ptr
yet another lock-free shared pointer and lock-free queue based on sticky reference counter

This is based wait-free sticky counter that Daniel Anderson introduced at CppCon 2024.

https://www.youtube.com/watch?v=kPh8pod0-gk


### Remark
If the platform is x86, this algorithm may be wait-free truely. Because x86 has the instruction lock xadd.
If the platform is ARM64, this algorithm is lock-free, but not wait-free. Because ARM64 maybe be used CAS loop by linked-store for fetchadd/fetchsub.
Caution: above check is depended on compiler type and/or compiler version. please check by yourself.


## rc_sticky_counter.hpp
wait-free stickey counter implemention based on wait-free sticky counter algorithm that Daniel Anderson introduced at CppCon 2024.

## yan_lf_shared_ptr.hpp
subset implementation of shared_ptr that based on wait-free stickey counter in rc_sticky_counter.hpp

### Remark
weak_ptr is not implemented yet.

## yan_lf_queue.hpp
lock-free queue that support Allocator.

### Remark
* Although class `yan::rc_lf_queue` itself is implemented with lock-free algorithm, if Allocator is not lock-free( ex. std::allocator\<T\> ), `yan::rc_lf_queue` is not lock-free.
* To avoid ABA problem, `yan::rc_lf_queue` uses the reference counter(wait-free sticky counter) based approach.

### typed_lfheap.hpp
class `lfheap::typed_pool_heap<T>` is sample implementation of lock-free Allocator that allocates the T's memory from the fixed size memory pool.

### Remark
class `lfheap::typed_pool_heap<T>` does not support array allocation. If you call allocate() with more than 1, class `lfheap::typed_pool_heap<T>` throws std::bad_alloc exception.

## Installation
CMake install is supported. Please see install target in Makefile
