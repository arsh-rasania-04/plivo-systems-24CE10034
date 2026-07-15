/* BASELINE SENDER (C) — naive on purpose. Rewrite it (C, C++, Go, or Rust).
 *
 * Ports (all 127.0.0.1):
 *   bind 47010  <- harness source delivers frame i here at t0 + i*20ms
 *                  (format: 4-byte big-endian seq + 160-byte payload)
 *   send 47001  -> relay uplink toward the receiver (YOUR wire format)
 *   bind 47004  <- feedback from your receiver, via the relay (optional)
 *
 * This baseline forwards each frame once, unchanged, and ignores feedback.
 * No redundancy, no retransmission. It cannot pass. That is the point.
 *
 * Env vars available if you want them: T0 (epoch seconds, float),
 * DURATION_S, DELAY_MS. The harness kills this process when the run ends,
 * so a forever-loop is fine.
 *
 * build: make        run: python3 run.py --delay_ms 60
 */
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <stdint.h>
#include <map>
#include <vector>

int main(void) {
    int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in in_addr = {0};
    in_addr.sin_family = AF_INET;
    in_addr.sin_port = htons(47010);
    in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    bind(in_fd, (struct sockaddr *)&in_addr, sizeof in_addr);

    int out_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in relay = {0};
    relay.sin_family = AF_INET;
    relay.sin_port = htons(47001);
    relay.sin_addr.s_addr = inet_addr("127.0.0.1");

    int nack_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in nack_addr = {0};
    nack_addr.sin_family = AF_INET;
    nack_addr.sin_port = htons(47004);
    nack_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    bind(nack_fd, (struct sockaddr *)&nack_addr, sizeof nack_addr);

    std::map<uint32_t, std::vector<unsigned char>> history;
    struct pollfd fds[2];
    fds[0].fd = in_fd;   fds[0].events = POLLIN;
    fds[1].fd = nack_fd; fds[1].events = POLLIN;

    unsigned char buf[2048];
    for (;;) {
        int ret = poll(fds, 2, -1);
        if (ret < 0) continue;

        if (fds[0].revents & POLLIN) {
            ssize_t n = recvfrom(in_fd, buf, sizeof buf, 0, NULL, NULL);
            if (n == 164) {
                uint32_t seq;
                memcpy(&seq, buf, 4);
                seq = ntohl(seq);
                
                history[seq] = std::vector<unsigned char>(buf, buf + 164);
                if (history.size() > 500) history.erase(history.begin());
                
                sendto(out_fd, buf, n, 0, (struct sockaddr *)&relay, sizeof relay);
            }
        }

        if (fds[1].revents & POLLIN) {
            ssize_t n = recvfrom(nack_fd, buf, sizeof buf, 0, NULL, NULL);
            if (n == 4) {
                uint32_t nack_seq;
                memcpy(&nack_seq, buf, 4);
                nack_seq = ntohl(nack_seq);
                
                if (history.find(nack_seq) != history.end()) {
                    sendto(out_fd, history[nack_seq].data(), 164, 0, 
                           (struct sockaddr *)&relay, sizeof relay);
                }
            }
        }
    }
    return 0;
}
