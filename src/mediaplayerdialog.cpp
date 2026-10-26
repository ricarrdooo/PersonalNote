#include "mediaplayerdialog.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaPlayer>
#include <QPushButton>
#include <QSlider>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoWidget>
#include <limits>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QAudioOutput>
#else
#include <QMediaContent>
#endif

namespace {
QString playbackTime(qint64 milliseconds)
{
    const qint64 seconds = qMax<qint64>(0, milliseconds) / 1000;
    const qint64 hours = seconds / 3600;
    const qint64 minutes = (seconds % 3600) / 60;
    const qint64 remainder = seconds % 60;
    if (hours)
        return QStringLiteral("%1:%2:%3").arg(hours).arg(minutes, 2, 10, QLatin1Char('0')).arg(remainder, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(minutes, 2, 10, QLatin1Char('0')).arg(remainder, 2, 10, QLatin1Char('0'));
}
}

MediaPlayerDialog::MediaPlayerDialog(const QString &filePath, QWidget *parent)
    : QDialog(parent), m_filePath(filePath)
{
    setWindowTitle(QStringLiteral("视频预览 · ") + QFileInfo(filePath).fileName());
    resize(920, 620);
    setMinimumSize(550, 380);
    setAttribute(Qt::WA_DeleteOnClose);
    setObjectName(QStringLiteral("mediaPlayerDialog"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(12);
    auto *fileLabel = new QLabel(QFileInfo(filePath).fileName(), this);
    fileLabel->setTextFormat(Qt::PlainText);
    fileLabel->setWordWrap(true);
    QFont titleFont = fileLabel->font();
    titleFont.setPointSize(12);
    titleFont.setWeight(QFont::DemiBold);
    fileLabel->setFont(titleFont);
    layout->addWidget(fileLabel);
    auto *video = new QVideoWidget(this);
    video->setMinimumHeight(240);
    video->setStyleSheet(QStringLiteral("background: #171b16;"));
    layout->addWidget(video, 1);
    m_errorLabel = new QLabel(this);
    m_errorLabel->setTextFormat(Qt::PlainText);
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setStyleSheet(QStringLiteral("color: #b17c68; padding: 8px;"));
    m_errorLabel->hide();
    layout->addWidget(m_errorLabel);
    m_progress = new QSlider(Qt::Horizontal, this);
    m_progress->setRange(0, 0);
    m_progress->setAccessibleName(QStringLiteral("视频播放进度"));
    layout->addWidget(m_progress);
    auto *controls = new QHBoxLayout;
    m_playButton = new QPushButton(QStringLiteral("播放"), this);
    m_playButton->setMinimumWidth(72);
    m_timeLabel = new QLabel(QStringLiteral("00:00 / 00:00"), this);
    controls->addWidget(m_playButton);
    controls->addWidget(m_timeLabel);
    controls->addStretch();
    auto *volumeLabel = new QLabel(QStringLiteral("音量"), this);
    auto *volume = new QSlider(Qt::Horizontal, this);
    volume->setRange(0, 100);
    volume->setValue(80);
    volume->setFixedWidth(95);
    volume->setAccessibleName(QStringLiteral("视频音量"));
    controls->addWidget(volumeLabel);
    controls->addWidget(volume);
    auto *external = new QPushButton(QStringLiteral("用系统播放器打开"), this);
    controls->addWidget(external);
    layout->addLayout(controls);
    auto *hint = new QLabel(QStringLiteral("视频格式的播放支持取决于系统已安装的解码器。"), this);
    hint->setStyleSheet(QStringLiteral("color: #929c87; font-size: 11px;"));
    layout->addWidget(hint);

    m_player = new QMediaPlayer(this);
    m_player->setVideoOutput(video);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    auto *audio = new QAudioOutput(this);
    audio->setVolume(0.8);
    m_player->setAudioOutput(audio);
    connect(volume, &QSlider::valueChanged, audio, [audio](int value) { audio->setVolume(value / 100.0); });
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this]() { updatePlaybackButton(); });
    connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &message) { showPlaybackError(message); });
    m_player->setSource(QUrl::fromLocalFile(filePath));
#else
    m_player->setVolume(80);
    connect(volume, &QSlider::valueChanged, m_player, &QMediaPlayer::setVolume);
    connect(m_player, &QMediaPlayer::stateChanged, this, [this]() { updatePlaybackButton(); });
    connect(m_player, QOverload<QMediaPlayer::Error>::of(&QMediaPlayer::error), this,
            [this](QMediaPlayer::Error) { showPlaybackError(m_player->errorString()); });
    m_player->setMedia(QMediaContent(QUrl::fromLocalFile(filePath)));
#endif
    connect(m_playButton, &QPushButton::clicked, this, [this]() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const bool playing = m_player->playbackState() == QMediaPlayer::PlayingState;
#else
        const bool playing = m_player->state() == QMediaPlayer::PlayingState;
#endif
        if (playing)
            m_player->pause();
        else
            m_player->play();
    });
    connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 duration) {
        m_progress->setRange(0, int(qMin<qint64>(duration, std::numeric_limits<int>::max())));
        updateTime();
    });
    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 position) {
        if (!m_progress->isSliderDown())
            m_progress->setValue(int(qMin<qint64>(position, std::numeric_limits<int>::max())));
        updateTime();
    });
    connect(m_progress, &QSlider::sliderMoved, this, [this](int position) { m_player->setPosition(position); });
    connect(external, &QPushButton::clicked, this, [this]() {
        m_player->pause();
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(m_filePath)))
            showPlaybackError(QStringLiteral("系统未能打开此文件，请为该格式设置默认播放器。"));
    });
    connect(this, &QDialog::finished, m_player, &QMediaPlayer::stop);
    m_player->play();
}

void MediaPlayerDialog::updatePlaybackButton()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const bool playing = m_player->playbackState() == QMediaPlayer::PlayingState;
#else
    const bool playing = m_player->state() == QMediaPlayer::PlayingState;
#endif
    m_playButton->setText(playing ? QStringLiteral("暂停") : QStringLiteral("播放"));
}

void MediaPlayerDialog::updateTime()
{
    m_timeLabel->setText(playbackTime(m_player->position()) + QStringLiteral(" / ") + playbackTime(m_player->duration()));
}

void MediaPlayerDialog::showPlaybackError(const QString &message)
{
    m_errorLabel->setText(QStringLiteral("暂时无法在应用内播放此视频。%1\n可以使用“用系统播放器打开”继续播放。")
                              .arg(message.trimmed().isEmpty() ? QString() : QStringLiteral("\n") + message));
    m_errorLabel->show();
}
