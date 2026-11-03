#pragma once
class WuxExtractThread
{
public:
    WuxExtractThread();
    virtual ~WuxExtractThread();
    bool isThreadTerminated() const;
    void shutdownThread();
};
