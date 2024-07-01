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
#include "os.h"
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <scsi/sg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <linux/hdreg.h>
#include <errno.h>
#include <vector>
#include <fstream>
#include <map>
#include <fnmatch.h>
#include <linux/fs.h>
#include <systemd/sd-device.h>
//#include <systemd/device-util.h>
#define FOREACH_DEVICE_PROPERTY(device, key, value)                     \
        for (const char *value, *key = sd_device_get_property_first(device, &value); \
             key;                                                       \
             key = sd_device_get_property_next(device, &value))

#include "DtaStructures.h"
#include "DtaDevLinuxSata.h"
#include "DtaDevLinuxScsi.h"
#include "DtaHexDump.h"

//
// taken from <scsi/scsi.h> to avoid SCSI/ATA name collision
//



bool DtaDevLinuxScsi::isDtaDevLinuxScsiDevRef(const char * devref) {
  return 0 == fnmatch("/dev/sd[a-z]", devref, 0);
}


DtaDevLinuxScsi *
DtaDevLinuxScsi::getDtaDevLinuxScsi(const char * devref, DTA_DEVICE_INFO & di) {

  bool accessDenied=false;
  OSDEVICEHANDLE osDeviceHandle = openDeviceHandle(devref, accessDenied);
  if (osDeviceHandle == INVALID_HANDLE_VALUE)
    return NULL;

  LOG(D4) << "Success opening device " << devref << " as file handle " << handleDescriptor(osDeviceHandle);

  InterfaceDeviceID interfaceDeviceIdentification;
  memset(interfaceDeviceIdentification, 0, sizeof(interfaceDeviceIdentification));
  IFLOG(D4) {
    LOG(D4) << "Initially";
    LOG(D4) << "interfaceDeviceIdentification:";
    DtaHexDump((void *)&interfaceDeviceIdentification, (unsigned int)sizeof(interfaceDeviceIdentification));
    LOG(D4) << "di:";
    DtaHexDump((void *)&di, (unsigned int)sizeof(DTA_DEVICE_INFO));
  }

  if (! identifyUsingSCSIInquiry(osDeviceHandle, interfaceDeviceIdentification, di)) {
    LOG(E) << " Device " << devref << " is NOT Scsi?! -- file handle " << handleDescriptor(osDeviceHandle);
    LOG(D4) << "Closing device " << devref << " as file handle " << handleDescriptor(osDeviceHandle);
    di.devType = DEVICE_TYPE_OTHER;
    closeDeviceHandle(osDeviceHandle);
    return NULL;
  }

  IFLOG(D4) {
    LOG(D4) << "After identifyUsingSCSIInquiry";
    LOG(D4) << "interfaceDeviceIdentification:";
    DtaHexDump((void *)&interfaceDeviceIdentification, (unsigned int)sizeof(interfaceDeviceIdentification));
    LOG(D4) << "di:";
    DtaHexDump((void *)&di, (unsigned int)sizeof(DTA_DEVICE_INFO));
  }

  // TODO: should be just DtaDevLinuxSata
  dictionary * identifyCharacteristics = NULL;
  if (DtaDevLinuxSata::identifyUsingATAIdentifyDevice(osDeviceHandle,
                                                      interfaceDeviceIdentification,
                                                      di,
                                                      &identifyCharacteristics)) {

    // The completed identifyCharacteristics map could be useful to customizing code here
    if ( NULL != identifyCharacteristics ) {
      LOG(D3) << "identifyCharacteristics for ATA Device: ";
      for (dictionary::iterator it=identifyCharacteristics->begin(); it!=identifyCharacteristics->end(); it++)
        LOG(D3) << "  " << it->first << ":" << it->second << std::endl;
      delete identifyCharacteristics;
      identifyCharacteristics = NULL ;
    }

    LOG(D4) << " Device " << devref << " is Sata";
    di.devType = DEVICE_TYPE_SATA;
    return new DtaDevLinuxSata(osDeviceHandle);
  }

  // Even though the device is SAS,
  // the (possibly partially completed) identifyCharacteristics map could be useful to customizing code here
  // The completed identifyCharacteristics map could be useful to customizing code here
  if ( NULL != identifyCharacteristics ) {
    LOG(D3) << "identifyCharacteristics for SAS Device: ";
    for (dictionary::iterator it=identifyCharacteristics->begin(); it!=identifyCharacteristics->end(); it++)
      LOG(D3) << "  " << it->first << ":" << it->second << std::endl;
    delete identifyCharacteristics;
    identifyCharacteristics = NULL ;
  }

  LOG(D3) << "Device " << devref << " is Scsi (not Sata, assuming plain SAS)" ;
  di.devType = DEVICE_TYPE_SAS;
  return new DtaDevLinuxScsi(osDeviceHandle);
}


