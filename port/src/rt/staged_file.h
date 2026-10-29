#pragma once
#include <stddef.h>

// Serial writer: never removes the destination before replacement. A failed
// write/flush/close/rename leaves the previous destination in place. The stage
// is not a save-loader candidate. Physical power-loss durability is not implied.
int rt_staged_file_write(const char *path, const void *data, size_t size);
