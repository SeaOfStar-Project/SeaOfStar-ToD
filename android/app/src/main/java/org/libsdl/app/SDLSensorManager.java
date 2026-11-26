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

import android.hardware.Sensor;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.util.Log;

// This class coordinates synchronized access to sensor manager registration
//
// This prevents a java.util.ConcurrentModificationException exception on
// Android 16, specifically on the Samsung Tab S9 Ultra.

class SDLSensorManager
{
    static private SDLSensorManager mManager = new SDLSensorManager();

    static final int RETRY_COUNT = 3;

    public static void registerListener(SensorManager manager, SensorEventListener listener, Sensor sensor, int samplingPeriodUs) {
        mManager.RegisterListener(manager, listener, sensor, samplingPeriodUs);
    }

    public static void unregisterListener(SensorManager manager, SensorEventListener listener, Sensor sensor) {
        mManager.UnregisterListener(manager, listener, sensor);
    }

    private synchronized void RegisterListener(SensorManager manager, SensorEventListener listener, Sensor sensor, int samplingPeriodUs) {
        int retries = 0;
        boolean complete = false;
        while (!complete) {
            try {
                manager.registerListener(listener, sensor, samplingPeriodUs, null);
                complete = true;
            } catch (java.util.ConcurrentModificationException e) {
                ++retries;
                if (retries <= RETRY_COUNT) {
                    // Sleep a bit and try again
                    try {
                        Thread.sleep(1);
                    } catch (Exception e2) {
                    }
                } else {
                    Log.v("SDL", "Multiple ConcurrentModificationException caught while registering sensor listener, canceling operation");
                    complete = true;
                }
            }
        }
    }

    private synchronized void UnregisterListener(SensorManager manager, SensorEventListener listener, Sensor sensor) {
        int retries = 0;
        boolean complete = false;
        while (!complete) {
            try {
                manager.unregisterListener(listener, sensor);
                complete = true;
            } catch (java.util.ConcurrentModificationException e) {
                ++retries;
                if (retries <= RETRY_COUNT) {
                    // Sleep a bit and try again
                    try {
                        Thread.sleep(1);
                    } catch (Exception e2) {
                    }
                } else {
                    Log.v("SDL", "Multiple ConcurrentModificationException caught while unregistering sensor listener, canceling operation");
                    complete = true;
                }
            }
        }
    }
}

