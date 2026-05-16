// Phase 19-01 Task 5 — test_camera_frame_stall.cpp
//
// HIGH-6 mitigation test at the state-machine level. The actual
// FrameStallWatchdog juce::Timer is exercised by manual UAT Cell 5 (permission
// revoke roundtrip); this test virtualizes the trigger condition by setting
// the fallback hint + dispatching WatchdogFired, then asserts the state
// machine transitions Capturing → Retrying with the correct emitFallback.
//
// Three cause variants are exercised so the dialog routing in 19-03 (which
// branches on CameraFallbackCause) has full coverage of the upstream
// emitFallback values.
//
// Pure-C++, links juce::juce_core only.
#include "juce/video/native/CameraStateMachine.h"
#include "juce/video/native/CameraFallbackCause.h"

#include <cassert>
#include <cstdio>

using jamwide::CameraStateMachine;
using jamwide::CameraEvent;
using jamwide::CameraState;
using jamwide::CameraFallbackCause;

namespace {

void driveToCapturing(CameraStateMachine& sm) {
    sm.dispatch(CameraEvent::UserToggle);
    sm.dispatch(CameraEvent::AuthGranted);
    sm.dispatch(CameraEvent::OpenSucceeded);
    sm.dispatch(CameraEvent::FirstFrameReceived);
    assert(sm.getState() == CameraState::Capturing);
}

void test_stall_tcc_denied() {
    CameraStateMachine sm;
    driveToCapturing(sm);
    sm.setFallbackHint(CameraFallbackCause::TCCDenied);
    auto r = sm.dispatch(CameraEvent::WatchdogFired);
    assert(r.newState == CameraState::Retrying);
    assert(r.emitFallback.has_value());
    assert(*r.emitFallback == CameraFallbackCause::TCCDenied);
    assert(r.stopFrameStallWatchdog);
    assert(r.startRetryWorker);
    assert(r.closeHardware);
}

void test_stall_camera_in_use() {
    CameraStateMachine sm;
    driveToCapturing(sm);
    sm.setFallbackHint(CameraFallbackCause::CameraInUse);
    auto r = sm.dispatch(CameraEvent::WatchdogFired);
    assert(r.newState == CameraState::Retrying);
    assert(*r.emitFallback == CameraFallbackCause::CameraInUse);
}

void test_stall_host_lacks_entitlement() {
    CameraStateMachine sm;
    driveToCapturing(sm);
    sm.setFallbackHint(CameraFallbackCause::HostLacksEntitlement);
    auto r = sm.dispatch(CameraEvent::WatchdogFired);
    assert(r.newState == CameraState::Retrying);
    assert(*r.emitFallback == CameraFallbackCause::HostLacksEntitlement);
}

void test_recovery_after_stall() {
    // After a stall transitions Capturing → Retrying, a subsequent
    // RetryTick → Opening + successful reopen recovers to Capturing.
    CameraStateMachine sm;
    driveToCapturing(sm);
    sm.setFallbackHint(CameraFallbackCause::CameraInUse);
    sm.dispatch(CameraEvent::WatchdogFired);
    assert(sm.getState() == CameraState::Retrying);

    sm.dispatch(CameraEvent::RetryTick);
    assert(sm.getState() == CameraState::Opening);

    sm.dispatch(CameraEvent::OpenSucceeded);
    sm.dispatch(CameraEvent::FirstFrameReceived);
    assert(sm.getState() == CameraState::Capturing);
}

void test_watchdog_during_retry_is_noop() {
    // Defensive: if a stall watchdog tick somehow leaks through to Retrying
    // (it shouldn't — stopFrameStallWatchdog is set on entry to Retrying),
    // the state machine treats it as a no-op rather than re-entering the
    // Retrying→Retrying transition (which would have weird side effects).
    CameraStateMachine sm;
    driveToCapturing(sm);
    sm.dispatch(CameraEvent::RuntimeError);
    assert(sm.getState() == CameraState::Retrying);

    auto r = sm.dispatch(CameraEvent::WatchdogFired);
    assert(r.newState == CameraState::Retrying);
    // No new side effects fired
    assert(!r.startRetryWorker);
    assert(!r.closeHardware);
}

} // namespace

int main() {
    test_stall_tcc_denied();
    test_stall_camera_in_use();
    test_stall_host_lacks_entitlement();
    test_recovery_after_stall();
    test_watchdog_during_retry_is_noop();
    std::printf("test_camera_frame_stall: PASS (5 scenarios — HIGH-6 verified)\n");
    return 0;
}