bool DtaDevLinuxScsi::identifyUsingSCSIInquiry(OSDEVICEHANDLE osDeviceHandle,
                                               InterfaceDeviceID & interfaceDeviceIdentification,
                                               DTA_DEVICE_INFO & disk_info) {


  if (!deviceIsStandardSCSI(osDeviceHandle, interfaceDeviceIdentification, disk_info)) {
    LOG(E) << " Device is not Standard SCSI -- not for this driver";
    return false;
  }



#define EXTRACT_INFORMATION_FROM_INQUIRY_VPD_PAGES
#define USE_INQUIRY_PAGE_00h
#define USE_INQUIRY_PAGE_80h


#if defined(EXTRACT_INFORMATION_FROM_INQUIRY_VPD_PAGES)
  // Extract information from Inquiry VPD pages
  //


  // Non-mandatory support
  bool deviceSupportsPage80=false;
  bool deviceSupportsPage89=false;


#if defined(USE_INQUIRY_PAGE_00h)
  if (deviceIsPage00SCSI(osDeviceHandle,
                         deviceSupportsPage80,
                         deviceSupportsPage89)) {
    LOG(D4) <<" Device is Page 00 SCSI";
    LOG(D4) << " Device " << (deviceSupportsPage80 ? "DOES" : "DOES NOT") << " support Page 80h";
  } else  {
    LOG(D4) <<" Device is not Page 00 SCSI";
#define ALLOW_INQUIRY_PAGE_00_FAILURES
#if defined( ALLOW_INQUIRY_PAGE_00_FAILURES )
    // Some external USB-SATA adapters do not support the VPD pages but it's OK
    // For instance, the Innostor Technology IS888 USB3.0 to SATA bridge identifies its
    // medium, not itself, in the Inquiry response, so we have no way of matching on it
    // short of delving into the USB world
    return true;  // ¯\_(ツ)_/¯
#else // !defined( ALLOW_INQUIRY_PAGE_00_FAILURES )
    return false;  // Mandatory, according to standard
#endif // defined( ALLOW_INQUIRY_PAGE_00_FAILURES )
  }
#endif // defined(USE_INQUIRY_PAGE_00h)

#define USE_INQUIRY_PAGE_80h
#if defined(USE_INQUIRY_PAGE_80h)
  if (deviceSupportsPage80) {
    if (deviceIsPage80SCSI(osDeviceHandle, interfaceDeviceIdentification, disk_info)) {
      LOG(D4) <<" Device is Page 80 SCSI";
    } else  {
      LOG(D4) <<" Device is not Page 80 SCSI";
      return false;  // Claims to support it on Page 00h, but does not
    }
  }
#endif // defined(USE_INQUIRY_PAGE_80h)

#if defined(USE_INQUIRY_PAGE_83h)
  if (deviceIsPage83SCSI(osDeviceHandle, disk_info)) {
    LOG(D4) <<" Device is Page 83 SCSI";
  } else  {
    LOG(D4) <<" Device is not Page 83 SCSI";
    return false;  // Mandatory, according to standard
  }
#endif // defined(USE_INQUIRY_PAGE_83h)


#if defined(USE_INQUIRY_PAGE_89h)
  if (deviceSupportsPage89) {
    if (deviceIsPage89SCSI(osDeviceHandle, disk_info)) {
      LOG(D4) <<" Device is Page 89 SCSI";
    } else  {
      LOG(D4) <<" Device is not Page 89 SCSI";
      return false;   // Claims to support it on page 00h, but does not
    }
  }
#if DRIVER_DEBUG
  else {
    LOG(D4) <<" Device does not claim to support Page 89 -- trying it anyway";
    if (deviceIsPage89SCSI(osDeviceHandle, disk_info)) {
      LOG(D4) <<" Device is Page 89 SCSI!!";
    }
  }
#endif
#endif // defined(USE_INQUIRY_PAGE_89h)

#if DRIVER_DEBUG
  deviceIsPageXXSCSI(kINQUIRY_PageB0_PageCode, IOInquiryPageB0ResponseKey);
  deviceIsPageXXSCSI(kINQUIRY_PageB1_PageCode, IOInquiryPageB1ResponseKey);
  deviceIsPageXXSCSI(kINQUIRY_PageB2_PageCode, IOInquiryPageB2ResponseKey);
  deviceIsPageXXSCSI(kINQUIRY_PageC0_PageCode, IOInquiryPageC0ResponseKey);
  deviceIsPageXXSCSI(kINQUIRY_PageC1_PageCode, IOInquiryPageC1ResponseKey);
#endif


#endif // defined(EXTRACT_INFORMATION_FROM_INQUIRY_VPD_PAGES)



  return true;
}



