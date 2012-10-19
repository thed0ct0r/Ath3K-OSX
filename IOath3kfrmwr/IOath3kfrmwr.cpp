/* Disclaimer: 
 This code is loosely based on the template of the class 
 in AnchorUSB Driver example from IOUSBFamily, 
 Open Source by Apple http://www.opensource.apple.com
 
 For information on driver matching for USB devices, see: 
 http://developer.apple.com/qa/qa2001/qa1076.html

 */
#include <IOKit/IOLib.h>
#include <IOKit/IOMessage.h>

#include <IOKit/usb/IOUSBDevice.h>
#include <IOKit/usb/IOUSBInterface.h>
#include <IOKit/usb/USB.h>

#include "IOath3kfrmwr.h"
#include "ath3k-1fw.h"

OSDefineMetaClassAndStructors(local_IOath3kfrmwr, IOService)
#define super IOService

#define USB_REQ_DFU_DNLOAD	1
#define CONTROL_PACKET_SIZE 20
#define BULK_SIZE	4096

#if !defined(MIN)
#define MIN(A,B)	({ __typeof__(A) __a = (A); __typeof__(B) __b = (B); __a < __b ? __a : __b; })
#endif

#if !defined(MAX)
#define MAX(A,B)	({ __typeof__(A) __a = (A); __typeof__(B) __b = (B); __a < __b ? __b : __a; })
#endif

bool local_IOath3kfrmwr::init(OSDictionary *propTable)
{
    IOLog("local_IOath3kfrmwr: init\n");
    return(super::init(propTable));
}

void local_IOath3kfrmwr::free(void)
{
    IOLog("local_IOath3kfrmwr: free\n");
    super::free();
}

IOService* local_IOath3kfrmwr::probe(IOService *provider, SInt32 *score)
{
    IOLog("%s(%p)::probe\n", getName(), this);
    return(super::probe(provider, score));
}

IOUSBInterface* local_IOath3kfrmwr::GetInterfaceWithBulkPipeOut(IOUSBDevice* pDeviceToSearch)
{
    IOUSBInterface* pInterfaceReturn = NULL;
    bool bFoundInterface = false;
    
    IOUSBFindInterfaceRequest requestFindInterface = {0};
    requestFindInterface.bAlternateSetting = kIOUSBFindInterfaceDontCare;
    requestFindInterface.bInterfaceClass = kIOUSBFindInterfaceDontCare;
    requestFindInterface.bInterfaceSubClass = kIOUSBFindInterfaceDontCare;
    requestFindInterface.bInterfaceProtocol = kIOUSBFindInterfaceDontCare;
    
    //iterate through the interfaces looking for our pipe
    while((pInterfaceReturn = pDeviceToSearch->FindNextInterface(pInterfaceReturn, &requestFindInterface)) != NULL)
    {
        //if we have a out bulk pipe then this is the right interface
        if (this->GetBulkPipeOutNumber(pInterfaceReturn) >= 0)
        {
            bFoundInterface = true;
            break;
        }
    }
    
    //if we couldn't find a bulk pipe - make sure we return null
    if (!bFoundInterface) pInterfaceReturn = NULL;
    
    return(pInterfaceReturn);
}

int local_IOath3kfrmwr::GetBulkPipeOutNumber(IOUSBInterface* pInterface)
{
    int iReturn = -1;
    
    int iNumberOfPipes = pInterface->GetNumEndpoints();
    if (iNumberOfPipes > 0)
    {
        for (int iPipeCounter = 0; iPipeCounter < iNumberOfPipes; iPipeCounter++)
        {
            IOUSBPipe* pPipeIterator = pInterface->GetPipeObj(iPipeCounter);
            if (pPipeIterator != NULL)
            {
                if (pPipeIterator->GetType() == kUSBBulk)
                {
                    if (pPipeIterator->GetDirection() == kUSBOut)
                    {
                        //stop and return this pipe #
                        iReturn = iPipeCounter;
                        break;
                    }
                }
                
                //cleanup
                //pPipeIterator->release();
            }
            else
            {
                IOLog("%s::%p::GetBulkPipeOutNumber -> could not open pipe #%d\n", this->getName(), this, iPipeCounter);
            }
        }
    }
    else
    {
        IOLog("%s::%p::GetBulkPipeOutNumber -> could not get number of pipes\n", this->getName(), this);
    }
    
    return(iReturn);
}

