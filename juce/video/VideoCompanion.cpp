/*
    VideoCompanion.cpp - WebSocket server + room ID + browser launch for VDO.Ninja companion

    Owns the local WebSocket server (127.0.0.1:7170) that sends config and roster
    JSON to the companion web page. Thread-safe roster dispatch via callAsync
    (same pattern as OscServer).

    FROZEN PUBLIC INTERFACE -- see VideoCompanion.h for contract.
*/

#include "VideoCompanion.h"
#include "JamWideJuceProcessor.h"
#include "core/njclient.h"
#include "wdl/sha.h"
#include <cmath>

namespace jamwide {

// ── Construction / Destruction ──────────────────────────────────────────────

VideoCompanion::VideoCompanion(JamWideJuceProcessor& processor)
    : processor_(processor)
{
}

VideoCompanion::~VideoCompanion()
{
    // Prevent pending callAsync lambdas from accessing dead object
    // (same UAF safety pattern as OscServer)
    alive_->store(false, std::memory_order_release);

    // WR-01 fix: Join any pending async teardown from a prior deactivate() call
    // before proceeding, so the detached task does not outlive this object / DLL.
    // Use a timeout to prevent indefinite blocking if the async teardown is stuck
    // (e.g., IXWebSocket wait() hung on a socket). If timeout expires, proceed to
    // synchronous cleanup below which force-stops the server.
    if (stopFuture_.valid())
    {
        auto status = stopFuture_.wait_for(std::chrono::seconds(3));
        if (status != std::future_status::ready)
            DBG("VideoCompanion: async teardown timeout, proceeding with synchronous cleanup");
    }

    // Destructor must block until server is fully stopped (unlike deactivate()
    // which defers to a background thread to avoid DAW state-save timeout).
    {
        std::lock_guard<std::mutex> lock(wsMutex_);
        if (wsServer_)
        {
            wsServer_->setOnClientMessageCallback(nullptr);
            wsServer_->stop();
            wsServer_.reset();
        }
    }
}

// ── Room ID Derivation (D-16, D-17) ────────────────────────────────────────

juce::String VideoCompanion::deriveRoomId(const juce::String& serverAddr,
                                          const juce::String& password)
{
    // D-16: Room ID = SHA-1(serverAddr + ":" + password)
    // D-17: Public servers (empty password) use fixed salt "jamwide-public"
    // NOTE: Including password is BY DESIGN -- private servers with different
    // passwords get unique rooms. All users on the same server with the same
    // password land in the same room (correct: they are in the same NINJAM session).
    juce::String input;
    if (password.isEmpty())
        input = serverAddr + ":jamwide-public";
    else
        input = serverAddr + ":" + password;

    auto utf8 = input.toUTF8();

    WDL_SHA1 sha;
    sha.add(utf8.getAddress(), static_cast<int>(strlen(utf8.getAddress())));

    unsigned char hash[WDL_SHA1SIZE];
    sha.result(hash);

    // Convert first 8 bytes to 16 hex chars
    juce::String hexString;
    for (int i = 0; i < 8; ++i)
        hexString += juce::String::toHexString(hash[i]).paddedLeft('0', 2);

    // "jw-" prefix + 16 hex chars = 19 chars total, valid for VDO.Ninja room names
    return "jw-" + hexString;
}

// ── Derived Room Password (D-05, D-06, D-07) ──────────────────────────────

juce::String VideoCompanion::deriveRoomPassword(const juce::String& password,
                                                const juce::String& roomId)
{
    if (password.isEmpty()) return {};  // D-07: no password for public rooms

    // D-06: SHA-256(ninjam_password + ":" + room_id)
    // Passed to VDO.Ninja as a password (not hash) per RESEARCH.md Open Question 1:
    // VDO.Ninja sees a password and handles its own internal hashing.
    // All JamWide users with same NINJAM password derive the same VDO.Ninja password.
    //
    // Truncation: 16 hex chars = 64 bits >> VDO.Ninja's internal 4 hex chars (16 bits).
    // See header comment for full rationale.
    juce::String input = password + ":" + roomId;
    juce::SHA256 sha(input.toUTF8());
    return sha.toHexString().substring(0, 16);
}

// ── Username Sanitization (D-22) ───────────────────────────────────────────

juce::String VideoCompanion::sanitizeUsername(const juce::String& name)
{
    juce::String result;
    for (int i = 0; i < name.length(); ++i)
    {
        auto c = name[i];
        if (juce::CharacterFunctions::isLetterOrDigit(c) || c == '_')
            result += c;
    }
    return result.isEmpty() ? juce::String("user") : result;
}

// ── Collision Resolution (D-23) ────────────────────────────────────────────

juce::String VideoCompanion::resolveCollision(const juce::String& sanitized,
                                               int userIdx,
                                               std::set<juce::String>& assigned)
{
    // D-23: Deterministic collision resolution using stable user index (not join order).
    // Addresses review concern #10: "Username collision instability."
    if (assigned.find(sanitized) == assigned.end())
    {
        assigned.insert(sanitized);
        return sanitized;
    }

    // Primary candidate: append "_N" where N = user index
    juce::String candidate = sanitized + "_" + juce::String(userIdx);
    if (assigned.find(candidate) == assigned.end())
    {
        assigned.insert(candidate);
        return candidate;
    }

    // Fallback: append letter suffix (unlikely but handles edge cases)
    for (char suffix = 'b'; suffix <= 'z'; ++suffix)
    {
        juce::String fallback = sanitized + "_" + juce::String(userIdx) + juce::String::charToString(suffix);
        if (assigned.find(fallback) == assigned.end())
        {
            assigned.insert(fallback);
            return fallback;
        }
    }

    // Ultimate fallback (should never reach here)
    juce::String ultimate = sanitized + "_" + juce::String(userIdx) + "_x";
    assigned.insert(ultimate);
    return ultimate;
}

// ── Companion URL Building (D-18, D-20) ────────────────────────────────────

juce::String VideoCompanion::buildCompanionUrl(const juce::String& roomId,
                                               const juce::String& pushId,
                                               int wsPort,
                                               const juce::String& derivedPassword)
{
    juce::String url = "https://jamwide.audio/video/?room="
         + juce::URL::addEscapeChars(roomId, true)
         + "&push="
         + juce::URL::addEscapeChars(pushId, true)
         + "&wsPort="
         + juce::String(wsPort)
         + "&ad=0&autostart";

    // D-05: Append derived room password as URL fragment (# not ?) -- never sent to server
    // Uses #password= so VDO.Ninja handles its own internal hashing from this value
    if (derivedPassword.isNotEmpty())
        url += "#password=" + derivedPassword;

    return url;
}

// ── Launch / Relaunch ──────────────────────────────────────────────────────

bool VideoCompanion::launchCompanion(const juce::String& serverAddr,
                                     const juce::String& username,
                                     const juce::String& password)
{
    currentRoom_ = deriveRoomId(serverAddr, password);
    currentPush_ = sanitizeUsername(username);
    currentPassword_ = password;  // Store for derivation, never sent over WebSocket

    // Compute initial buffer delay from current BPM/BPI BEFORE starting WS server
    // (Pitfall 6: cache for late-connecting WS clients)
    // Addresses review concern: invalid BPM/BPI guards
    float bpm = processor_.uiSnapshot.bpm.load(std::memory_order_relaxed);
    int bpi = processor_.uiSnapshot.bpi.load(std::memory_order_relaxed);
    if (bpm > 0.0f && bpi > 0 && !std::isnan(bpm))
        cachedDelayMs_ = static_cast<int>((60.0 / static_cast<double>(bpm)) * bpi * 1000.0);

    // CR-01 fix: Snapshot session config before starting WS server.
    // The IXWebSocket callback fires on its own thread; passing a value snapshot
    // avoids a data race on currentRoom_, currentPush_, wsPort_, cachedDelayMs_.
    SessionSnapshot snap { currentRoom_, currentPush_, wsPort_, cachedDelayMs_ };

    if (!startWebSocketServer(snap))
    {
        DBG("VideoCompanion: Failed to start WebSocket server on port " + juce::String(wsPort_));
        return false;
    }

    currentDerivedPassword_ = deriveRoomPassword(password, currentRoom_);
    currentCompanionUrl_ = buildCompanionUrl(currentRoom_, currentPush_, wsPort_, currentDerivedPassword_);
    juce::URL(currentCompanionUrl_).launchInDefaultBrowser();
    active_.store(true, std::memory_order_relaxed);

    // Store launch parameters for OSC relaunch (Phase 13)
    storedServerAddr_ = serverAddr;
    storedUsername_ = username;
    storedPassword_ = password;
    hasLaunchedThisSession_ = true;

    return true;
}

void VideoCompanion::relaunchBrowser()
{
    // D-04: Re-click while active re-opens browser, does NOT restart server
    if (!isActive())
        return;

    juce::URL(currentCompanionUrl_).launchInDefaultBrowser();
}

// ── WebSocket Server Lifecycle ─────────────────────────────────────────────

bool VideoCompanion::startWebSocketServer(const SessionSnapshot& snap)
{
    std::lock_guard<std::mutex> lock(wsMutex_);

    // Idempotent -- addresses review concern #12
    if (wsServer_)
        return true;

    // CRITICAL: Bind to 127.0.0.1 only (T-11-01 mitigation, Research pitfall 1 & 7)
    wsServer_ = std::make_unique<ix::WebSocketServer>(wsPort_, "127.0.0.1");

    // Disable per-message deflate: localhost-only traffic with small JSON payloads
    // does not benefit from compression, and zlib's deflate context on the IXWebSocket
    // thread is a known source of heap corruption that can cascade into Windows BSOD
    // (KERNEL_SECURITY_CHECK_FAILURE 0x139) via network driver pool corruption.
    wsServer_->disablePerMessageDeflate();

    // CR-01 fix: Capture snapshot by value so the callback reads a thread-safe copy
    // instead of unsynchronized shared members. IXWebSocket fires this callback on
    // its own internal thread.
    // Poison-pill pattern (adopted from Ninja-VST3): capture alive_ flag and check it
    // at the top of the callback to prevent use-after-free during shutdown. Without this,
    // the callback can fire between alive_->store(false) and wsServer_->stop() in the
    // destructor, accessing a partially-destroyed VideoCompanion via `this`.
    auto aliveFlag = alive_;
    wsServer_->setOnClientMessageCallback(
        [this, aliveFlag, snap](std::shared_ptr<ix::ConnectionState> /*connectionState*/,
               ix::WebSocket& webSocket,
               const ix::WebSocketMessagePtr& msg)
        {
            if (!aliveFlag->load(std::memory_order_acquire))
                return;
            if (msg->type == ix::WebSocketMessageType::Open)
            {
                sendConfigToClient(webSocket, snap);
            }
            // Close: no-op (client disconnected)
        }
    );

    auto res = wsServer_->listen();
    if (!res.first)
    {
        // T-11-03 mitigation: port bind failure handled gracefully
        DBG("VideoCompanion: WebSocket listen failed on port "
            + juce::String(wsPort_) + ": " + juce::String(res.second.c_str()));
        wsServer_.reset();
        return false;
    }

    wsServer_->start();  // Non-blocking, runs on IXWebSocket's own thread
    return true;
}

void VideoCompanion::stopWebSocketServer()
{
    std::unique_ptr<ix::WebSocketServer> serverToStop;
    {
        std::lock_guard<std::mutex> lock(wsMutex_);
        if (!wsServer_)
            return;

        // Clear callback before stopping to prevent late callbacks from accessing
        // `this` during teardown (adopted from Ninja-VST3's callback-clear pattern).
        wsServer_->setOnClientMessageCallback(nullptr);
        wsServer_->stop();
        serverToStop = std::move(wsServer_);
        // wsServer_ is now null — broadcastRoster/sendConfig will bail early
    }

    // Destroy the server object off the message thread to avoid DAW state-save timeout.
    // stop() above already joined all IXWebSocket threads and closed the socket.
    // DO NOT call s->wait() here — stop() already fired the condition variable notification,
    // so wait() would block forever (classic CV race: notify before wait = lost signal).
    // The async lambda only needs to destroy the unique_ptr, which is safe on any thread.
    if (serverToStop)
    {
        stopFuture_ = std::async(std::launch::async,
            [s = std::move(serverToStop)]() mutable {
                s.reset();
            });
    }
}

// ── JSON Protocol (D-13, D-14) ─────────────────────────────────────────────

static juce::String escapeJsonString(const juce::String& s)
{
    juce::String result;
    for (int i = 0; i < s.length(); ++i)
    {
        auto c = s[i];
        switch (c)
        {
            case '\\': result += "\\\\"; break;
            case '"':  result += "\\\""; break;
            case '\n': result += "\\n";  break;
            case '\r': result += "\\r";  break;
            case '\t': result += "\\t";  break;
            default:
                if (c < 0x20)
                    result += juce::String::formatted("\\u%04x", static_cast<int>(c));
                else
                    result += c;
                break;
        }
    }
    return result;
}

void VideoCompanion::sendConfigToClient(ix::WebSocket& client, const SessionSnapshot& snap)
{
    // D-13: config message with room, push, noaudio, wsPort
    // D-14: Password intentionally excluded from config message (security)
    // CR-01 fix: Reads snapshot (captured by value) instead of shared members,
    // eliminating the data race when this runs on IXWebSocket's callback thread.
    juce::String json = "{\"type\":\"config\""
                        ",\"room\":\"" + escapeJsonString(snap.room) + "\""
                        ",\"push\":\"" + escapeJsonString(snap.push) + "\""
                        ",\"noaudio\":true"
                        ",\"wsPort\":" + juce::String(snap.wsPort) +
                        "}";

    client.send(json.toStdString());

    // Send cached buffer delay if available (protocol contract: config then bufferDelay)
    // Addresses review concern: state preservation -- new/reconnecting clients get full state
    if (snap.cachedDelayMs > 0)
    {
        juce::String delayJson = "{\"type\":\"bufferDelay\",\"delayMs\":"
                                 + juce::String(snap.cachedDelayMs) + "}";
        client.send(delayJson.toStdString());
    }
}

// ── Roster Dispatch (Thread-Safe) ──────────────────────────────────────────

void VideoCompanion::onRosterChanged(const std::vector<NJClient::RemoteUserInfo>& users)
{
    // T-11-06 mitigation: Marshal to message thread via callAsync.
    // This method may be called from ANY thread (run thread on roster change).
    // Uses same callAsync + alive_ flag pattern as OscServer (UAF safety).
    auto usersCopy = users;  // Copy for lambda capture
    auto aliveFlag = alive_;
    juce::MessageManager::callAsync([this, aliveFlag, usersCopy = std::move(usersCopy)]() {
        if (!aliveFlag->load(std::memory_order_acquire))
            return;
        broadcastRoster(usersCopy);
    });
}

void VideoCompanion::broadcastRoster(const std::vector<NJClient::RemoteUserInfo>& users)
{
    // Runs on message thread (marshalled from onRosterChanged)
    std::lock_guard<std::mutex> lock(wsMutex_);
    if (!wsServer_)
        return;

    // Build roster JSON
    juce::String json = "{\"type\":\"roster\",\"users\":[";

    // Cache resolved roster for OSC popout lookup (Phase 13)
    cachedRoster_.clear();
    cachedRoster_.reserve(users.size());

    std::set<juce::String> assigned;
    for (size_t i = 0; i < users.size(); ++i)
    {
        if (i > 0)
            json += ",";

        juce::String rawName(users[i].name);
        auto sanitized = sanitizeUsername(rawName);
        auto streamId = resolveCollision(sanitized, static_cast<int>(i), assigned);

        cachedRoster_.push_back({rawName, streamId});

        json += "{\"idx\":" + juce::String(static_cast<int>(i))
              + ",\"name\":\"" + escapeJsonString(rawName) + "\""
              + ",\"streamId\":\"" + escapeJsonString(streamId) + "\""
              + "}";
    }

    json += "]}";

    // Broadcast to all connected clients
    auto clients = wsServer_->getClients();
    for (auto& client : clients)
    {
        client->send(json.toStdString());
    }
}

// ── Buffer Delay Broadcast (D-01, D-02, D-03, D-04) ──────────────────────

void VideoCompanion::broadcastBufferDelay(float bpm, int bpi)
{
    // D-02: Called on BPM/BPI change only, not on every beat
    // Addresses review concern: invalid BPM/BPI guards (zero, NaN, transitional state)
    if (bpm <= 0.0f || bpi <= 0) return;
    if (std::isnan(bpm)) return;  // Guard against NaN during session transitions
    if (!isActive()) return;      // Pitfall 6: guard if video not active

    // D-03: delay_ms = (60.0 / bpm) * bpi * 1000, truncated to integer
    // Integer truncation (not rounding) is deliberate -- sub-millisecond precision
    // is irrelevant for video buffering at multi-second intervals.
    // WR-03 fix: Guard against zero/negative delay before caching or broadcasting.
    // Extreme BPM values can truncate to 0; sending delayMs:0 would disable VDO.Ninja buffering.
    int computed = static_cast<int>((60.0 / static_cast<double>(bpm)) * bpi * 1000.0);
    if (computed <= 0) return;

    // D-04: {"type":"bufferDelay","delayMs":N}
    juce::String json = "{\"type\":\"bufferDelay\",\"delayMs\":"
                        + juce::String(computed) + "}";

    std::lock_guard<std::mutex> lock(wsMutex_);
    if (!wsServer_) return;
    cachedDelayMs_ = computed;
    auto clients = wsServer_->getClients();
    for (auto& client : clients)
        client->send(json.toStdString());
}

// ── Popout / Roster Lookup (Phase 13) ─────────────────────────────────────

void VideoCompanion::requestPopout(const juce::String& streamId)
{
    if (!isActive()) return;

    juce::String json = "{\"type\":\"popout\",\"streamId\":\""
                        + escapeJsonString(streamId) + "\"}";

    std::lock_guard<std::mutex> lock(wsMutex_);
    if (!wsServer_) return;
    auto clients = wsServer_->getClients();
    for (auto& client : clients)
        client->send(json.toStdString());
}

juce::String VideoCompanion::getStreamIdForUserIndex(int index) const
{
    if (index < 0 || index >= static_cast<int>(cachedRoster_.size()))
        return {};
    return cachedRoster_[static_cast<size_t>(index)].streamId;
}

// ── OSC Relaunch (Phase 13) ───────────────────────────────────────────────

bool VideoCompanion::relaunchFromOsc()
{
    // Addresses Codex review HIGH concern: privacy-gate state.
    // OSC cannot show a privacy modal (no editor context).
    // First activation MUST be via plugin UI (editor shows VideoPrivacyDialog).
    // OSC can only relaunch if video was previously launched this session.
    if (!hasLaunchedThisSession_) return false;
    if (isActive()) return false;  // Already active, nothing to do

    // Relaunch using stored session parameters
    return launchCompanion(storedServerAddr_, storedUsername_, storedPassword_);
}

// ── Deactivate ─────────────────────────────────────────────────────────────

void VideoCompanion::deactivate()
{
    // D-13: Broadcast deactivate BEFORE stopping server.
    // Addresses Pitfall 5 from research: send must complete before server teardown.
    {
        std::lock_guard<std::mutex> lock(wsMutex_);
        if (wsServer_)
        {
            juce::String json = "{\"type\":\"deactivate\"}";
            auto clients = wsServer_->getClients();
            for (auto& client : clients)
                client->send(json.toStdString());
        }
    }

    active_.store(false, std::memory_order_relaxed);
    stopWebSocketServer();
    currentRoom_.clear();
    currentPush_.clear();
    currentCompanionUrl_.clear();
    currentPassword_.clear();
    currentDerivedPassword_.clear();
    cachedDelayMs_ = 0;
    cachedRoster_.clear();
    // NOTE: storedServerAddr_, storedUsername_, storedPassword_, hasLaunchedThisSession_
    // are intentionally NOT cleared here. They persist across deactivate/reactivate
    // so that OSC /video/active 1.0 can relaunch without the editor.
}

}  // namespace jamwide
