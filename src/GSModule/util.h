/*
 * Arduino library for Gainspan Wifi2Serial modules
 *
 * Copyright (C) 2014 Matthijs Kooijman <matthijs@stdin.nl>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

// This file defines some utility macros

#ifndef GS_UTIL_H
#define GS_UTIL_H

#include <limits.h>

#define lengthof(x) (sizeof(x) / sizeof(*x))

// Macros to find the min and max value of a type. Based on macros in
// the "limits" file in gcc / libstdc++.
#define is_type_signed(T)     ((T)(-1) < 0)

#define min_for_type(T) \
  (is_type_signed (T) ? -max_for_type (T) - 1 : (T)0)

#define max_for_type(T) \
  (is_type_signed (T) ? \
  (((((T)1 << (value_bits_for_type (T) - 1)) - 1) << 1) + 1) : \
  (T)(~(T)0))

#define value_bits_for_type(T) \
  (sizeof(T) * __CHAR_BIT__ - is_type_signed (T))

#define is_power_of_two(v) (v && ((v & (v-1)) == 0))

#endif // GS_UTIL_H

// vim: set sw=2 sts=2 expandtab:
