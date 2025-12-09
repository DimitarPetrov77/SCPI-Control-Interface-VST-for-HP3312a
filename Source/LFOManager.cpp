#include "LFOManager.h"
#include <algorithm>

LFOManager::LFOManager(HP33120ADriver* device)
    : device(device)
{
}

LFOManager::~LFOManager()
{
    lfos.clear();
}

LFOEngine* LFOManager::createLFO()
{
    auto lfo = std::make_unique<LFOEngine>(device);
    LFOEngine* ptr = lfo.get();
    lfos.push_back(std::move(lfo));
    return ptr;
}

void LFOManager::removeLFO(LFOEngine* lfo)
{
    lfos.erase(
        std::remove_if(lfos.begin(), lfos.end(),
            [lfo](const std::unique_ptr<LFOEngine>& ptr) {
                return ptr.get() == lfo;
            }),
        lfos.end()
    );
}

