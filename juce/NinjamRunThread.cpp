#include "NinjamRunThread.h"
#include "JamWideJuceProcessor.h"

NinjamRunThread::NinjamRunThread(JamWideJuceProcessor& p)
    : juce::Thread("NinjamRun"),
      processor(p)
{
}

NinjamRunThread::~NinjamRunThread()
{
    // CRITICAL: Ensure thread stops before destruction (JUCE requirement).
    // This is the safety-net -- normal shutdown path is via releaseResources().
    stopThread(5000);
}

void NinjamRunThread::run()
{
    while (!threadShouldExit())
    {
        // ---------------------------------------------------------------
        // Phase 3 will add:
        //   1. Command queue processing (connect, disconnect, chat, etc.)
        //   2. NJClient::Run() call for network I/O and state management
        // ---------------------------------------------------------------

        // Use wait() (not Thread::sleep()) so signalThreadShouldExit()
        // wakes the thread immediately for prompt shutdown.
        wait(50);
    }
}
