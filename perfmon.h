#include <chrono>

class Perfmon {
public:
    void start();
    void process();
    void printFps(uint64_t millis);
private:
    std::chrono::high_resolution_clock::time_point mStart;
    uint64_t mProcessed = 0;
};
