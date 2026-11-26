/*
  This file is part of SDL 3.4.20 (https://www.libsdl.org/), copied unmodified from
  android-project/app/src/main/java/org/libsdl/app/ of the SDL release tarball.
  It carries SDL's own licence (zlib), reproduced here; see also LICENSE-SDL.txt.

  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>
  
  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.
  
  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:
  
  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
package org.libsdl.app;

import android.hardware.usb.UsbDevice;

interface HIDDevice
{
    public int getId();
    public int getVendorId();
    public int getProductId();
    public String getSerialNumber();
    public int getVersion();
    public String getManufacturerName();
    public String getProductName();
    public UsbDevice getDevice();
    public boolean open();
    public int writeReport(byte[] report, boolean feature);
    public boolean readReport(byte[] report, boolean feature);
    public void setFrozen(boolean frozen);
    public void close();
    public void shutdown();
}
