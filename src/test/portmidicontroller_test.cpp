#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <QScopedPointer>

#include "controllers/midi/portmidicontroller.h"
#include "controllers/midi/portmididevice.h"
#include "test/mixxxtest.h"
#include "util/time.h"

using ::testing::_;
using ::testing::DoAll;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::Sequence;
using ::testing::SetArrayArgument;

class MockPortMidiController : public PortMidiController {
  public:
    MockPortMidiController(const PmDeviceInfo* inputDeviceInfo,
            const PmDeviceInfo* outputDeviceInfo,
            int inputDeviceIndex,
            int outputDeviceIndex)
            : PortMidiController(inputDeviceInfo,
                      outputDeviceInfo,
                      inputDeviceIndex,
                      outputDeviceIndex) {
    }
    ~MockPortMidiController() override {
    }

    void sendShortMsg(unsigned char status, unsigned char byte1, unsigned char byte2) override {
        PortMidiController::sendShortMsg(status, byte1, byte2);
    }

    void sendSysexMsg(const QList<int>& data, unsigned int length) {
        PortMidiController::sendSysexMsg(data, length);
    }

    MOCK_METHOD4(receivedShortMessage,
            void(unsigned char, unsigned char, unsigned char, mixxx::Duration));
    MOCK_METHOD2(receive, void(const QByteArray&, mixxx::Duration));

    // These tests are unrelated to scripting.
    MOCK_METHOD0(startEngine, void());
    MOCK_METHOD0(stopEngine, void());
};

class MockPortMidiDevice : public PortMidiDevice {
  public:
    MockPortMidiDevice(PmDeviceInfo* info, int index)
            : PortMidiDevice(info, index) {
    }

    MOCK_CONST_METHOD0(isOpen, bool());
    MOCK_METHOD1(openInput, PmError(int32_t));
    MOCK_METHOD0(openOutput, PmError());
    MOCK_METHOD0(close, PmError());
    MOCK_METHOD0(poll, PmError());
    MOCK_METHOD2(read, int(PmEvent*, int32_t));
    MOCK_METHOD1(writeShort, PmError(int32_t));
    MOCK_METHOD1(writeSysEx, PmError(unsigned char*));
};

class PortMidiControllerTest : public MixxxTest {
  protected:
    PortMidiControllerTest()
            : m_mockInput(new MockPortMidiDevice(&m_inputDeviceInfo, 0)),
              m_mockOutput(new MockPortMidiDevice(&m_outputDeviceInfo, 0)) {
        // PmDeviceInfo::name is non const since portmidi 2.0.1
        // We maintain the memory here in place of Pm_GetDeviceInfo()
        char inputDeviceName[] = "Test Input Device";
        char outputDeviceName[] = "Test Output Device";
        constexpr const char interf[] = "Test";
        m_inputDeviceInfo.name = inputDeviceName;
        m_inputDeviceInfo.interf = interf;
        m_inputDeviceInfo.input = 1;
        m_inputDeviceInfo.output = 0;
        m_inputDeviceInfo.opened = 0;

        m_outputDeviceInfo.name = outputDeviceName;
        m_outputDeviceInfo.interf = interf;
        m_outputDeviceInfo.input = 0;
        m_outputDeviceInfo.output = 1;
        m_outputDeviceInfo.opened = 0;

        m_pController.reset(new MockPortMidiController(
                &m_inputDeviceInfo, &m_outputDeviceInfo, 0, 0));
        m_pController->setPortMidiInputDevice(m_mockInput);
        m_pController->setPortMidiOutputDevice(m_mockOutput);
    }

    void openDevice() {
        m_pController->open({});
    }

    void closeDevice() {
        m_pController->close();
    }

    void pollDevice() {
        m_pController->poll();
    }

    // CP125 output watchdog: no threads, no dialogs, scripted replacement devices.
    void installWatchdogFakes() {
        m_pController->m_closeOutputDetached =
                [this](PortMidiDevice* pDevice,
                        std::shared_ptr<std::atomic<bool>> pClosed) {
                    m_detachedClosed.append(pDevice);
                    m_pClosedFlag = pClosed;
                };
        m_pController->m_createOutputDevice = [this]() -> PortMidiDevice* {
            if (m_nextOutputs.isEmpty()) {
                return new MockPortMidiDevice(&m_outputDeviceInfo, 0);
            }
            return m_nextOutputs.takeFirst();
        };
        m_pController->m_showOutputDialog = [this](bool offline) {
            m_dialogs.append(offline);
        };
        mixxx::Time::setTestMode(true);
    }

