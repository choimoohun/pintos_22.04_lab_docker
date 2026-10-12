#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#include <stdint.h>

/* 소수부 14비트 */
#define F (1 << 14)

/* 정수 -> 고정소수점 */
static int int_to_fp(int n) {
    return n * F;
}

/* 고정소수점 -> 정수: 버림 */
static int fp_to_int(int x) {
    return x / F;
}

/* 고정소수점 -> 정수: 반올림 */
static int fp_to_int_round(int x) {
    if (x >= 0)
        return (x + F / 2) / F;
    else
        return (x - F / 2) / F;
}

/* 고정소수점 + 고정소수점 */
static int fp_add(int x, int y) {
    return x + y;
}

/* 고정소수점 - 고정소수점 */
static int fp_sub(int x, int y) {
    return x - y;
}

/* 고정소수점 + 정수 */
static int fp_add_int(int x, int n) {
    return x + n * F;
}

/* 고정소수점 - 정수 */
static int fp_sub_int(int x, int n) {
    return x - n * F;
}

/* 고정소수점 * 고정소수점 */
static int fp_mul(int x, int y) {
    return (int64_t) x * y / F;
}

/* 고정소수점 * 정수 */
static int fp_mul_int(int x, int n) {
    return x * n;
}

/* 고정소수점 / 고정소수점 */
static int fp_div(int x, int y) {
    return (int64_t) x * F / y;
}

/* 고정소수점 / 정수 */
static int fp_div_int(int x, int n) {
    return x / n;
}

#endif /* THREADS_FIXED_POINT_H */