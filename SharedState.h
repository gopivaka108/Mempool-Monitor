#pragma once
#include <queue>
#include <string>
#include <mutex>

// Extern declarations tell the compiler these exist somewhere else (in main.cpp)
extern std::queue<std::string> tx_queue;
extern std::mutex queue_lock;