    void TearDown() override {
        mixxx::Time::setTestMode(false);
        qDeleteAll(m_detachedClosed);
        qDeleteAll(m_nextOutputs);
        MixxxTest::TearDown();
    }

    bool outputOffline() const {
        return m_pController->m_bOutputOffline;
    }

    PortMidiDevice* currentOutput() const {
        return m_pController->m_pOutputDevice.get();
    }

    QList<PortMidiDevice*> m_detachedClosed;
    std::shared_ptr<std::atomic<bool>> m_pClosedFlag;
    QList<MockPortMidiDevice*> m_nextOutputs;
    QList<bool> m_dialogs;

    PmDeviceInfo m_inputDeviceInfo;
    PmDeviceInfo m_outputDeviceInfo;
    MockPortMidiDevice* m_mockInput;
    MockPortMidiDevice* m_mockOutput;
    QScopedPointer<MockPortMidiController> m_pController;
};

PmEvent MakeEvent(PmMessage message, PmTimestamp timestamp) {
    PmEvent event;
    event.message = message;
    event.timestamp = timestamp;
    return event;
}

MATCHER_P(ByteArrayEquals, value,
          "Checks that the non-NULL terminated argument array exactly equals "
          "the provided byte container.") {
    for (int i = 0; i < value.size(); ++i) {
        if (arg[i] != value.at(i))
            return false;
    }
    return true;
}

TEST_F(PortMidiControllerTest, OpenClose) {
    Sequence input;
    ON_CALL(*m_mockInput, isOpen())
            .WillByDefault(Return(false));
    EXPECT_CALL(*m_mockInput, openInput(MIXXX_PORTMIDI_BUFFER_LEN))
            .InSequence(input)
            .WillOnce(Return(pmNoError));
    EXPECT_CALL(*m_mockInput, isOpen())
            .InSequence(input)
            .WillRepeatedly(Return(true));
    EXPECT_CALL(*m_mockInput, close())
            .InSequence(input)
            .WillOnce(Return(pmNoError));

    Sequence output;
    ON_CALL(*m_mockOutput, isOpen())
            .WillByDefault(Return(false));
    EXPECT_CALL(*m_mockOutput, openOutput())
            .WillOnce(Return(pmNoError));
    EXPECT_CALL(*m_mockOutput, isOpen())
            .InSequence(output)
            .WillRepeatedly(Return(true));
    EXPECT_CALL(*m_mockOutput, close())
            .InSequence(output)
            .WillOnce(Return(pmNoError));

    openDevice();
    EXPECT_TRUE(m_pController->isOpen());
    closeDevice();
    EXPECT_FALSE(m_pController->isOpen());
};

TEST_F(PortMidiControllerTest, WriteShort) {
    // Note that Pm_WriteShort takes an int32_t formatted as 0x00B2B1SS where SS
    // is the status byte, B1 is the first message byte and B2 is the second
    // message byte.
    Sequence output;
    EXPECT_CALL(*m_mockOutput, isOpen())
            .WillRepeatedly(Return(true));
    EXPECT_CALL(*m_mockOutput, writeShort(0x403C90))
            .InSequence(output)
            .WillOnce(Return(pmNoError));
    EXPECT_CALL(*m_mockOutput, writeShort(0xFFFFFF))
            .InSequence(output)
            .WillOnce(Return(pmBadData));
    EXPECT_CALL(*m_mockOutput, writeShort(0x403C80))
            .InSequence(output)
            .WillOnce(Return(pmNoError));

    m_pController->sendShortMsg(0x90, 0x3C, 0x40);
    m_pController->sendShortMsg(0xFF, 0xFF, 0xFF);
    m_pController->sendShortMsg(0x80, 0x3C, 0x40);
};

TEST_F(PortMidiControllerTest, WriteSysex) {
    QList<int> sysex;
    sysex.append(0xF0);
    sysex.append(0x12);
    sysex.append(0xF7);

    EXPECT_CALL(*m_mockOutput, isOpen())
            .WillRepeatedly(Return(true));
    EXPECT_CALL(*m_mockOutput, writeSysEx(ByteArrayEquals(sysex)))
            .WillOnce(Return(pmNoError));
    m_pController->sendSysexMsg(sysex, sysex.length());
};

