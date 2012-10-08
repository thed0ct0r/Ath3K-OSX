/* IOath3kfrmwr class */
#ifndef __IOATH3KFRMWR__
#define __IOATH3KFRMWR__

#include <IOKit/usb/IOUSBDevice.h>

class local_IOath3kfrmwr : public IOService
{
    OSDeclareDefaultStructors(local_IOath3kfrmwr)
    
protected:
    IOUSBDevice * pUsbDev;
        
public:
    // this is from the IORegistryEntry - no provider yet
    virtual bool 	init(OSDictionary *propTable);
    
    // IOKit methods. These methods are defines in <IOKit/IOService.h>
    virtual IOService* probe(IOService *provider, SInt32 *score );
    
    virtual bool attach(IOService *provider);
    virtual void detach(IOService *provider);
    
    virtual bool start(IOService *provider);
    virtual void stop(IOService *provider);
    
    virtual bool handleOpen(IOService *forClient, IOOptionBits options = 0, void *arg = 0 );
    virtual void handleClose(IOService *forClient, IOOptionBits options = 0 );
    
    virtual IOReturn message(UInt32 type, IOService *provider, void *argument);
    
    virtual bool terminate(IOOptionBits options = 0);
    virtual bool finalize(IOOptionBits options);
};

#endif //__IOATH3KFRMWR__ 

