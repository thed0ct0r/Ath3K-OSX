/* IOath3kfrmwr class */
#ifndef __IOATH3KFRMWR__
#define __IOATH3KFRMWR__

#include <IOKit/IOService.h>

class local_IOath3kfrmwr : public IOService
{
    OSDeclareDefaultStructors(local_IOath3kfrmwr)

private:
    IOUSBInterface* GetInterfaceWithBulkPipeOut(IOUSBDevice* pDeviceToSearch);
    int GetBulkPipeOutNumber(IOUSBInterface* pInterface);
    
public:
    virtual bool init(OSDictionary* dictionary = 0);
    virtual void free(void);
    
    virtual bool attach(IOService* provider);
    virtual void detach(IOService* provider);
    
    virtual IOService* probe(IOService* provider, SInt32 *score);
    
    virtual bool start(IOService* provider);
    virtual void stop(IOService* provider);
};

#endif //__IOATH3KFRMWR__ 

