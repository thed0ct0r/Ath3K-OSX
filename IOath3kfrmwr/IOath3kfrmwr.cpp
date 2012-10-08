/* Disclaimer: 
 This code is loosely based on the template of the class 
 in AnchorUSB Driver example from IOUSBFamily, 
 Open Source by Apple http://www.opensource.apple.com
 
 For information on driver matching for USB devices, see: 
 http://developer.apple.com/qa/qa2001/qa1076.html

 */
#include <IOKit/IOLib.h>
#include <IOKit/IOMessage.h>

#include <IOKit/usb/IOUSBInterface.h>

#include "IOath3kfrmwr.h"

#include "ath3k-1fw.h"

#define USB_REQ_DFU_DNLOAD	1
#define BULK_SIZE	4096
#define MAX_FILE_SIZE 2000000


OSDefineMetaClassAndStructors(local_IOath3kfrmwr, IOService)
#define super IOService

bool 	
local_IOath3kfrmwr::init(OSDictionary *propTable)
{
    IOLog("local_IOath3kfrmwr: init\n");
    return (super::init(propTable));
}



IOService* 
local_IOath3kfrmwr::probe(IOService *provider, SInt32 *score)
{
    IOLog("%s(%p)::probe\n", getName(), this);
    return super::probe(provider, score);			// this returns this
}



bool 
local_IOath3kfrmwr::attach(IOService *provider)
{
    // be careful when performing initialization in this method. It can be and
    // usually will be called mutliple 
    // times per instantiation
    IOLog("%s(%p)::attach\n", getName(), this);
    return super::attach(provider);
}


void 
local_IOath3kfrmwr::detach(IOService *provider)
{
    // Like attach, this method may be called multiple times
    IOLog("%s(%p)::detach\n", getName(), this);
    return super::detach(provider);
}



