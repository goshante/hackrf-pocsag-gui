#include "PagerSender.h"
#include <QIntValidator>
#include <qdebug.h>
#include <qregexp.h>
#include <cmath>

#include "POCSAG.h"
#include "HackRF_PCMSource.h"

constexpr const int RIC_MAX = 2097151;
constexpr const int MESSAGE_MAX = 192;

using namespace std::chrono_literals;

PagerSender::PagerSender(QWidget *parent)
    : QMainWindow(parent)
    , m_busy(false)
{
    ui.setupUi(this);
    setWindowFlags(Qt::Dialog | Qt::MSWindowsFixedSizeDialogHint);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    ui.pagerRIC->setValidator(new QIntValidator(0, RIC_MAX, this));
    ui.freq->setValidator(new QRegExpValidator(QRegExp("^[0-9]{0,4}\\.{1,1}[0-9]{0,4}$"), this));
    
    ui.freq->setText("144.5000");
    ui.pagerRIC->setText("0000000");

    ui.messageType->addItem("Alphanumeric");
    ui.messageType->addItem("Numeric");
    ui.messageType->addItem("Tone");
    ui.messageType->setCurrentIndex(0);

    ui.msgFunc->addItem("A (00)");
    ui.msgFunc->addItem("B (01)");
    ui.msgFunc->addItem("C (10)");
    ui.msgFunc->addItem("D (11)");
    ui.msgFunc->setCurrentIndex(0);

    ui.msgBitrate->addItem("512 bps");
    ui.msgBitrate->addItem("1200 bps");
    ui.msgBitrate->addItem("2400 bps");
    ui.msgBitrate->setCurrentIndex(0);

    ui.msgCharset->addItem("Raw text");
    ui.msgCharset->addItem("Latin");
    ui.msgCharset->addItem("Cyrilic");
    ui.msgCharset->setCurrentIndex(1);

    ui.bandwidth->addItem("4.5 KHz");
    ui.bandwidth->addItem("9 KHz");
    ui.bandwidth->addItem("10 KHz");
    ui.bandwidth->addItem("15 KHz");
    ui.bandwidth->addItem("25 KHz");
    ui.bandwidth->addItem("50 KHz");
    ui.bandwidth->setCurrentIndex(4);

    ui.dateTime->addItem("None");
    ui.dateTime->addItem("Begin");
    ui.dateTime->addItem("End");
    ui.dateTime->setCurrentIndex(0);

    ui.rfGain->addItem("Low");
    ui.rfGain->addItem("Medium");
    ui.rfGain->addItem("High");
    ui.rfGain->setCurrentIndex(2);

    ui.useAmp->setChecked(true);
    
    auto pal = ui.charCount->palette();
    pal.setColor(QPalette::Light, QColor(255, 0, 0));
    ui.charCount->setPalette(pal);

    connect(ui.pagerRIC, &QLineEdit::textChanged, this, &PagerSender::PagerRIC_Changed, Qt::QueuedConnection);
    connect(ui.messageText, &QPlainTextEdit::textChanged, this, &PagerSender::Message_Changed, Qt::QueuedConnection);
    connect(ui.freq, &QLineEdit::textChanged, this, &PagerSender::Freq_Changed, Qt::QueuedConnection);
    connect(ui.messageType, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PagerSender::MsgType_Changed, Qt::QueuedConnection);
    connect(ui.send, &QPushButton::released, this, &PagerSender::SendMessage, Qt::QueuedConnection);
    connect(&m_txMonitor, &QTimer::timeout, this, &PagerSender::TXMonitorTick, Qt::QueuedConnection);
}

PagerSender::~PagerSender()
{
}

void PagerSender::Message_Changed()
{
    static QString prev;
    QRegExp re("^[0-9\\-\\*uU\\[\\]\\(\\)$\\s\\n]*$");
    if (ui.messageText->toPlainText().length() <= MESSAGE_MAX && (ui.messageType->currentIndex() != 1 || re.exactMatch(ui.messageText->toPlainText())))
    {
        prev = ui.messageText->toPlainText();
        ui.charCount->display(ui.messageText->toPlainText().length());
    }
    else
    {
        ui.messageText->blockSignals(true);
        ui.messageText->setPlainText(prev);
        ui.messageText->blockSignals(false);
    }
}

