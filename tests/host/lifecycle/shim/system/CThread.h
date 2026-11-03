#pragma once
#include <thread>
class CThread
{
public:
    using Callback = void (*)(CThread *, void *);
    CThread(int attributes, int priority = 16, int stackSize = 0x8000,
            Callback callback = nullptr, void *callbackArg = nullptr);
    virtual ~CThread();
    virtual void resumeThread();
    virtual void shutdownThread();
    virtual void executeThread();
    enum { eAttributeAffCore0 = 0x01, eAttributePinnedAff = 0x10 };
private:
    std::thread worker;
    Callback callback;
    void *callbackArg;
};
