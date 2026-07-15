# Systems Assignment Run Log

| Profile | Delay (ms) | Miss % | Overhead | Changes & Reasoning |
| :--- | :--- | :--- | :--- | :--- |
| A.json | 40 | 22.47% | 1.02x | **Baseline:** Initial C code. Failed due to having no recovery mechanisms for dropped packets. |
| A.json | 40 | 42.73% | 2.02x | **FEC Attempt:** Switched to C++ and appended the previous payload to current frames. Failed because it breached the 2.0x bandwidth cap and the buffer caused head-of-line blocking. |
| A.json | 100 | 1.13% | 1.15x | **ARQ / NACK Pivot:** Implemented a jitter buffer using `std::map`, instant playout of contiguous frames, and aggressive NACKs for missing sequences. Dropped bandwidth overhead significantly but slightly missed the 1% cap due to the network's Round Trip Time (RTT). |
| A.json | 150 | 0.00% | 1.16x | **Baseline Validated:** Increased the delay budget to allow sufficient time for NACK rescue packets to traverse the network. Achieved a perfect 0% miss rate. |
| A.json | 125 | 0.13% | 1.15x | **Optimization (Binary Search):** Tested the midpoint to lower the score. Safely below the 1.00% cap. |
| A.json | 112 | 0.27% | 1.14x | **Optimization (Binary Search):** Continued narrowing the valid boundary. |
| A.json | 106 | 0.53% | 1.15x | **Optimization (Binary Search):** Found the near-optimal floor for Profile A. |
| B.json | 106 | 5.33% | 1.54x | **Stress Test:** Profile B has much higher packet loss (163 drops). The 106ms delay does not provide enough time for the increased volume of NACK round-trips. |
| B.json | 150 | 3.87% | 1.72x | **Stress Test:** Increased delay to 150ms, but the high packet loss (193 drops) and variable RTT of Profile B still caused rescue packets to arrive late. |
| B.json | 200 | 0.47% | 1.81x | **Baseline Validated:** Found the safe floor for Profile B. High NACK volume pushed overhead to 1.81x, but misses are safely under 1%. |