void PagerSender::PagerRIC_Changed(const QString& text)
{
    static QString prev;

    if (!m_busy)
    {
        if (text.isEmpty() || ui.freq->text().isEmpty())
            ui.send->setEnabled(false);
        else
            ui.send->setEnabled(true);
    }

    if (text.toInt() <= RIC_MAX)
        prev = text;
    else
    {
        ui.pagerRIC->blockSignals(true);
        ui.pagerRIC->setText(prev);
        ui.pagerRIC->blockSignals(false);
    }

}

void PagerSender::Freq_Changed(const QString& text)
{
    static QString prev;
    QString begin;
    int dot = text.indexOf('.');

    if (!m_busy)
    {
        if (text.isEmpty() || ui.pagerRIC->text().isEmpty())
            ui.send->setEnabled(false);
        else
            ui.send->setEnabled(true);
    }


    if (dot == -1)
        begin = text.mid(0, text.length());
    else
        begin = text.mid(0, dot);

    if (text.endsWith('.'))
    {
        prev = text.mid(0, dot);
        goto freq_reset;
    }
    if (begin.length() < 4 && text.length() <= (3 + 1 + 4))
        prev = text;
    else if (!text.contains('.') && begin.length() > 3)
    {
        prev = begin.mid(0, 3) + "." + text.mid(3, text.length() - 3);
        goto freq_reset;
    }
    else
    {
    freq_reset:
        ui.freq->blockSignals(true);
        ui.freq->setText(prev);
        ui.freq->blockSignals(false);
    }
}

void PagerSender::MsgType_Changed(int index)
{
    if (index == 2)
    {
        ui.messageText->setEnabled(false);
        ui.charCount->display(0);
    }
    else
    {
        ui.messageText->setEnabled(true);
        ui.charCount->display(ui.messageText->toPlainText().length());
    }

    if (index == 0)
    {
        ui.dateTime->setEnabled(true);
        ui.msgCharset->setEnabled(true);
    }
    else
    {
        ui.dateTime->setEnabled(false);
        ui.msgCharset->setEnabled(false);
    }

    if (index == 1)
        ui.messageText->clear();
}

void PagerSender::StatusText(const QString& text, StatusType type)
{
    QPalette pal = ui.status->palette();
    switch (type)
    {
    case StatusType::OK:
        pal.setColor(QPalette::ColorRole::WindowText, QColor(10, 210, 10));
        break;

    case StatusType::Progress:
        pal.setColor(QPalette::ColorRole::WindowText, QColor(210, 210, 0));
        break;

    case StatusType::Error:
        pal.setColor(QPalette::ColorRole::WindowText, QColor(255, 0, 0));
        break;

    case StatusType::Info:
        pal.setColor(QPalette::ColorRole::WindowText, QColor(0, 0, 0));
        break;
    }
    ui.status->setPalette(pal);

    ui.status->setText(text);
}

std::shared_ptr<HackRFTransmitter> PagerSender::tryCreateTransmitter(int mhz, int khz, int hz, int rfGain, float bandwidth, bool amp)
{
    std::shared_ptr<HackRFTransmitter> ptr;

    try
    {
        ptr = std::make_shared<HackRFTransmitter>();
    }
    catch (const std::exception& ex)
    {
        qWarning() << ex.what();
        StatusText("Failed connect to HackRF device. Maybe not connedted or busy?", StatusType::Error);
        return nullptr;
    }

    try
    {
        ptr->SetSubChunkSizeSamples(4096);
        ptr->SetFrequency(mhz, khz, hz);
        ptr->SetFMDeviationKHz(bandwidth);
        ptr->SetAMP(amp);
        ptr->SetGainRF(rfGain);
        ptr->SetTurnOffTXWhenIdle(true);
        ptr->StartTX();
    }
    catch (const std::exception& ex)
    {
        qWarning() << ex.what();
        StatusText("Failed connect to set TX parameters.", StatusType::Error);
        return nullptr;
    }

    return ptr;
}

