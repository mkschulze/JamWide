// Phase 19-01 Task 5 — test_camera_retry_backoff.cpp
//
// D-20 retry timing coverage via the pure-C++ CameraStateMachine::RetryBackoff
// helper. The full RetryWorker (juce::Thread + MessageManager::callAsync) is
// exercised by the manual UAT cells in 19-03; this test isolates the timing
// schedule + state-machine retry semantics so we get deterministic coverage.
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

void test_delay_schedule() {
    // Sequence: 1000, 2000, 4000, 8000, 16000 ms
    assert(CameraStateMachine::RetryBackoff::delayMs(0) == 1000);
    assert(CameraStateMachine::RetryBackoff::delayMs(1) == 2000);
    assert(CameraStateMachine::RetryBackoff::delayMs(2) == 4000);
    assert(CameraStateMachine::RetryBackoff::delayMs(3) == 8000);
    assert(CameraStateMachine::RetryBackoff::delayMs(4) == 16000);
}

void test_cumulative_schedule() {
    // cumulativeMs(N) = sum of first N delays
    assert(CameraStateMachine::RetryBackoff::cumulativeMs(0) == 0);
    assert(CameraStateMachine::RetryBackoff::cumulativeMs(1) == 1000);
    assert(CameraStateMachine::RetryBackoff::cumulativeMs(2) == 3000);     // 1+2
    assert(CameraStateMachine::RetryBackoff::cumulativeMs(3) == 7000);     // 1+2+4
    assert(CameraStateMachine::RetryBackoff::cumulativeMs(4) == 15000);    // 1+2+4+8
    assert(CameraStateMachine::RetryBackoff::cumulativeMs(5) == 31000);    // 1+2+4+8+16

    // Beyond MAX_ATTEMPTS the cumulative is clamped.
    assert(CameraStateMachine::RetryBackoff::cumulativeMs(10) == 31000);
}

void test_exhausted_predicate() {
    for (int i = 0; i < 5; ++i) {
        assert(!CameraStateMachine::RetryBackoff::isExhausted(i));
    }
    assert(CameraStateMachine::RetryBackoff::isExhausted(5));
    assert(CameraStateMachine::RetryBackoff::isExhausted(99));
}

void test_max_attempts_constant() {
    static_assert(CameraStateMachine::RetryBackoff::MAX_ATTEMPTS == 5,
                  "Plan locks MAX_ATTEMPTS=5");
}

void test_retry_loop_state_machine_recovery() {
    // Full state-machine reopen-mid-retry path: a successful reopen after
    // RuntimeError + RetryTick brings the device back to Capturing.
    CameraStateMachine sm;
    sm.dispatch(CameraEvent::UserToggle);
    sm.dispatch(CameraEvent::AuthGranted);
    sm.dispatch(CameraEvent::OpenSucceeded);
    sm.dispatch(CameraEvent::FirstFrameReceived);
    assert(sm.getState() == CameraState::Capturing);

    sm.setFallbackHint(CameraFallbackCause::CameraInUse);
    sm.dispatch(CameraEvent::RuntimeError);
    assert(sm.getState() == CameraState::Retrying);

    sm.dispatch(CameraEvent::RetryTick);
    assert(sm.getState() == CameraState::Opening);

    sm.dispatch(CameraEvent::OpenSucceeded);
    sm.dispatch(CameraEvent::FirstFrameReceived);
    assert(sm.getState() == CameraState::Capturing);
    // Recovered — retry attempts counter resets in the *device* layer; the
    // state machine does not track attempts (RetryWorker owns that).
}

void test_retry_exhaustion_path() {
    CameraStateMachine sm;
    sm.dispatch(CameraEvent::UserToggle);
    sm.dispatch(CameraEvent::AuthGranted);
    sm.dispatch(CameraEvent::OpenSucceeded);
    sm.dispatch(CameraEvent::FirstFrameReceived);
    sm.dispatch(CameraEvent::RuntimeError);
    assert(sm.getState() == CameraState::Retrying);

    sm.setFallbackHint(CameraFallbackCause::CameraInUse);
    auto r = sm.dispatch(CameraEvent::RetryExhausted);
    assert(r.newState == CameraState::Unavailable);
    assert(r.stopRetryWorker);
    assert(r.emitFallback.has_value());
    assert(*r.emitFallback == CameraFallbackCause::CameraInUse);
}

} // namespace

int main() {
    test_delay_schedule();
    test_cumulative_schedule();
    test_exhausted_predicate();
    test_max_attempts_constant();
    test_retry_loop_state_machine_recovery();
    test_retry_exhaustion_path();
    std::printf("test_camera_retry_backoff: PASS (6 scenarios — D-20 verified)\n");
    return 0;
}
