#include "controllers/midi/portmidicontroller.h"

#include <thread>

#include "controllers/midi/midiutils.h"
#include "errordialoghandler.h"
#include "moc_portmidicontroller.cpp"
#include "util/time.h"

namespace {
const QString kUnknownControllerName = QStringLiteral("Unknown PortMidiController");

// CP125: consecutive failed writes before the output counts as dead. One
// failure can be a glitch; on 2026-10-25 every write failed from the first one.
constexpr int kOutputOfflineAfterFailures = 3;
const mixxx::Duration kOutputReconnectInterval = mixxx::Duration::fromMillis(2000);

// Key for the LED cache: message type + channel, and the note/CC number.
// Note off is stored under note on, so the latest of the two wins.
bool outputCacheKey(unsigned int word, quint16* pKey) {
    const unsigned char status = word & 0xFF;
    const unsigned char type = status & 0xF0;
    if (type < 0x80 || type > 0xB0) {
        return false; // only note off/on, poly aftertouch and CC carry LED state
    }
    const unsigned char keyStatus = type == 0x80 ? (0x90 | (status & 0x0F)) : status;
    *pKey = static_cast<quint16>((keyStatus << 8) | ((word >> 8) & 0xFF));
    return true;
}
} // namespace

std::atomic<int> PortMidiController::s_pendingDetachedCloses{0};

PortMidiController::PortMidiController(const PmDeviceInfo* inputDeviceInfo,
        const PmDeviceInfo* outputDeviceInfo,
        int inputDeviceIndex,
        int outputDeviceIndex)
        : MidiController((inputDeviceInfo || outputDeviceInfo)
                          ? QString::fromLocal8Bit(inputDeviceInfo
                                            ? inputDeviceInfo->name
                                            : outputDeviceInfo->name)
                          : kUnknownControllerName),
          m_pOutputDeviceInfo(outputDeviceInfo),
          m_outputDeviceIndex(outputDeviceIndex),
          m_outputFailures(0),
          m_bOutputOffline(false),
          m_cReceiveMsg_index(0),
          m_bInSysex(false) {
    m_createOutputDevice = [this]() -> PortMidiDevice* {
        return new PortMidiDevice(m_pOutputDeviceInfo, m_outputDeviceIndex);
    };
    m_closeOutputDetached = [](PortMidiDevice* pDevice,
                                    std::shared_ptr<std::atomic<bool>> pClosed) {
        s_pendingDetachedCloses.fetch_add(1);
        std::thread([pDevice, pClosed]() {
            pDevice->close();
            delete pDevice;
            pClosed->store(true);
            s_pendingDetachedCloses.fetch_sub(1);
        }).detach();
    };
    m_showOutputDialog = [this](bool offline) {
        showOutputDialog(offline);
    };
    for (int k = 0; k < MIXXX_PORTMIDI_BUFFER_LEN; ++k) {
        m_midiBuffer[k] = {0, 0};
    }

    // Note: We prepend the input stream's index to the device's name to prevent
    // duplicate devices from causing mayhem.
    //setDeviceName(QString("%1. %2").arg(QString::number(m_iInputDeviceIndex), inputDeviceInfo->name));
    if (inputDeviceInfo) {
        setInputDevice(inputDeviceInfo->input);
        m_pInputDevice.reset(new PortMidiDevice(
            inputDeviceInfo, inputDeviceIndex));
    }
    if (outputDeviceInfo) {
        setOutputDevice(outputDeviceInfo->output);
        m_pOutputDevice.reset(new PortMidiDevice(
            outputDeviceInfo, outputDeviceIndex));
    }
}

PortMidiController::~PortMidiController() {
    if (isOpen()) {
        close();
    }
}

