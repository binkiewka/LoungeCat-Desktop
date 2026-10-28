#pragma once

#include <QObject>
#include <QByteArray>
#include <QThreadPool>
#include <atomic>
#include <memory>

namespace loungecat {

enum class SoundType {
    MENTION,         // 880.0 Hz, 150 ms
    PRIVATE_MESSAGE, // 660.0 Hz, 200 ms
    HIGHLIGHT,       // 1046.5 Hz, 100 ms
    CONNECT,         // 523.25 Hz, 300 ms
    DISCONNECT       // 392.0 Hz, 400 ms
};

class SoundService : public QObject {
    Q_OBJECT
public:
    static constexpr int SAMPLE_RATE = 44100;

    explicit SoundService(QObject* parent = nullptr);
    ~SoundService() override;

    static QByteArray synthesizePcm16Mono(double frequencyHz, int durationMs, float volume);
    static QByteArray wrapWavHeader(const QByteArray& pcmData, int sampleRate = SAMPLE_RATE);

    void play(SoundType type, float volume = 0.7f);
private:
    std::shared_ptr<std::atomic_bool> m_stopping = std::make_shared<std::atomic_bool>(false);
    QThreadPool m_pool;
};

} // namespace loungecat
