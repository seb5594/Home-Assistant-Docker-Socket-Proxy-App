/*
 * docker-socket-proxy
 *
 * A read-only, allowlist-only TCP front for the Docker socket.
 *
 * Every connection carries one HTTP request. The request is checked against a
 * short list of harmless GET endpoints; if it passes, a fresh request is built
 * and sent to the Docker socket, and the answer is relayed back unchanged.
 * Only method, path and query ever reach Docker. Client headers and bodies are
 * dropped, so there is nothing left to smuggle past the check.
 *
 * One process per connection keeps the code trivial; a handful of dashboards
 * polling every few seconds is the intended load.
 */

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#define LISTEN_PORT 5594
#define DOCKER_SOCKET "/var/run/docker.sock"

#define MAX_CLIENTS 32
#define MAX_REQUEST_HEAD 8192
#define MAX_PATH_SEGMENTS 4
#define HEAD_TIMEOUT_SECONDS 10
#define WRITE_TIMEOUT_SECONDS 30

struct request {
    const char *method;
    size_t method_len;
    const char *target; /* path plus optional query, as sent by the client */
    size_t target_len;
    size_t path_len;    /* length of the target up to the '?' */
};

/* ---- small helpers ------------------------------------------------------- */

static int write_all(int fd, const char *data, size_t len)
{
    while (len > 0) {
        ssize_t written = write(fd, data, len);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            return 0;
        data += written;
        len -= (size_t)written;
    }
    return 1;
}

static int is_alnum(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int segment_is(const char *segment, size_t len, const char *literal)
{
    return strlen(literal) == len && memcmp(segment, literal, len) == 0;
}

/* ---- allowlist ----------------------------------------------------------- */

/* An API version prefix such as "v1.47". */
static int is_version_segment(const char *segment, size_t len)
{
    size_t i = 1;
    size_t digits;

    if (len < 4 || segment[0] != 'v')
        return 0;
    for (digits = 0; i < len && segment[i] >= '0' && segment[i] <= '9'; i++)
        digits++;
    if (digits == 0 || i >= len || segment[i++] != '.')
        return 0;
    for (digits = 0; i < len && segment[i] >= '0' && segment[i] <= '9'; i++)
        digits++;
    return digits > 0 && i == len;
}

/* A container name or id: starts with a letter or digit, so "." and ".." never match. */
static int is_container_name(const char *segment, size_t len)
{
    size_t i;

    if (len == 0 || !is_alnum(segment[0]))
        return 0;
    for (i = 1; i < len; i++) {
        char c = segment[i];
        if (!is_alnum(c) && c != '_' && c != '.' && c != '-')
            return 0;
    }
    return 1;
}

/*
 * Endpoints used by Monitor Docker and similar dashboards, all read-only:
 *   _ping, version, info        daemon status
 *   events                      live container events (long-lived stream)
 *   containers/json             container list
 *   containers/<id>/json        container details
 *   containers/<id>/stats       CPU, memory, network and disk figures
 *   images/json                 image list
 * Deliberately absent: logs, exec, archive, export and attach, which can read
 * files or open a shell inside a container even with a plain GET.
 */
static int is_allowed_endpoint(const char *const *segment, const size_t *length, size_t count)
{
    switch (count) {
    case 1:
        return segment_is(segment[0], length[0], "_ping")
            || segment_is(segment[0], length[0], "version")
            || segment_is(segment[0], length[0], "info")
            || segment_is(segment[0], length[0], "events");
    case 2:
        return segment_is(segment[1], length[1], "json")
            && (segment_is(segment[0], length[0], "containers")
                || segment_is(segment[0], length[0], "images"));
    case 3:
        return segment_is(segment[0], length[0], "containers")
            && is_container_name(segment[1], length[1])
            && (segment_is(segment[2], length[2], "json")
                || segment_is(segment[2], length[2], "stats"));
    default:
        return 0;
    }
}

static int is_allowed_path(const char *path, size_t len)
{
    const char *segment[MAX_PATH_SEGMENTS];
    size_t length[MAX_PATH_SEGMENTS];
    size_t count = 0;
    const char *cursor = path + 1;
    const char *end = path + len;
    size_t skip;

    if (len < 2 || path[0] != '/' || path[len - 1] == '/')
        return 0;

    while (cursor < end) {
        const char *slash = memchr(cursor, '/', (size_t)(end - cursor));
        size_t segment_len = (size_t)((slash ? slash : end) - cursor);

        if (segment_len == 0 || count == MAX_PATH_SEGMENTS)
            return 0;
        segment[count] = cursor;
        length[count] = segment_len;
        count++;
        cursor += segment_len + 1;
    }

    /* Drop an optional API version prefix such as "v1.47". */
    skip = is_version_segment(segment[0], length[0]) ? 1 : 0;
    return is_allowed_endpoint(segment + skip, length + skip, count - skip);
}

/* The query only carries parameters; plain printable ASCII is all it may contain. */
static int is_valid_query(const char *query, size_t len)
{
    size_t i;

    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)query[i];
        if (c < 0x21 || c > 0x7e)
            return 0;
    }
    return 1;
}

