/*
 * Copyright (C) 2023 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.mediatek.incallservice;

import android.media.AudioDeviceInfo;
import android.media.AudioManager;
import android.media.AudioSystem;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemProperties;
import android.util.Log;

public class GainUtils {
    public static final String LOG_TAG = "MtkInCallService";
    public static final int volSteps = SystemProperties.getInt("ro.config.vc_call_vol_steps", 7);

    // The framework also pushes AudioSystem.setVoiceVolume(float) on every change, which the MTK
    // HAL maps onto its own shorter scale. Whichever call lands last wins, so re-apply our index
    // a few times after each change to make sure the HAL ends up on it.
    private static final long[] REAPPLY_DELAYS_MS = {60, 200, 500};
    private static final Handler sHandler = new Handler(Looper.getMainLooper());
    private static final Object sReapplyToken = new Object();

    /**
     * Maps a STREAM_VOICE_CALL UI index (0..max) onto the HAL's 0..volSteps scale.
     */
    public static int toHalIndex(AudioManager audioManager, int uiIndex) {
        int max = audioManager.getStreamMaxVolume(AudioManager.STREAM_VOICE_CALL);
        if (max <= 0) {
            return Math.min(volSteps, uiIndex);
        }
        return Math.max(0, Math.min(volSteps, Math.round(uiIndex * (float) volSteps / max)));
    }

    /**
     * Sets the gain level for a given audio device.
     * @param audioDevice The audio device to set the gain level for.
     * @param gainIndex The gain level to set, already on the HAL's 0..volSteps scale.
     * @param streamType The stream type to set the gain level for.
     */
    public static void setGainLevel(int audioDevice, int gainIndex, int streamType) {
        String parameters = String.format("volumeDevice=%d;volumeIndex=%d;volumeStreamType=%d",
                                          audioDevice, Math.min(volSteps, gainIndex), streamType);
        Log.d(LOG_TAG, "Setting audio parameters to: " + parameters);
        AudioSystem.setParameters(parameters);
    }

    /**
     * Sets the gain level for built-in earpiece and bluetooth SCO devices.
     * @param gainIndex The gain level to set, already on the HAL's 0..volSteps scale.
     */
    public static void setGainLevel(int gainIndex) {
        GainUtils.setGainLevel(AudioDeviceInfo.TYPE_BUILTIN_EARPIECE, gainIndex, AudioSystem.STREAM_VOICE_CALL);
        GainUtils.setGainLevel(AudioDeviceInfo.TYPE_BLUETOOTH_SCO, gainIndex, AudioSystem.STREAM_VOICE_CALL);
    }

    /**
     * Applies the current voice call volume to the current communication device now and again
     * shortly afterwards, so it isn't overridden by the framework's setVoiceVolume().
     */
    public static void applyCurrentGain(AudioManager audioManager) {
        sHandler.removeCallbacksAndMessages(sReapplyToken);
        applyCurrentGainOnce(audioManager);
        for (long delay : REAPPLY_DELAYS_MS) {
            sHandler.postDelayed(() -> applyCurrentGainOnce(audioManager), sReapplyToken, delay);
        }
    }

    private static void applyCurrentGainOnce(AudioManager audioManager) {
        int halIndex = toHalIndex(audioManager,
                audioManager.getStreamVolume(AudioManager.STREAM_VOICE_CALL));
        AudioDeviceInfo callDevice = audioManager.getCommunicationDevice();
        if (callDevice != null) {
            setGainLevel(callDevice.getPort().type(), halIndex, AudioSystem.STREAM_VOICE_CALL);
        } else {
            setGainLevel(halIndex);
        }
    }
}