//
// start
// when this method is called, I have been selected as the driver for this device.
// I can still return false to allow a different driver to load
//
bool 
local_IOath3kfrmwr::start(IOService *provider)
{
    IOReturn 				err;
    const IOUSBConfigurationDescriptor *cd;
    
    // Do all the work here, on an IOKit matching thread.
    
    // 0.1 Get my USB Device
    IOLog("%s(%p)::start!\n", getName(), this);
    pUsbDev = OSDynamicCast(IOUSBDevice, provider);
    if(!pUsbDev) 
    {
        IOLog("%s(%p)::start - Provider isn't a USB device!!!\n", getName(), this);
        return false;
    }

    // 0.2 Reset the device
    err = pUsbDev->ResetDevice();
    if (err)
    {
        IOLog("%s(%p)::start - failed to reset the device\n", getName(), this);
        pUsbDev->close(this);
        return false;
    } else IOLog("%s(%p)::start: device reset\n", getName(), this);
    
    // 0.3 Find the first config/interface
    int numconf = 0;
    if ((numconf = pUsbDev->GetNumConfigurations()) < 1)
    {
        IOLog("%s(%p)::start - no composite configurations\n", getName(), this);
        return false;
    } else IOLog("%s(%p)::start: num configurations %d\n", getName(), this, numconf);
        
    cd = pUsbDev->GetFullConfigurationDescriptor(0);
    
    // Set the configuration to the first config
    if (!cd)
    {
        IOLog("%s(%p)::start - no config descriptor\n", getName(), this);
        return false;
    }
	
    // 1.0 Open the USB device
    if (!pUsbDev->open(this))
    {
        IOLog("%s(%p)::start - unable to open device for configuration\n", getName(), this);
        return false;
    }
    
    // 1.1 Set the configuration to the first config
    err = pUsbDev->SetConfiguration(this, cd->bConfigurationValue, true);
    if (err)
    {
        IOLog("%s(%p)::start - unable to set the configuration\n", getName(), this);
        pUsbDev->close(this);
        return false;
    }
    
    // 1.2 Get the status of the USB device (optional, for diag.)
    USBStatus status;
    err = pUsbDev->GetDeviceStatus(&status);
    if (err)
    {
        IOLog("%s(%p)::start - unable to get device status\n", getName(), this);
        pUsbDev->close(this);
        return false;
    } else IOLog("%s(%p)::start: device status %d\n", getName(), this, (int)status);

    // 2.0 Find the interface for bulk endpoint transfers
    IOUSBFindInterfaceRequest request;
    request.bInterfaceClass = kIOUSBFindInterfaceDontCare;
    request.bInterfaceSubClass = kIOUSBFindInterfaceDontCare;
    request.bInterfaceProtocol = kIOUSBFindInterfaceDontCare;
    request.bAlternateSetting = kIOUSBFindInterfaceDontCare;
    
    IOUSBInterface * intf = pUsbDev->FindNextInterface(NULL, &request);
    if (!intf) {
        IOLog("%s(%p)::start - unable to find interface\n", getName(), this);
        pUsbDev->close(this);
        return false;
    }

    // 2.1 Open the interface
    if (!intf->open(this))
    {
        IOLog("%s(%p)::start - unable to open interface\n", getName(), this);
        pUsbDev->close(this);
        return false;
    }

    // 2.2 Get info on endpoints (optional, for diag.)
    int numep = intf->GetNumEndpoints();
    IOLog("%s(%p)::start: interface has %d endpoints\n", getName(), this, numep);
    
    UInt8 transferType = 0;
    UInt16 maxPacketSize = 0;
    UInt8 interval = 0;
    err = intf->GetEndpointProperties(0, 0x02, kUSBOut, &transferType, &maxPacketSize, &interval);
    if (err) {
        IOLog("%s(%p)::start - failed to get endpoint 2 properties\n", getName(), this);
        intf->close(this);
        pUsbDev->close(this);
        return false;    
    } else IOLog("%s(%p)::start: EP2 %d %d %d\n", getName(), this, transferType, maxPacketSize, interval);
    
    err = intf->GetEndpointProperties(0, 0x01, kUSBIn, &transferType, &maxPacketSize, &interval);
    if (err) {
        IOLog("%s(%p)::start - failed to get endpoint 1 properties\n", getName(), this);
        intf->close(this);
        pUsbDev->close(this);
        return false;    
    } else IOLog("%s(%p)::start: EP1 %d %d %d\n", getName(), this, transferType, maxPacketSize, interval);


    // 2.3 Get the pipe for bulk endpoint 2 Out
    IOUSBPipe * pipe = intf->GetPipeObj(0x02);
    if (!pipe) {
        IOLog("%s(%p)::start - failed to find bulk out pipe\n", getName(), this);
        intf->close(this);
        pUsbDev->close(this);
        return false;    
    }
    /*  // TODO: Test the alternative way to do it:
     IOUSBFindEndpointRequest pipereq;
     pipereq.type = kUSBBulk;
     pipereq.direction = kUSBOut;
     pipereq.maxPacketSize = BULK_SIZE;
     pipereq.interval = 0;
     IOUSBPipe *pipe = intf->FindNextPipe(NULL, &pipereq);
     pipe = intf->FindNextPipe(pipe, &pipereq);
     if (!pipe) {
     IOLog("%s(%p)::start - failed to find bulk out pipe 2\n", getName(), this);
     intf->close(this);
     pUsbDev->close(this);
     return false;    
     }
     */

    
    // 3.0 Send request to Control Endpoint to initiate the firmware transfer
    IOUSBDevRequest ctlreq;
    ctlreq.bmRequestType = USBmakebmRequestType(kUSBOut, kUSBVendor, kUSBDevice);
    ctlreq.bRequest = USB_REQ_DFU_DNLOAD;
    ctlreq.wValue = 0;
    ctlreq.wIndex = 0;
    ctlreq.wLength = 20;
    ctlreq.pData = firmware_buf;

#if 0  // Trying to troubleshoot the problem after Restart (with OSBundleRequired Root)
    for (int irep = 0; irep < 5; irep++) { // retry on error
        err = pUsbDev->DeviceRequest(&ctlreq); // (synchronous, will block)
        if (err)
            IOLog("%s(%p)::start - failed to initiate firmware transfer (%d), retrying (%d)\n", getName(), this, err, irep+1);
        else
            break;
    }
#else
    err = pUsbDev->DeviceRequest(&ctlreq); // (synchronous, will block)
#endif
    if (err) {
        IOLog("%s(%p)::start - failed to initiate firmware transfer (%d)\n", getName(), this, err);
        intf->close(this);
        pUsbDev->close(this);
        return false;
    }

    // 3.1 Create IOMemoryDescriptor for bulk transfers
    char buftmp[BULK_SIZE];
    IOMemoryDescriptor * membuf = IOMemoryDescriptor::withAddress(&buftmp, BULK_SIZE, kIODirectionNone);
    if (!membuf) {
        IOLog("%s(%p)::start - failed to map memory descriptor\n", getName(), this);
        intf->close(this);
        pUsbDev->close(this);
        return false; 
    }
    err = membuf->prepare();
    if (err) {
        IOLog("%s(%p)::start - failed to prepare memory descriptor\n", getName(), this);
        intf->close(this);
        pUsbDev->close(this);
        return false; 
    }
    
    // 3.2 Send the rest of firmware to the bulk pipe
    char * buf = firmware_buf;
    int size = sizeof(firmware_buf); 
    buf += 20;
    size -= 20;
    int ii = 1;
    while (size) {
        int to_send = size < BULK_SIZE ? size : BULK_SIZE; 
        
        memcpy(buftmp, buf, to_send);
        err = pipe->Write(membuf, 10000, 10000, to_send);
        if (err) {
            IOLog("%s(%p)::start - failed to write firmware to bulk pipe (%d)\n", getName(), this, ii);
            intf->close(this);
            pUsbDev->close(this);
            return false; 
        }
        buf += to_send;
        size -= to_send;
        ii++;
    }
    IOLog("%s(%p)::start: firmware was sent to bulk pipe\n", getName(), this);
    
    err = membuf->complete();
    if (err) {
        IOLog("%s(%p)::start - failed to complete memory descriptor\n", getName(), this);
        intf->close(this);
        pUsbDev->close(this);
        return false; 
    }

    /*  // TODO: Test the alternative way to do it:
     IOMemoryDescriptor * membuf = IOMemoryDescriptor::withAddress(&firmware_buf[20], 246804-20, kIODirectionNone); // sizeof(firmware_buf)
     if (!membuf) {
     IOLog("%s(%p)::start - failed to map memory descriptor\n", getName(), this);
     intf->close(this);
     pUsbDev->close(this);
     return false; 
     }
     err = membuf->prepare();
     if (err) {
     IOLog("%s(%p)::start - failed to prepare memory descriptor\n", getName(), this);
     intf->close(this);
     pUsbDev->close(this);
     return false; 
     }
     
     //err = pipe->Write(membuf);
     err = pipe->Write(membuf, 10000, 10000, 246804-20, NULL);
     if (err) {
     IOLog("%s(%p)::start - failed to write firmware to bulk pipe\n", getName(), this);
     intf->close(this);
     pUsbDev->close(this);
     return false; 
     }
     IOLog("%s(%p)::start: firmware was sent to bulk pipe\n", getName(), this);
     */
    
    
    // 4.0 Get device status (it fails, but somehow is important for operational device)
    err = pUsbDev->GetDeviceStatus(&status);
    if (err)
    {
        IOLog("%s(%p)::start - unable to get device status\n", getName(), this);
        intf->close(this);
        pUsbDev->close(this);
        return false;
    } else IOLog("%s(%p)::start: device status %d\n", getName(), this, (int)status);


    // Close the interface
    intf->close(this);

    // Close the USB device
    pUsbDev->close(this);
    return false;  // return false to allow a different driver to load
}



