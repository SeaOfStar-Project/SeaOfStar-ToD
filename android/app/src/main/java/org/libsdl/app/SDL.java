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

import android.app.Activity;
import android.content.Context;

import java.lang.reflect.Method;

/**
    SDL library initialization
*/
public class SDL {

    // This function should be called first and sets up the native code
    // so it can call into the Java classes
    static public void setupJNI() {
        SDLActivity.nativeSetupJNI();
        SDLAudioManager.nativeSetupJNI();
        SDLControllerManager.nativeSetupJNI();
    }

    // This function should be called each time the activity is started
    static public void initialize() {
        setContext(null);

        SDLActivity.initialize();
        SDLAudioManager.initialize();
        SDLControllerManager.initialize();
    }

    // This function stores the current activity (SDL or not)
    static public void setContext(Activity context) {
        SDLAudioManager.setContext(context);
        mContext = context;
    }

    static public Activity getContext() {
        return mContext;
    }

    static void loadLibrary(String libraryName) throws UnsatisfiedLinkError, SecurityException, NullPointerException {
        loadLibrary(libraryName, mContext);
    }

    static void loadLibrary(String libraryName, Context context) throws UnsatisfiedLinkError, SecurityException, NullPointerException {

        if (libraryName == null) {
            throw new NullPointerException("No library name provided.");
        }

        try {
            // Let's see if we have ReLinker available in the project.  This is necessary for
            // some projects that have huge numbers of local libraries bundled, and thus may
            // trip a bug in Android's native library loader which ReLinker works around.  (If
            // loadLibrary works properly, ReLinker will simply use the normal Android method
            // internally.)
            //
            // To use ReLinker, just add it as a dependency.  For more information, see
            // https://github.com/KeepSafe/ReLinker for ReLinker's repository.
            //
            Class<?> relinkClass = context.getClassLoader().loadClass("com.getkeepsafe.relinker.ReLinker");
            Class<?> relinkListenerClass = context.getClassLoader().loadClass("com.getkeepsafe.relinker.ReLinker$LoadListener");
            Class<?> contextClass = context.getClassLoader().loadClass("android.content.Context");
            Class<?> stringClass = context.getClassLoader().loadClass("java.lang.String");

            // Get a 'force' instance of the ReLinker, so we can ensure libraries are reinstalled if
            // they've changed during updates.
            Method forceMethod = relinkClass.getDeclaredMethod("force");
            Object relinkInstance = forceMethod.invoke(null);
            Class<?> relinkInstanceClass = relinkInstance.getClass();

            // Actually load the library!
            Method loadMethod = relinkInstanceClass.getDeclaredMethod("loadLibrary", contextClass, stringClass, stringClass, relinkListenerClass);
            loadMethod.invoke(relinkInstance, context, libraryName, null, null);
        }
        catch (final Throwable e) {
            // Fall back
            try {
                System.loadLibrary(libraryName);
            }
            catch (final UnsatisfiedLinkError ule) {
                throw ule;
            }
            catch (final SecurityException se) {
                throw se;
            }
        }
    }

    protected static Activity mContext;
}