TEST_F(PortMidiControllerTest, WriteSysex_Malformed) {
    QList<int> sysex;
    sysex.append(0xF0);
    sysex.append(0x12);

    EXPECT_CALL(*m_mockOutput, isOpen())
            .WillRepeatedly(Return(true));
    EXPECT_CALL(*m_mockOutput, writeSysEx(_))
            .Times(0);
    m_pController->sendSysexMsg(sysex, sysex.length());
};


TEST_F(PortMidiControllerTest, Poll_Read_Basic) {
    std::vector<PmEvent> messages;
    messages.push_back(MakeEvent(0x403C90, 0x0));
    messages.push_back(MakeEvent(0x403C80, 0x1));

    Sequence read;
    EXPECT_CALL(*m_mockInput, isOpen())
            .WillRepeatedly(Return(true));
    EXPECT_CALL(*m_mockInput, read(NotNull(), _))
            .InSequence(read)
            .WillOnce(DoAll(SetArrayArgument<0>(messages.begin(), messages.end()),
                    Return(static_cast<int>(messages.size()))));

    EXPECT_CALL(*m_pController, receivedShortMessage(0x90, 0x3C, 0x40, _))
            .InSequence(read);
    EXPECT_CALL(*m_pController, receivedShortMessage(0x80, 0x3C, 0x40, _))
            .InSequence(read);

    pollDevice();
};

TEST_F(PortMidiControllerTest, Poll_Read_SysExWithRealtime) {
    std::vector<PmEvent> messages;
    messages.push_back(MakeEvent(0x332211F0, 0x0));
    messages.push_back(MakeEvent(0x000000F8, 0x1));
    messages.push_back(MakeEvent(0x77665544, 0x0));
    messages.push_back(MakeEvent(0x000000FA, 0x2));
    messages.push_back(MakeEvent(0x000000F7, 0x0));

    QByteArray sysex;
    sysex.append('\xF0');
    sysex.append('\x11');
    sysex.append('\x22');
    sysex.append('\x33');
    sysex.append('\x44');
    sysex.append('\x55');
    sysex.append('\x66');
    sysex.append('\x77');
    sysex.append('\xF7');

    Sequence read;
    EXPECT_CALL(*m_mockInput, isOpen())
            .WillRepeatedly(Return(true));
    EXPECT_CALL(*m_mockInput, read(NotNull(), _))
            .InSequence(read)
            .WillOnce(DoAll(SetArrayArgument<0>(messages.begin(), messages.end()),
                            Return(messages.size())));
    EXPECT_CALL(*m_pController, receivedShortMessage(0xF8, 0x00, 0x00, _))
            .InSequence(read);
    EXPECT_CALL(*m_pController, receivedShortMessage(0xFA, 0x00, 0x00, _))
            .InSequence(read);
    EXPECT_CALL(*m_pController, receive(sysex, _))
            .InSequence(read);

    pollDevice();
};

TEST_F(PortMidiControllerTest, Poll_Read_SysEx) {
    std::vector<PmEvent> messages;
    messages.push_back(MakeEvent(0x332211F0, 0x0));
    messages.push_back(MakeEvent(0xF7665544, 0x1));

    QByteArray sysex;
    sysex.append('\xF0');
    sysex.append('\x11');
    sysex.append('\x22');
    sysex.append('\x33');
    sysex.append('\x44');
    sysex.append('\x55');
    sysex.append('\x66');
    sysex.append('\xF7');

    Sequence read;
    EXPECT_CALL(*m_mockInput, isOpen())
            .WillRepeatedly(Return(true));
    EXPECT_CALL(*m_mockInput, read(NotNull(), _))
            .InSequence(read)
            .WillOnce(DoAll(SetArrayArgument<0>(messages.begin(), messages.end()),
                            Return(messages.size())));
    EXPECT_CALL(*m_pController, receive(sysex, _))
            .InSequence(read);

    pollDevice();
};

