#include "stdafx.h"
#include "DataShuttle.h"


std::atomic<int32_t> DataShuttleInternals::Consumer::ourStartBufferIterator(0);
