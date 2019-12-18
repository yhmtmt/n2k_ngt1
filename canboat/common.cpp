/*

(C) 2009-2015, Kees Verruijt, Harlingen, The Netherlands.

This file is part of CANboat.

CANboat is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

CANboat is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with CANboat.  If not, see <http://www.gnu.org/licenses/>.

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/timeb.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>

#include "common.h"

#ifdef _WIN32
#define snprintf _snprintf
#endif

static const char * logLevels[] =
{ "FATAL"
, "ERROR"
, "INFO"
, "DEBUG"
};

static LogLevel logLevel = LOGLEVEL_INFO;

static char * progName;

#ifndef _WIN32

const char * now(char str[DATE_LENGTH])
{
  struct timeval tv;
  time_t t;
  struct tm tm;
  int msec;
  size_t len;

  if (gettimeofday(&tv, NULL) == 0)
  {
    t = tv.tv_sec;
    msec = tv.tv_usec / 1000L;
    gmtime_r(&t, &tm);
    strftime(str, DATE_LENGTH - 5, "%Y-%m-%dT%H:%M:%S", &tm);
    len = strlen(str);
    snprintf(str + len, DATE_LENGTH - len, ".%3.3dZ", msec);
  }
  else
  {
    strcpy(str, "?");
  }

  return (const char *) str;
}

#else

const char * now(char str[DATE_LENGTH])
{
  struct _timeb timebuffer;
  struct tm tm;
  size_t len;

  _ftime_s(&timebuffer);
  gmtime_s(&tm, &timebuffer.time);
  strftime(str, DATE_LENGTH - 5, "%Y-%m-%dT%H:%M:%S", &tm);
  len = strlen(str);
  snprintf(str + len, DATE_LENGTH - len, ".%3.3dZ", timebuffer.millitm);

  return (const char *) str;
}

#endif

static int logBase(LogLevel level, const char * format, va_list ap)
{
  char strTmp[DATE_LENGTH];

  if (level > logLevel)
  {
    return 0;
  }

  fprintf(stderr, "%s %s [%s] ", logLevels[level], now(strTmp), progName);

  return vfprintf(stderr, format, ap);
}

int logInfo(const char * format, ...)
{
  va_list ap;
  va_start(ap, format);

  return logBase(LOGLEVEL_INFO, format, ap);
}

int logDebug(const char * format, ...)
{
  va_list ap;
  va_start(ap, format);

  return logBase(LOGLEVEL_DEBUG, format, ap);
}

int logError(const char * format, ...)
{
  va_list ap;
  va_start(ap, format);

  return logBase(LOGLEVEL_ERROR, format, ap);
}

void logAbort(const char * format, ...)
{
  va_list ap;
  va_start(ap, format);

  logBase(LOGLEVEL_FATAL, format, ap);
  exit(2);
}

void die(const char * t)
{
  int e = errno;
  char * s = 0;

  if (e)
  {
    s = strerror(e);
  }

  if (s)
  {
    logAbort("%s: %s\n", t, s);
  }
  else
  {
    logAbort("%s\n", t);
  }
}

void setLogLevel(LogLevel level)
{
  logLevel = CB_MIN(CB_MAX(level, LOGLEVEL_FATAL), LOGLEVEL_DEBUG);
  logDebug("Loglevel now %d\n", logLevel);
}

void setProgName(char * name)
{
  progName = strrchr(name, '/');
  if (!progName)
  {
    progName = strrchr(name, '\\');
  }
  if (!progName)
  {
    progName = name;
  }
  else
  {
    progName++;
  }
}

void sbAppendData(StringBuffer * sb, const void * data, size_t len)
{
  while (len + sb->len + 1 >= sb->alloc)
  {
    if (!sb->alloc)
    {
      sb->alloc = 64;
    }
    else
    {
      sb->alloc *= 2;
    }
  }
  if (!sb->data)
  {
    sb->data = (char*)malloc(sb->alloc);
  }
  else
  {
    sb->data = (char*)realloc(sb->data, sb->alloc);
  }
  if (!sb->data)
  {
    die("Out of memory");
  }
  memcpy(sb->data + sb->len, data, len);
  sb->len += len;
  sb->data[sb->len] = 0;
  logDebug("Appended %u bytes to %p len %u\n", len, sb->data, sb->len);
  //logDebug("+ [%1.*s]\n", len, data);
  //logDebug("= [%1.*s]\n", sb->len, sb->data);
}

void sbAppendString(StringBuffer * sb, const char * string)
{
  size_t len = strlen(string);
  sbAppendData(sb, string, len);
}

static void sbReserve(StringBuffer *const sb, size_t len)
{
  size_t nextSize;

  if (len < sb->len)
  {
    len = sb->len;
  }

  /* Double the allocation until it is large enough */
  /* Note that we reserve len + 1 bytes (== len + zero terminator) */
  for (nextSize = 32; nextSize < len + 1; nextSize = nextSize * 2)
    ;

  if (sb->data)
  {
    sb->data = (char*)realloc(sb->data, nextSize);
  }
  else
  {
    sb->data = (char*)malloc(nextSize);
  }

  if (!sb->data)
  {
    logAbort("Out of memory in allocating string buffer\n");
  }
  sbTerminate(sb);
  sb->alloc = nextSize;
}