bool DtaDevLinuxScsi::deviceIsStandardSCSI(OSDEVICEHANDLE osDeviceHandle, InterfaceDeviceID & interfaceDeviceIdentification, DTA_DEVICE_INFO &di)
{
  // Test whether device is a SCSI drive by attempting
  // SCSI Inquiry command
  // If it works, as a side effect, parse the Inquiry response
  // and save it in the IO Registry
  bool isStandardSCSI = false;
  unsigned int transferSize = sizeof(CScsiCmdInquiry_StandardData);
  void * inquiryResponse =  alloc_aligned_MIN_BUFFER_LENGTH_buffer();
  if ( inquiryResponse != NULL ) {
    memset(inquiryResponse, 0, transferSize );
    isStandardSCSI = ( 0 == inquiryStandardDataAll_SCSI(osDeviceHandle,  inquiryResponse, transferSize ) );
    if (isStandardSCSI) {
      dictionary * inquiryCharacteristics =
        parseInquiryStandardDataAllResponse(static_cast <const unsigned char * >(inquiryResponse),
                                            interfaceDeviceIdentification,
                                            di);
      // Customization could use Inquiry characteristics
      if ( NULL != inquiryCharacteristics ) {
        LOG(D3) << "inquiryCharacteristics for Scsi Device: ";
        for (dictionary::iterator it=inquiryCharacteristics->begin(); it!=inquiryCharacteristics->end(); it++)
          LOG(D3) << "  " << it->first << ":" << it->second << std::endl;
        delete inquiryCharacteristics;
        inquiryCharacteristics = NULL ;
      }
    }
    free(inquiryResponse);
    inquiryResponse = NULL;
  }
  return isStandardSCSI;
}

int DtaDevLinuxScsi::inquiryStandardDataAll_SCSI(OSDEVICEHANDLE osDeviceHandle, void * inquiryResponse, unsigned int & dataSize )
{
  return __inquiry( osDeviceHandle, 0x00, 0x00, inquiryResponse, dataSize);
}



int DtaDevLinuxScsi::__inquiry(OSDEVICEHANDLE osDeviceHandle, uint8_t evpd, uint8_t page_code,
                               void * inquiryResponse, unsigned int & dataSize)
{
  static CScsiCmdInquiry cdb =
    { CScsiCmdInquiry::OPCODE , // m_Opcode
      0                       , // m_EVPD
      0x00                    , // m_Reserved_1
      0x00                    , // m_PageCode
      0x0000                  , // m_AllocationLength
      0x00                    , // m_Control
    };

  cdb.m_EVPD             = evpd;
  cdb.m_PageCode         = page_code;
  cdb.m_AllocationLength = htons(dataSize);
  unsigned char sense[32];
  unsigned char senselen=sizeof(sense);
  unsigned char masked_status;
  return PerformSCSICommand(osDeviceHandle,
                            PSC_FROM_DEV,
                            (uint8_t *)&cdb, sizeof(cdb),
                            inquiryResponse, dataSize,
                            sense, senselen,
                            &masked_status);
}

