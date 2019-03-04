# ConcurrentQueue

The WizardLoaf ConcurrentQueue 
Made for the x86/x64 architecture in Visual Studio 2017, focusing
on performance. The Queue preserves the FIFO property within the 
context of single producers. Push operations are wait-free, TryPop & Size 
are lock-free and producer capacities grows dynamically

Features basic exception safety (May be turned off for a slight performance increase)
