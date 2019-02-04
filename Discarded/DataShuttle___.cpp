#include "stdafx.h"
#include "DataShuttle.h"


std::atomic<uint32_t> DataShuttleInternals::Consumer::ourStartBufferIterator(0);
