/*
 * Copyright (C) 2023 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */
 
package org.lineageos.mediatek.incallservice;

import android.content.Intent;
import android.content.Context;
import android.content.BroadcastReceiver;

import android.media.AudioManager;
import android.media.AudioSystem;
import android.media.AudioDeviceInfo;

import android.util.Log;

public class VolumeChangeReceiver extends BroadcastReceiver {
    public static final String LOG_TAG = "MediatekInCallService";

    private AudioManager mAudioManager;

    public VolumeChangeReceiver(AudioManager audioManager) {
        mAudioManager = audioManager;
    }

    private void handleVolumeStateChange(Intent intent) {
        if (intent.getIntExtra(AudioManager.EXTRA_VOLUME_STREAM_TYPE, -1) == AudioManager.STREAM_VOICE_CALL) {
            GainUtils.applyCurrentGain(mAudioManager);
        }
    }

    @Override
    public void onReceive(Context context, Intent intent) {
            handleVolumeStateChange(intent);
    }
}