dictionary *
DtaDevLinuxScsi::parseInquiryStandardDataAllResponse(const unsigned char * response,
                                                     InterfaceDeviceID & interfaceDeviceIdentification,
                                                     DTA_DEVICE_INFO & di)
{
  const CScsiCmdInquiry_StandardData *resp = reinterpret_cast <const CScsiCmdInquiry_StandardData *>(response);

  memcpy(interfaceDeviceIdentification, resp->m_T10VendorId, sizeof(InterfaceDeviceID));

  safecopy(di.vendorID, sizeof(di.vendorID), resp->m_T10VendorId, sizeof(resp->m_T10VendorId));
  safecopy(di.firmwareRev, sizeof(di.firmwareRev), resp->m_ProductRevisionLevel, sizeof(resp->m_ProductRevisionLevel));
  safecopy(di.modelNum, sizeof(di.modelNum), resp->m_ProductId, sizeof(resp->m_ProductId));

  // device is apparently a SCSI disk
  return new dictionary
    {
      {"Device Type"       , "SCSI"                       },
      {"Vendor ID"         , (const char *)di.vendorID    },
      {"Model Number"      , (const char *)di.modelNum    },
      {"Firmware Revision" , (const char *)di.firmwareRev },
      {"Serial Number"     , (const char *)di.serialNum   },
    };
}




#if defined(USE_INQUIRY_PAGE_00h)
// #pragma mark -
// #pragma mark Inquiry Page 00h

bool DtaDevLinuxScsi::deviceIsPage00SCSI(OSDEVICEHANDLE osDeviceHandle,
                                         bool & deviceSupportsPage80,
                                         bool & deviceSupportsPage89)
{
  // Test whether device is a SCSI drive by attempting
  // SCSI Inquiry command
  // If it works, as a side effect, parse the Inquiry response
  // and save it in the IO Registry
  bool isPage00SCSI = false;

  // Mandatory support checked locally
  bool deviceSupportsPage00=false;
  bool deviceSupportsPage83=false;
  unsigned int transferSize = 260; // 256 possible page codes + 4 bytes header
  void * inquiryResponse = alloc_aligned_MIN_BUFFER_LENGTH_buffer();
  if ( NULL != inquiryResponse ) {
    memset(inquiryResponse, 0, transferSize);
    unsigned int dataSize = static_cast<unsigned int>(transferSize);
    isPage00SCSI = ( 0 == inquiryPage00_SCSI(osDeviceHandle, inquiryResponse, dataSize ) );
    if (isPage00SCSI) {
      dictionary * characteristics =
        parseInquiryPage00Response(static_cast <const unsigned char * >(inquiryResponse),
                                   deviceSupportsPage00,
                                   deviceSupportsPage80,
                                   deviceSupportsPage83,
                                   deviceSupportsPage89);
      if (!(deviceSupportsPage00 && deviceSupportsPage83)) {
#if defined(REPORT_NON_SUPPORTED_MANDATORY_VPD_PAGES)
        if (!deviceSupportsPage00) {
          LOG(E) << " Mandatory Inquiry VPD page code 00h support not indicated";
        }
        if (!deviceSupportsPage83) {
          LOG(E) << " Mandatory Inquiry VPD page code 83h support not indicated";
        }
#endif // defined(REPORT_NON_SUPPORTED_MANDATORY_VPD_PAGES)
        isPage00SCSI = false;
      }
      free(characteristics);
      characteristics = NULL ;
    }
    free_aligned_MIN_BUFFER_LENGTH_buffer(inquiryResponse);
  }
  LOG(D4) << "isPage00SCSI is " << (isPage00SCSI ? "true" : "false");
  return isPage00SCSI;
}


int DtaDevLinuxScsi::inquiryPage00_SCSI(OSDEVICEHANDLE osDeviceHandle, void * buffer, unsigned int & dataSize )
{
  return __inquiry__EVPD(osDeviceHandle, kINQUIRY_Page00_PageCode, buffer, dataSize);
}

