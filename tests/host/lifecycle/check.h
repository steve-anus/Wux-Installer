#pragma once
#include "model.h"
#include <algorithm>
#include <initializer_list>
#include <iostream>
#include <string>
#include <vector>

namespace host {
using Events = std::vector<std::string>;

inline std::size_t count(const Events &events, const std::string &event)
{
    return static_cast<std::size_t>(std::count(events.begin(), events.end(), event));
}

inline bool contains(const Events &events, const std::string &text)
{
    return std::any_of(events.begin(), events.end(), [&](const std::string &event) {
        return event.find(text) != std::string::npos;
    });
}

inline Events select(const Events &events, std::initializer_list<std::string> names)
{
    Events result;
    for(const auto &event : events)
        if(std::find(names.begin(), names.end(), event) != names.end())
            result.push_back(event);
    return result;
}

inline std::string describe(const Events &events)
{
    std::string result = "[";
    for(const auto &event : events)
    {
        if(result.size() > 1) result += ", ";
        result += event;
    }
    return result + "]";
}

class Check
{
public:
    void require(bool condition, const std::string &reason)
    {
        if(!condition) reasons.push_back(reason);
    }

    void equal(const Events &actual, const Events &expected, const std::string &reason)
    {
        require(actual == expected, reason + ": expected " + describe(expected) +
                ", got " + describe(actual));
    }

    bool report(const std::string &name) const
    {
        std::cout << name << (reasons.empty() ? " PASS\n" : " FAIL\n");
        for(const auto &reason : reasons) std::cout << "  " << reason << '\n';
        std::cout.flush();
        return reasons.empty();
    }
private:
    Events reasons;
};
}
