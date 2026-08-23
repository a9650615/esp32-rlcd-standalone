/*
 * AirConnect: Chromecast & UPnP to AirPlay
 *
 * (c) Philippe 2016-2017, philippe_44@outlook.com
 *
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 *
 */

#include "platform.h"
#include "esp_netif.h"
#include <ctype.h>
#include <stdarg.h>
#include "pthread.h"
#include <esp_heap_caps.h>

#include "util.h"
#include "log_util.h"

/*----------------------------------------------------------------------------*/
/* globals */
/*----------------------------------------------------------------------------*/

extern log_level	util_loglevel;

/*----------------------------------------------------------------------------*/
/* locals */
/*----------------------------------------------------------------------------*/

static char *ltrim(char *s);
static int read_line(int fd, char *line, int maxlen, int timeout);

/*----------------------------------------------------------------------------*/
/* 																			  */
/* NETWORKING utils														  */
/* 																			  */
/*----------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------*/
// AirPlay artwork is the only body that is ever large. Everything else - DMAP
// metadata, progress, volume - is a few hundred bytes, so the split is between
// "a message" and "an image" rather than an arbitrary size.
//
// kMaxBodyBytes is 512 KB because nobody has yet measured what an artwork body
// actually is: util.c used to zero the length before reporting it, so the size
// has never appeared in a log. The LOG_INFO above prints it now, and this
// number should be revisited once there is a real figure rather than left at a
// guess that happens to work.
#define kSmallBodyBytes  8192
#define kMaxBodyBytes    (512 * 1024)

#define MAX_INTERFACES 256
#define DEFAULT_INTERFACE 1
#define INVALID_SOCKET (-1)

in_addr_t get_localhost(char **name)
{
	esp_netif_ip_info_t ipInfo = { };
	esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");

	// If STA not available, try AP
	if (!netif) {
		netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
	}

	if (!netif) {
		return INADDR_ANY;
	}

	esp_netif_get_ip_info(netif, &ipInfo);

	// get hostname if required
	if (name) {
		const char *hostname;
		if (esp_netif_get_hostname(netif, &hostname) == ESP_OK) {
			*name = strdup(hostname);
		} else {
			*name = strdup("esp-airsync");
		}
	}

	return ipInfo.ip.addr;
}

/*----------------------------------------------------------------------------*/
int shutdown_socket(int sd)
{
	if (sd <= 0) return -1;

	shutdown(sd, SHUT_RDWR);

	LOG_DEBUG("closed socket %d", sd);

	return closesocket(sd);
}


/*----------------------------------------------------------------------------*/
int bind_socket(unsigned short *port, int mode)
{
	int sock;
	socklen_t len = sizeof(struct sockaddr);
	struct sockaddr_in addr;

	if ((sock = socket(AF_INET, mode, 0)) < 0) {
		LOG_ERROR("cannot create socket %d", sock);
		return sock;
	}

	/*  Populate socket address structure  */
	memset(&addr, 0, sizeof(addr));
	addr.sin_family      = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port        = htons(*port);

	if (bind(sock, (struct sockaddr*) &addr, sizeof(addr)) < 0) {
		closesocket(sock);
		LOG_ERROR("cannot bind socket %d", sock);
		return -1;
	}

	if (!*port) {
		getsockname(sock, (struct sockaddr *) &addr, &len);
		*port = ntohs(addr.sin_port);
	}

	LOG_DEBUG("socket binding %d on port %d", sock, *port);

	return sock;
}