int PortMidiController::open(const QString& resourcePath) {
    if (isOpen()) {
        qCWarning(m_logBase) << "PortMIDI device" << getName() << "already open";
        return -1;
    }

    if (getName() == MIXXX_PORTMIDI_NO_DEVICE_STRING) {
        return -1;
    }

    m_bInSysex = false;
    m_cReceiveMsg_index = 0;
    m_outputFailures = 0;
    m_bOutputOffline = false;
    m_outputCache.clear();
    m_outputCacheOrder.clear();

    if (m_pInputDevice && isInputDevice()) {
        qCInfo(m_logBase) << "PortMidiController: Opening"
                          << m_pInputDevice->info()->name << "index"
                          << m_pInputDevice->index() << "for input";
        PmError err = m_pInputDevice->openInput(MIXXX_PORTMIDI_BUFFER_LEN);

        if (err != pmNoError) {
            qCWarning(m_logBase) << "PortMidi error:" << Pm_GetErrorText(err);
            return -2;
        }
    }
    if (m_pOutputDevice && isOutputDevice()) {
        qCInfo(m_logBase) << "PortMidiController: Opening"
                          << m_pOutputDevice->info()->name << "index"
                          << m_pOutputDevice->index() << "for output";

        PmError err = m_pOutputDevice->openOutput();
        if (err != pmNoError) {
            qCWarning(m_logBase) << "PortMidi error:" << Pm_GetErrorText(err);
            if (!m_pInputDevice || !m_pInputDevice->isOpen()) {
                return -2;
            }
            // CP125: the input works - run the controller without lights
            // (e.g. a rescan while the old dead output is still closing) and
            // let poll() keep trying to open the output.
            m_bOutputOffline = true;
            m_nextOutputReconnect = mixxx::Time::elapsed() + kOutputReconnectInterval;
            if (m_showOutputDialog) {
                m_showOutputDialog(true);
            }
        }
    }
    startEngine();
    applyMapping(resourcePath);
    setOpen(true);
    return 0;
}

int PortMidiController::close() {
    if (!isOpen()) {
        qCWarning(m_logBase) << "PortMIDI device" << getName() << "already closed";
        return -1;
    }

    stopEngine();
    MidiController::close();

    int result = 0;

    if (m_pInputDevice && m_pInputDevice->isOpen()) {
        PmError err = m_pInputDevice->close();
        if (err != pmNoError) {
            qCWarning(m_logBase) << "PortMidi error:" << Pm_GetErrorText(err);
            result = -1;
        }
    }

    if (m_pOutputDevice && m_pOutputDevice->isOpen()) {
        PmError err = m_pOutputDevice->close();
        if (err != pmNoError) {
            qCWarning(m_logBase) << "PortMidi error:" << Pm_GetErrorText(err);
            result = -1;
        }
    }

    setOpen(false);
    return result;
}

bool PortMidiController::poll() {
    if (m_bOutputOffline) {
        tryReconnectOutput();
    }

    // Poll the controller for new data if it's an input device
    if (m_pInputDevice.isNull() || !m_pInputDevice->isOpen()) {
        return false;
    }

    int numEvents = m_pInputDevice->read(m_midiBuffer, MIXXX_PORTMIDI_BUFFER_LEN);

    //qDebug() << "PortMidiController::poll()" << numEvents;

    if (numEvents < 0) {
        qCWarning(m_logInput) << "PortMidi error:" << Pm_GetErrorText((PmError)numEvents);
        return false;
    }

    for (int i = 0; i < numEvents; i++) {
        unsigned char status = Pm_MessageStatus(m_midiBuffer[i].message);
        mixxx::Duration timestamp = mixxx::Duration::fromMillis(m_midiBuffer[i].timestamp);

        if ((status & 0xF8) == 0xF8) {
            // Handle real-time MIDI messages at any time
            receivedShortMessage(status, 0, 0, timestamp);
            continue;
        }

        reprocessMessage:

        if (!m_bInSysex) {
            if (status == 0xF0) {
                m_bInSysex = true;
                status = 0;
            } else {
                //unsigned char channel = status & 0x0F;
                unsigned char note = Pm_MessageData1(m_midiBuffer[i].message);
                unsigned char velocity = Pm_MessageData2(m_midiBuffer[i].message);
                receivedShortMessage(status, note, velocity, timestamp);
            }
        }

        if (m_bInSysex) {
            // Abort (drop) the current System Exclusive message if a
            //  non-realtime status byte was received
            if (status > 0x7F && status < 0xF7) {
                m_bInSysex = false;
                m_cReceiveMsg_index = 0;
                qCWarning(m_logInput) << "Buggy MIDI device: SysEx interrupted!";
                goto reprocessMessage;    // Don't lose the new message
            }

            // Collect bytes from PmMessage
            uint8_t data = 0;
            for (int shift = 0; shift < 32 &&
                    (data != MidiUtils::opCodeValue(MidiOpCode::EndOfExclusive));
                    shift += 8) {
                // TODO(rryan): This prevents buffer overflow if the sysex is
                // larger than 1024 bytes. I don't want to radically change
                // anything before the 2.0 release so this will do for now.
                data = (m_midiBuffer[i].message >> shift) & 0xFF;
                if (m_cReceiveMsg_index < MIXXX_SYSEX_BUFFER_LEN) {
                    m_cReceiveMsg[m_cReceiveMsg_index++] = data;
                }
            }

            // End System Exclusive message if the EOX byte was received
            if (data == MidiUtils::opCodeValue(MidiOpCode::EndOfExclusive)) {
                m_bInSysex = false;
                const char* buffer = reinterpret_cast<const char*>(m_cReceiveMsg);
                receive(QByteArray::fromRawData(buffer, m_cReceiveMsg_index),
                        timestamp);
                m_cReceiveMsg_index = 0;
            }
        }
    }
    return numEvents > 0;
}

