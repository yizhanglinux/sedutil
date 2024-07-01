/* C:B**************************************************************************
   This software is Copyright (c) 2014-2024 Bright Plaza Inc. <drivetrust@drivetrust.com>

   This file is part of sedutil.

   sedutil is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   sedutil is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with sedutil.  If not, see <http://www.gnu.org/licenses/>.

   * C:E********************************************************************** */


#include "log.h"
#include "fnmatch.h"
#include <IOKit/storage/IOMedia.h>
#include <SEDKernelInterface/SEDKernelInterface.h>

#include "DtaDevMacOSBlockStorageDevice.h"


bool DtaDevMacOSBlockStorageDevice::isDtaDevMacOSBlockStorageDeviceDevRef(const char * devref) {
    if (! (  ( 0 == fnmatch("/dev/disk[0-9]"     , devref, 0) )
           ||( 0 == fnmatch("/dev/disk[1-9][0-9]", devref, 0) ) ) )
        return false;

    const char * bsdName=devref+5; // After the "/dev/"
    io_registry_entry_t media = findBSDName(bsdName);
    io_registry_entry_t parent = media == IO_OBJECT_NULL ? IO_OBJECT_NULL : findParent(media);
    io_registry_entry_t grandparent = parent == IO_OBJECT_NULL ? IO_OBJECT_NULL : findParent(parent);
    
    bool result = grandparent != IO_OBJECT_NULL && conformsToBlockStorageDeviceClass(grandparent);
    
    IOObjectRelease(grandparent);
    IOObjectRelease(parent);
    IOObjectRelease(media);
    return result;
}



DtaDevMacOSBlockStorageDevice *
DtaDevMacOSBlockStorageDevice::getDtaDevMacOSBlockStorageDevice(const char * devref, DTA_DEVICE_INFO & /* device_info */) {

  bool accessDenied=false;
  OSDEVICEHANDLE osDeviceHandle = openAndCheckDeviceHandle(devref, accessDenied);
  if (osDeviceHandle==INVALID_HANDLE_VALUE || accessDenied)
    return NULL;

  LOG(D4) << "Success opening device " << devref << " as file handle " << HEXON(4) << (size_t) osDeviceHandle;


  return new DtaDevMacOSBlockStorageDevice(osDeviceHandle);
}


/** Send an ioctl to the device using pass through. */
uint8_t DtaDevMacOSBlockStorageDevice::sendCmd(ATACOMMAND /* cmd */, uint8_t /* protocol */, uint16_t /* comID */,
                                 void * /* buffer */, unsigned int /* bufferlen */)
{
  LOG(D4) << "Entering DtaDevMacOSBlockStorageDevice::sendCmd";


  LOG(D4) << "Returning 0xff from DtaDevMacOSBlockStorageDevice::sendCmd";
  return 0xff;
}


// Some macros to access the properties dicts and values
#define GetBool(dict,name) (CFBooleanRef)CFDictionaryGetValue(dict, CFSTR(name))
#define GetDict(dict,name) (CFDictionaryRef)CFDictionaryGetValue(dict, CFSTR(name))
#define GetData(dict,name) (CFDataRef)CFDictionaryGetValue(dict, CFSTR(name))
#define GetString(dict,name) (CFStringRef)CFDictionaryGetValue(dict, CFSTR(name))
#define GetNumber(dict,name) (CFNumberRef)CFDictionaryGetValue(dict, CFSTR(name))

#define ERRORS_TO_STDERR
//#undef ERRORS_TO_STDERR

