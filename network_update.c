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
#include "network_update.h"
#include "utils.h"

int network_update_thread(SceSize args, void *argp) {
  // Upstream packages and its embedded updater target the regular VITASHELL.
  return sceKernelExitDeleteThread(0);
}

int update_extract_thread(SceSize args, void *argp) {
  closeWaitDialog();
  infoDialog("Install VitaShell Sys updates manually using its VPK.");
  return sceKernelExitDeleteThread(0);
}