void PortMidiController::sendShortMsg(unsigned char status, unsigned char byte1,
                                      unsigned char byte2) {
    unsigned int word = (((unsigned int)byte2) << 16) |
                         (((unsigned int)byte1) << 8) | status;
    // Cached even while offline, so a reconnect replays the current state.
    cacheOutputMessage(word);

    if (m_bOutputOffline) {
        return; // the reconnect in poll() sends it
    }
    if (!m_pOutputDevice || !m_pOutputDevice->isOpen()) {
        return;
    }

    PmError err = m_pOutputDevice->writeShort(word);
    if (err == pmNoError) {
        m_outputFailures = 0;
        qCDebug(m_logOutput) << QStringLiteral("outgoing: ")
                             << MidiUtils::formatMidiOpCode(getName(),
                                        status,
                                        byte1,
                                        byte2,
                                        MidiUtils::channelFromStatus(status),
                                        MidiUtils::opCodeFromStatus(status));
    } else {
        // Use two qWarnings() to ensure line break works on all operating systems
        qCWarning(m_logOutput) << "Error sending short message"
                               << MidiUtils::formatMidiOpCode(getName(),
                                          status,
                                          byte1,
                                          byte2,
                                          MidiUtils::channelFromStatus(status),
                                          MidiUtils::opCodeFromStatus(status));
        qCWarning(m_logOutput) << "PortMidi error:" << Pm_GetErrorText(err);
        if (++m_outputFailures >= kOutputOfflineAfterFailures) {
            setOutputOffline();
        }
    }
}

void PortMidiController::cacheOutputMessage(unsigned int word) {
    quint16 key;
    if (!outputCacheKey(word, &key)) {
        return;
    }
    if (!m_outputCache.contains(key)) {
        m_outputCacheOrder.append(key);
    }
    m_outputCache.insert(key, word);
}

void PortMidiController::setOutputOffline() {
    qCWarning(m_logOutput) << "Output to" << getName() << "failed"
                           << m_outputFailures
                           << "times in a row - output offline, controller LEDs are frozen."
                           << "Retrying every"
                           << kOutputReconnectInterval.toIntegerMillis() << "ms";
    const bool wasOffline = m_bOutputOffline;
    m_bOutputOffline = true;
    m_nextOutputReconnect = mixxx::Time::elapsed() + kOutputReconnectInterval;
    if (m_pOutputDevice && m_pOutputDevice->isOpen()) {
        // Never close it here: midiOutClose() on the dead handle blocked for
        // 7 minutes on 2026-10-25, and this thread also reads the input.
        closeOutputDetached(m_pOutputDevice.release());
    }
    if (!m_pOutputDevice) {
        m_pOutputDevice.reset(m_createOutputDevice());
    }
    if (!wasOffline && m_showOutputDialog) {
        m_showOutputDialog(true);
    }
}

void PortMidiController::closeOutputDetached(PortMidiDevice* pDevice) {
    m_pDeadOutputClosed = std::make_shared<std::atomic<bool>>(false);
    m_closeOutputDetached(pDevice, m_pDeadOutputClosed);
}

