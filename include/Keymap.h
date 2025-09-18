#pragma once
#include "PlatformTypes.h"
#include <string>
#include <unordered_map>

extern const std::unordered_map<std::string, keycode> keyToCode;

inline keycode getKeycode(const std::string& name)
{
    auto it = keyToCode.find(name);
    if (it != keyToCode.end())
        return it->second;
    return 0;
}