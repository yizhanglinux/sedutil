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
#pragma once
#include <string>
#include <stdlib.h>
#include "DtaDev.h"

/** virtual implementation for a disk interface-generic disk drive
 */
class DtaDevOSDrive {
public:

    DtaDevOSDrive(OSDEVICEHANDLE _osDeviceHandle)
    : osDeviceHandle(_osDeviceHandle)
    {};

    // The next two functions must be implemented in an OS-specific build
    // as pass-throughs since C++ does not have virtual static class functions.
    // For instance, isDtaDevOSDriveDefRef could just pass through to
    // isDtaDevLinuxDriveDefRef, which could then return true iff devref looked
    // like something the linux implementation would support.

  /** Factory function to look at the devref to filter out whether it could be a DtaDevOSDrive
   *
   * @param devref OS device reference e.g. "/dev/sda" on a POSIX-style system
   */
  static bool isDtaDevOSDriveDevRef(const char * devref, bool& accessDenied);

  /** Factory function to enumerate all the devrefs that pass the above filter
   *
   */
  static
  std::vector<std::string> enumerateDtaDevOSDriveDevRefs(bool & accessDenied);

  /** Factory function to look at the devref and create an instance of the appropriate subclass of
   *  DtaDevOSDrive
   *
   * @param devref OS device reference e.g. "/dev/sda" on a POSIX-style system
   * @param disk_info reference to DTA_DEVICE_INFO structure filled out during device identification
   */
  static DtaDevOSDrive * getDtaDevOSDrive(const char * devref,
                                          DTA_DEVICE_INFO & disk_info,
                                          bool& accessDenied);


  /** Method to send a command to the device
   * @param cmd command to be sent to the device
   * @param protocol security protocol to be used in the command
   * @param comID communications ID to be used
   * @param buffer input/output buffer
   * @param bufferlen length of the input/output buffer
   */
  virtual uint8_t sendCmd(ATACOMMAND cmd, uint8_t protocol, uint16_t comID,
                      void * buffer, unsigned int bufferlen) = 0;

  /** Routine to identify the device and fill out the device info structure.
   *
   * If it is an ATA device, perform an ATA Identify,
   * or if SCSI (SAS) , perform a SCSI Inquiry,
   * or if NVME, perform an NVMe Identify,
   * to fill out the disk_info structure
   * @param disk_info reference to the device info structure to fill out
   */
  virtual bool identify(DTA_DEVICE_INFO& disk_info) = 0;



  virtual uint8_t discovery0(DTA_DEVICE_INFO & di) = 0;


  virtual ~DtaDevOSDrive() {
    LOG(D4) << "Destroying DtaDevOSDrive";
    closeDrive();
  }


  static OSDEVICEHANDLE openDeviceHandle(const char* devref, bool& accessDenied);

  static void closeDeviceHandle(OSDEVICEHANDLE osDeviceHandle);

  virtual bool isOpen(void) { return ( osDeviceHandle != INVALID_HANDLE_VALUE ) ;}

protected:
  using base_type = DtaDevOSDrive;

  static OSDEVICEHANDLE openAndCheckDeviceHandle(const char* devref, bool& accessDenied);

  void closeDrive(void){
    if (isOpen()) {
      closeDeviceHandle(osDeviceHandle);
      osDeviceHandle = INVALID_HANDLE_VALUE;
    }
  }

public:  // *** TODO *** DEBUGGING *** this should just be protected
    OSDEVICEHANDLE osDeviceHandle = INVALID_HANDLE_VALUE;

};



template <typename T>
static inline void safecopy(T * dst, size_t dstsize, const T * src, size_t srcsize, const T fill = (T)(0))
{
  if (__is_all_NULs(src,srcsize)) return;  // Do not erase dst if src is all zeros

  if (dstsize<=srcsize)
    memcpy(dst,src,dstsize);
  else {
    memcpy(dst,src,srcsize);
    memset(dst+srcsize, fill, dstsize-srcsize);
  }
}



template <typename T>
static inline void softcopy(T * dst, size_t dstsize, const T * src, size_t srcsize, const T fill = (T)(0))
{
  if (__is_all_NULs(src,srcsize)) return;  // Do not erase dst if src is all zeros

  T * p = dst, * e = dst+dstsize;
  while (p<e && ((T)' ')==(*p)) p++;     // Skip all initial blanks
  while (p<e && ((T) 0 )==(*p)) p++;     // Skip all subsequent NULs
  if (p!=e) return;                      // Do not replace dst if it was non-empty, i.e. not just maybe some blanks followed by NULs

  if (dstsize<=srcsize)
    memcpy(dst,src,dstsize);
  else {
    memcpy(dst,src,srcsize);
    memset(dst+srcsize, fill, dstsize-srcsize);
  }
}
