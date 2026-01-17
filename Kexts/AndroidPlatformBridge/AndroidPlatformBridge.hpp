#include <IOKit/IOService.h>

class AndroidPlatformBridge : public IOService
{
    OSDeclareDefaultStructors(AndroidPlatformBridge)

public:
    virtual bool start(IOService *provider) override;
    virtual void stop(IOService *provider) override;

private:
    void publishPlatformProperties(void);
};