static bool FillDeviceInfoFromProperties(CFDictionaryRef deviceProperties, CFDictionaryRef mediaProperties,
                                         DTA_DEVICE_INFO &device_info) {
    if (NULL != mediaProperties) {
        CFNumberRef size = GetNumber(mediaProperties, "Size");
        if (NULL != size) {
            CFNumberType numberType = CFNumberGetType(size);
            switch (numberType) {
                case kCFNumberLongLongType:
                    {
                        long long sSize ;
                        if (CFNumberGetValue(size, numberType, &sSize)) {
                            device_info.devSize = (uint64_t)sSize;
                        }
                    }
                    break;
                case kCFNumberSInt64Type:
                    {
                        int64_t sSize ;
                        if (CFNumberGetValue(size, numberType, &sSize)) {
                            device_info.devSize = (uint64_t)sSize;
                        }
                    }
                    break;
                default:
                    ;
            }
        }
    }
    if (NULL != deviceProperties ) {
        CFDictionaryRef deviceCharacteristics = GetDict(deviceProperties, "Device Characteristics");
        if (NULL != deviceCharacteristics) {
            CFStringRef vendorID = GetString(deviceCharacteristics, "Vendor ID");
            if (vendorID != NULL )
                CFStringGetCString(vendorID, (char *)&device_info.vendorID, sizeof(device_info.vendorID)+sizeof(device_info.vendorIDNull), kCFStringEncodingASCII);
            CFStringRef modelNumber = GetString(deviceCharacteristics, "Product Name");
            if (modelNumber != NULL)
                CFStringGetCString(modelNumber, (char *)&device_info.modelNum, sizeof(device_info.modelNum)+sizeof(device_info.modelNumNull), kCFStringEncodingASCII);
            CFStringRef firmwareRevision = GetString(deviceCharacteristics, "Product Revision Level");
            if (firmwareRevision != NULL)
                CFStringGetCString(firmwareRevision, (char *)&device_info.firmwareRev, sizeof(device_info.firmwareRev)+sizeof(device_info.firmwareRevNull), kCFStringEncodingASCII);
            CFStringRef serialNumber = GetString(deviceCharacteristics, "Serial Number");
            if (serialNumber != NULL )
                CFStringGetCString(serialNumber, (char *)&device_info.serialNum, sizeof(device_info.serialNum)+sizeof(device_info.serialNumNull), kCFStringEncodingASCII);

        }

        CFDictionaryRef protocolProperties = GetDict(deviceProperties, "Protocol Characteristics");
        if (NULL != protocolProperties) {
            CFStringRef interconnect = GetString(protocolProperties, "Physical Interconnect");
            if (NULL != interconnect) {
                CFStringGetCString(interconnect,
                                   (char *)device_info.physicalInterconnect,
                                   sizeof(device_info.physicalInterconnect)+sizeof(device_info.physicalInterconnectNull),
                                   kCFStringEncodingASCII);
            }
            CFStringRef interconnectLocation = GetString(protocolProperties, "Physical Interconnect Location");
            if (NULL != interconnectLocation) {
                CFStringGetCString(interconnectLocation,
                                   (char *)device_info.physicalInterconnectLocation,
                                   sizeof(device_info.physicalInterconnectLocation)+sizeof(device_info.physicalInterconnectLocationNull),
                                   kCFStringEncodingASCII);
            }
        }

        return true;
    }
    return false;
}

static
bool parse_properties_into_device_info(io_service_t deviceService, CFDictionaryRef & properties, DTA_DEVICE_INFO & device_info) {
    bool isPhysical = false;
    CFDictionaryRef deviceProperties = GetDict(properties, "device");
    if (NULL != deviceProperties) {

        CFDictionaryRef protocolProperties = GetDict(deviceProperties, "Protocol Characteristics");
        if (NULL != protocolProperties) {
            CFStringRef interconnect = GetString(protocolProperties, "Physical Interconnect");
            if (NULL != interconnect) {
                if (CFEqual(interconnect, CFSTR("USB"))) {
                    device_info.devType = DEVICE_TYPE_USB;
                    isPhysical = true;
                } else if (CFEqual(interconnect, CFSTR("Apple Fabric")) ||
                           CFEqual(interconnect, CFSTR("PCI-Express" ))) {
                    io_service_t controllerService = findParent(deviceService);
                    if (IOObjectConformsTo(deviceService, kIONVMeBlockStorageDevice) ||
                        IOObjectConformsTo(controllerService, kIONVMeBlockStorageDevice)) {
                        device_info.devType = DEVICE_TYPE_NVME;
                        isPhysical = true;
                    } else {
                        device_info.devType = DEVICE_TYPE_OTHER;
                        isPhysical = false; // ??? Just don't know how to handle this
                    }
                } else if (CFEqual(interconnect, CFSTR("SATA"))) {
                    device_info.devType = DEVICE_TYPE_ATA;
                    isPhysical = true;
                } else if (CFEqual(interconnect, CFSTR("Virtual Interface"))) {
                    device_info.devType = DEVICE_TYPE_OTHER;
                    isPhysical = false;
                } else {
                    device_info.devType = DEVICE_TYPE_OTHER;
                    isPhysical = false;
                }
            }
        }


    } else {
        device_info.devType = DEVICE_TYPE_OTHER; // TODO -- generalize for other devices when they are supported by BPTperDriver
        isPhysical = true;
    }

    CFDictionaryRef mediaProperties = GetDict(properties, "media");

    FillDeviceInfoFromProperties(deviceProperties, mediaProperties, device_info);

    return isPhysical;
}


// Create a copy of the properties of this I/O registry entry
// Receiver owns this CFMutableDictionary instance if not NULL
static
CFMutableDictionaryRef copyProperties(io_registry_entry_t service) {
    CFMutableDictionaryRef cfproperties = NULL;
    if (KERN_SUCCESS != IORegistryEntryCreateCFProperties(service,
                                                          &cfproperties,
                                                          CFAllocatorGetDefault(),
                                                          0)) {
        return NULL;
    }
    return cfproperties;
}

