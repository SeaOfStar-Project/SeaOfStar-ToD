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

import android.content.*;
import android.text.InputType;
import android.view.*;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;

/* This is a fake invisible editor view that receives the input and defines the
 * pan&scan region
 */
public class SDLDummyEdit extends View implements View.OnKeyListener
{
    InputConnection ic;
    int input_type;

    SDLDummyEdit(Context context) {
        super(context);
        setFocusableInTouchMode(true);
        setFocusable(true);
        setOnKeyListener(this);
    }

    void setInputType(int input_type) {
        this.input_type = input_type;
    }

    @Override
    public boolean onCheckIsTextEditor() {
        return true;
    }

    @Override
    public boolean onKey(View v, int keyCode, KeyEvent event) {
        return SDLActivity.handleKeyEvent(v, keyCode, event, ic);
    }

    //
    @Override
    public boolean onKeyPreIme (int keyCode, KeyEvent event) {
        // As seen on StackOverflow: http://stackoverflow.com/questions/7634346/keyboard-hide-event
        // FIXME: Discussion at http://bugzilla.libsdl.org/show_bug.cgi?id=1639
        // FIXME: This is not a 100% effective solution to the problem of detecting if the keyboard is showing or not
        // FIXME: A more effective solution would be to assume our Layout to be RelativeLayout or LinearLayout
        // FIXME: And determine the keyboard presence doing this: http://stackoverflow.com/questions/2150078/how-to-check-visibility-of-software-keyboard-in-android
        // FIXME: An even more effective way would be if Android provided this out of the box, but where would the fun be in that :)
        if (event.getAction()==KeyEvent.ACTION_UP && keyCode == KeyEvent.KEYCODE_BACK) {
            if (SDLActivity.mTextEdit != null && SDLActivity.mTextEdit.getVisibility() == View.VISIBLE) {
                SDLActivity.onNativeKeyboardFocusLost();
            }
        }
        return super.onKeyPreIme(keyCode, event);
    }

    @Override
    public InputConnection onCreateInputConnection(EditorInfo outAttrs) {
        ic = new SDLInputConnection(this, true);

        outAttrs.inputType = input_type;
        outAttrs.imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI |
                              EditorInfo.IME_FLAG_NO_FULLSCREEN /* API 11 */;

        return ic;
    }
}

