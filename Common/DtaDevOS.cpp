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
#include <algorithm>
#include <time.h>
#include "DtaDevOS.h"
#include "DtaHexDump.h"



/** Factory method to produce instance of appropriate subclass
 *   Note that all of DtaDevGeneric, DtaDevEnterprise, DtaDevOpal, ... derive from DtaDevOS
 * @param devref             name of the device in the OS lexicon
 * @param pdev                reference into which to store the address of the new instance
 * @param genericIfNotTPer   if true, store an instance of DtaDevGeneric for non-TPers;
 *                           if false, store NULL for non-TPers
 */
// static
uint8_t DtaDevOS::getDtaDevOS(const char * devref,
                              DtaDevOS * * pdev,
                              bool genericIfNotTPer)
{
  // LOG(D4) << "DtaDevOS::getDtaDevOS(devref=\"" << devref << "\")";
  DTA_DEVICE_INFO device_info;
  memset(&device_info, 0, sizeof(device_info));

  bool accessDenied = false;

  DtaDevOSDrive * drive = DtaDevOSDrive::getDtaDevOSDrive(devref, device_info, accessDenied);
  if (drive == NULL || accessDenied) {
    *pdev = NULL;
    // LOG(D4) << "DtaDevOSDrive::getDtaDevOSDrive(\"" << devref <<  "\", disk_info) returned NULL";
    if (!accessDenied && !genericIfNotTPer) {
      // LOG(E) << "(From DtaDevOS::getDtaDevOS)";
      // LOG(E) << "Invalid or unsupported device " << devref;
    }
    // LOG(D4) << "DtaDevOS::getDtaDevOS(devref=\"" << devref << "\") returning DTAERROR_COMMAND_ERROR";
    if (accessDenied)
      return DTAERROR_DEVICE_ACCESS_DENIED;
    else
      return DTAERROR_COMMAND_ERROR;
  }

  *pdev =  getDtaDevOS(devref, drive, device_info, genericIfNotTPer) ;
  if (*pdev == NULL) {

    delete drive;

    LOG(D4) << "getDtaDevOS(" << "\"" << devref <<  "\"" << ", "
            << "drive"                 << ", "
            << "disk_info"             << ", "
            << ( genericIfNotTPer ? "true" : "false" )
            <<  ")"
            << " returned NULL";

    if (!accessDenied && !genericIfNotTPer) {
      // LOG(E) << "( DtaDevOS::getDtaDevOS)";
      LOG(E) << "Invalid or unsupported device " << devref;
    }
    if (accessDenied) {
      LOG(D4) << "DtaDevOS::getDtaDevOS(devref=\"" << devref << "\") returning DTAERROR_DEVICE_ACCESS_DENIED";
      return DTAERROR_DEVICE_ACCESS_DENIED;
    } else {
      LOG(D4) << "DtaDevOS::getDtaDevOS(devref=\"" << devref << "\") returning DTAERROR_COMMAND_ERROR";
      return DTAERROR_COMMAND_ERROR;
    }
  }


  LOG(D4) << "DtaDevOS::getDtaDevOS(devref=\"" << devref << "\") disk_info:";
  IFLOG(D4) DtaHexDump(&device_info, (int)sizeof(device_info));
  LOG(D4) << "DtaDevOS::getDtaDevOS(devref=\"" << devref << "\") returning DTAERROR_SUCCESS";
  return DTAERROR_SUCCESS;
}


/** The Device class represents a OS generic storage device.
 * At instantiation we determine if we create an instance of the NVMe or SATA or Scsi (SAS) derived class
 */

const unsigned long long DtaDevOS::getSize() { return disk_info.devSize; }

uint8_t DtaDevOS::sendCmd(ATACOMMAND cmd, uint8_t protocol, uint16_t comID,
                          void * buffer, unsigned int bufferlen)
{
  if (!isOpen) return DTAERROR_DEVICE_NOT_OPEN; //disk open failed so this will too

  if (NULL == drive)
    {
      LOG(E) << "DtaDevOS::sendCmd ERROR - unknown drive type";
      return DTAERROR_DEVICE_TYPE_UNKNOWN;
    }

  return drive->sendCmd(cmd, protocol, comID, buffer, bufferlen);
}

bool DtaDevOS::identify(DTA_DEVICE_INFO& disk_info)
{
  return drive->identify(disk_info)
    &&   DTAERROR_SUCCESS == drive->discovery0(disk_info);
}



