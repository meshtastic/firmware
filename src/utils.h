#pragma once

/// C++ v17+ clamp function, limits a given value to a range defined by lo and hi
template<class T>
constexpr const T& clamp( const T& v, const T& lo, const T& hi ) {
    return (v < lo) ? lo : (hi < v) ? hi : v;
}