void PortMidiController::tryReconnectOutput() {
    const mixxx::Duration now = mixxx::Time::elapsed();
    if (now < m_nextOutputReconnect) {
        return;
    }
    m_nextOutputReconnect = now + kOutputReconnectInterval;

    // PortMidi allows one open stream per device: wait for the old close.
    if (m_pDeadOutputClosed && !m_pDeadOutputClosed->load()) {
        qCDebug(m_logOutput) << "Output reconnect: dead stream of" << getName()
                             << "is still closing";
        return;
    }
    if (!m_pOutputDevice) {
        m_pOutputDevice.reset(m_createOutputDevice());
    }
    PmError err = m_pOutputDevice->openOutput();
    if (err != pmNoError) {
        qCDebug(m_logOutput) << "Output reconnect to" << getName()
                             << "failed:" << Pm_GetErrorText(err);
        return;
    }
    // Replay the LED state; every write must get through.
    for (quint16 key : std::as_const(m_outputCacheOrder)) {
        err = m_pOutputDevice->writeShort(m_outputCache.value(key));
        if (err != pmNoError) {
            qCWarning(m_logOutput) << "Output reconnect to" << getName()
                                   << "opened, but sending failed:"
                                   << Pm_GetErrorText(err);
            closeOutputDetached(m_pOutputDevice.release());
            m_pOutputDevice.reset(m_createOutputDevice());
            return;
        }
    }
    m_bOutputOffline = false;
    m_outputFailures = 0;
    qCInfo(m_logOutput) << "Output to" << getName() << "reconnected,"
                        << m_outputCacheOrder.size() << "LED messages replayed";
    if (m_showOutputDialog) {
        m_showOutputDialog(false);
    }
}

void PortMidiController::showOutputDialog(bool offline) {
    ErrorDialogProperties* props =
            ErrorDialogHandler::instance()->newDialogProperties();
    props->setModal(false);
    if (offline) {
        props->setType(DLG_WARNING);
        props->setTitle(tr("Controller lights offline"));
        props->setText(tr("Mixxx can no longer send to \"%1\". "
                          "Its lights are frozen and do NOT show what is playing.")
                               .arg(getName()));
        props->setInfoText(
                tr("Buttons and faders still work. Windows rejects every "
                   "message to the controller (MIDI host error).") +
                QStringLiteral("<br><br>") +
                tr("Mixxx retries every 2 seconds and redraws all lights when "
                   "the connection is back. If they stay frozen, unplug the "
                   "controller and plug it back in."));
        props->setKey(QStringLiteral("controller-output-offline:") + getName());
    } else {
        props->setType(DLG_INFO);
        props->setTitle(tr("Controller lights back"));
        props->setText(tr("\"%1\" is reconnected. All lights were redrawn and "
                          "show the current state again.")
                               .arg(getName()));
        props->setKey(QStringLiteral("controller-output-back:") + getName());
    }
    props->addButton(QMessageBox::Ok);
    props->setDefaultButton(QMessageBox::Ok);
    props->setEscapeButton(QMessageBox::Ok);
    ErrorDialogHandler::instance()->requestErrorDialog(props);
}

bool PortMidiController::sendBytes(const QByteArray& data) {
    // PortMidi does not receive a length argument for the buffer we provide to
    // Pm_WriteSysEx. Instead, it scans for a MidiOpCode::EndOfExclusive byte
    // to know when the message is over. If one is not provided, it will
    // overflow the buffer and cause a segfault.
    if (!data.endsWith(MidiUtils::opCodeValue(MidiOpCode::EndOfExclusive))) {
        qCDebug(m_logOutput) << "SysEx message does not end with 0xF7 -- ignoring.";
        return false;
    }

    if (!m_pOutputDevice || !m_pOutputDevice->isOpen()) {
        return false;
    }

    PmError err = m_pOutputDevice->writeSysEx((unsigned char*)data.constData());
    if (err == pmNoError) {
        qCDebug(m_logOutput) << QStringLiteral("outgoing: ")
                             << MidiUtils::formatSysexMessage(getName(), data);
        return true;
    } else {
        // Use two qWarnings() to ensure line break works on all operating systems
        qCWarning(m_logOutput) << "Error sending SysEx message:"
                               << MidiUtils::formatSysexMessage(getName(), data);
        qCWarning(m_logOutput) << "PortMidi error:" << Pm_GetErrorText(err);
    }
    return false;
}
