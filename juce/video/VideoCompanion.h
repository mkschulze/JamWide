#pragma once
#include <JuceHeader.h>
#include <ixwebsocket/IXWebSocketServer.h>
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <set>
#include <mutex>
#include <future>

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
/// Snapshot of session config captured before WS server start.
/// Passed by value into the IXWebSocket callback to avoid data races
/// (CR-01: callback fires on IXWebSocket thread, members written on message thread).
struct SessionSnapshot
{
    juce::String room;
    juce::String push;
    int wsPort = 0;
    int cachedDelayMs = 0;
};

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

    /// Called from message thread when BPM or BPI changes.
    /// Broadcasts {"type":"bufferDelay","delayMs":N} to all connected WebSocket clients.
    /// Also called internally on initial client connection via sendConfigToClient.
    /// Guard: no-op if !isActive() or no wsServer_, or if bpm/bpi are invalid (<=0 or NaN).
    void broadcastBufferDelay(float bpm, int bpi);

    /// Called from message thread (timerCallback) on each beat position change.
    /// Broadcasts {"type":"beatHeartbeat","beat":N,"bpi":N,"interval":N} to all
    /// connected WebSocket clients. Change-detection avoids redundant sends.
    void broadcastBeatHeartbeat(int beat, int bpi, int intervalCount);

    /// Send a popout request for the given stream ID to all connected WebSocket clients.
    /// Called from OscServer when /JamWide/video/popout/{idx} is received.
    /// Message thread only. No-op if !isActive().
    void requestPopout(const juce::String& streamId);

    /// Look up the resolved stream ID for a remote user at the given 0-based index.
    /// Returns empty string if index is out of range.
    /// Uses the cached roster from the most recent broadcastRoster() call.
    /// Thread safety: message thread only (cachedRoster_ is written by broadcastRoster
    /// which marshals via callAsync, and read by OSC handlers which dispatch via callAsync).
    /// Addresses Codex review MEDIUM concern: "cachedRoster_ thread safety".
    juce::String getStreamIdForUserIndex(int index) const;

    /// Attempt to relaunch video from OSC context (no editor, no privacy modal).
    /// Only succeeds if video was previously launched this session (currentRoom_ is set).
    /// Returns true if relaunch was performed, false if no stored session config exists.
    /// Addresses Codex review HIGH concern: "OSC activation depends on processor fields
    /// that may not exist". Solution: use VideoCompanion's own stored session config
    /// (currentRoom_, currentPush_, currentPassword_) instead of processor fields.
    /// The privacy modal is an editor-only concept; OSC can only relaunch, never first-launch.
    bool relaunchFromOsc();

    static constexpr int kDefaultWsPort = 7170;

private:
    bool startWebSocketServer(const SessionSnapshot& snap);
    void stopWebSocketServer();

    juce::String deriveRoomId(const juce::String& serverAddr,
                              const juce::String& password);
    static juce::String sanitizeUsername(const juce::String& name);
    static juce::String resolveCollision(const juce::String& sanitized,
                                         int userIdx,
                                         std::set<juce::String>& assigned);

    juce::String buildCompanionUrl(const juce::String& roomId,
                                   const juce::String& pushId,
                                   int wsPort,
                                   const juce::String& derivedPassword);

    /// Derive a deterministic VDO.Ninja room password from NINJAM session password.
    /// Uses SHA-256(password + ":" + roomId), truncated to 16 hex chars (64 bits).
    ///
    /// Truncation rationale (addresses review concern R-HIGH-01):
    /// 16 hex chars = 64 bits of entropy. VDO.Ninja itself internally truncates its
    /// own password hashes to just 4 hex chars (16 bits) via
    /// SHA-256(encodeURIComponent(pw) + salt).substring(0, 4).
    /// Our 64 bits is 2^48 times stronger than VDO.Ninja's own security level.
    /// The NINJAM password is the primary access control; this derived password
    /// prevents casual unauthorized viewers from joining the VDO.Ninja room.
    /// Brute-forcing 64 bits is computationally infeasible.
    ///
    /// Returns empty string for public rooms (no password).
    juce::String deriveRoomPassword(const juce::String& password,
                                    const juce::String& roomId);

    void sendConfigToClient(ix::WebSocket& client, const SessionSnapshot& snap);
    void broadcastRoster(const std::vector<NJClient::RemoteUserInfo>& users);

    JamWideJuceProcessor& processor_;

    // DESTRUCTION ORDER: alive_ is declared FIRST among video state so it is destroyed
    // LAST (C++ destroys members in reverse declaration order). This ensures IXWebSocket
    // callbacks that captured alive_ can safely read the flag during wsServer_ teardown.
    // Adopted from Ninja-VST3's member-ordering pattern (receiveBuffer_ before session_).
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);

    std::atomic<bool> active_{false};
    int wsPort_ = kDefaultWsPort;

    std::unique_ptr<ix::WebSocketServer> wsServer_;
    std::mutex wsMutex_;  // Guards wsServer_ start/stop, broadcast, and validatedClients_
    std::future<void> stopFuture_;  // WR-01 fix: joinable handle for async server teardown

    // Security: only clients whose Origin and Host headers matched the companion
    // URL are added here on Open. Broadcasts iterate this set instead of
    // wsServer_->getClients() so a rogue webpage cannot receive config/roster
    // even briefly. Pointers are the address of the ix::WebSocket& passed to the
    // onClientMessage callback — valid from Open until Close, and Close removes
    // the entry. Guarded by wsMutex_.
    std::set<ix::WebSocket*> validatedClients_;

    // Current session config (set on launch, sent to each connecting WS client)
    juce::String currentRoom_;
    juce::String currentPush_;
    juce::String currentCompanionUrl_;
    juce::String currentPassword_;       // Stored for password derivation, never sent over WebSocket
    juce::String currentDerivedPassword_; // SHA-256 derived VDO.Ninja password, used in URL fragment
    int cachedDelayMs_ = 0;              // Last computed buffer delay, sent to new WS clients
    int lastBroadcastBeat_ = -1;         // Change detection for beat heartbeat

    // Cached roster with resolved stream IDs (for OSC popout lookup).
    // Thread safety: written by broadcastRoster (message thread via callAsync),
    // read by getStreamIdForUserIndex (message thread via callAsync from OSC).
    // Both paths run on the message thread, so no mutex needed.
    // Addresses Codex review MEDIUM concern about thread ownership.
    struct CachedRosterEntry {
        juce::String name;
        juce::String streamId;
    };
    std::vector<CachedRosterEntry> cachedRoster_;

    // Stored launch parameters for OSC relaunch.
    // Set during launchCompanion(), cleared during deactivate().
    // These are the credentials used to relaunch via OSC without the editor/privacy modal.
    juce::String storedServerAddr_;
    juce::String storedUsername_;
    // SECURITY RATIONALE (Codex Round 2 MEDIUM): storedPassword_ retains the raw
    // NINJAM password in memory for the session lifetime so OSC can relaunch the
    // companion after deactivate. This is acceptable because:
    // 1. NJClient already holds the same raw password in memory (it must, to
    //    reconnect after network drops). storedPassword_ adds no new exposure.
    // 2. The companion page never sees storedPassword_ -- it receives only the
    //    SHA-256 derived password (currentDerivedPassword_) via the companion URL.
    // 3. storedPassword_ lives in the plugin process only, never serialized to
    //    disk or sent over any network channel.
    // 4. It is cleared when the plugin is destroyed (~VideoCompanion).
    juce::String storedPassword_;
    bool hasLaunchedThisSession_ = false;  // True after first successful launchCompanion

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VideoCompanion)
};

}  // namespace jamwide
