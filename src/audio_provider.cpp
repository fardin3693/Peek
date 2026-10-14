#include "audio_provider.h"

#include "audio_preview.h"
#include "preview_util.h"

#include <QAudioOutput>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaPlayer>
#include <QPushButton>
#include <QSlider>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

namespace
{

using peek_util::formatMediaTime;

} // namespace

ProviderSupport audioProviderSupports(const QString &path)
{
    return isAudioFile(path) ? ProviderSupport::Yes : ProviderSupport::No;
}

bool renderAudioPreview(const QString &path, QLabel *statusLabel, QVBoxLayout *layout)
{
    if (statusLabel == nullptr || layout == nullptr) {
        return false;
    }
    // Parent views to the host widget owning the layout so Qt destroys them
    // with the window; the dispatch layer tracks them via object names.
    QWidget *host = peek_util::hostWidget(layout, statusLabel);
    // Streaming playback like video, but with no video output: QMediaPlayer
    // reads through the FFmpeg backend and never loads the whole file into
    // memory. Playback starts paused; the user opts in with the Play button.
    // The player is independent of the video player so the two backends can
    // never share state across repeated previews.
    auto *player = new QMediaPlayer(host);
    player->setObjectName(QStringLiteral("audioPlayer"));
    auto *audio = new QAudioOutput(host);
    player->setAudioOutput(audio);
    player->setSource(QUrl::fromLocalFile(path));

    auto *audioLabel = new QLabel(QStringLiteral("Audio preview"), host);
    audioLabel->setObjectName(QStringLiteral("audioLabel"));
    audioLabel->setAlignment(Qt::AlignCenter);
    audioLabel->setTextFormat(Qt::PlainText);
    audioLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    audioLabel->setMinimumSize(160, 120);

    auto *playButton = new QPushButton(QStringLiteral("Play"), host);
    playButton->setObjectName(QStringLiteral("playButton"));
    auto *seekSlider = new QSlider(Qt::Horizontal, host);
    seekSlider->setObjectName(QStringLiteral("seekSlider"));
    seekSlider->setRange(0, 0);
    auto *positionLabel =
        new QLabel(formatMediaTime(0) + QStringLiteral(" / ") + formatMediaTime(0), host);
    positionLabel->setObjectName(QStringLiteral("positionLabel"));
    positionLabel->setTextFormat(Qt::PlainText);

    auto *controls = new QWidget(host);
    controls->setObjectName(QStringLiteral("audioControls"));
    auto *controlsLayout = new QHBoxLayout(controls);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    controlsLayout->addWidget(playButton);
    controlsLayout->addWidget(seekSlider, 1);
    controlsLayout->addWidget(positionLabel);

    statusLabel->setText(QStringLiteral("Audio preview — paused."));

    QObject::connect(playButton, &QPushButton::clicked, host, [player] {
        if (player == nullptr) {
            return;
        }
        if (player->playbackState() == QMediaPlayer::PlayingState) {
            player->pause();
        } else {
            player->play();
        }
    });
    QObject::connect(player, &QMediaPlayer::playbackStateChanged, host,
                     [playButton](QMediaPlayer::PlaybackState state) {
                         if (playButton != nullptr) {
                             playButton->setText(state == QMediaPlayer::PlayingState
                                                     ? QStringLiteral("Pause")
                                                     : QStringLiteral("Play"));
                         }
                     });
    QObject::connect(player, &QMediaPlayer::durationChanged, host,
                     [player, seekSlider, positionLabel](qint64 duration) {
                         if (seekSlider != nullptr) {
                             seekSlider->setRange(0, peek_util::clampMsForSlider(duration));
                         }
                         if (positionLabel != nullptr && player != nullptr) {
                             positionLabel->setText(formatMediaTime(player->position()) +
                                                    QStringLiteral(" / ") +
                                                    formatMediaTime(duration));
                         }
                     });
    QObject::connect(player, &QMediaPlayer::positionChanged, host,
                     [player, seekSlider, positionLabel](qint64 position) {
                         if (player == nullptr) {
                             return;
                         }
                         if (seekSlider != nullptr && !seekSlider->isSliderDown()) {
                             seekSlider->setValue(peek_util::clampMsForSlider(position));
                         }
                         if (positionLabel != nullptr) {
                             positionLabel->setText(formatMediaTime(position) +
                                                    QStringLiteral(" / ") +
                                                    formatMediaTime(player->duration()));
                         }
                     });
    QObject::connect(seekSlider, &QSlider::sliderMoved, host, [player](int value) {
        if (player != nullptr) {
            player->setPosition(static_cast<qint64>(value));
        }
    });
    // Unsupported codecs, missing backends, and invalid files report through
    // the audio status line. The controls stay in place so the failure never
    // swaps in an unrelated preview.
    QObject::connect(player, &QMediaPlayer::errorOccurred, host,
                     [statusLabel](QMediaPlayer::Error, const QString &errorString) {
                         QString detail = errorString.trimmed();
                         if (detail.isEmpty()) {
                             detail = QStringLiteral("unreadable or unsupported");
                         }
                         statusLabel->setText(
                             QStringLiteral("Could not play this audio (%1).").arg(detail));
                     });

    layout->addWidget(audioLabel, 1);
    layout->addWidget(controls);
    return true;
}

const PreviewProvider &audioProvider()
{
    static const PreviewProvider provider = {"audio", &audioProviderSupports, &renderAudioPreview};
    return provider;
}
