/* echo-probe.c -- tiny TCP echo server for the peer link smoke test.
 *
 * busybox nc on the Kindle is client-only (nc [IPADDR PORT], no -l), so this
 * is the listener half of the "two Kindles talk to each other" proof: it
 * binds 0.0.0.0:7777, reads one line, writes it straight back, and exits.
 * A one-off diagnostic, not a service: one message per run keeps it simple
 * and honest -- if the peer never connects, it prints that and exits 1.
 *
 *   make echo-probe                     # static ARM, cross-built like grabtest
 *   cat echo-probe | ssh ... 'cat > /mnt/us/echo-probe && chmod +x ...'
 *   /mnt/us/echo-probe                  # on unit A: listens on :7777
 *   echo hello | nc 10.30.0.x 7777      # on unit B: sends, gets it back
 *
 * Exit status: 0 echoed a line, 1 connect failed or read failed.
 */
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 7777

int main(void) {
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) {
        fprintf(stderr, "echo-probe: socket: %s\n", strerror(errno));
        return 1;
    }
    int on = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);

    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(PORT);
    if (bind(srv, (struct sockaddr *)&a, sizeof a) != 0) {
        fprintf(stderr, "echo-probe: bind: %s\n", strerror(errno));
        return 1;
    }
    if (listen(srv, 1) != 0) {
        fprintf(stderr, "echo-probe: listen: %s\n", strerror(errno));
        return 1;
    }
    fprintf(stderr, "echo-probe: listening on :%d\n", PORT);

    struct sockaddr_in peer;
    socklen_t plen = sizeof peer;
    int cli = accept(srv, (struct sockaddr *)&peer, &plen);
    if (cli < 0) {
        fprintf(stderr, "echo-probe: accept: %s\n", strerror(errno));
        return 1;
    }
    fprintf(stderr, "echo-probe: connected from %s:%d\n",
            inet_ntoa(peer.sin_addr), ntohs(peer.sin_port));

    char buf[512];
    ssize_t n = read(cli, buf, sizeof buf - 1);
    if (n <= 0) {
        fprintf(stderr, "echo-probe: read: %s\n", n == 0 ? "peer closed" : strerror(errno));
        close(cli);
        close(srv);
        return 1;
    }
    buf[n] = '\0';
    /* strip a trailing newline so the echoed line is clean */
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';

    if (write(cli, buf, n) != n) {
        fprintf(stderr, "echo-probe: write: %s\n", strerror(errno));
        close(cli);
        close(srv);
        return 1;
    }
    printf("echo-probe: echoed %zd bytes: [%s]\n", n, buf);
    close(cli);
    close(srv);
    return 0;
}