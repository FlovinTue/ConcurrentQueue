# concurrent_queue

The Queue preserves the FIFO property within the 
context of single producers. push is wait-free(as long as no allocation needs to be made), 
try_pop & size are lock-free and producer capacities grows dynamically.

Features basic exception safety (May be turned off for a slight performance increase).
Just include concurrent_queue_.h (optionally concurrent_queue.natvis) and go :) 
