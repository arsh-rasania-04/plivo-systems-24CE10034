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
#include <map>
#include <vector>

int main(void) {
    int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in in_addr = {0};
    in_addr.sin_family = AF_INET;
    in_addr.sin_port = htons(47002);
    in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (bind(in_fd, (struct sockaddr *)&in_addr, sizeof in_addr) < 0) {
        perror("bind 47002");
        return 1;
    }

    int out_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in player = {0};
    player.sin_family = AF_INET;
    player.sin_port = htons(47020);
    player.sin_addr.s_addr = inet_addr("127.0.0.1");

    unsigned char buf[2048];
    // Map automatically sorts packets by sequence number
    std::map<uint32_t, std::vector<unsigned char>> jitter_buffer;
    uint32_t next_expected = 0;
    bool first_packet = true;

    for (;;) {
        ssize_t n = recvfrom(in_fd, buf, sizeof buf, 0, NULL, NULL);
        if (n <= 0) continue;

        // Extract sequence number (network to host byte order)
        uint32_t seq;
        memcpy(&seq, buf, 4);
        seq = ntohl(seq);

        if (first_packet) {
            next_expected = seq;
            first_packet = false;
        }

        // 1. Store the current packet if we don't have it and it's not too late
        if (seq >= next_expected && jitter_buffer.find(seq) == jitter_buffer.end()) {
            jitter_buffer[seq] = std::vector<unsigned char>(buf, buf + 164);
        }

        // 2. Recover the previous packet from the redundant data if needed
        if (n == 324 && seq > 0) {
            uint32_t prev_seq = seq - 1;
            if (prev_seq >= next_expected && jitter_buffer.find(prev_seq) == jitter_buffer.end()) {
                std::vector<unsigned char> recovered_pkt(164);
                uint32_t net_prev_seq = htonl(prev_seq);
                
                // Reconstruct the 164-byte frame: 4-byte seq + 160-byte payload
                memcpy(recovered_pkt.data(), &net_prev_seq, 4);
                memcpy(recovered_pkt.data() + 4, buf + 164, 160);
                
                jitter_buffer[prev_seq] = recovered_pkt;
            }
        }

        // 3. Play out all contiguous frames available in the buffer
        while (jitter_buffer.find(next_expected) != jitter_buffer.end()) {
            sendto(out_fd, jitter_buffer[next_expected].data(), 164, 0, 
                   (struct sockaddr *)&player, sizeof player);
            jitter_buffer.erase(next_expected);
            next_expected++;
        }
    }
    return 0;
}