void PagerSender::SendMessage()
{
    QString freq = ui.freq->text();
    QString a, b, c;
    int dot = freq.indexOf('.');
    m_busy = true;
    ui.send->setEnabled(false);


    if (dot == -1)
        a = freq.mid(0, freq.length());
    else
    {
        a = freq.mid(0, dot);
        if (!freq.endsWith('.'))
            b = freq.mid(dot + 1, freq.length() - 1 - dot);
    }

    int mhz, khz = 0, hz = 0;
    mhz = a.toInt();
    if (!b.isEmpty())
    {
        if (b.length() > 3)
        {
            c = b[b.length() - 1];
            b = b.mid(0, 3);
        }

        int m = pow(10, 3 - b.length());
        khz = b.toInt() * m;
        hz = c.toInt() * 10;
    }

    bool amp = ui.useAmp->isChecked();
    int gain = 0;
    float bandwidth = 0.0f;

    switch (ui.rfGain->currentIndex())
    {
    case 0:
        gain = 5;
        break;

    case 1:
        gain = 23;
        break;

    case 2:
        gain = 47;
        break;
    }

    switch (ui.bandwidth->currentIndex())
    {
    case 0:
        bandwidth = 4.5f;
        break;

    case 1:
        bandwidth = 9.0f;
        break;

    case 2:
        bandwidth = 10.0f;
        break;

    case 3:
        bandwidth = 15.0f;
        break;

    case 4:
        bandwidth = 25.0f;
        break;

    case 5:
        bandwidth = 50.0f;
        break;
    }

    StatusText("Connecting to HackRF...", StatusType::Progress);
    m_tx = tryCreateTransmitter(mhz, khz, hz, gain, bandwidth, amp);
    if (!m_tx)
    {
        m_busy = false;
        if (!ui.freq->text().isEmpty() && !ui.pagerRIC->text().isEmpty())
            ui.send->setEnabled(true);
        return;
    }

    /////////////////////////////////////////////
    int ric = ui.pagerRIC->text().toInt();
    std::string msg = ui.messageText->toPlainText().toUtf8();
    POCSAG::Type type;
    POCSAG::BPS bps;
    POCSAG::Charset cs;
    POCSAG::Function func;
    POCSAG::DateTimePosition dt;

    switch (ui.messageType->currentIndex())
    {
    case 0:
        type = POCSAG::Type::Alphanumeric;
        break;

    case 1:
        type = POCSAG::Type::Numeric;
        break;

    case 2:
        type = POCSAG::Type::Tone;
        break;
    }

    switch (ui.msgBitrate->currentIndex())
    {
    case 0:
        bps = POCSAG::BPS::BPS_512;
        break;

    case 1:
        bps = POCSAG::BPS::BPS_1200;
        break;

    case 2:
        bps = POCSAG::BPS::BPS_2400;
        break;
    }

    switch (ui.msgCharset->currentIndex())
    {
    case 0:
        cs = POCSAG::Charset::Raw;
        break;

    case 1:
        cs = POCSAG::Charset::Latin;
        break;

    case 2:
        cs = POCSAG::Charset::Cyrilic;
        break;
    }

    switch (ui.dateTime->currentIndex())
    {
    case 0:
        dt = POCSAG::DateTimePosition::None;
        break;

    case 1:
        dt = POCSAG::DateTimePosition::Begin;
        break;

    case 2:
        dt = POCSAG::DateTimePosition::End;
        break;
    }
    switch (ui.msgFunc->currentIndex())
    {
    case 0:
        func = POCSAG::Function::A;
        break;

    case 1:
        func = POCSAG::Function::B;
        break;

    case 2:
        func = POCSAG::Function::C;
        break;

    case 3:
        func = POCSAG::Function::D;
        break;
    }

    StatusText("Encoding message...", StatusType::Progress);
    std::vector<uint8_t> message;
    try
    {
        POCSAG::Encoder pocsag;
        pocsag.SetAmplitude(8000);
        pocsag.SetDateTimePosition(dt);
        pocsag.encode(message, ric, type, msg, bps, cs, func);
    }
    catch (const std::exception& ex)
    {
        qWarning() << ex.what();
        StatusText("Failed connect to encode POCSAG message.", StatusType::Error);
        m_tx->StopTX();
        m_tx->WaitForEnd(5000ms);
        m_tx.reset();
        m_busy = false;
        if (!ui.freq->text().isEmpty() && !ui.pagerRIC->text().isEmpty())
            ui.send->setEnabled(true);
        return;
    }

    StatusText("Sending...", StatusType::Progress);
    m_tx->PushSamples(message);
    m_txMonitor.start(250);
}

void PagerSender::TXMonitorTick()
{
    if (!m_tx)
    {
        StatusText("Ready", StatusType::Info);
        if (!ui.freq->text().isEmpty() && !ui.pagerRIC->text().isEmpty())
            ui.send->setEnabled(true);
        m_busy = false;
        m_txMonitor.stop();
        return;
    }

    if (m_tx->IsIdle())
    {
        StatusText("Success", StatusType::OK);
        m_txMonitor.stop();
        m_tx->StopTX();
        m_tx->WaitForEnd(5000ms);
        m_tx.reset();
        if (!ui.freq->text().isEmpty() && !ui.pagerRIC->text().isEmpty())
            ui.send->setEnabled(true);
        m_busy = false;
    }
}