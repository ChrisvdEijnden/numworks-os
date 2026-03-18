#pragma once
#ifndef _SYS_STAT_H
#define _SYS_STAT_H
#include "../stddef.h"
#define S_IFMT   0170000
#define S_IFCHR  0020000
#define S_IFREG  0100000
#define S_IFDIR  0040000
#define S_IRUSR  0000400
#define S_IWUSR  0000200
struct stat {
    unsigned int st_mode;
    unsigned int st_size;
};
#endif
