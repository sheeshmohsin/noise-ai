#include "device.hpp"
#include <CoreAudio/AudioServerPlugIn.h>
#include <CoreFoundation/CoreFoundation.h>

// Entry point called by coreaudiod when loading the plugin
extern "C" __attribute__((visibility("default")))
void* NoiseAI_Create(CFAllocatorRef allocator, CFUUIDRef typeUUID)
{
    // Verify this is the right plugin type
    if (!CFEqual(typeUUID, kAudioServerPlugInTypeUUID)) {
        return nullptr;
    }

    // Create driver (static so it lives for coreaudiod's lifetime)
    static auto components = CreateNoiseAIDriver();

    return components.driver->GetReference();
}
