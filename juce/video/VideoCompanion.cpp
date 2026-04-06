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
    stopWebSocketServer();
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
                                               int wsPort)
{
    return "https://video.jamwide.app/?room="
         + juce::URL::addEscapeChars(roomId, true)
         + "&push="
         + juce::URL::addEscapeChars(pushId, true)
         + "&wsPort="
         + juce::String(wsPort);
}

// ── Launch / Relaunch ──────────────────────────────────────────────────────

bool VideoCompanion::launchCompanion(const juce::String& serverAddr,
                                     const juce::String& username,
                                     const juce::String& password)
{
    currentRoom_ = deriveRoomId(serverAddr, password);
    currentPush_ = sanitizeUsername(username);

    if (!startWebSocketServer())
    {
        DBG("VideoCompanion: Failed to start WebSocket server on port " + juce::String(wsPort_));
        return false;
    }

    currentCompanionUrl_ = buildCompanionUrl(currentRoom_, currentPush_, wsPort_);
    juce::URL(currentCompanionUrl_).launchInDefaultBrowser();
    active_.store(true, std::memory_order_relaxed);
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

bool VideoCompanion::startWebSocketServer()
{
    std::lock_guard<std::mutex> lock(wsMutex_);

    // Idempotent -- addresses review concern #12
    if (wsServer_)
        return true;

    // CRITICAL: Bind to 127.0.0.1 only (T-11-01 mitigation, Research pitfall 1 & 7)
    wsServer_ = std::make_unique<ix::WebSocketServer>(wsPort_, "127.0.0.1");

    wsServer_->setOnClientMessageCallback(
        [this](std::shared_ptr<ix::ConnectionState> /*connectionState*/,
               ix::WebSocket& webSocket,
               const ix::WebSocketMessagePtr& msg)
        {
            if (msg->type == ix::WebSocketMessageType::Open)
            {
                sendConfigToClient(webSocket);
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
    std::lock_guard<std::mutex> lock(wsMutex_);
    if (!wsServer_)
        return;

    wsServer_->stop();
    wsServer_->wait();
    wsServer_.reset();
}

// ── JSON Protocol (D-13, D-14) ─────────────────────────────────────────────

static juce::String escapeJsonString(const juce::String& s)
{
    return s.replace("\\", "\\\\").replace("\"", "\\\"");
}

void VideoCompanion::sendConfigToClient(ix::WebSocket& client)
{
    // D-13: config message with room, push, noaudio, wsPort
    // D-14: Password intentionally excluded from config message (security)
    juce::String json = "{\"type\":\"config\""
                        ",\"room\":\"" + escapeJsonString(currentRoom_) + "\""
                        ",\"push\":\"" + escapeJsonString(currentPush_) + "\""
                        ",\"noaudio\":true"
                        ",\"wsPort\":" + juce::String(wsPort_) +
                        "}";

    client.send(json.toStdString());
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

    std::set<juce::String> assigned;
    for (size_t i = 0; i < users.size(); ++i)
    {
        if (i > 0)
            json += ",";

        juce::String rawName(users[i].name);
        auto sanitized = sanitizeUsername(rawName);
        auto streamId = resolveCollision(sanitized, static_cast<int>(i), assigned);

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

// ── Deactivate ─────────────────────────────────────────────────────────────

void VideoCompanion::deactivate()
{
    active_.store(false, std::memory_order_relaxed);
    stopWebSocketServer();
    currentRoom_.clear();
    currentPush_.clear();
    currentCompanionUrl_.clear();
}

}  // namespace jamwide