int  DtaDevOS::diskScan()
{
  LOG(D1) << "Entering DtaDevOS:diskScan ";

  IFLOG(D1) {
    fprintf(Output2FILE::Stream(), "Scanning for TCG SWG compliant disks (loglevel=%d)\n", CLog::Level());
  } else {
    fprintf(Output2FILE::Stream(), "Scanning for Opal compliant disks\n");
  }

  bool accessDenied=false;
  vector<string> devRefs(DtaDevOSDrive::enumerateDtaDevOSDriveDevRefs(accessDenied));
  if (accessDenied) {
    LOG(E)  << "You do not have permission to access the raw device(s) in write mode";
    LOG(E)  << "Perhaps you might try to run as administrator";
    LOG(D1) << "Exiting DtaDevOS::scanDisk ";
    return DTAERROR_DEVICE_ACCESS_DENIED;
  }

  if (devRefs.size()!=0) {
    // Deal with device names being of various sizes in various OSes.  E.g. in Windows, a devRef might be
    // as long as "\\.\PhysicalDrive123" while on linux they might all be as short as "/dev/sda"

      vector<string>::iterator longest_devRef{max_element(devRefs.begin(), devRefs.end(),
                                                          [](string a, string b){
                                                            return a.length() < b.length();
                                                          })};
    size_t const longest_devRef_length{longest_devRef->length()};
    string column_header{" device "};
    size_t column_header_width{column_header.length()};
    size_t device_column_width{max(column_header_width, longest_devRef_length)};
    size_t const left_pad{(device_column_width - column_header_width) / 2};
    size_t const right_pad{device_column_width - column_header_width - left_pad};
    string const padded_column_header{string(left_pad, ' ') + column_header + string(right_pad, ' ')};
    string const padded_column_underline{string(device_column_width, '-')};


    IFLOG(D1) {
      string const padded_column_headers{string(left_pad, ' ') + column_header + string(right_pad, ' ')
                                         + " SSC        Model Number       Firmware Locn   World Wide Name        Serial Number     Vendor      Manufacturer Name\n"};
      string const padded_column_underlines{string(device_column_width, '-')
                                            + " --- ------------ ------------ -------- -----  ----- ---- -----   ---------- ---------  -------  --------------- -------\n"};

      fputs(padded_column_headers.c_str()    , Output2FILE::Stream());
      fputs(padded_column_underlines.c_str() , Output2FILE::Stream());
    }

    for (string & devref:devRefs) {
      //      LOG(E) << "Scanning \"" << devref << "\" ...";  // TODO: debugging

      DtaDevOS * dev=NULL;

      uint8_t result=getDtaDevOS(devref.c_str(),&dev,true);

      if (DTAERROR_SUCCESS == result && dev!=NULL) {

        fprintf(Output2FILE::Stream(), "%-*s", (int)device_column_width, devref.c_str());
        if (dev->isAnySSC()) {
          fprintf(Output2FILE::Stream(), " %s%s%s ",
                  (dev->isOpal1()  ? "1" : " "),
                  (dev->isOpal2()  ? "2" : " "),
                  (dev->isEprise() ? "E" : " "));
        } else {
          fprintf(Output2FILE::Stream(), "%s", " No  ");
        }

        const char * devType = NULL;
        switch (dev->getDevType()) {
        case DEVICE_TYPE_ATA:
          devType = "ATA";
          break;
        case DEVICE_TYPE_SAS:
          devType = "SAS";
          break;
        case DEVICE_TYPE_NVME:
          devType = "NVME";
          break;
        case DEVICE_TYPE_USB:
          devType = "USB";
          break;
        case DEVICE_TYPE_OTHER:
          devType = "OTHER";
          break;
        default:
          devType = "UNKWN";
        }

        IFLOG(D1) {
          char WWN[19]="                  ";  // 18 blanks as placeholder if missing
          vector<uint8_t>wwn(dev->getWorldWideName());
          if (__is_not_all_NULs(wwn.data(), (unsigned int)wwn.size())) {
            snprintf(WWN, 19, "%02X%02X%02X%02X%02X%02X%02X%02X %c",
                     wwn[0], wwn[1], wwn[2], wwn[3], wwn[4], wwn[5], wwn[6], wwn[7],
                     dev->isWorldWideNameSynthetic() ? '*' : ' ');
          }
          fprintf(Output2FILE::Stream(), "%-25.25s %-8.8s %-5.5s  %18s %-20.20s %-8.8s %-25.25s\n",
                  dev->getModelNum(),
                  dev->getFirmwareRev(),
                  devType,
                  WWN,
                  dev->getSerialNum(),
                  dev->getVendorID(),
                  dev->getManufacturerName());

        } else {
          fprintf(Output2FILE::Stream(), "%-25.25s %-8.8s %-5.5s\n",
                  dev->getModelNum(),
                  dev->getFirmwareRev(),
                  devType);
        }

        delete dev;
        dev=NULL;
      }

      //      LOG(E) << "... done scanning \"" << devref << "\"";  // TODO: debugging

    }
  }
  printf("No more disks present -- ending scan\n");
  LOG(D1) << "Exiting DtaDevOS::scanDisk ";
  return DTAERROR_SUCCESS;
}

/** Close the device reference so this object can be delete. */
DtaDevOS::~DtaDevOS()
{
  LOG(D4) << "Destroying DtaDevOS";
  if (NULL != drive)
    delete drive;
}
