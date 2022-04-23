
// math.h
//
// Math functions.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _MATH_H_
#define _MATH_H_

#include "stdint.h"

#define M_PI 3.1415926

double floor(double x);
int32_t abs(int32_t j);
double pow(double x, double y);
double exp(double x);
double fmod(double x, double y);
double sqrt(double x);
float sqrtf(float x);
double fabs(double x);
float fabsf(float x);
double sin(double x);
double cos(double x);
double frexp(double x, int *exp);

#define HUGE_VAL (__builtin_huge_val())

double acos(double x);
double asin(double x);
double atan2(double y, double x);
double ceil(double x);
double cosh(double x);
double ldexp(double a, int exp);
double log(double x);
double log10(double x);
double log2(double x);
double sinh(double x);
double tan(double x);
double tanh(double x);
double atan(double x);
double modf(double x, double *iptr);
double hypot(double x, double y);

#endif /* _MATH_H_ */
