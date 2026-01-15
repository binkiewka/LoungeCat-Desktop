package com.loungecat.irc.service

import java.awt.Toolkit
import javax.sound.sampled.*
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/** Sound alert service for IRC events. Uses Java Sound API with system beep fallback. */
object SoundService {

    enum class SoundType {
        MENTION,
        PRIVATE_MESSAGE,
        HIGHLIGHT,
        CONNECT,
        DISCONNECT
    }

    private var enabled = false
    private var volume = 0.7f

    private val enabledSounds =
            mutableMapOf(
                    SoundType.MENTION to true,
                    SoundType.PRIVATE_MESSAGE to true,
                    SoundType.HIGHLIGHT to true,
                    SoundType.CONNECT to false,
                    SoundType.DISCONNECT to false
            )

    fun initialize(soundEnabled: Boolean = true, soundVolume: Float = 0.7f) {
        println(
                "[SoundService] initialize() called: soundEnabled=$soundEnabled, volume=$soundVolume"
        )
        enabled = soundEnabled
        volume = soundVolume.coerceIn(0f, 1f)
    }

    fun setEnabled(isEnabled: Boolean) {
        enabled = isEnabled
    }

    fun setVolume(newVolume: Float) {
        volume = newVolume.coerceIn(0f, 1f)
    }

    fun setSoundEnabled(type: SoundType, isEnabled: Boolean) {
        enabledSounds[type] = isEnabled
    }

    fun isSoundEnabled(type: SoundType): Boolean = enabledSounds[type] ?: false

    /**
     * Play sound for the given event type. Falls back to system beep if custom sounds aren't
     * available.
     */
    suspend fun play(type: SoundType) =
            withContext(Dispatchers.IO) {
                println(
                        "[SoundService] play() called for $type, enabled=$enabled, typeEnabled=${enabledSounds[type]}"
                )
                if (!enabled || enabledSounds[type] != true) {
                    println("[SoundService] Sound disabled, skipping")
                    return@withContext
                }

                try {
                    // Try to play a generated tone based on the event type
                    println("[SoundService] Attempting to play tone...")
                    playTone(type)
                    println("[SoundService] Tone played successfully")
                } catch (e: Exception) {
                    println("[SoundService] playTone failed: ${e.message}")
                    e.printStackTrace()
                    // Fallback to system beep
                    try {
                        println("[SoundService] Trying system beep fallback...")
                        Toolkit.getDefaultToolkit().beep()
                    } catch (e2: Exception) {
                        println("[SoundService] System beep also failed: ${e2.message}")
                    }
                }
            }

    /**
     * Plays a programmatically generated tone based on event type. Different frequencies and
     * durations for different events.
     */
    private fun playTone(type: SoundType) {
        val (frequency, durationMs) =
                when (type) {
                    SoundType.MENTION -> 880.0 to 150 // A5, short
                    SoundType.PRIVATE_MESSAGE -> 660.0 to 200 // E5, medium
                    SoundType.HIGHLIGHT -> 1046.5 to 100 // C6, very short
                    SoundType.CONNECT -> 523.25 to 300 // C5, longer
                    SoundType.DISCONNECT -> 392.0 to 400 // G4, longest, lower
                }

        val sampleRate = 44100f
        val numSamples = (sampleRate * durationMs / 1000).toInt()
        val samples = ByteArray(numSamples * 2) // 16-bit audio = 2 bytes per sample

        // Generate sine wave with fade in/out
        for (i in 0 until numSamples) {
            val time = i / sampleRate.toDouble()
            var amplitude = kotlin.math.sin(2.0 * kotlin.math.PI * frequency * time)

            // Apply volume
            amplitude *= volume

            // Apply fade in/out envelope (10% fade)
            val fadeLength = numSamples / 10
            val envelope =
                    when {
                        i < fadeLength -> i.toDouble() / fadeLength
                        i > numSamples - fadeLength -> (numSamples - i).toDouble() / fadeLength
                        else -> 1.0
                    }
            amplitude *= envelope

            // Convert to 16-bit sample
            val sample = (amplitude * Short.MAX_VALUE).toInt().toShort()
            samples[i * 2] = sample.toByte()
            samples[i * 2 + 1] = (sample.toInt() shr 8).toByte()
        }

        // Create audio format and play
        val format = AudioFormat(sampleRate, 16, 1, true, false)
        val info = DataLine.Info(SourceDataLine::class.java, format)

        println("[SoundService] Checking if audio line is supported...")
        if (!AudioSystem.isLineSupported(info)) {
            println("[SoundService] Audio line NOT supported, using beep")
            // Fall back to system beep if audio line not supported
            Toolkit.getDefaultToolkit().beep()
            return
        }

        println("[SoundService] Opening audio line...")
        val line = AudioSystem.getLine(info) as SourceDataLine
        line.open(format)
        println("[SoundService] Starting playback...")
        line.start()
        line.write(samples, 0, samples.size)
        line.drain()
        line.close()
        println("[SoundService] Playback complete")
    }

    /**
     * Play sound synchronously (blocking). Use this for testing or when coroutines aren't
     * available.
     */
    fun playBlocking(type: SoundType) {
        if (!enabled || enabledSounds[type] != true) return

        try {
            playTone(type)
        } catch (e: Exception) {
            try {
                Toolkit.getDefaultToolkit().beep()
            } catch (_: Exception) {
                // Silently ignore
            }
        }
    }
}
