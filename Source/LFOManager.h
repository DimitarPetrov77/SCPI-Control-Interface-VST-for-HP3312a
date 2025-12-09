#pragma once

#include "LFOEngine.h"
#include <vector>
#include <memory>

class LFOManager
{
public:
    LFOManager(HP33120ADriver* device);
    ~LFOManager();
    
    LFOEngine* createLFO();
    void removeLFO(LFOEngine* lfo);
    
    const std::vector<std::unique_ptr<LFOEngine>>& getLFOs() const { return lfos; }
    
private:
    HP33120ADriver* device;
    std::vector<std::unique_ptr<LFOEngine>> lfos;
};