dictionary * DtaDevLinuxScsi::parseInquiryPage00Response(const unsigned char * response,
                                                         bool & deviceSupportsPage00,
                                                         bool & deviceSupportsPage80,
                                                         bool & deviceSupportsPage83,
                                                         bool & deviceSupportsPage89)
{
  SCSICmd_INQUIRY_Page00_Header *resp = (SCSICmd_INQUIRY_Page00_Header *)response;

  LOG(D4) << " supported VPD page codes:" ;
  // TODO -- dump this buffer IOLOGBUFFER_DEBUG(NULL, 1+(&resp->PAGE_LENGTH), resp->PAGE_LENGTH);
  for (UInt8 *p = &resp->PAGE_LENGTH, * const pLast = p+*p; ++p<=pLast ;) {
    UInt8 pageCode = *p;
    switch (pageCode) {
    case kINQUIRY_Page00_PageCode:
      deviceSupportsPage00=true;
      LOG(D4) << "deviceSupportsPage00=true";
      break;
    case kINQUIRY_Page80_PageCode:
      deviceSupportsPage80=true;
      LOG(D4) << "deviceSupportsPage80=true";
      break;
    case kINQUIRY_Page83_PageCode:
      deviceSupportsPage83=true;
      LOG(D4) << "deviceSupportsPage83=true";
      break;
    case kINQUIRY_Page89_PageCode:
      deviceSupportsPage89=true;
      LOG(D4) << "deviceSupportsPage89=true";
      break;
    default:  // Others ignored
      LOG(D4) << "VPD page code " << HEXON(2) << (unsigned int)pageCode << " ignored";
      ;
    }
  }

  return new dictionary
    {
      {"Inquiry Page 00 Response" , "to-hex(resp) belongs here" /* vector<uint8_t>((const uint8_t *)resp, (const uint8_t *)(4+resp->PAGE_LENGTH)) */}
    };
}
#endif // defined(USE_INQUIRY_PAGE_00h)





#if defined(USE_INQUIRY_PAGE_80h)
// #pragma mark -
// #pragma mark Inquiry Page 80h

bool DtaDevLinuxScsi::deviceIsPage80SCSI(OSDEVICEHANDLE osDeviceHandle, const InterfaceDeviceID & interfaceDeviceIdentification,
                                         DTA_DEVICE_INFO &di)
{
  // Test whether device is a SCSI drive by attempting
  // SCSI Inquiry command
  // If it works, as a side effect, parse the Inquiry response
  // and save it in the IO Registry
  bool isPage80SCSI = false;
  unsigned int transferSize = 256;
  void * inquiryResponse = alloc_aligned_MIN_BUFFER_LENGTH_buffer();
  if (NULL != inquiryResponse) {
    memset(inquiryResponse, 0, transferSize );
    isPage80SCSI = ( 0 == inquiryPage80_SCSI(osDeviceHandle, inquiryResponse, transferSize ) );
    if (isPage80SCSI) {
      dictionary * characteristics =
        parseInquiryPage80Response(interfaceDeviceIdentification,
                                   static_cast <const unsigned char * >(inquiryResponse),
                                   di);
      delete characteristics ;
    }
    free_aligned_MIN_BUFFER_LENGTH_buffer(inquiryResponse);

  }
  LOG(D4) << "isPage80SCSI is " << (isPage80SCSI ? "true" : "false");
  return isPage80SCSI;
}


int DtaDevLinuxScsi::inquiryPage80_SCSI(OSDEVICEHANDLE osDeviceHandle, void * buffer, unsigned int & dataSize)
{
  return __inquiry__EVPD(osDeviceHandle, kINQUIRY_Page80_PageCode, buffer, dataSize);
}


int DtaDevLinuxScsi::__inquiry__EVPD(OSDEVICEHANDLE osDeviceHandle, uint8_t page_code,
                                     void * inquiryResponse, unsigned int & dataSize )
{
  return __inquiry(osDeviceHandle, 0x01, page_code, inquiryResponse, dataSize);
}



static void strrev(char *cString) {
  size_t n = strlen(const_cast<const char *>(cString)); // depend on NUL-termination
  if ( 1 < n ) {
    char temp, * p = cString , * q = p + n - 1 ;
    do {
      // Alternatively, *q ^= *p ; *p ^= *q ; *q ^= *p;
      temp = *p;
      *p = *q;
      *q = temp;
    } while (++p<--q) ;
  }
}