/* ---- request parsing ----------------------------------------------------- */

/* Parses "METHOD SP target SP HTTP/1.x"; head must contain a complete CRLF line. */
static int parse_request_line(const char *head, size_t head_len, struct request *request)
{
    const char *line_end = memmem(head, head_len, "\r\n", 2);
    const char *method_end = memchr(head, ' ', (size_t)(line_end - head));
    const char *target;
    const char *target_end;
    const char *version;
    const char *query;

    if (method_end == NULL)
        return 0;
    target = method_end + 1;
    target_end = memchr(target, ' ', (size_t)(line_end - target));
    if (target_end == NULL || target_end == target)
        return 0;
    version = target_end + 1;
    if ((size_t)(line_end - version) != 8
        || (memcmp(version, "HTTP/1.1", 8) != 0 && memcmp(version, "HTTP/1.0", 8) != 0))
        return 0;

    request->method = head;
    request->method_len = (size_t)(method_end - head);
    request->target = target;
    request->target_len = (size_t)(target_end - target);
    query = memchr(target, '?', request->target_len);
    request->path_len = query ? (size_t)(query - target) : request->target_len;
    return 1;
}

/* Reads until the end of the request head. Returns its length or -1. */
static ssize_t read_request_head(int fd, char *buffer, size_t capacity)
{
    size_t used = 0;

    while (used < capacity) {
        ssize_t received = recv(fd, buffer + used, capacity - used, 0);
        if (received <= 0)
            return -1;
        used += (size_t)received;
        if (memmem(buffer, used, "\r\n\r\n", 4) != NULL)
            return (ssize_t)used;
    }
    return -1;
}

/* ---- responses and logging ----------------------------------------------- */

static void respond(int fd, int status, const char *reason, const char *extra_headers,
                    const char *body, int head_only)
{
    char response[768];
    int len = snprintf(response, sizeof response,
                       "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\n"
                       "Content-Length: %zu\r\n%sConnection: close\r\n\r\n%s",
                       status, reason, strlen(body), extra_headers, head_only ? "" : body);

    if (len > 0 && (size_t)len < sizeof response)
        write_all(fd, response, (size_t)len);
}

static void respond_error(int fd, int status, const char *reason, const char *extra_headers,
                          const char *message, int head_only)
{
    char body[256];

    snprintf(body, sizeof body, "{\"message\":\"docker-socket-proxy: %s\"}\n", message);
    respond(fd, status, reason, extra_headers, body, head_only);
}

/* Client input is untrusted: keep control characters out of the log. */
static void log_rejected(const struct sockaddr_in *peer, const struct request *request, int status)
{
    char line[160];
    char address[INET_ADDRSTRLEN] = "?";
    char *c;

    snprintf(line, sizeof line, "%.*s %.*s",
             (int)(request->method_len < 16 ? request->method_len : 16), request->method,
             (int)(request->target_len < 120 ? request->target_len : 120), request->target);
    for (c = line; *c != '\0'; c++) {
        if (*c < 0x20 || *c > 0x7e)
            *c = '?';
    }
    inet_ntop(AF_INET, &peer->sin_addr, address, sizeof address);
    fprintf(stderr, "[REJECTED] %s \"%s\" -> %d\n", address, line, status);
}

/* ---- docker side --------------------------------------------------------- */

static int connect_docker(void)
{
    struct sockaddr_un address = { .sun_family = AF_UNIX };
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (fd < 0)
        return -1;
    memcpy(address.sun_path, DOCKER_SOCKET, sizeof DOCKER_SOCKET);
    if (connect(fd, (struct sockaddr *)&address, sizeof address) < 0) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }
    return fd;
}

/* Passes the answer back until either side hangs up; stream endpoints end here too. */
static void relay(int client, int docker)
{
    char buffer[16384];
    struct pollfd fds[2] = { { .fd = client, .events = POLLIN }, { .fd = docker, .events = POLLIN } };

    for (;;) {
        ssize_t received;

        if (poll(fds, 2, -1) < 0) {
            if (errno == EINTR)
                continue;
            return;
        }
        if (fds[1].revents) {
            received = read(docker, buffer, sizeof buffer);
            if (received <= 0 || !write_all(client, buffer, (size_t)received))
                return;
        }
        if (fds[0].revents) {
            /* Anything after the request head is dropped; a hang-up ends the stream. */
            received = read(client, buffer, sizeof buffer);
            if (received <= 0)
                return;
        }
    }
}

/* ---- connection handling ------------------------------------------------- */