TEST_F(PortMidiControllerTest,
       Poll_Read_SysExWithRealtime_CoincidentalRealtimeByte) {
    // We used to incorrectly treat an 0xF8 occurring in a SysEx message as a
    // realtime message. This test verifies that we do not do this anymore.
    std::vector<PmEvent> messages;
    messages.push_back(MakeEvent(0x332211F0, 0x0));
    messages.push_back(MakeEvent(0x6655F844, 0x0));
    messages.push_back(MakeEvent(0x0000F777, 0x0));

    QByteArray sysex;
    sysex.append('\xF0');
    sysex.append('\x11');
    sysex.append('\x22');
    sysex.append('\x33');
    sysex.append('\x44');
    sysex.append('\xF8');
    sysex.append('\x55');
    sysex.append('\x66');
    sysex.append('\x77');
    sysex.append('\xF7');

    Sequence read;
    EXPECT_CALL(*m_mockInput, isOpen())
            .WillRepeatedly(Return(true));
    EXPECT_CALL(*m_mockInput, read(NotNull(), _))
            .InSequence(read)
            .WillOnce(DoAll(SetArrayArgument<0>(messages.begin(), messages.end()),
                            Return(messages.size())));
    EXPECT_CALL(*m_pController, receive(sysex, _))
            .InSequence(read);

    pollDevice();
};

TEST_F(PortMidiControllerTest, Poll_Read_SysExInterrupted_FollowedByNormalMessage) {
    // According to the PortMIDI documentation when a SysEx message is
    // interrupted, we will expect to see a non-realtime status byte as a new
    // message before seeing an EOX terminating SysEx. In this event we drop the
    // SysEx message and process the new message as normal.

    std::vector<PmEvent> messages;
    messages.push_back(MakeEvent(0x332211F0, 0x0));
    messages.push_back(MakeEvent(0x00403C90, 0x0));

    Sequence read;
    EXPECT_CALL(*m_mockInput, isOpen())
            .WillRepeatedly(Return(true));
    EXPECT_CALL(*m_mockInput, read(NotNull(), _))
            .InSequence(read)
            .WillOnce(DoAll(SetArrayArgument<0>(messages.begin(), messages.end()),
                            Return(messages.size())));
    EXPECT_CALL(*m_pController, receivedShortMessage(0x90, 0x3C, 0x40, _))
            .InSequence(read);

    pollDevice();
};

TEST_F(PortMidiControllerTest, Poll_Read_SysExInterrupted_FollowedBySysExMessage) {
    // According to the PortMIDI documentation when a SysEx message is
    // interrupted, we will expect to see a non-realtime status byte as a new
    // message before seeing an EOX terminating SysEx. In this event we drop the
    // SysEx message and process the new message as normal.

    std::vector<PmEvent> messages;
    messages.push_back(MakeEvent(0x332211F0, 0x0));
    messages.push_back(MakeEvent(0x77665544, 0x0));
    messages.push_back(MakeEvent(0x332211F0, 0x1));
    messages.push_back(MakeEvent(0xF7665544, 0x0));

    QByteArray sysex;
    sysex.append('\xF0');
    sysex.append('\x11');
    sysex.append('\x22');
    sysex.append('\x33');
    sysex.append('\x44');
    sysex.append('\x55');
    sysex.append('\x66');
    sysex.append('\xF7');

    Sequence read;
    EXPECT_CALL(*m_mockInput, isOpen())
            .WillRepeatedly(Return(true));
    EXPECT_CALL(*m_mockInput, read(NotNull(), _))
            .InSequence(read)
            .WillOnce(DoAll(SetArrayArgument<0>(messages.begin(), messages.end()),
                            Return(messages.size())));
    EXPECT_CALL(*m_pController, receive(sysex, _))
            .InSequence(read);

    pollDevice();
};