dictionary * DtaDevLinuxScsi::parseInquiryPage80Response(const InterfaceDeviceID & interfaceDeviceIdentification,
                                                         const unsigned char * response,
                                                         DTA_DEVICE_INFO & di)
{
  SCSICmd_INQUIRY_Page80_Header *resp = (SCSICmd_INQUIRY_Page80_Header *)response;

  uint8_t serialNumber[257];
  memset(serialNumber, 0, sizeof(serialNumber));
  memcpy(serialNumber, &resp->PRODUCT_SERIAL_NUMBER, resp->PAGE_LENGTH);
  memcpy(di.passwordSalt, serialNumber, sizeof(di.passwordSalt));  // save value before polishing
  if (deviceNeedsSpecialAction(interfaceDeviceIdentification,
                               reverseInquiryPage80SerialNumber)) {
    LOG(D4) << "*** reversing Inquiry Page80 serial number";
    LOG(D4) << "Inquiry Page80 serial number was " << serialNumber;
    strrev((char *)serialNumber);
  }
  LOG(D4) << "Inquiry Page80 serial number is " << serialNumber;
  memcpy(di.serialNum, serialNumber, sizeof(di.serialNum));


  return new dictionary
    {
      {"Serial Number", (const char *)serialNumber},
      {"Inquiry Page 80 Response" , "to-hex(resp) belongs here" /* vector<uint8_t>((const uint8_t *)resp, (const uint8_t *)(4+resp->PAGE_LENGTH)) */}
    };
}
#endif // defined(USE_INQUIRY_PAGE_80h)



/////////////////////////////////////////////////////////////////////////////////////////////////







/** Send an ioctl to the device using pass through. */
uint8_t DtaDevLinuxScsi::sendCmd(ATACOMMAND cmd, uint8_t protocol, uint16_t comID,
                                 void * buffer, uint32_t bufferlen)
{
  LOG(D4) << "Entering DtaDevLinuxScsi::sendCmd";


  int dxfer_direction;

  // initialize SCSI CDB and dxfer_direction
  uint8_t cdb[12];
  memset(&cdb, 0, sizeof (cdb));
  switch(cmd)
    {
    case IF_RECV:
      {
        dxfer_direction = PSC_FROM_DEV;
        CScsiCmdSecurityProtocolIn & p = * (CScsiCmdSecurityProtocolIn *) cdb;
        p.m_Opcode = CScsiCmdSecurityProtocolIn::OPCODE;
        p.m_SecurityProtocol = protocol;
        p.m_SecurityProtocolSpecific = htons(comID);
        p.m_INC_512 = 1;
        p.m_AllocationLength = htonl(bufferlen/512);
        break;
      }
    case IF_SEND:
      {
        dxfer_direction = PSC_TO_DEV;
        CScsiCmdSecurityProtocolOut & p = * (CScsiCmdSecurityProtocolOut *) cdb;
        p.m_Opcode = CScsiCmdSecurityProtocolIn::OPCODE;
        p.m_SecurityProtocol = protocol;
        p.m_SecurityProtocolSpecific = htons(comID);
        p.m_INC_512 = 1;
        p.m_TransferLength = htonl(bufferlen/512);
        break;
      }
    default:
      {
        LOG(D4) << "Unknown cmd=0x" << std::hex << cmd
                << " -- returning 0xff from DtaDevLinuxScsi::sendCmd";
        return 0xff;
      }
    }


  // execute I/O
  unsigned int transferlen = bufferlen;

  unsigned char sense[32]; // how big should this be??
  unsigned char senselen=sizeof(sense);
  memset(&sense, 0, senselen);

  unsigned char masked_status=GOOD;

  int result=PerformSCSICommand(dxfer_direction,
                                cdb, sizeof(cdb),
                                buffer, transferlen,
                                sense, senselen,
                                &masked_status);
  if (result < 0) {
    LOG(D4) << "cdb after ";
    IFLOG(D4) DtaHexDump(cdb, sizeof (cdb));
    LOG(D4) << "sense after ";
    IFLOG(D4) DtaHexDump(sense, senselen);
    LOG(D4) << "Error result=" << result << " from PerformSCSICommand "
            << " -- returning 0xff from DtaDevLinuxScsi::sendCmd";
    return 0xff;
  }

  // check for successful target completion
  if (masked_status != GOOD)
    {
      LOG(D4) << "cdb after ";
      IFLOG(D4) DtaHexDump(cdb, sizeof (cdb));
      LOG(D4) << "sense after ";
      IFLOG(D4) DtaHexDump(sense, senselen);
      LOG(D4) << "Masked_status = " << statusName(masked_status) << "!=GOOD "
              << "-- returning 0xff from DtaDevLinuxScsi::sendCmd";
      return 0xff;
    }

  // success
  LOG(D4) << "Returning 0x00 from DtaDevLinuxScsi::sendCmd";
  return 0x00;
}

