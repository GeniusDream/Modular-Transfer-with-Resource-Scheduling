//
// Created by genius_dream on 22年12月1日.
//

#ifndef TEST_GZCOMPRESS_H
#define TEST_GZCOMPRESS_H

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <stddef.h>
#include <unistd.h>
int gzcompress(void *data, size_t ndata, void *zdata, size_t *nzdata);
int gzdecompress(void *zdata, size_t nzdata, void *data, size_t *ndata);

#endif //TEST_GZCOMPRESS_H
