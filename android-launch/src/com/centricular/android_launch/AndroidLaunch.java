/* GStreamer
 *
 * Copyright (C) 2014 Sebastian Dröge <sebastian@centricular.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

package com.centricular.android_launch;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.TimeZone;

import android.app.Activity;
import android.content.Context;
import android.os.Bundle;
import android.os.PowerManager;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.View.OnClickListener;
import android.widget.ImageButton;
import android.widget.SeekBar;
import android.widget.SeekBar.OnSeekBarChangeListener;
import android.widget.TextView;
import android.widget.Toast;

import org.freedesktop.gstreamer.GStreamer;

public class AndroidLaunch extends Activity implements SurfaceHolder.Callback, OnSeekBarChangeListener {
    private native void nativeInit();
    private native void nativeFinalize();
    private native void nativePlay();
    private native void nativePause();
    private native void nativeSeek(int milliseconds);
    private static native boolean nativeClassInit();
    private native void nativeSurfaceInit(Object surface);
    private native void nativeSurfaceFinalize();
    private long native_app_data;
    private PowerManager.WakeLock wake_lock;

    static {
        System.loadLibrary("gstreamer_android");
        System.loadLibrary("android_launch");
        nativeClassInit();
    }

    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);

        try {
            GStreamer.init(this);
        } catch (Exception e) {
            Toast.makeText(this, e.getMessage(), Toast.LENGTH_LONG).show();
            finish();
            return;
        }

        setContentView(R.layout.main);

        PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
        wake_lock = pm.newWakeLock(PowerManager.FULL_WAKE_LOCK, "Android Launch");
        wake_lock.setReferenceCounted(false);
        wake_lock.acquire();

        ImageButton play = (ImageButton) this.findViewById(R.id.button_play);
        play.setOnClickListener(new OnClickListener() {
            public void onClick(View v) {
                nativePlay();
            }
        });

        ImageButton pause = (ImageButton) this.findViewById(R.id.button_pause);
        pause.setOnClickListener(new OnClickListener() {
            public void onClick(View v) {
                nativePause();
            }
        });

        SurfaceView sv = (SurfaceView) this.findViewById(R.id.surface_video);
        SurfaceHolder sh = sv.getHolder();
        sh.addCallback(this);

        SeekBar sb = (SeekBar) this.findViewById(R.id.seek_bar);
        sb.setOnSeekBarChangeListener(this);

        this.findViewById(R.id.button_play).setEnabled(false);
        this.findViewById(R.id.button_pause).setEnabled(false);

        nativeInit();
    }

    protected void onDestroy() {
        nativeFinalize();
        super.onDestroy();
    }

    private void setMessage(final String message) {
        final TextView tv = (TextView) this.findViewById(R.id.textview_message);
        runOnUiThread (new Runnable() {
          public void run() {
            tv.setText(message);
          }
        });
    }

    private void onGStreamerInitialized () {
        Log.i ("GStreamer", "GStreamer initialized:");

        final Activity activity = this;
        runOnUiThread(new Runnable() {
            public void run() {
                activity.findViewById(R.id.button_play).setEnabled(true);
                activity.findViewById(R.id.button_pause).setEnabled(true);
            }
        });
    }

    private void updateTimeWidget () {
        final TextView tv = (TextView) this.findViewById(R.id.textview_time);
        final SeekBar sb = (SeekBar) this.findViewById(R.id.seek_bar);
        final int pos = sb.getProgress();
        final int max = sb.getMax();

        SimpleDateFormat df = new SimpleDateFormat("HH:mm:ss");
        df.setTimeZone(TimeZone.getTimeZone("UTC"));
        final String message = df.format(new Date (pos)) + " / " + df.format(new Date (max));
        tv.setText(message);
    }

    private void setCurrentPosition(final int position, final int duration) {
        final SeekBar sb = (SeekBar) this.findViewById(R.id.seek_bar);

        if (sb.isPressed()) return;

        runOnUiThread (new Runnable() {
          public void run() {
            sb.setMax(duration);
            sb.setProgress(position);
            updateTimeWidget();
          }
        });
    }

    public void surfaceChanged(SurfaceHolder holder, int format, int width,
            int height) {
        Log.d("GStreamer", "Surface changed to format " + format + " width "
                + width + " height " + height);
        nativeSurfaceInit (holder.getSurface());
    }

    public void surfaceCreated(SurfaceHolder holder) {
        Log.d("GStreamer", "Surface created: " + holder.getSurface());
    }

    public void surfaceDestroyed(SurfaceHolder holder) {
        Log.d("GStreamer", "Surface destroyed");
        nativeSurfaceFinalize ();
    }

    private void onMediaSizeChanged (int width, int height) {
        Log.i ("GStreamer", "Media size changed to " + width + "x" + height);
        final GStreamerSurfaceView gsv = (GStreamerSurfaceView) this.findViewById(R.id.surface_video);
        gsv.media_width = width;
        gsv.media_height = height;
        runOnUiThread(new Runnable() {
            public void run() {
                gsv.requestLayout();
            }
        });
    }

    public void onProgressChanged(SeekBar sb, int progress, boolean fromUser) {
        if (!fromUser) return;

        updateTimeWidget();
    }

    public void onStartTrackingTouch(SeekBar sb) {
    }

    public void onStopTrackingTouch(SeekBar sb) {
        nativeSeek(sb.getProgress());
    }
}
