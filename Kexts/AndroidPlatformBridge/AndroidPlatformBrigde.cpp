#include "AndroidPlatformBridge.hpp"
#include <IOKit/IOLib.h>

#define super IOService
OSDefineMetaClassAndStructors(AndroidPlatformBridge, IOService)

bool AndroidPlatformBridge::start(IOService *provider)
{
    if (!super::start(provider))
        return false;

    IOLog("PocketDarwin: AndroidPlatformBridge starting\n");

    publishPlatformProperties();

    registerService(); // Make ourselves visible
    return true;
}

void AndroidPlatformBridge::stop(IOService *provider)
{
    IOLog("PocketDarwin: AndroidPlatformBridge stopping\n");
    super::stop(provider);
}

void AndroidPlatformBridge::publishPlatformProperties(void)
{
    // These are placeholders — honest ones
    setProperty("PDPlatform", "Android");
    setProperty("PDArchitecture", "ARM");
    setProperty("PDTranslated", true);

    // Future home of:
    // - Device Tree import
    // - Memory map
    // - Boot arguments
    // - Power hints
}
