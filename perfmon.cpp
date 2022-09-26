#include "perfmon.h"
#include <iostream>

using namespace std;
using namespace std::chrono;

void Perfmon::start()
{
    mStart = high_resolution_clock::now();
}

void Perfmon::process()
{
    mProcessed++;
}

void Perfmon::printFps(uint64_t millis)
{    
    auto elapsed = duration_cast<milliseconds>(high_resolution_clock::now() - mStart).count();    
    if (elapsed<millis) {
        return;
    }
    mStart = high_resolution_clock::now();

    float fps = (1.0f * mProcessed)/(1.0f * elapsed / 1000.0f);
    
    cout << "FPS: " << fps << endl;
    mProcessed = 0;
}
