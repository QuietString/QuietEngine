#include "ConsoleIO.h"
#include "DirtyStreamBuffer.h"
#include <iostream>
#include <mutex>

namespace
{
    DirtyStreamBuffer* DirtyBuf = nullptr;
    std::once_flag InstallOnce;
}

namespace ConsoleIO
{
    void InstallDirtyCout()
    {
        std::call_once(InstallOnce, []{
            static DirtyStreamBuffer Buf(std::cout.rdbuf());
            std::cout.rdbuf(&Buf);
            DirtyBuf = &Buf;
        });
    }

    bool HasPendingCout()
    {
        return DirtyBuf && DirtyBuf->has_pending();
    }

    void FlushCoutIfDirty()
    {
        if (HasPendingCout())
        {
            std::cout.flush();   
        }
    }
}
