#pragma once
#include <JuceHeader.h>
#include <ixwebsocket/IXWebSocketServer.h>
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <set>
#include <mutex>

class JamWideJuceProcessor;

// NJClient::RemoteUserInfo is a nested type, so full include is required
#include "core/njclient.h"

namespace jamwide {

/// VideoCompanion: Owns WebSocket server, room ID derivation, username sanitization,
/// and browser launch for the video companion page.
///
/// FROZEN PUBLIC INTERFACE -- Plan 03 depends on exactly these public methods.
/// Do not add, remove, or change signatures without updating Plan 03.
///
/// Thread safety:
/// - launchCompanion(): call from message thread only
/// - relaunchBrowser(): call from message thread only (re-click while active, per D-04)
/// - onRosterChanged(): call from ANY thread (marshals to message thread via callAsync)
/// - isActive(): lock-free atomic read, safe from any thread
/// - deactivate(): call from message thread only
///
/// Key definitions:
/// - push: The local user's sanitized username used as their VDO.Ninja stream ID.
///   Derived by sanitizeUsername(username). Example: "Dave@guitar" -> "Daveguitar"
/// - streamId: Each roster entry's collision-resolved sanitized name, used to identify
///   their VDO.Ninja stream. For roster users, collisions are resolved with "_N" suffix
///   using deterministic user-index-based ordering (not join order).
/// - Room ID: SHA-1 hash of (serverAddr + ":" + password) or (serverAddr + ":jamwide-public")
///   for password-less servers. Prefix "jw-", 16 hex chars. D-16/D-17 deliberately include
///   password so private servers get unique rooms and public servers share a deterministic room.
class VideoCompanion
{
public:
    explicit VideoCompanion(JamWideJuceProcessor& processor);
    ~VideoCompanion();

    /// Called by message thread when user clicks Video button (after privacy modal accepted).
    /// serverAddr: "host:port", username: local user's name, password: session password (may be empty).
    /// Starts WebSocket server if not already running, builds companion URL, opens browser.
    /// Returns true if launch succeeded, false if WebSocket server failed to start.
    bool launchCompanion(const juce::String& serverAddr,
                         const juce::String& username,
                         const juce::String& password);

    /// Called by message thread on re-click while active (D-04). Re-opens browser only.
    /// Does NOT restart WebSocket server. Does NOT show modal again.
    void relaunchBrowser();

    /// Called from ANY thread (run thread, message thread) when roster changes.
    /// Marshals to message thread via callAsync before accessing wsServer_.
    /// Addresses review concern: "Thread safety for roster -> WebSocket" (HIGH).
    /// Uses same callAsync + alive flag pattern as OscServer.
    void onRosterChanged(const std::vector<NJClient::RemoteUserInfo>& users);

    bool isActive() const { return active_.load(std::memory_order_relaxed); }

    /// Reset state on disconnect (button goes inactive, WS server stops).
    /// Call from message thread only.
    void deactivate();

    static constexpr int kDefaultWsPort = 7170;

private:
    bool startWebSocketServer();
    void stopWebSocketServer();

    juce::String deriveRoomId(const juce::String& serverAddr,
                              const juce::String& password);
    static juce::String sanitizeUsername(const juce::String& name);
    static juce::String resolveCollision(const juce::String& sanitized,
                                         int userIdx,
                                         std::set<juce::String>& assigned);

    juce::String buildCompanionUrl(const juce::String& roomId,
                                   const juce::String& pushId,
                                   int wsPort);

    void sendConfigToClient(ix::WebSocket& client);
    void broadcastRoster(const std::vector<NJClient::RemoteUserInfo>& users);

    JamWideJuceProcessor& processor_;
    std::atomic<bool> active_{false};
    int wsPort_ = kDefaultWsPort;

    std::unique_ptr<ix::WebSocketServer> wsServer_;
    std::mutex wsMutex_;  // Guards wsServer_ start/stop and broadcast

    // Current session config (set on launch, sent to each connecting WS client)
    juce::String currentRoom_;
    juce::String currentPush_;
    juce::String currentCompanionUrl_;

    // callAsync UAF safety -- same pattern as OscServer
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VideoCompanion)
};

}  // namespace jamwide
