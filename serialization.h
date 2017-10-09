

#include <stdio.h>
#include <ctype.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>

#ifndef SERIALIZATION_H
#define SERIALIZATION_H

#define pack754_32(f) (pack754((f), 32, 8))
#define pack754_64(f) (pack754((f), 64, 11))
#define unpack754_32(i) (unpack754((i), 32, 8))
#define unpack754_64(i) (unpack754((i), 64, 11))

unsigned int pack(unsigned char *buf, char *format, ...);
void unpack(unsigned char *buf, char *format, ...);

#endif // SERIALIZATION_H
