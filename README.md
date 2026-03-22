# Real-Time Mempool Monitor & Honeypot Detector

A multithreaded C++ security engine that intercepts pending Ethereum transactions from the mempool, decodes complex Uniswap payloads, calculates MEV slippage, and runs local fork execution to detect malicious honeypot contracts before blocks are mined.

## Architecture
* **Language:** C++ (Boost.Asio, Boost.Beast)
* **Network:** WebSockets (WSS) to Alchemy Ethereum Node
* **Execution:** Foundry (Anvil) for zero-day local testing
* **Concurrency:** Thread-safe lock-guard queuing for non-blocking stream parsing

## Prerequisites
To run this project locally, you need:
1.  **C++ Build Tools:** CMake and a C++17 compiler (MSVC/GCC).
2.  **Libraries:** Boost (1.80+) and OpenSSL installed and configured.
3.  **Foundry:** [Foundry/Anvil](https://getfoundry.sh/) installed for local blockchain execution.
4.  **Alchemy Key:** An Ethereum Mainnet API key from Alchemy.

## Build Instructions
This project uses CMake. To build the monitor:
1. `mkdir build && cd build`
2. `cmake ..`
3. `make`

## How to Test and Run

**1. Start the Local Fork**
Open a terminal (Ubuntu/WSL recommended) and spin up a local fork of the Ethereum mainnet using your Alchemy URL:
```bash
anvil --fork-url [https://eth-mainnet.g.alchemy.com/v2/YOUR_ALCHEMY_KEY_HERE](https://eth-mainnet.g.alchemy.com/v2/YOUR_ALCHEMY_KEY_HERE)
