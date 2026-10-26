#ifndef MEDIAPLAYERDIALOG_H
#define MEDIAPLAYERDIALOG_H

#include <QDialog>

class QMediaPlayer;
class QLabel;
class QPushButton;
class QSlider;

class MediaPlayerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit MediaPlayerDialog(const QString &filePath, QWidget *parent = nullptr);

private:
    void updatePlaybackButton();
    void updateTime();
    void showPlaybackError(const QString &message);
    QString m_filePath;
    QMediaPlayer *m_player = nullptr;
    QPushButton *m_playButton = nullptr;
    QSlider *m_progress = nullptr;
    QLabel *m_timeLabel = nullptr;
    QLabel *m_errorLabel = nullptr;
};

#endif
