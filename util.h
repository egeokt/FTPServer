/* util.h
 * Handles data sending over a socket.
 * Author  : Jonatan Schroeder
 * Modified: Nov 5, 2017
 */

#ifndef _UTIL_H_
#define _UTIL_H_

#include <stdio.h>

int send_all(int fd, char buf[], size_t size);

// The attribute in this function allows gcc to provided useful
// warnings when compiling the code.
int send_string(int fd, const char *str, ...)
  __attribute__ ((format(printf, 2, 3)));

#endif
