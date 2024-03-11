#pragma once
#include <cmath>

//Returns true if l/r are within +/-v of each other
bool within(int l, int r, int v)
{
    return std::abs(l - r) <= v;
}

//Used to represent screen/window sizes
struct rect
{
    static constexpr int comparisonLeeway = 3;

    int left, top, width, height;

    bool operator==(const rect&) const = default;

    //Returns true if the rects are nearly the same, within comparisonLeeway units
    bool approximately(const rect& other) const
    {
        return
            within(left, other.left, comparisonLeeway) &&
            within(top, other.top, comparisonLeeway) &&
            within(width, other.width, comparisonLeeway) &&
            within(height, other.height, comparisonLeeway);
    }
};
