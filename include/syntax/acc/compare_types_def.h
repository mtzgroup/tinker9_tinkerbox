#pragma once
#include "macro.h"
#include <type_traits>


namespace tinker {
template <class T, class U>
constexpr bool eq()
{
   return std::is_same<T, U>::value;
}
}
