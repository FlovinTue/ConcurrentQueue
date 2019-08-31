# concurrent_queue

The Queue preserves the FIFO property within the 
context of single producers. push is wait-free(except for actual memory allocations), 
try_pop & size are lock-free and producer capacities grows dynamically.

Features optional basic exception safety at the cost of a slight performance decrease.