void 
local_IOath3kfrmwr::stop(IOService *provider)
{
    IOLog("%s(%p)::stop\n", getName(), this);
    super::stop(provider);
}



bool 
local_IOath3kfrmwr::handleOpen(IOService *forClient, IOOptionBits options, void *arg )
{
    IOLog("%s(%p)::handleOpen\n", getName(), this);
    return super::handleOpen(forClient, options, arg);
}



void 
local_IOath3kfrmwr::handleClose(IOService *forClient, IOOptionBits options )
{
    IOLog("%s(%p)::handleClose\n", getName(), this);
    super::handleClose(forClient, options);
}



IOReturn 
local_IOath3kfrmwr::message(UInt32 type, IOService *provider, void *argument)
{
    IOLog("%s(%p)::message\n", getName(), this);
    switch ( type )
    {
        case kIOMessageServiceIsTerminated:
            if (pUsbDev->isOpen(this))
            {
                IOLog("%s(%p)::message - service is terminated - closing device\n", getName(), this);
//                pUsbDev->close(this);
            }
            break;
            
        case kIOMessageServiceIsSuspended:
        case kIOMessageServiceIsResumed:
        case kIOMessageServiceIsRequestingClose:
        case kIOMessageServiceWasClosed: 
        case kIOMessageServiceBusyStateChange:
        default:
            break;
    }
    
    return kIOReturnSuccess;
    return super::message(type, provider, argument);
}



bool 
local_IOath3kfrmwr::terminate(IOOptionBits options)
{
    IOLog("%s(%p)::terminate\n", getName(), this);
    return super::terminate(options);
}


bool 
local_IOath3kfrmwr::finalize(IOOptionBits options)
{
    IOLog("%s(%p)::finalize\n", getName(), this);
    return super::finalize(options);
}
