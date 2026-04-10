#pragma once
#include <JuceHeader.h>
#include "net/server_list.h"

class JamWideJuceProcessor;
class NJClient;

/**
 * NinjamRunThread -- juce::Thread subclass that runs the NJClient::Run() loop.
 *
 * Lifecycle: Created and started in JamWideJuceProcessor::prepareToPlay(),
 * stopped and destroyed in releaseResources(). Destructor has a safety-net
 * stopThread() call in case releaseResources() was not invoked by the host.
 *
 * The run loop:
 *   1. Drains cmd_queue from Processor and dispatches commands under clientLock
 *   2. Calls NJClient::Run() under clientLock for network I/O
 *   3. Tracks status changes and sets up default local channel on connect
 *   4. Pushes events (status, chat, user info, server list, topic) to Processor queues
 *   5. Updates UiAtomicSnapshot (BPM, BPI, beat position, VU levels)
 *   6. Adaptive sleep: 20ms connected, 50ms disconnected
 */
class NinjamRunThread : public juce::Thread
{
public:
    explicit NinjamRunThread(JamWideJuceProcessor& processor);
    ~NinjamRunThread() override;

    void run() override;

private:
    void processCommands(NJClient* client);

    JamWideJuceProcessor& processor;
    jamwide::ServerListFetcher serverListFetcher;
    int lastStatus_ = -1;  // NJClient::NJC_STATUS_DISCONNECTED
    // BPM/BPI change detection: suppress the initial default→real transition
    // that fires when the server sends its actual config on login. NJClient
    // constructs with m_bpm=120, m_bpi=32 defaults — GetActualBPM()/GetBPI()
    // return those until the server sends its config message, which can take
    // hundreds of ms. Use a time-based suppression window: after status goes
    // OK, silently track values for ~2.5s without emitting chat messages.
    // After the window expires, normal change detection resumes.
    juce::int64 suppressBpmBpiUntilMs_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NinjamRunThread)
};
