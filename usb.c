/*
  VitaShell
  Copyright (C) 2015-2018, TheFloW

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "main.h"
#include "usb.h"
#include "file.h"
#include "utils.h"

int mountGamecardUx0() {
  // This operation unmounts game storage and originally destroyed other apps.
  return VITASHELL_ERROR_SYSAPP_UNAVAILABLE;
}

int umountGamecardUx0() {
  // This operation unmounts game storage and originally destroyed other apps.
  return VITASHELL_ERROR_SYSAPP_UNAVAILABLE;
}

int mountUsbUx0() {
  // This operation unmounts game storage and originally destroyed other apps.
  return VITASHELL_ERROR_SYSAPP_UNAVAILABLE;
}

int umountUsbUx0() {
  // This operation unmounts game storage and originally destroyed other apps.
  return VITASHELL_ERROR_SYSAPP_UNAVAILABLE;
}

SceUID startUsb(const char *usbDevicePath, const char *imgFilePath, int type) {
  // This operation unmounts game storage and originally destroyed other apps.
  return VITASHELL_ERROR_SYSAPP_UNAVAILABLE;
}

int stopUsb(SceUID modid) {
  int res;

  // Stop USB storage
  res = sceUsbstorVStorStop();
  if (res < 0)
    return res;

  // Start MTP driver
  res = sceMtpIfStartDriver(1);
  if (res < 0)
    return res;

  // Stop and unload usbdevice module
  res = taiStopUnloadKernelModule(modid, 0, NULL, 0, NULL, NULL);
  if (res < 0)
    return res;

  // Remount Memory Card
  remount(0x800);

  // Remount imc0:
  if (checkFolderExist("imc0:"))
    remount(0xD00);

  // Remount xmc0:
  if (checkFolderExist("xmc0:"))
    remount(0xE00);

  // Remount uma0:
  if (checkFolderExist("uma0:"))
    remount(0xF00);

  return 0;
}
