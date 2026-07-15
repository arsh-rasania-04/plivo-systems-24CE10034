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
#include <set>

int main(void) {
    // 1. Setup Relay Input (47002)
    int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in in_addr = {0};
    in_addr.sin_family = AF_INET;
    in_addr.sin_port = htons(47002);
    in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    bind(in_fd, (struct sockaddr *)&in_addr, sizeof in_addr);

    // 2. Setup Player Output (47020)
    int out_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in player = {0};
    player.sin_family = AF_INET;
    player.sin_port = htons(47020);
    player.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    // 3. Setup NACK Output (47003)
    int nack_out_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in relay_nack = {0};
    relay_nack.sin_family = AF_INET;
    relay_nack.sin_port = htons(47003);
    relay_nack.sin_addr.s_addr = inet_addr("127.0.0.1");

    unsigned char buf[2048];
    uint32_t max_seq = 0;
    bool first = true;
    std::set<uint32_t> missing;

    for (;;) {
        ssize_t n = recvfrom(in_fd, buf, sizeof buf, 0, NULL, NULL);
        if (n <= 0) continue;

        uint32_t seq;
        memcpy(&seq, buf, 4);
        seq = ntohl(seq);

        // INSTANT PLAYOUT: Fixes head-of-line blocking
        sendto(out_fd, buf, n, 0, (struct sockaddr *)&player, sizeof player);

        // Gap Detection Logic
        if (first) {
            max_seq = seq;
            first = false;
        } else {
            if (seq > max_seq) {
                for (uint32_t i = max_seq + 1; i < seq; ++i) {
                    missing.insert(i);
                }
                max_seq = seq;
            }
            missing.erase(seq); // We got it, remove from missing set
        }

        // Keep the set clean from ancient lost packets
        while (!missing.empty() && max_seq > *missing.begin() && (max_seq - *missing.begin()) >= 200) {
            missing.erase(missing.begin());
        }

        // Blast NACKs for remaining missing packets
        for (auto it = missing.begin(); it != missing.end(); ++it) {
            uint32_t net_seq = htonl(*it);
            sendto(nack_out_fd, &net_seq, 4, 0, (struct sockaddr *)&relay_nack, sizeof relay_nack);
        }
    }
    return 0;
}