static void sbEnsureCapacity(StringBuffer *const sb, size_t len)
{
  len++; // Make room for termination zero byte
  if (!sb->data || (len >= sb->alloc))
  {
    sbReserve(sb, len);
  }
}


void sbAppendFormatV(StringBuffer *const sb, const char *const format, va_list ap)
{
  int n;
  size_t len;
  va_list ap2;

  len = 128;

  while (len < 4 * 1024 * 1024)
  {
    sbEnsureCapacity(sb, sb->len + len);

    va_copy(ap2, ap);
    n = vsnprintf(sb->data + sb->len, len, format, ap2);
    va_end(ap2);
    /*
     * Platform returned not an error, and the number of bytes was less
     * than the size of the buffer -> we're done
     * Note that some implementations (tru64) nicely guarantee zero terminated strings,
     * so we test for len - 1, not len.
     */
    if (n > -1 && (size_t) n < len - 1)
    {
      sb->len += n;
      sbTerminate(sb);
      break;
    }

    /*
     * Otherwise, increase the buffer smartly
     */

    if (n == -1)
    {
      len *= 2;
    }
    else
    {
      len = n + 2;
    }
  }
}

void sbAppendFormat(StringBuffer *const sb, const char *const format, ...)
{
  va_list ap;

  va_start(ap, format);
  sbAppendFormatV(sb, format, ap);
  va_end(ap);
}

/*
 * Retrieve a value out of a JSON styled message.
 */
int getJSONValue( const char * message, const char * fieldName, char * value, size_t len )
{
  const char * loc = message + 1;
  size_t fieldLen = strlen(fieldName);

  for (;;)
  {
    loc = strstr(loc, fieldName);
    if (!loc)
    {
      return 0;
    }
    if (loc[-1] == '"' && loc[fieldLen] == '"' && loc[fieldLen + 1] == ':')
    {
      break;
    }
    loc += fieldLen;
  }

  /* field has been found */
  loc += fieldLen + 2;

  while (isspace(*loc))
  {
    loc++;
  }

  if (*loc != '"')
  {
    while ((isdigit(*loc) || *loc == '.' || *loc == '-' || *loc == 'E' || *loc == 'e' || *loc == '+') && len > 1)
    {
      *value++ = *loc++;
      len--;
    }
    *value = 0;
    return 1;
  }

  /* field is string */

  loc++;

  while (len > 1)
  {
    if (*loc == '\\')
    {
      loc++;
      switch (*loc)
      {
      case 'b':
        *value++ = '\b';
        loc++;
        break;
      case 'f':
        *value++ = '\f';
        loc++;
        break;
      case 'n':
        *value++ = '\n';
        loc++;
        break;
      case 'r':
        *value++ = '\r';
        loc++;
        break;
      case 't':
        *value++ = '\t';
        loc++;
        break;
      case 'u':
        {
          unsigned int n;
          sscanf(loc, "%4x", &n);
          loc += 4;
          *value++ = n & 255; // We're single byte, forget about the high byte...
        }
        break;

      default:
        *value++ = *loc++;
      }
    }
    else if (*loc == '"')
    {
      break;
    }
    else
    {
      *value++ = *loc++;
    }
    len--;
  }
  *value = 0;
  return 1;
}


/*
 * Table 1 - Mapping of ISO 11783 into CAN's Arbitration and Control Fields
29 Bit Identifiers
CAN   ISO 11783 Bit Number
SOF     SOF*      1
ID 28   P 3       2
ID 27   P 2       3
ID 26   P 1       4
ID 23   R 1       5
ID 24   DP        6
ID 23   PF 8      7
ID 22   PF 7      8
ID 21   PF 6      9
ID 20   PF 5     10
ID 19   PF 4     11
ID 18   PF 3     12
SRR (r) SRR*     13
IDE (r) IDE*     14
ID 17   PF 2     15
ID 16   PF 1     16
ID 13   PS 8     17
ID 14   PS 7     18
ID 13   PS 6     19
ID 12   PS 5     20
ID 11   PS 4     21
ID 10   PS 3     22
ID 9    PS 2     23
ID 8    PS 1     24
ID 7    SA 8     25
ID 6    SA 7     26
ID 3    SA 6     27
ID 4    SA 5     28
ID 3    SA 4     29
ID 2    SA 3     30
ID 1    SA 2     31
ID 0    SA 1     32
RTR (x) RTR*     33
r 1     r 1*     34
r 0     r 0*     35
DLC 4   DLC 4    36
DLC 3   DLC 3    37
DLC 2   DLC 2    38
DLC 1   DLC 1    39
Notes:
SOF - Start of Frame Bit P# - ISO 11783 Priority Bit #n
ID## - Identifier Bit #n R# - ISO 11783 Reserved Bit #n
SRR - Substitute Remote Request SA# - ISO 11783 Source Address Bit #n
RTR - Remote Transmission Request Bit DP - ISO 11783 Data Page
IDE - Identifier Extension Bit PF# - ISO 11783 PDU Format Bit #n
r# - CAN Reserved Bit #n PS# - ISO 11783 PDU Specific Bit #n
DLC# - Data Length Code Bit #n *CAN Defined Bit, Unchanged in ISO 11783
(d) - dominant bit 1 Required format of proprietary 11 bit identifiers
(r) - recessive bit
*/

