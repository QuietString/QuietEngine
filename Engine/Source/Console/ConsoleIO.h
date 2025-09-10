#pragma once
#include <ostream>

namespace ConsoleIO
{
    void InstallDirtyCout();      // call once at startup
    bool HasPendingCout();        // true if cout saw any write since last flush
    void FlushCoutIfDirty();      // flush only when pending
}
