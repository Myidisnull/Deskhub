package com.deskhub.app

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioPlaybackCaptureConfiguration
import android.media.AudioRecord
import android.media.projection.MediaProjection
import android.os.Build
import android.util.Log
import androidx.core.content.ContextCompat
import kotlin.concurrent.thread

object AudioShare {
    private const val TAG = "Deskhub"
    private const val SAMPLE_RATE = 48000
    private const val CHANNELS = 2
    private const val SAMPLES_PER_FRAME = 960
    private const val FRAME_SHORTS = SAMPLES_PER_FRAME * CHANNELS

    val isSupported: Boolean
        get() = Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q

    fun permissionGranted(context: Context): Boolean =
        ContextCompat.checkSelfPermission(context, Manifest.permission.RECORD_AUDIO) ==
            PackageManager.PERMISSION_GRANTED

    @Volatile
    private var record: AudioRecord? = null

    @Volatile
    private var running = false

    fun start(
        context: Context,
        projection: MediaProjection,
    ): Boolean {
        stop()
        if (!isSupported) {
            Log.i(TAG, "[audio] evt=capture_skip reason=android below 10 has no playback capture")
            return false
        }
        if (!permissionGranted(context)) {
            Log.i(TAG, "[audio] evt=capture_skip reason=record audio permission not granted")
            return false
        }

        val config =
            AudioPlaybackCaptureConfiguration
                .Builder(projection)
                .addMatchingUsage(AudioAttributes.USAGE_MEDIA)
                .addMatchingUsage(AudioAttributes.USAGE_GAME)
                .addMatchingUsage(AudioAttributes.USAGE_UNKNOWN)
                .build()

        val format =
            AudioFormat
                .Builder()
                .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                .setSampleRate(SAMPLE_RATE)
                .setChannelMask(AudioFormat.CHANNEL_IN_STEREO)
                .build()

        val minBytes =
            AudioRecord.getMinBufferSize(
                SAMPLE_RATE,
                AudioFormat.CHANNEL_IN_STEREO,
                AudioFormat.ENCODING_PCM_16BIT,
            )
        val bufferBytes = maxOf(minBytes, FRAME_SHORTS * Short.SIZE_BYTES * 4)

        val built =
            runCatching {
                AudioRecord
                    .Builder()
                    .setAudioFormat(format)
                    .setBufferSizeInBytes(bufferBytes)
                    .setAudioPlaybackCaptureConfig(config)
                    .build()
            }.getOrNull()

        if (built == null || built.state != AudioRecord.STATE_INITIALIZED) {
            Log.w(TAG, "[audio] evt=capture_open_fail backend=playback capture")
            built?.release()
            return false
        }

        record = built
        running = true
        built.startRecording()
        Log.i(TAG, "[audio] evt=capture_open backend=playback capture rate=$SAMPLE_RATE ch=$CHANNELS")

        thread(name = "deskhub-audio-capture", isDaemon = true) { pump(built) }
        return true
    }

    fun stop() {
        running = false
        val open = record ?: return
        record = null
        runCatching { open.stop() }
        open.release()
    }

    private fun pump(source: AudioRecord) {
        val frame = ShortArray(FRAME_SHORTS)
        while (running) {
            var filled = 0
            while (running && filled < FRAME_SHORTS) {
                val got = source.read(frame, filled, FRAME_SHORTS - filled)
                if (got <= 0) break
                filled += got
            }
            if (!running) break
            if (filled < FRAME_SHORTS) {
                java.util.Arrays.fill(frame, filled, FRAME_SHORTS, 0)
            }
            NativeHost.offerAudio(frame, FRAME_SHORTS)
        }
    }
}