TEST_F(PortMidiControllerTest, Poll_Read_SysEx_BufferOverflow) {
    // According to the PortMIDI documentation when a SysEx message is
    // interrupted, we will expect to see a non-realtime status byte as a new
    // message before seeing an EOX terminating SysEx. In this event we drop the
    // SysEx message and process the new message as normal.

    std::vector<PmEvent> messages1;
    messages1.push_back(MakeEvent(0x332211F0, 0x0));
    messages1.push_back(MakeEvent(0x77665544, 0x0));

    std::vector<PmEvent> messages2;
    messages2.push_back(MakeEvent(0x332211F0, 0x1));

    std::vector<PmEvent> messages3;
    messages3.push_back(MakeEvent(0xF7665544, 0x2));

    QByteArray sysex;
    sysex.append('\xF0');
    sysex.append('\x11');
    sysex.append('\x22');
    sysex.append('\x33');
    sysex.append('\x44');
    sysex.append('\x55');
    sysex.append('\x66');
    sysex.append('\xF7');

    Sequence read;
    EXPECT_CALL(*m_mockInput, isOpen())
            .WillRepeatedly(Return(true));

    // Poll 1 -- returns messages1.
    EXPECT_CALL(*m_mockInput, read(NotNull(), _))
            .InSequence(read)
            .WillOnce(DoAll(SetArrayArgument<0>(messages1.begin(), messages1.end()),
                            Return(messages1.size())));

    // Poll 2 -- buffer overflow.
    EXPECT_CALL(*m_mockInput, read(NotNull(), _))
            .InSequence(read)
            .WillOnce(Return(pmBufferOverflow));

    // Poll 3 -- returns messages2.
    EXPECT_CALL(*m_mockInput, read(NotNull(), _))
            .InSequence(read)
            .WillOnce(DoAll(SetArrayArgument<0>(messages2.begin(), messages2.end()),
                            Return(messages2.size())));

    // Poll 4 -- returns messages3.
    EXPECT_CALL(*m_mockInput, read(NotNull(), _))
            .InSequence(read)
            .WillOnce(DoAll(SetArrayArgument<0>(messages3.begin(), messages3.end()),
                            Return(messages3.size())));
    EXPECT_CALL(*m_pController, receive(sysex, _))
            .InSequence(read);

    pollDevice();
    pollDevice();
    pollDevice();
    pollDevice();
};

// CP125: three failed writes in a row take the output offline: the dead stream
// is handed to the detached closer (never closed inline), nothing more is
// written to it, and the warning is shown once.
TEST_F(PortMidiControllerTest, OutputWatchdog_GoesOfflineAfterRepeatedFailures) {
    installWatchdogFakes();
    EXPECT_CALL(*m_mockOutput, isOpen()).WillRepeatedly(Return(true));
    EXPECT_CALL(*m_mockOutput, close()).Times(0);
    EXPECT_CALL(*m_mockOutput, writeShort(_))
            .Times(3)
            .WillRepeatedly(Return(pmHostError));

    m_pController->sendShortMsg(0x90, 0x40, 0x01);
    m_pController->sendShortMsg(0x90, 0x41, 0x01);
    EXPECT_FALSE(outputOffline());
    m_pController->sendShortMsg(0x90, 0x42, 0x01);
    EXPECT_TRUE(outputOffline());
    // more sends while offline reach no device
    m_pController->sendShortMsg(0x90, 0x43, 0x01);
    m_pController->sendShortMsg(0x90, 0x44, 0x01);

    ASSERT_EQ(1, m_detachedClosed.size());
    EXPECT_EQ(m_mockOutput, m_detachedClosed.first());
    EXPECT_NE(m_mockOutput, currentOutput());
    EXPECT_EQ(QList<bool>{true}, m_dialogs);
}

// A single failure (glitch) followed by a success keeps the output online.
TEST_F(PortMidiControllerTest, OutputWatchdog_SuccessResetsFailureCount) {
    installWatchdogFakes();
    EXPECT_CALL(*m_mockOutput, isOpen()).WillRepeatedly(Return(true));
    EXPECT_CALL(*m_mockOutput, writeShort(_))
            .WillOnce(Return(pmHostError))
            .WillOnce(Return(pmHostError))
            .WillOnce(Return(pmNoError))
            .WillOnce(Return(pmHostError))
            .WillOnce(Return(pmHostError));
    for (int i = 0; i < 5; ++i) {
        m_pController->sendShortMsg(0x90, 0x40, 0x01);
    }
    EXPECT_FALSE(outputOffline());
    EXPECT_TRUE(m_detachedClosed.isEmpty());
}