/*----------------------------------------------------------------------------*/
int conn_socket(unsigned short port)
{
	struct sockaddr_in addr;
	int sd;

	sd = socket(AF_INET, SOCK_STREAM, 0);

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons(port);

	if (sd < 0 || connect(sd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		close(sd);
		return -1;
	}

	LOG_DEBUG("created socket %d", sd);

	return sd;
}

/*----------------------------------------------------------------------------*/
/* 																			  */
/* STDLIB extensions													 	  */
/* 																			  */
/*----------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------*/
char *strlwr(char *str) {
	char *p = str;
	while (*p) {
		*p = tolower((unsigned char)*p);
		p++;
	}
	return str;
}

/*----------------------------------------------------------------------------*/
char* strextract(char *s1, char *beg, char *end)
{
	char *p1, *p2, *res;

	p1 = strcasestr(s1, beg);
	if (!p1) return NULL;

	p1 += strlen(beg);
	p2 = strcasestr(p1, end);
	if (!p2) return strdup(p1);

	res = malloc(p2 - p1 + 1);
	memcpy(res, p1, p2 - p1);
	res[p2 - p1] = '\0';

	return res;
}

/*---------------------------------------------------------------------------*/
static char *ltrim(char *s)
{
	while(isspace((int) *s)) s++;
	return s;
}

/*----------------------------------------------------------------------------*/
/* 																			  */
/* HTTP management														 	  */
/* 																			  */
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
bool http_parse(int sock, char *method, key_data_t *rkd, char **body, int *len)
{
	char line[256], *dp;
	int i, timeout = 100;

	rkd[0].key = NULL;

	if ((i = read_line(sock, line, sizeof(line), timeout)) <= 0) {
		if (i < 0) {
			LOG_ERROR("cannot read method", NULL);
		}
		return false;
	}

	if (!sscanf(line, "%s", method)) {
		LOG_ERROR("missing method", NULL);
		return false;
	}

	i = *len = 0;

	while (read_line(sock, line, sizeof(line), timeout) > 0) {

		// Check for max headers (leave room for NULL terminator)
		if (i >= 31) {  // Assuming max 32 headers
			LOG_ERROR("Too many headers, ignoring rest");
			break;
		}

		LOG_SDEBUG("sock: %u, received %s", line);

		// line folding should be deprecated
		if (i && rkd[i-1].key && (line[0] == ' ' || line[0] == '\t')) {
			// Find first non-whitespace
			char *trimmed = line;
			while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

			// Append to previous data
			size_t old_len = strlen(rkd[i-1].data);
			size_t new_len = strlen(trimmed);
			rkd[i-1].data = realloc(rkd[i-1].data, old_len + new_len + 1);
			memcpy(rkd[i-1].data + old_len, trimmed, new_len + 1);
			continue;
		}

		dp = strstr(line,":");

		if (!dp){
			LOG_ERROR("Request failed, bad header", NULL);
			kd_free(rkd);
			return false;
		}

		*dp = 0;
		rkd[i].key = strdup(line);
		rkd[i].data = strdup(ltrim(dp + 1));

		// Check for Content-Length using cached key
		if (strcasecmp(rkd[i].key, "Content-Length") == 0) {
			*len = atol(rkd[i].data);
		}

		i++;
		rkd[i].key = NULL;
	}

	if (*len) {
    int size = 0;

    // Large bodies come from PSRAM, small ones keep the plain malloc.
    //
    // The ceiling used to be 8192 bytes, and anything over it was read into a
    // discard buffer and reported as `*len = 0` - which destroyed the size on
    // the same line that was about to report it, so raop.c logged
    // "JPEG image discarded (too large: 0 bytes)" and nobody could tell a
    // dropped 200 KB image from a zero-byte one. AirPlay artwork is the only
    // body that ever exceeds 8192, so the effect was that RAOP_INT_ARTWORK had
    // never fired once.
    //
    // PSRAM specifically, and not by raising the malloc ceiling: malloc() here
    // is internal RAM, which measured 29,807 bytes free with a 5,120-byte
    // largest block during the TLS fetches at startup. A JPEG-sized contiguous
    // internal request there fails intermittently rather than cleanly, which is
    // the failure mode behind five separate defects in this codebase. Small
    // bodies - every DMAP and progress message - stay on the internal path
    // where their allocation is trivially satisfiable and the extra hop is not
    // worth it.
    if (*len > kSmallBodyBytes) {
        // Logged before anything else touches *len, because the old code's one
        // real failure was reporting a size it had already zeroed.
        LOG_INFO("body of %d bytes exceeds the %d-byte small-body path; "
                 "buffering from PSRAM", *len, kSmallBodyBytes);
        if (*len > kMaxBodyBytes) {
            // Still a ceiling, just a useful one. Without any bound a sender -
            // or a corrupted Content-Length - could ask for an arbitrary
            // allocation, and PSRAM is also where the display buffers and the
            // RTP ring live.
            LOG_ERROR("body of %d bytes exceeds the %d-byte maximum; discarding",
                      *len, kMaxBodyBytes);
            char discard[256];
            int remaining = *len;
            while (remaining > 0) {
                int to_read = remaining < (int) sizeof(discard) ? remaining : (int) sizeof(discard);
                int bytes = recv(sock, discard, to_read, 0);
                if (bytes <= 0) break;
                remaining -= bytes;
            }
            *body = NULL;
            *len = 0;
            return true;
        }
        *body = heap_caps_malloc(*len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    } else {
        *body = malloc(*len + 1);
    }
    if (!*body) {
        // Says which path failed and how much was wanted. The old message said
        // neither, and an allocation failure that cannot be sized is a report
        // that something went wrong without saying what to change.
        LOG_ERROR("failed to allocate a %d-byte body buffer", *len);
        return false;
    }

		while (size < *len) {
			int bytes = recv(sock, *body + size, *len - size, 0);
			if (bytes <= 0) break;
			size += bytes;
		}

		(*body)[*len] = '\0';

		if (size != *len) {
			LOG_ERROR("content length receive error %d %d", *len, size);
		}
	}

	return true;
}


/*----------------------------------------------------------------------------*/
static int read_line(int fd, char *line, int maxlen, int timeout)
{
	struct pollfd pfds;
	char buffer[512];
	int n, count = 0;

	pfds.fd = fd;
	pfds.events = POLLIN;
	*line = 0;

	while (count < maxlen - 1) {
		// Wait for data with timeout (only on first iteration)
		if (count == 0 && poll(&pfds, 1, timeout) <= 0) {
			return 0;
		}

		// Peek at available data without removing it from socket
		n = recv(fd, buffer, sizeof(buffer), MSG_PEEK);
		if (n <= 0) {
			if (n == 0) LOG_INFO("disconnected on the other end %u", fd);
			else if (errno != EAGAIN) LOG_ERROR("fd: %d read error: %s", fd, strerror(errno));
			return n < 0 ? -1 : count;
		}

		// Find line ending in peeked data
		int consumed = 0;
		for (int i = 0; i < n && count < maxlen - 1; i++) {
			char ch = buffer[i];
			consumed = i + 1;

			if (ch == '\n') {
				// Now actually consume up to and including \n
				recv(fd, buffer, consumed, 0);
				*line = 0;
				return count;
			}
			if (ch == '\r') continue;

			*line++ = ch;
			count++;
		}

		// Consume what we processed
		recv(fd, buffer, consumed, 0);

		// If we didn't find \n and buffer is full, line is too long
		if (consumed == n && count >= maxlen - 1) {
			*line = 0;
			LOG_WARN("line too long, truncated at %d chars", count);
			return count;
		}

		// Continue reading (no timeout for subsequent reads)
		timeout = 0;
	}

	*line = 0;
	return count;
}


/*----------------------------------------------------------------------------*/
char *http_send(int sock, char *method, key_data_t *rkd)
{
	unsigned sent, len;
	char *resp = kd_dump(rkd);
	char *data = malloc(strlen(method) + 2 + strlen(resp) + 2 + 1);

	len = sprintf(data, "%s\r\n%s\r\n", method, resp);
	NFREE(resp);

	LOG_SDEBUG("Sending HTTP response, length: %u", len);
	sent = send(sock, data, len, 0);

	if (sent != len) {
		LOG_ERROR("HTTP send() error:%s %u (strlen=%u), errno=%d (%s)",
		          data, sent, len, errno, strerror(errno));
		NFREE(data);
	} else {
		LOG_SDEBUG("HTTP send successful: %u bytes", sent);
	}

	return data;
}


/*----------------------------------------------------------------------------*/
char *kd_lookup(key_data_t *kd, char *key)
{
	int i = 0;
	while (kd && kd[i].key){
		if (!strcasecmp(kd[i].key, key)) return kd[i].data;
		i++;
	}
	return NULL;
}


/*----------------------------------------------------------------------------*/
bool kd_add(key_data_t *kd, char *key, char *data)
{
	int i = 0;

	// Callers pass kd_lookup() results straight in, and kd_lookup() returns
	// NULL for a header that is not there - so strdup(data) was strdup(NULL),
	// which on newlib dereferences NULL inside strlen(). raop.c:423 does
	// exactly this with kd_lookup(headers, "CSeq"), which means an RTSP
	// request that simply omits CSeq crashed the whole board. That request is
	// three lines long and needs no authentication, on a port this firmware
	// listens on by design, so it was a remote reset for anyone on the
	// network rather than only a bug a well-behaved sender would avoid.
	//
	// Guarded here rather than at the call site: the Apple-Response additions
	// pass base64_encode() output, which is also NULL on failure, and every
	// caller routes through this one function. Nothing is added when there is
	// no value to add - an absent header stays absent in the response, which
	// is more honest than echoing an empty one.
	if (!kd || !key || !data) return false;

	while (kd[i].key) i++;

	kd[i].key = strdup(key);
	kd[i].data = strdup(data);
	kd[i+1].key = NULL;

	return true;
}


/*----------------------------------------------------------------------------*/
void kd_free(key_data_t *kd)
{
	int i = 0;
	while (kd && kd[i].key){
		free(kd[i].key);
		if (kd[i].data) free(kd[i].data);
		i++;
	}

	kd[0].key = NULL;
}


/*----------------------------------------------------------------------------*/
char *kd_dump(key_data_t *kd)
{
	int i = 0;
	int pos = 0;
	char *str;

	if (!kd || !kd[0].key) return strdup("\r\n");

	// First pass: calculate total size needed
	int total_size = 0;
	while (kd[i].key) {
		total_size += strlen(kd[i].key) + strlen(kd[i].data) + 4; // ": \r\n"
		i++;
	}

	// Allocate exact size needed
	str = malloc(total_size + 1);
	if (!str) return NULL;

	// Second pass: build string
	i = 0;
	while (kd[i].key) {
		int len = sprintf(str + pos, "%s: %s\r\n", kd[i].key, kd[i].data);
		pos += len;
		i++;
	}

	str[pos] = '\0';
	return str;
}

/*--------------------------------------------------------------------------*/
void free_metadata(struct metadata_s *metadata)
{
	NFREE(metadata->artist);
	NFREE(metadata->album);
	NFREE(metadata->title);
	NFREE(metadata->genre);
	NFREE(metadata->path);
	NFREE(metadata->artwork);
	NFREE(metadata->remote_title);
}

// Time utility implementation
uint32_t gettime_ms(void) {
	return (uint32_t)(esp_timer_get_time() / 1000ULL);
}