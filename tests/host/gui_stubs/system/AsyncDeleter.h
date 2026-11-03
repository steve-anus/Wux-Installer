#ifndef HOST_STUB_ASYNC_DELETER_H
#define HOST_STUB_ASYNC_DELETER_H

class AsyncDeleter {
public:
    class Element {
    public:
        Element() {}
        virtual ~Element() {}
    };
    static void pushForDelete(Element* e) { delete e; }
};

#endif
