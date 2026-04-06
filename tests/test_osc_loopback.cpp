/*
    test_osc_loopback.cpp - Loopback smoke test for juce_osc linkage

    Sends an OSC message to localhost and verifies it is received back.
    No external test framework -- matches existing test_flac_codec pattern.
    Returns 0 on success, 1 on failure.
*/

#include <JuceHeader.h>
#include <cstdio>
#include <atomic>

static std::atomic<bool> messageReceived{false};
static std::atomic<float> receivedValue{0.0f};

class TestListener : public juce::OSCReceiver::Listener<juce::OSCReceiver::RealtimeCallback>
{
public:
    void oscMessageReceived(const juce::OSCMessage& msg) override
    {
        // RealtimeCallback: called on the OSC network thread (no message loop needed)
        if (msg.size() > 0 && msg[0].isFloat32())
            receivedValue.store(msg[0].getFloat32(), std::memory_order_relaxed);
        messageReceived.store(true, std::memory_order_release);
    }
};

int main()
{
    // JUCE requires message thread initialization for OSC
    juce::ScopedJuceInitialiser_GUI juceInit;

    printf("OSC Loopback Test\n");
    printf("=================\n");

    juce::OSCReceiver receiver;
    juce::OSCSender sender;
    TestListener listener;

    // Try to bind receiver to a port
    int port = 19876;
    if (!receiver.connect(port))
    {
        port = 19877;
        if (!receiver.connect(port))
        {
            printf("FAIL: Could not bind receiver to port 19876 or 19877\n");
            return 1;
        }
    }
    printf("  Receiver bound to port %d\n", port);

    receiver.addListener(&listener);

    // Connect sender to the same port on localhost
    if (!sender.connect("127.0.0.1", port))
    {
        printf("FAIL: Could not connect sender to 127.0.0.1:%d\n", port);
        receiver.disconnect();
        return 1;
    }
    printf("  Sender connected to 127.0.0.1:%d\n", port);

    // Send a test message
    const float testValue = 0.42f;
    if (!sender.send("/test/loopback", testValue))
    {
        printf("FAIL: Could not send OSC message\n");
        receiver.disconnect();
        sender.disconnect();
        return 1;
    }
    printf("  Sent /test/loopback with value %.2f\n", testValue);

    // Wait up to 500ms for the message to arrive (polling loop)
    // Use Thread::sleep since runDispatchLoopUntil requires JUCE_MODAL_LOOPS_PERMITTED.
    // MessageLoopCallback listener dispatches on the message thread via the event loop,
    // so we just need to give the run loop time to process.
    for (int i = 0; i < 50; ++i)
    {
        juce::Thread::sleep(10);
        if (messageReceived.load(std::memory_order_relaxed))
            break;
    }

    // Clean up
    receiver.removeListener(&listener);
    receiver.disconnect();
    sender.disconnect();

    // Verify
    if (!messageReceived.load(std::memory_order_relaxed))
    {
        printf("FAIL: No message received within 500ms timeout\n");
        return 1;
    }

    float rv = receivedValue.load(std::memory_order_relaxed);
    if (std::abs(rv - testValue) > 0.001f)
    {
        printf("FAIL: Expected value %.3f, got %.3f\n", testValue, rv);
        return 1;
    }

    printf("PASS: OSC loopback successful (value=%.3f)\n", rv);
    return 0;
}
