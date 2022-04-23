
// ctype.h
//
// Characters.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _CTYPE_H_
#define _CTYPE_H_

#include "stdint.h"

int32_t isalnum(int32_t c);
int32_t isalpha(int32_t c);
int32_t isdigit(int32_t c);
int32_t islower(int32_t c);
int32_t isprint(int32_t c);
int32_t isgraph(int32_t c);
int32_t iscntrl(int32_t c);
int32_t isgraph(int32_t c);
int32_t ispunct(int32_t c);
int32_t isspace(int32_t c);
int32_t isupper(int32_t c);
int32_t isxdigit(int32_t c);
int32_t isascii(int32_t c);
int32_t tolower(int32_t c);
int32_t toupper(int32_t c);

#endif /* _CTYPE_H_ */