static void set_client_options(int client)
{
    struct timeval head_timeout = { .tv_sec = HEAD_TIMEOUT_SECONDS };
    struct timeval write_timeout = { .tv_sec = WRITE_TIMEOUT_SECONDS };
    int on = 1, idle = 60, interval = 10, probes = 3;

    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &head_timeout, sizeof head_timeout);
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &write_timeout, sizeof write_timeout);
    /* Notice clients that vanished without closing, so their stream is released. */
    setsockopt(client, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof on);
    setsockopt(client, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof idle);
    setsockopt(client, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof interval);
    setsockopt(client, IPPROTO_TCP, TCP_KEEPCNT, &probes, sizeof probes);
}

static void forward_request(int client, const struct request *request, int head_only)
{
    char outgoing[MAX_REQUEST_HEAD];
    int len;
    int docker = connect_docker();

    if (docker < 0) {
        int reason = errno;
        const char *message = reason == ENOENT
            ? "Docker socket not available, turn off Protection mode of this app"
            : "Docker daemon is not reachable";
        fprintf(stderr, "[ERROR] %s (%s)\n", message, strerror(reason));
        respond_error(client, 502, "Bad Gateway", "", message, head_only);
        return;
    }

    len = snprintf(outgoing, sizeof outgoing,
                   "%.*s %.*s HTTP/1.1\r\nHost: docker\r\nConnection: close\r\n\r\n",
                   (int)request->method_len, request->method,
                   (int)request->target_len, request->target);
    if (len > 0 && (size_t)len < sizeof outgoing && write_all(docker, outgoing, (size_t)len))
        relay(client, docker);
    close(docker);
}

static void handle_client(int client, const struct sockaddr_in *peer)
{
    char head[MAX_REQUEST_HEAD];
    struct request request;
    ssize_t head_len;
    int head_only;
    int status;

    set_client_options(client);

    head_len = read_request_head(client, head, sizeof head);
    if (head_len < 0 || !parse_request_line(head, (size_t)head_len, &request)) {
        respond_error(client, 400, "Bad Request", "", "malformed or oversized request", 0);
        return;
    }

    head_only = request.method_len == 4 && memcmp(request.method, "HEAD", 4) == 0;
    if (!head_only && !(request.method_len == 3 && memcmp(request.method, "GET", 3) == 0)) {
        status = 405;
        respond_error(client, status, "Method Not Allowed", "Allow: GET, HEAD\r\n",
                      "read-only, only GET and HEAD are allowed", 0);
        log_rejected(peer, &request, status);
        return;
    }

    /* Liveness probe for the Supervisor watchdog; does not touch Docker. */
    if (request.target_len == 8 && memcmp(request.target, "/healthz", 8) == 0) {
        respond(client, 200, "OK", "", "{\"status\":\"ok\"}\n", head_only);
        return;
    }

    if (!is_allowed_path(request.target, request.path_len)
        || !is_valid_query(request.target + request.path_len, request.target_len - request.path_len)) {
        status = 403;
        respond_error(client, status, "Forbidden", "", "endpoint is not allowed", head_only);
        log_rejected(peer, &request, status);
        return;
    }

    forward_request(client, &request, head_only);
}

/* ---- server -------------------------------------------------------------- */

static int listen_on_port(void)
{
    struct sockaddr_in address = { .sin_family = AF_INET, .sin_port = htons(LISTEN_PORT) };
    int on = 1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd < 0)
        return -1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    if (bind(fd, (struct sockaddr *)&address, sizeof address) < 0 || listen(fd, 16) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int main(void)
{
    struct stat info;
    int listener;
    int active = 0;

    signal(SIGPIPE, SIG_IGN);

    /* The Supervisor only mounts the socket while Protection mode is off. */
    if (stat(DOCKER_SOCKET, &info) != 0 || !S_ISSOCK(info.st_mode))
        fprintf(stderr, "[WARNING] Docker socket not found at %s. Turn off 'Protection mode' "
                        "on the Info page of this app and start it again.\n", DOCKER_SOCKET);

    listener = listen_on_port();
    if (listener < 0) {
        fprintf(stderr, "[ERROR] Cannot listen on port %d: %s\n", LISTEN_PORT, strerror(errno));
        return 1;
    }
    fprintf(stderr, "[INFO] Serving the Docker API read-only on port %d\n", LISTEN_PORT);

    for (;;) {
        struct sockaddr_in peer;
        socklen_t peer_len = sizeof peer;
        int client;
        pid_t child;

        /* Reap finished connections; wait for one if the limit is reached. */
        while (waitpid(-1, NULL, WNOHANG) > 0)
            active--;
        if (active >= MAX_CLIENTS && waitpid(-1, NULL, 0) > 0)
            active--;

        client = accept(listener, (struct sockaddr *)&peer, &peer_len);
        if (client < 0)
            continue;

        child = fork();
        if (child == 0) {
            close(listener);
            handle_client(client, &peer);
            _exit(0);
        }
        if (child > 0)
            active++;
        close(client);
    }
}
