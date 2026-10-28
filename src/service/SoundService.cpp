#include "service/SoundService.h"
#include <QtConcurrent>
#include <QtEndian>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <QElapsedTimer>
#include <QThread>

#if defined(HAVE_ALSA)
#include <alsa/asoundlib.h>
#endif

namespace loungecat {

SoundService::SoundService(QObject* parent)
    : QObject(parent) { m_pool.setMaxThreadCount(1); }

SoundService::~SoundService() {
    m_stopping->store(true);
    m_pool.clear();
    m_pool.waitForDone();
}

QByteArray SoundService::synthesizePcm16Mono(double frequencyHz, int durationMs, float volume) {
    if (durationMs <= 0 || durationMs > 2000 || !std::isfinite(frequencyHz) || !std::isfinite(volume)) return {};
    const int numSamples = (SAMPLE_RATE * durationMs) / 1000;
    const int fadeSamples = std::max(1, numSamples / 10);
    const double clampedVol = std::clamp(static_cast<double>(volume), 0.0, 1.0);
    constexpr double twoPi = 2.0 * 3.14159265358979323846;

    QByteArray pcm(numSamples * static_cast<int>(sizeof(int16_t)), Qt::Uninitialized);
    auto* samples = reinterpret_cast<int16_t*>(pcm.data());

    for (int i = 0; i < numSamples; ++i) {
        double envelope = 1.0;
        if (i < fadeSamples) {
            envelope = static_cast<double>(i) / static_cast<double>(fadeSamples);
        } else if (i > numSamples - fadeSamples) {
            envelope = static_cast<double>(numSamples - i) / static_cast<double>(fadeSamples);
        }

        const double angle = twoPi * frequencyHz * static_cast<double>(i) / static_cast<double>(SAMPLE_RATE);
        const double val = std::sin(angle) * 32767.0 * clampedVol * envelope;
        const int16_t sample = static_cast<int16_t>(std::clamp(std::llround(val), -32768LL, 32767LL));
        samples[i] = qToLittleEndian(sample);
    }

    return pcm;
}

QByteArray SoundService::wrapWavHeader(const QByteArray& pcmData, int sampleRate) {
    QByteArray wav;
    wav.reserve(44 + pcmData.size());
    auto appendU32 = [&](uint32_t v) {
        uint32_t le = qToLittleEndian(v);
        wav.append(reinterpret_cast<const char*>(&le), 4);
    };
    auto appendU16 = [&](uint16_t v) {
        uint16_t le = qToLittleEndian(v);
        wav.append(reinterpret_cast<const char*>(&le), 2);
    };

    wav.append("RIFF", 4);
    appendU32(static_cast<uint32_t>(36 + pcmData.size()));
    wav.append("WAVE", 4);
    wav.append("fmt ", 4);
    appendU32(16); // PCM chunk size
    appendU16(1);  // AudioFormat = 1 (PCM)
    appendU16(1);  // NumChannels = 1 (Mono)
    appendU32(static_cast<uint32_t>(sampleRate));
    appendU32(static_cast<uint32_t>(sampleRate * 2)); // ByteRate
    appendU16(2);  // BlockAlign
    appendU16(16); // BitsPerSample
    wav.append("data", 4);
    appendU32(static_cast<uint32_t>(pcmData.size()));
    wav.append(pcmData);
    return wav;
}

void SoundService::play(SoundType type, float volume) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_stopping->load() || volume <= 0 || !std::isfinite(volume)) return;
    if (m_pool.activeThreadCount() > 0) return;
    double freq = 880.0;
    int durationMs = 150;
    switch (type) {
        case SoundType::MENTION:
            freq = 880.0;
            durationMs = 150;
            break;
        case SoundType::PRIVATE_MESSAGE:
            freq = 660.0;
            durationMs = 200;
            break;
        case SoundType::HIGHLIGHT:
            freq = 1046.5;
            durationMs = 100;
            break;
        case SoundType::CONNECT:
            freq = 523.25;
            durationMs = 300;
            break;
        case SoundType::DISCONNECT:
            freq = 392.0;
            durationMs = 400;
            break;
    }

    const QByteArray pcm = synthesizePcm16Mono(freq, durationMs, volume);

    const auto stopping = m_stopping;
    auto* task = QRunnable::create([pcm, stopping]() {
#if defined(HAVE_ALSA)
        snd_pcm_t* handle = nullptr;
        if (snd_pcm_open(&handle, "default", SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK) >= 0 && handle) {
            if (snd_pcm_set_params(
                    handle,
                    SND_PCM_FORMAT_S16_LE,
                    SND_PCM_ACCESS_RW_INTERLEAVED,
                    1,
                    SAMPLE_RATE,
                    1,
                    50000
                ) >= 0) {
                const snd_pcm_uframes_t frames = pcm.size() / sizeof(int16_t);
                snd_pcm_uframes_t offset = 0;
                QElapsedTimer deadline;
                deadline.start();
                while (offset < frames && !stopping->load() && !deadline.hasExpired(1000)) {
                    const auto written = snd_pcm_writei(handle, pcm.constData() + offset * sizeof(int16_t), frames - offset);
                    if (written > 0) offset += static_cast<snd_pcm_uframes_t>(written);
                    else if (written == -EAGAIN) snd_pcm_wait(handle, 20);
                    else if (snd_pcm_recover(handle, static_cast<int>(written), 1) < 0) break;
                }
                snd_pcm_sframes_t remaining = 0;
                while (!stopping->load() && !deadline.hasExpired(1250) && snd_pcm_delay(handle, &remaining) == 0 && remaining > 0)
                    QThread::msleep(10);
                snd_pcm_drop(handle);
            }
            snd_pcm_close(handle);
        }
#endif
    });
    if (!m_pool.tryStart(task)) delete task;
}

} // namespace loungecat