void getISO11783BitsFromCanId(unsigned int id, unsigned int * prio, unsigned int * pgn, unsigned int * src, unsigned int * dst)
{
  unsigned char PF = (unsigned char) (id >> 16);
  unsigned char PS = (unsigned char) (id >> 8);
  unsigned char DP = (unsigned char) (id >> 24) & 1;

  if (src)
  {
    *src = (unsigned char) id >> 0;
  }
  if (prio)
  {
    *prio = (unsigned char) ((id >> 26) & 0x7);
  }

  if (PF < 240)
  {
    /* PDU1 format, the PS contains the destination address */
    if (dst)
    {
      *dst = PS;
    }
    if (pgn)
    {
      *pgn = (DP << 16) + (PF << 8);
    }
  }
  else
  {
    /* PDU2 format, the destination is implied global and the PGN is extended */
    if (dst)
    {
      *dst = 0xff;
    }
    if (pgn)
    {
      *pgn = (DP << 16) + (PF << 8) + PS;
    }
  }

}

/*
  This does the opposite from getISO11783BitsFromCanId: given n2k fields produces the extended frame CAN id
*/
unsigned int getCanIdFromISO11783Bits(unsigned int prio, unsigned int pgn, unsigned int src, unsigned int dst)
{
  unsigned int canId = src | 0x80000000U;  // src bits are the lowest ones of the CAN ID. Also set the highest bit to 1 as n2k uses only extended frames (EFF bit).

  if((unsigned char)pgn == 0) {  // PDU 1 (assumed if 8 lowest bits of the PGN are 0)
    canId += dst << 8;
    canId += pgn << 8;
    canId += prio << 26;
  } else {                       // PDU 2
    canId += pgn << 8;
    canId += prio << 26;
  }

  return canId;
}

static void resolve_address(const char * url, char ** host, const char ** service)
{
  const char *s;
  size_t hostlen;

  if (strncmp(url, "tcp:", STRSIZE("tcp:")) == 0)
  {
    url += STRSIZE("tcp:");
  }
  while (*url == '/')
  {
    url++;
  }

  s = strchr(url, ':');
  if (s)
  {
    hostlen = s - url;
    s++;
  }
  else
  {
    hostlen = strlen(url);
  }

  *host = (char*)malloc(hostlen + 1);
  if (!*host)
  {
    die("Out of memory");
  }
  memcpy(*host, url, hostlen);
  (*host)[hostlen] = 0;

  if (s)
  {
    *service = s;
  }
  else
  {
    *service = "80";
  }
}

/*
SOCKET open_socket_stream(const char * url)
{
  int sockfd = INVALID_SOCKET;
  int n;
  struct addrinfo hints, *res, *addr;
  char * host;
  const char * service;

  resolve_address(url, &host, &service);

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  n = getaddrinfo(host, service, &hints, &res);
  if (n != 0)
  {
    logError("Unable to open connection to %s:%s: %s\n", host, service, gai_strerror(n));
  }
  else
  {
    for (addr = res; addr; addr = addr->ai_next)
    {
      sockfd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
      if (sockfd == INVALID_SOCKET)
      {
        continue;
      }

      if (!connect(sockfd, res->ai_addr, res->ai_addrlen))
      {
        break;
      }
      close(sockfd);
      sockfd = INVALID_SOCKET;
    }
    if (!addr)
    {
      logError("Unable to open connection to %s:%s: %s\n", host, service, strerror(errno));
    }
  }

  freeaddrinfo(res);
  free(host);

  return sockfd;
}
*/


uint8_t scanNibble(char c)
{
  if (isdigit(c))
  {
    return c - '0';
  }
  if (c >= 'A' && c <= 'F')
  {
    return c - 'A' + 10;
  }
  if (c >= 'a' && c <= 'f')
  {
    return c - 'a' + 10;
  }
  return 16;
}

int scanHex(char ** p, uint8_t * m)
{
  uint8_t hi, lo;

  if (!(*p)[0] || !(*p)[1])
  {
    return 1;
  }

  hi = scanNibble((*p)[0]);
  if (hi > 15)
  {
    return 1;
  }
  lo = scanNibble((*p)[1]);
  if (lo > 15)
  {
    return 1;
  }
  (*p) += 2;
  *m = hi << 4 | lo;
  /* printf("(b=%02X,p=%p) ", *m, *p); */
  return 0;
}