//
// start
// when this method is called, I have been selected as the driver for this device.
// I can still return false to allow a different driver to load
//
bool local_IOath3kfrmwr::start(IOService *provider)
{
    kern_return_t kResult = KERN_SUCCESS;
    
    //get the device
    IOUSBDevice* pDeviceRaw = OSDynamicCast(IOUSBDevice, provider);
    if (pDeviceRaw != NULL)
    {
        IOLog("%s::%p::start -> device cast\n", this->getName(), this);
        
        //opent the device
        kResult = pDeviceRaw->open(this);
        if (kResult != KERN_SUCCESS)
        {
            IOLog("%s::%p::start -> error opening device (%08x)\n", this->getName(), this, kResult);
        }
        else
        {
            IOLog("%s::%p::start -> device open\n", this->getName(), this);
            
            //reset the device to set the device for configuration
            kResult = pDeviceRaw->ResetDevice();
            if (kResult != KERN_SUCCESS)
            {
                IOLog("%s::%p::start -> error resetting device (%08x)\n", this->getName(), this, kResult);
            }
            else
            {
                IOLog("%s::%p::start -> device reset\n", this->getName(), this);
                
                //get the configuration descriptor so we can set the default one
                const IOUSBConfigurationDescriptor* pDeviceConfiguration = pDeviceRaw->GetFullConfigurationDescriptor(0);
                if (pDeviceConfiguration != NULL)
                {
                    IOLog("%s::%p::start -> device configuration recieved\n", this->getName(), this);
                    
                    //set the configuration for the device
                    kResult = pDeviceRaw->SetConfiguration(this, pDeviceConfiguration->bConfigurationValue);
                    if (kResult != KERN_SUCCESS)
                    {
                        IOLog("%s::%p::start -> error setting device configuration (%08x)\n", this->getName(), this,
                              kResult);
                    }
                    else
                    {
                        IOLog("%s::%p::start -> device configured\n", this->getName(), this);
                        
                        //get the interface with the bulk pipe out
                        IOUSBInterface* pInterfaceWithBulkPipeOut = this->GetInterfaceWithBulkPipeOut(pDeviceRaw);
                        if (pInterfaceWithBulkPipeOut != NULL)
                        {
                            //open the interface
                            if (!pInterfaceWithBulkPipeOut->open(this))
                            {
                                IOLog("%s::%p::start -> error opening interface (%08x)\n", this->getName(), this,
                                      kResult);
                            }
                            else
                            {
                                //get the bulk pipe number
                                int iBulkPipeOutNumber = this->GetBulkPipeOutNumber(pInterfaceWithBulkPipeOut);
                                if (iBulkPipeOutNumber >= 0)
                                {
                                    IOLog("%s::%p::start -> using bulk pipe #%d\n", this->getName(), this,
                                          iBulkPipeOutNumber);
                                    
                                    //get the pointer to the bulk pipe
                                    IOUSBPipe* pBulkPipe = pInterfaceWithBulkPipeOut->GetPipeObj(iBulkPipeOutNumber);
                                    if (pBulkPipe != NULL)
                                    {
                                        IOLog("%s::%p::start -> bulk pipe assigned\n", this->getName(), this);
                                        
                                        
                                        //start the actual transfer
                                        //stage 1: use the control request to set the device to receive
                                        //         and transfer the first 20 bytes from the firmware
                                        
                                        //set up parameters for the transfer
                                        int iFirmwareRemaining = sizeof(g_bytesFirmware);
                                        int iPosition = 0;
                                        int iTransferSize = 20;
                                        
                                        //set up memory - create a buffer in kernel io memory
                                        unsigned char* pBufferTransfer = (unsigned char*)::IOMalloc(BULK_SIZE);
                                        if (pBufferTransfer != NULL)
                                        {
                                            IOLog("%s::%p::start -> kernel io memory allocated\n", this->getName(), this);
                                            
                                            //copy firmware from global buffer to the kernel io memory
                                            ::memcpy(pBufferTransfer, g_bytesFirmware, iTransferSize);
                                            
                                            //create the request
                                            IOUSBDevRequest requestWriteFirmware;
                                            requestWriteFirmware.bmRequestType = USBmakebmRequestType(kUSBOut, kUSBVendor,
                                                                                                      kUSBDevice);
                                            requestWriteFirmware.bRequest = USB_REQ_DFU_DNLOAD;
                                            requestWriteFirmware.wIndex = 0;
                                            requestWriteFirmware.wValue = 0;
                                            requestWriteFirmware.wLength = iTransferSize;
                                            requestWriteFirmware.pData = pBufferTransfer;
                                            
                                            //send the request
                                            kResult = pDeviceRaw->DeviceRequest(&requestWriteFirmware, 10000, 10000);
                                            if (kResult != KERN_SUCCESS)
                                            {
                                                IOLog("%s::%p::start -> error sending control request (%08x)\n",
                                                      this->getName(), this, kResult);
                                            }
                                            else
                                            {
                                                IOLog("%s::%p::start -> control request sent\n", this->getName(), this);
                                                
                                                //update the counters
                                                iPosition += iTransferSize;
                                                iFirmwareRemaining -= iTransferSize;
                                                
                                                //set the memorydescriptor we will need in the bulk request
                                                IOMemoryDescriptor* pDescriptorBuffer = IOMemoryDescriptor::withAddress(pBufferTransfer,
                                                                                                                        BULK_SIZE,
                                                                                                                        kIODirectionNone);
                                                if (pDescriptorBuffer != NULL)
                                                {
                                                    kResult = pDescriptorBuffer->prepare();
                                                    if (kResult != KERN_SUCCESS)
                                                    {
                                                        IOLog("%s::%p::start -> error preparing io memory descriptor (%08x)\n", this->getName(),
                                                              this, kResult);
                                                    }
                                                    else
                                                    {
                                                        //loop through the rest of the firmware and transfer through the bulk pipe
                                                        while (iFirmwareRemaining > 0)
                                                        {
                                                            //is memcpy the right one to use???!?!
                                                            iTransferSize = MIN(iFirmwareRemaining, BULK_SIZE);
                                                            ::memcpy(pBufferTransfer, g_bytesFirmware + iPosition, iTransferSize);
                                                            
                                                            kResult = pBulkPipe->Write(pDescriptorBuffer, 10000, 10000, iTransferSize);
                                                            if (kResult != KERN_SUCCESS)
                                                            {
                                                                IOLog("%s::%p::start -> error writing to bulk pipe (%08x)\n", this->getName(),
                                                                      this, kResult);
                                                                
                                                                break;
                                                            }
                                                            else
                                                            {
                                                                iPosition += iTransferSize;
                                                                iFirmwareRemaining -= iTransferSize;
                                                            }
                                                        }
                                                        
                                                        pDescriptorBuffer->complete();
                                                        pDescriptorBuffer->release();
                                                    }
                                                }
                                                else
                                                {
                                                    IOLog("%s::%p::start -> error creating descriptor for io memory\n", this->getName(), this);
                                                }
                                                
                                                //check if we transferred everything
                                                if (iFirmwareRemaining <= 0)
                                                {
                                                    IOLog("%s::%p::start -> transfer successful\n", this->getName(), this);
                                                }
                                                else
                                                {
                                                    IOLog("%s::%p::start -> error: transfer failed, bytes remaining: %d, position %d\n",
                                                          this->getName(), this, iFirmwareRemaining, iPosition);
                                                }
                                            }
                                            
                                            //clean up - unallocate kernel io memory
                                            IOFree(pBufferTransfer, BULK_SIZE);
                                        }
                                        else
                                        {
                                            IOLog("%s::%p::start -> error allocating kernel io memory\n", this->getName(),
                                                  this);
                                        }
                                        
                                        //clean up - release the pipe
                                        //pBulkPipe->release();
                                    }
                                    else
                                    {
                                        IOLog("%s::%p::start -> could not assign bulk pipe\n", this->getName(), this);
                                    }
                                }
                                else
                                {
                                    IOLog("%s::%p::start -> error getting bulk pipe out #\n", this->getName(), this);
                                }
                                
                                //clean up
                                pInterfaceWithBulkPipeOut->close(this);
                                //pInterfaceWithBulkPipeOut->release();
                                IOLog("%s::%p::start -> interface closed\n", this->getName(), this);
                            }
                        }
                        else
                        {
                            IOLog("%s::%p::start -> error getting interface with bulk pipe\n", this->getName(), this);
                        }
                    }
                }
                else
                {
                    IOLog("%s::%p::start -> error getting configuration descriptor\n", this->getName(), this);
                }
            }
            
            //clean up
            pDeviceRaw->close(this);
            //pDeviceRaw->release();
            IOLog("%s::%p::start -> device closed\n", this->getName(), this);
        }
    }
    else IOLog("%s::%p::start -> error casting provider to usb device\n", this->getName(), this);
    
    return(false);
}



void local_IOath3kfrmwr::stop(IOService *provider)
{
    IOLog("%s(%p)::stop\n", getName(), this);
    super::stop(provider);
}

/*IOReturn local_IOath3kfrmwr::message(UInt32 type, IOService *provider, void *argument)
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
}*/
