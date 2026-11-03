#pragma once
class AsyncDeleter
{
public:
    static void triggerDeleteProcess();
    static bool deleteQueueEmpty();
    static void destroyInstance();
};