bool  DtaDevLinuxScsi::identify(DTA_DEVICE_INFO& disk_info)
{
  InterfaceDeviceID interfaceDeviceIdentification;
  return identifyUsingSCSIInquiry(osDeviceHandle, interfaceDeviceIdentification, disk_info);
}


/** Send a SCSI command using an ioctl to the device. */

int DtaDevLinuxScsi::PerformSCSICommand(OSDEVICEHANDLE osDeviceHandle,
                                        int dxfer_direction,
                                        uint8_t * cdb,   unsigned char cdb_len,
                                        void * buffer,   unsigned int & bufferlen,
                                        unsigned char * sense, unsigned char & senselen,
                                        unsigned char * pmasked_status,
                                        unsigned int timeout)
{
  if (osDeviceHandle==INVALID_HANDLE_VALUE) {
    LOG(E) << "Scsi device not open";
    return EBADF;
  }

  sg_io_hdr_t sg;
  memset(&sg, 0, sizeof(sg));

  sg.interface_id = 'S';
  sg.dxfer_direction = dxfer_direction;  // We pun on dxfer_direction so no conversion
  sg.cmd_len = cdb_len;
  sg.mx_sb_len = senselen;
  sg.dxfer_len = bufferlen;
  sg.dxferp = buffer;
  sg.cmdp = cdb;
  sg.sbp = sense;
  sg.timeout = timeout;

  IFLOG(D4)
    if (dxfer_direction ==  PSC_TO_DEV) {
      LOG(D4) << "PerformSCSICommand buffer before";
      DtaHexDump(buffer,bufferlen);
    }


  /*
   * Do the IO
   */

  IFLOG(D4) {
    LOG(D4) << "PerformSCSICommand sg:" ;
    DtaHexDump(&sg, sizeof(sg));
    LOG(D4) << "cdb before:" ;
    DtaHexDump(cdb, cdb_len);
  }
  int result = ioctl(handleDescriptor(osDeviceHandle), SG_IO, &sg);
  LOG(D4) << "PerformSCSICommand ioctl result=" << result ;
  IFLOG(D4) {
    if (result < 0) {
      LOG(D4)
        << "cdb after ioctl returned " << result << " (" << strerror(result) << ")" ;
      DtaHexDump(cdb, cdb_len);
      if (sense != NULL) {
        LOG(D4) << "sense after ";
        DtaHexDump(sense, senselen);
      }
    }
  }

  // Without any real justification we set bufferlen to the value of dxfer_len - resid
  bufferlen = sg.dxfer_len - sg.resid;

  senselen = sg.sb_len_wr;

  if (pmasked_status != NULL) {
    *pmasked_status = sg.masked_status;
    IFLOG(D4) {
      if (*pmasked_status != GOOD) {
        LOG(D4)
          << "cdb after with masked_status == " << statusName(*pmasked_status)
          << " == " << std::hex << (int)sg.masked_status;
        DtaHexDump(cdb, cdb_len);
        if (sense != NULL) {
          LOG(D4) << "sense after ";
          DtaHexDump(sense, senselen);
        }
      }
    }
  }

  IFLOG(D4)
    if (dxfer_direction ==  PSC_FROM_DEV && 0==result && sg.masked_status == GOOD) {
      LOG(D4) << "PerformSCSICommand buffer after 0==result && sg.masked_status == GOOD:";
      DtaHexDump(buffer, bufferlen);
    }
  return result;
}