CFDictionaryRef createIOBlockStorageDeviceProperties(io_service_t deviceService) {

    CFDictionaryRef allProperties=NULL;

    CFDictionaryRef deviceProperties;
    CFDictionaryRef protocolCharacteristics;
    CFStringRef physicalInterconnectLocation;
    std::vector<const void *>keys;
    std::vector<const void *>values;
    io_service_t mediaService;
    CFDictionaryRef mediaProperties;
    io_service_t tPerService;
    CFDictionaryRef tPerProperties;

    deviceProperties = copyProperties( deviceService );
    if (NULL == deviceProperties) {
        goto finishedWithDevice;

    }
    protocolCharacteristics = GetDict(deviceProperties, "Protocol Characteristics");
    if (NULL == protocolCharacteristics) {
        goto finishedWithDevice;
    }
    physicalInterconnectLocation = GetString(protocolCharacteristics, "Physical Interconnect Location");
    if (NULL == physicalInterconnectLocation) {
        goto finishedWithDevice;
    }

    keys.push_back( CFSTR("device"));
    values.push_back( deviceProperties);

    mediaService = findServiceForClassInChildren(deviceService, kIOMediaClass);
    if (IO_OBJECT_NULL == mediaService) {
        goto finishedWithMedia;
    }
    mediaProperties = copyProperties( mediaService );
    if (NULL == mediaProperties ) {
        goto finishedWithMedia ;
    }
    keys.push_back( CFSTR("media"));
    values.push_back( mediaProperties);


    tPerService = findParent(deviceService);
    if (IOObjectConformsTo(tPerService, kDriverClass)) {
        tPerProperties = copyProperties( tPerService );
        keys.push_back( CFSTR("TPer"));
        values.push_back(tPerProperties);
    }
    IOObjectRelease(tPerService);


    allProperties = CFDictionaryCreate(CFAllocatorGetDefault(), keys.data(), values.data(),
                                       (CFIndex)keys.size(), NULL, NULL);


finishedWithMedia:
    IOObjectRelease(mediaService);

finishedWithDevice:
    return allProperties;
}

bool DtaDevMacOSBlockStorageDevice::BlockStorageDeviceUpdate(io_registry_entry_t deviceService, DTA_DEVICE_INFO &disk_info) {
    char name[128];
    kern_return_t kret=IORegistryEntryGetName(deviceService,name);
    if (kret != KERN_SUCCESS)
        return false;
    CFDictionaryRef properties = createIOBlockStorageDeviceProperties(deviceService);
    if (properties == NULL)
        return false;
    bool result = parse_properties_into_device_info(deviceService, properties, disk_info);
    CFRelease(properties);
    return result;
}

bool DtaDevMacOSBlockStorageDevice::identify(DTA_DEVICE_INFO& disk_info ) {
    return BlockStorageDeviceUpdate(handleDeviceService(osDeviceHandle), disk_info);
}

uint8_t DtaDevMacOSBlockStorageDevice::discovery0(DTA_DEVICE_INFO& disk_info ) {
    return BlockStorageDeviceUpdate(handleDeviceService(osDeviceHandle), disk_info) ? 0x00 : 0xff;
}





  /** Factory function to enumerate all the devrefs that pass the above filter
   *
   */
std::vector<std::string> DtaDevMacOSBlockStorageDevice::enumerateDtaDevMacOSBlockStorageDeviceDevRefs(bool & /* accessDenied */) {
    std::vector<DtaDevMacOSBlockStorageDevice *>devices;
    std::vector<std::string>device_names;
    io_iterator_t iterator = findBlockStorageDevices();
    io_service_t aBlockStorageDevice;
    io_service_t media;
    CFDictionaryRef deviceProperties;
    CFDictionaryRef mediaProperties;

    const CFIndex kCStringSize = 128;
    char nameBuffer[kCStringSize];
    memset(nameBuffer, 0, kCStringSize);

    // Iterate over nodes of class IOBlockStorageDevice or subclass thereof
    while ( (IO_OBJECT_NULL != (aBlockStorageDevice = IOIteratorNext( iterator )))) {

        CFDictionaryRef protocolCharacteristics;
        CFStringRef physicalInterconnectLocation;

        deviceProperties = copyProperties( aBlockStorageDevice );
        if (NULL == deviceProperties) {
            goto finishedWithDevice;
        }
        protocolCharacteristics = GetDict(deviceProperties, "Protocol Characteristics");
        if (NULL == protocolCharacteristics) {
            goto finishedWithDevice;
        }
        physicalInterconnectLocation = GetString(protocolCharacteristics, "Physical Interconnect Location");
        if (NULL == physicalInterconnectLocation ||
            CFEqual(physicalInterconnectLocation, CFSTR("File"))) {
            goto finishedWithDevice;
        }


        media = findServiceForClassInChildren(aBlockStorageDevice, kIOMediaClass);
        if (IO_OBJECT_NULL == media) {
            goto finishedWithMedia;
        }

        mediaProperties = copyProperties( media );
        if (NULL == mediaProperties ) {
            goto finishedWithMedia ;
        }

        memset(nameBuffer, 0, kCStringSize);
        CFStringGetCString(GetString(mediaProperties, "BSD Name"),
                           nameBuffer, kCStringSize, kCFStringEncodingUTF8);
        CFRelease(mediaProperties);

        device_names.push_back(nameBuffer);

    finishedWithMedia:
        IOObjectRelease(media);

    finishedWithDevice:
        IOObjectRelease(aBlockStorageDevice);
    }

    sort(device_names.begin(), device_names.end());
    return device_names;
}
