/* util.c
 * Some function to handle data sending.
 * Author  : Jonatan Schroeder, Ege Okten
 * Modified: Mar 27, 2019
 *
 * Notes: This code is adapted from Beej's Guide to Network
 * Programming (http://beej.us/guide/bgnet/), in particular the code
 * available in functions sigchld_handler, get_in_addr and
 * send_all.
 */

#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <stdarg.h>
#include <signal.h>
#include <assert.h>

/* Fixes a problem in OSX that it does not define MSG_NOSIGNAL */
#ifndef MSG_NOSIGNAL 
#define MSG_NOSIGNAL 0x2000 /* don't raise SIGPIPE */
#endif

#define BACKLOG 10     // how many pending connections queue will hold

/** Signal handler used to destroy zombie children (forked) processes
 *  once they finish executing.
 */
static void sigchld_handler(int s) {

  // waitpid() might overwrite errno, so we save and restore it:
  int saved_errno = errno;
  while(waitpid(-1, NULL, WNOHANG) > 0);
  errno = saved_errno;
}

/** Returns the IPv4 or IPv6 object for a socket address, depending on
 *  the family specified in that address.
 */
static void *get_in_addr(struct sockaddr *sa) {
  if (sa->sa_family == AF_INET)
    return &(((struct sockaddr_in*)sa)->sin_addr);
  else
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}



/** Sends a buffer of data, until all data is sent or an error is
 *  received. This function is used to handle cases where send is able
 *  to send only part of the data. If this is the case, this function
 *  will call send again with the remainder of the data, untill all
 *  data is sent.
 *
 *  Data is sent using the MSG_NOSIGNAL flag, so that, if the
 *  connection is interrupted, instead of a PIPE signal that crashes
 *  the program, this function will be able to return an error that
 *  can be handled by the caller.
 *
 *  Parameters: fd: Socket file descriptor.
 *              buf: Buffer where data to be sent is stored.
 *              size: Number of bytes to be used in the buffer.
 *
 *  Returns: If the buffer was successfully sent, returns
 *           size. Otherwise, returns -1.
 */
int send_all(int fd, char buf[], size_t size) {
  
  size_t rem = size;
  while (rem > 0) {
    int rv = send(fd, buf, rem, MSG_NOSIGNAL);
    // If there was an error, interrupt sending and returns an error
    if (rv <= 0)
      return rv;
    buf += rv;
    rem -= rv;
  }
  return size;
}

/** Sends a potentially-formatted string to a socket descriptor. The
 *  string can contain format directives (e.g., %d, %s, %u), which
 *  will be translated using a printf-like behaviour. For example, you
 *  may call it like:
 *
 *  send_string(fd, "+OK Server ready\r\n");
 *  send_string(fd, "+OK %d messages found\r\n", msg_count);
 *  
 *  Parameters: fd: Socket file descriptor.
 *              str: String to be sent, including potential
 *                   printf-like format directives.
 *              additional parameters based on string format.
 *
 *  Returns: If the string was successfully sent, returns
 *           the number of bytes sent. Otherwise, returns -1.
 */
int send_string(int fd, const char *str, ...) {
  
  static char *buf = NULL;
  static int bufsize = 0;
  va_list args;
  int strsize;
  
  // Start with string length, increase later if needed
  if (bufsize < strlen(str) + 1) {
    bufsize = strlen(str) + 1;
    buf = realloc(buf, bufsize);
  }
  
  while (1) {
    
    va_start(args, str);
    strsize = vsnprintf(buf, bufsize, str, args);
    va_end(args);
    
    if (strsize < 0)
      return -1;
    
    // If buffer was enough to fit entire string, send it
    if (strsize <= bufsize)
      return send_all(fd, buf, strsize);
    
    // Try again with more space
    bufsize = strsize + 1;
    buf = realloc(buf, bufsize);
  }
}

