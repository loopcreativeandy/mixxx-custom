#include <gtest/gtest.h>

#include <QElapsedTimer>
#include <QSignalSpy>

#include "controllers/controller.h"
#include "controllers/controllermanager.h"
#include "controllers/defs_controllers.h"
#include "test/mixxxtest.h"

// Rescanning tears down every controller enumerator (Pm_Terminate(), hid_exit(),
// libusb_exit()) and builds them again, destroying and recreating all Controller
// objects on the way. This test does not need a controller to be plugged in: the
// teardown/rebuild cycle itself is the part that used to be impossible and that
// can take the process down.
class ControllerManagerRescanTest : public MixxxTest {};

namespace {

// Building the enumerators calls into the host MIDI/HID APIs, which is slow on
// an emulated CI runner - the Windows ARM64 one needs several seconds per
// rescan. Budget generously, but keep the worst case of a whole test under the
// 45 s ctest timeout the CI workflow uses.
constexpr int kFirstRescanTimeoutMs = 15000;
constexpr int kRescanTimeoutMs = 12000;

// QSignalSpy::wait() only counts emissions that arrive while it is running, and
// the controller thread may well have answered before we get here. Wait for a
// total count instead.
bool waitForDevicesChanged(QSignalSpy& spy, int expectedCount, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (spy.count() < expectedCount && timer.elapsed() < timeoutMs) {
        spy.wait(100);
    }
    return spy.count() >= expectedCount;
}

} // namespace

TEST_F(ControllerManagerRescanTest, RescanIsRepeatableAndReportsBack) {
    ControllerManager controllerManager(config());

    QSignalSpy devicesChangedSpy(&controllerManager, &ControllerManager::devicesChanged);
    ASSERT_TRUE(devicesChangedSpy.isValid());

    controllerManager.setUpDevices();

    // Get the one-off cost out of the way first and do not hold it to the
    // per-rescan budget below: this rescan queues behind the initial
    // Pm_Initialize()/hid_init()/libusb_init(), which is what used to push the
    // first wait past its timeout on the ARM64 runner.
    controllerManager.rescanDevices();
    ASSERT_TRUE(waitForDevicesChanged(devicesChangedSpy, 1, kFirstRescanTimeoutMs));

    // Every rescan must report back exactly once, even when the set of
    // connected devices did not change. The preferences page destroys its
    // controller pages before asking for a rescan and only rebuilds them when
    // this signal arrives.
    for (int i = 0; i < 2; i++) {
        const int before = devicesChangedSpy.count();
        controllerManager.rescanDevices();
        // The work happens in the controller thread, so give it time to answer.
        EXPECT_TRUE(waitForDevicesChanged(devicesChangedSpy, before + 1, kRescanTimeoutMs));
        EXPECT_EQ(before + 1, devicesChangedSpy.count());

        // Whatever the enumerators found must be reachable without dangling.
        const QList<Controller*> controllers = controllerManager.getControllers();
        for (Controller* pController : controllers) {
            EXPECT_FALSE(pController->getName().isNull());
        }
    }
}

#ifdef __PORTMIDI__
namespace {

int countMidiThroughPorts(const ControllerManager& controllerManager) {
    int count = 0;
    const QList<Controller*> controllers = controllerManager.getControllers();
    for (Controller* pController : controllers) {
        if (pController->getName().startsWith(kMidiThroughPortPrefix, Qt::CaseInsensitive)) {
            count++;
        }
    }
    return count;
}

} // namespace

// Whether a MIDI device shows up can be toggled without touching the hardware:
// the MIDI Through Port is filtered out unless it is enabled in the
// preferences. That makes it a stand-in for plugging a controller in while
// Mixxx runs, which is what the rescan is for. Skipped on machines that have no
// MIDI Through Port (it does not exist on all platforms).
TEST_F(ControllerManagerRescanTest, RescanPicksUpDeviceThatAppeared) {
    config()->setValue(kMidiThroughCfgKey, true);

    ControllerManager controllerManager(config());
    QSignalSpy devicesChangedSpy(&controllerManager, &ControllerManager::devicesChanged);
    ASSERT_TRUE(devicesChangedSpy.isValid());

    // Deliberately a rescan and not setUpDevices(): setUpDevices() only reports
    // back when the device list actually changed, so on a machine with no
    // controller attached - which is every CI runner - the signal never arrives
    // and we would time out here instead of reaching the skip below. A rescan
    // always reports back.
    controllerManager.rescanDevices();
    ASSERT_TRUE(waitForDevicesChanged(devicesChangedSpy, 1, kFirstRescanTimeoutMs));
    if (countMidiThroughPorts(controllerManager) == 0) {
        GTEST_SKIP() << "No MIDI Through Port on this machine";
    }

    // Make the device disappear, then appear again. Without a rescan the list
    // would stay as it was at startup.
    config()->setValue(kMidiThroughCfgKey, false);
    controllerManager.rescanDevices();
    ASSERT_TRUE(waitForDevicesChanged(devicesChangedSpy, 2, kRescanTimeoutMs));
    EXPECT_EQ(0, countMidiThroughPorts(controllerManager));

    config()->setValue(kMidiThroughCfgKey, true);
    controllerManager.rescanDevices();
    ASSERT_TRUE(waitForDevicesChanged(devicesChangedSpy, 3, kRescanTimeoutMs));
    EXPECT_EQ(1, countMidiThroughPorts(controllerManager));
}
#endif
