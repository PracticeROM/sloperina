#ifndef OOTGZ_MEMFILEFIELDS_H
#define OOTGZ_MEMFILEFIELDS_H

#include <stdint.h>

typedef struct {
  uint32_t hash;
  const char *name;
} memfilefield_t;

extern const memfilefield_t memfilefields[];
extern const int memfilefields_count;

#endif
