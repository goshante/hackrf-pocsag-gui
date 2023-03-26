#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_PagerSender.h"
#include "HackRFTransmitter.h"
#include <memory>
#include <qtimer.h>

class PagerSender : public QMainWindow
{
    Q_OBJECT
private:
    Ui::PagerSenderClass ui;
    QTimer m_txMonitor;
    bool m_busy;
    std::shared_ptr<HackRFTransmitter> m_tx;

    enum class StatusType
    {
        OK,
        Progress,
        Error,
        Info
    };

public:
    PagerSender(QWidget *parent = nullptr);
    ~PagerSender();

public slots:
    void PagerRIC_Changed(const QString& text);
    void Message_Changed();
    void Freq_Changed(const QString& text);
    void MsgType_Changed(int index);
    void SendMessage();
    void TXMonitorTick();

private:
    void StatusText(const QString& text, StatusType type);
    std::shared_ptr<HackRFTransmitter> tryCreateTransmitter(int mhz, int khz, int hz, int rfGain, float bandwidth, bool amp);
};
