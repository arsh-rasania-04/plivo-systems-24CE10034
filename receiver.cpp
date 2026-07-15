/* BASELINE RECEIVER (C) — naive on purpose. Rewrite it (C, C++, Go, or Rust).
 *
 * Ports (all 127.0.0.1):
 *   bind 47002  <- media from your sender, via the hostile relay
 *   send 47020  -> harness player. MUST be: 4-byte big-endian seq +
 *                  160-byte payload. Frame i counts only if it arrives
 *                  BEFORE its deadline t0 + DELAY_MS + i*20ms.
 *   send 47003  -> feedback to your sender, via the relay (optional)
 *
 * This baseline forwards whatever arrives straight to the player: lost
 * frames stay lost, late frames stay late, duplicates are re-sent
 * harmlessly. All yours to fix — jitter buffer, reordering, recovery.
 *
 * Env vars available: T0, DURATION_S, DELAY_MS. Harness kills the process
 * at run end; a forever-loop is fine.
 */
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/time.h>
#include <map>
#include <set>
#include <vector>

double get_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

int main(void) {
    int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in in_addr = {0};
    in_addr.sin_family = AF_INET;
    in_addr.sin_port = htons(47002);
    in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    bind(in_fd, (struct sockaddr *)&in_addr, sizeof in_addr);

    int out_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in player = {0};
    player.sin_family = AF_INET;
    player.sin_port = htons(47020);
    player.sin_addr.s_addr = inet_addr("127.0.0.1");

    int nack_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in relay_nack = {0};
    relay_nack.sin_family = AF_INET;
    relay_nack.sin_port = htons(47003);
    relay_nack.sin_addr.s_addr = inet_addr("127.0.0.1");

    double t0 = atof(getenv("T0"));
    double delay_sec = atof(getenv("DELAY_MS")) / 1000.0;

    uint32_t next_play = 0;
    uint32_t max_seq = 0;
    std::map<uint32_t, std::vector<unsigned char>> buffer;
    std::set<uint32_t> missing;

    struct pollfd fds[1];
    fds[0].fd = in_fd;
    fds[0].events = POLLIN;

    for (;;) {
        // 1. Play contiguous frames IMMEDIATELY to guarantee they arrive early
        while (buffer.find(next_play) != buffer.end()) {
            sendto(out_fd, buffer[next_play].data(), 164, 0, (struct sockaddr *)&player, sizeof player);
            buffer.erase(next_play);
            missing.erase(next_play);
            next_play++;
        }

        // 2. Calculate the strict deadline for the current missing frame
        double now = get_time();
        double deadline = t0 + delay_sec + (next_play * 0.020);
        
        // If time runs out, skip it instantly to prevent stalling the rest of the stream
        if (now >= deadline) {
            missing.erase(next_play);
            next_play++;
            continue; 
        }

        // 3. Wait for rescue packets, but ONLY up to the deadline
        double wait_sec = deadline - now;
        int wait_ms = (int)(wait_sec * 1000);
        if (wait_ms < 0) wait_ms = 0;

        int ret = poll(fds, 1, wait_ms);
        if (ret > 0 && (fds[0].revents & POLLIN)) {
            unsigned char buf[2048];
            ssize_t n = recvfrom(in_fd, buf, sizeof buf, 0, NULL, NULL);
            if (n == 164) {
                uint32_t seq;
                memcpy(&seq, buf, 4);
                seq = ntohl(seq);

                if (seq >= next_play) {
                    buffer[seq] = std::vector<unsigned char>(buf, buf + 164);
                }

                if (seq > max_seq) {
                    for (uint32_t i = max_seq + 1; i < seq; ++i) {
                        if (i >= next_play) missing.insert(i);
                    }
                    max_seq = seq;
                }
                missing.erase(seq);

                // Aggressively send NACKs for all detected gaps
                for (auto it = missing.begin(); it != missing.end(); ++it) {
                    uint32_t net_seq = htonl(*it);
                    sendto(nack_fd, &net_seq, 4, 0, (struct sockaddr *)&relay_nack, sizeof relay_nack);
                }
            }
        }
    }
    return 0;
}