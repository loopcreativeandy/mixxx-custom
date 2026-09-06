#include <gtest/gtest.h>

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

TEST_F(ControllerManagerRescanTest, RescanIsRepeatableAndReportsBack) {
    ControllerManager controllerManager(config());

    QSignalSpy devicesChangedSpy(&controllerManager, &ControllerManager::devicesChanged);
    ASSERT_TRUE(devicesChangedSpy.isValid());

    controllerManager.setUpDevices();

    // Every rescan must report back exactly once, even when the set of
    // connected devices did not change. The preferences page destroys its
    // controller pages before asking for a rescan and only rebuilds them when
    // this signal arrives.
    for (int i = 0; i < 3; i++) {
        const int before = devicesChangedSpy.count();
        controllerManager.rescanDevices();
        // The work happens in the controller thread, so give it time to answer.
        EXPECT_TRUE(devicesChangedSpy.wait(10000));
        EXPECT_EQ(before + 1, devicesChangedSpy.count());

        // Whatever the enumerators found must be reachable without dangling.
        for (Controller* pController : controllerManager.getControllers()) {
            EXPECT_FALSE(pController->getName().isNull());
        }
    }
}

#ifdef __PORTMIDI__
namespace {

int countMidiThroughPorts(const ControllerManager& controllerManager) {
    int count = 0;
    for (Controller* pController : controllerManager.getControllers()) {
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

    controllerManager.setUpDevices();
    ASSERT_TRUE(devicesChangedSpy.wait(10000));
    if (countMidiThroughPorts(controllerManager) == 0) {
        GTEST_SKIP() << "No MIDI Through Port on this machine";
    }

    // Make the device disappear, then appear again. Without a rescan the list
    // would stay as it was at startup.
    config()->setValue(kMidiThroughCfgKey, false);
    controllerManager.rescanDevices();
    ASSERT_TRUE(devicesChangedSpy.wait(10000));
    EXPECT_EQ(0, countMidiThroughPorts(controllerManager));

    config()->setValue(kMidiThroughCfgKey, true);
    controllerManager.rescanDevices();
    ASSERT_TRUE(devicesChangedSpy.wait(10000));
    EXPECT_EQ(1, countMidiThroughPorts(controllerManager));
}
#endif