// Reconnect waits for the dead stream's close and the retry interval, then
// opens a fresh stream and replays the latest message per note/CC in first-sent
// order (note off replaces note on of the same note; non-LED messages skipped).
TEST_F(PortMidiControllerTest, OutputWatchdog_ReconnectReplaysLedState) {
    installWatchdogFakes();
    auto* pFresh = new MockPortMidiDevice(&m_outputDeviceInfo, 0);
    m_nextOutputs.append(pFresh);

    EXPECT_CALL(*m_mockOutput, isOpen()).WillRepeatedly(Return(true));
    EXPECT_CALL(*m_mockOutput, writeShort(_)).WillRepeatedly(Return(pmHostError));
    EXPECT_CALL(*m_mockInput, isOpen()).WillRepeatedly(Return(false));

    m_pController->sendShortMsg(0x90, 0x10, 0x01); // note 0x10 green
    m_pController->sendShortMsg(0xB0, 0x30, 0x7F); // CC 0x30
    m_pController->sendShortMsg(0x90, 0x11, 0x05); // -> offline
    ASSERT_TRUE(outputOffline());
    m_nextOutputs.clear(); // now owned by the controller
    m_pController->sendShortMsg(0x90, 0x10, 0x03); // while offline: red
    m_pController->sendShortMsg(0x80, 0x11, 0x00); // note off replaces note on
    m_pController->sendShortMsg(0xF8, 0x00, 0x00); // clock: not cached

    Sequence replay;
    EXPECT_CALL(*pFresh, openOutput()).Times(0);
    // close still running: no reopen even after the interval
    mixxx::Time::addTestTime(std::chrono::seconds(3));
    pollDevice();
    ::testing::Mock::VerifyAndClearExpectations(pFresh);

    m_pClosedFlag->store(true);
    // interval restarted by the last attempt: nothing before it passes
    EXPECT_CALL(*pFresh, openOutput()).Times(0);
    pollDevice();
    ::testing::Mock::VerifyAndClearExpectations(pFresh);

    EXPECT_CALL(*pFresh, openOutput()).WillOnce(Return(pmNoError));
    EXPECT_CALL(*pFresh, writeShort(0x011090)).Times(0);
    EXPECT_CALL(*pFresh, writeShort(0x031090)).InSequence(replay).WillOnce(Return(pmNoError));
    EXPECT_CALL(*pFresh, writeShort(0x7F30B0)).InSequence(replay).WillOnce(Return(pmNoError));
    EXPECT_CALL(*pFresh, writeShort(0x001180)).InSequence(replay).WillOnce(Return(pmNoError));
    mixxx::Time::addTestTime(std::chrono::seconds(3));
    pollDevice();

    EXPECT_FALSE(outputOffline());
    EXPECT_EQ(pFresh, currentOutput());
    EXPECT_EQ((QList<bool>{true, false}), m_dialogs);

    // back online: sends go straight to the new stream
    EXPECT_CALL(*pFresh, isOpen()).WillRepeatedly(Return(true));
    EXPECT_CALL(*pFresh, writeShort(0x014290)).WillOnce(Return(pmNoError));
    m_pController->sendShortMsg(0x90, 0x42, 0x01);
}

// Reopen works but the replay fails: the new stream is closed detached too and
// the output stays offline (no "lights back" message).
TEST_F(PortMidiControllerTest, OutputWatchdog_ReplayFailureStaysOffline) {
    installWatchdogFakes();
    auto* pFresh = new MockPortMidiDevice(&m_outputDeviceInfo, 0);
    m_nextOutputs.append(pFresh);
    EXPECT_CALL(*m_mockOutput, isOpen()).WillRepeatedly(Return(true));
    EXPECT_CALL(*m_mockOutput, writeShort(_)).WillRepeatedly(Return(pmHostError));
    EXPECT_CALL(*m_mockInput, isOpen()).WillRepeatedly(Return(false));
    for (int i = 0; i < 3; ++i) {
        m_pController->sendShortMsg(0x90, 0x20 + i, 0x01);
    }
    ASSERT_TRUE(outputOffline());
    m_nextOutputs.clear();
    m_pClosedFlag->store(true);

    EXPECT_CALL(*pFresh, openOutput()).WillOnce(Return(pmNoError));
    EXPECT_CALL(*pFresh, writeShort(_)).WillOnce(Return(pmHostError));
    mixxx::Time::addTestTime(std::chrono::seconds(3));
    pollDevice();

    EXPECT_TRUE(outputOffline());
    ASSERT_EQ(2, m_detachedClosed.size());
    EXPECT_EQ(pFresh, m_detachedClosed.last());
    EXPECT_EQ(QList<bool>{true}, m_dialogs);
}
