# concurrent_queue

The queue preserves the FIFO property within the 
context of single producers. push is wait-free(assuming a wait-free allocator), 
try_pop & size are lock-free and producer capacities grows dynamically.

Features optional basic exception safety at the cost of a slight performance decrease.


Includes needed are concurrent_queue.h and atomic_shared_ptr.h @ https://github.com/FlovinTue/atomic_shared_ptr 
concurrent_queue.natvis and atomic_shared_ptr.natvis may be included for additional debug information in Visual Studio
