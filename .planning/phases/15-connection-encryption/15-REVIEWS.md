---
phase: 15
reviewers: [codex]
reviewed_at: 2026-04-11T00:00:00Z
plans_reviewed: [15-01-PLAN.md, 15-02-PLAN.md]
---

# Cross-AI Plan Review — Phase 15

## Codex Review

### Plan 15-01 Review

#### Summary
Plan 15-01 is a solid first wave structurally: isolate crypto behind a small module, prove it with unit tests, and get OpenSSL wired into the build before touching protocol code. That sequencing is correct. The main weaknesses are a few test expectations that do not match AES-CBC behavior, and the fact that the plan does not yet define the API shape needed by the integration layer for deterministic testing, buffer ownership, and secure failure handling.

#### Strengths
- Good separation of concerns: crypto implementation is isolated from protocol integration.
- Correct dependency ordering: build/linking and unit coverage first, protocol wiring second.
- Scope is reasonable for Wave 1 and avoids mixing handshake changes with low-level crypto validation.
- Testing focus is appropriate for a security-sensitive module.
- Using OpenSSL EVP keeps the implementation at the right abstraction level.
- Threat model is present instead of being hand-waved.

#### Concerns
- **HIGH**: The proposed `zero-length passthrough` test appears inconsistent with AES-CBC + PKCS padding and the stated design of `random IV per message` plus `IV prepended to payload`. Zero-length plaintext should still produce encrypted output, not a passthrough.
- **HIGH**: `corrupted ciphertext` / `wrong key` expectations are underspecified. With CBC and no MAC, many corruptions will decrypt to garbage and only some will fail on padding. Tests that assume reliable tamper detection will be false confidence.
- **MEDIUM**: A known-vector test requires deterministic IV control, but the plan only mentions random IV generation. The module likely needs a test-only or internal API path that accepts an explicit IV.
- **MEDIUM**: No explicit API contract for size expansion and output ownership. Integration will need exact overhead behavior: `cipher_len = padded(plain_len) + IV_len`.
- **MEDIUM**: The plan mentions DoS via allocation, but not an explicit size cap inside the crypto API. Relying only on caller-side limits is brittle.
- **LOW**: Cross-platform OpenSSL discovery is called out, but Windows packaging/runtime DLL behavior is not. Linking may succeed while CI artifacts fail at runtime.

#### Suggestions
- Replace `zero-length passthrough` with a test that verifies zero-length plaintext produces a valid encrypted payload and round-trips correctly.
- Define test expectations for tampering narrowly: padding failure may be detected, but integrity is not guaranteed without MAC.
- Add an API or helper for deterministic IV injection in tests only.
- Add explicit tests for exact ciphertext overhead for representative sizes, decrypt failure on payload shorter than IV, decrypt failure on non-block-aligned ciphertext after IV removal, maximum accepted payload size.
- Specify whether the crypto API returns a new buffer or mutates caller-provided storage.
- Add a clear error contract: no partial plaintext output on decrypt failure.

#### Risk Assessment
**MEDIUM**. The wave is well-scoped and achievable, but several planned tests currently encode incorrect security assumptions. If those are corrected, this becomes low-to-medium risk.

---

### Plan 15-02 Review

#### Summary
Plan 15-02 has the right implementation surface area, but it currently has a major protocol-design gap: as written, it does not convincingly satisfy `SEC-01` while also preserving graceful fallback. The current code path is `server challenge -> client auth_user -> server auth_reply`, and the plan says key derivation happens after auth reply. That is too late to encrypt credentials. More broadly, capability negotiation is under-specified, server-side dependency is under-scoped, and the send/receive integration details need tighter definition to avoid lifetime, size, and downgrade bugs.

#### Strengths
- Correctly targets the real integration points in this repo.
- Keeps encryption below higher-level message parsing, which matches the framing model.
- Includes fallback behavior and max-size handling explicitly.
- Includes CI changes, which matters because OpenSSL is a build-system dependency.
- Key scrubbing is at least mentioned.

#### Concerns
- **HIGH**: The plan says `derive key after auth reply`. That cannot satisfy `SEC-01`, because credentials are already sent in `MESSAGE_CLIENT_AUTH_USER` before the auth reply arrives.
- **HIGH**: Capability negotiation is incomplete for backward-compatible encrypted credentials. In the current protocol flow, the client needs to know whether to encrypt `auth_user` before sending it. A flag only in `auth_reply` is too late.
- **HIGH**: `Silent fallback` creates downgrade ambiguity. If the client opportunistically falls back to plaintext when encryption is unavailable, the retry/decision rules must be explicit or an active attacker can force downgrade.
- **HIGH**: `Task 1 Part G` is under-scoped. This repo does not appear to contain the `ninbot` server implementation, so "shared Net_Connection gets it automatically" is not a sufficient execution plan.
- **MEDIUM**: Send-path integration is underspecified relative to current `Net_Connection::Run()`. Messages are queued and may be partially sent over multiple iterations.
- **MEDIUM**: Receive-path integration needs exact sequencing. Decrypt can only happen after the full payload is buffered.
- **MEDIUM**: Key scrubbing is too narrow. Clearing `m_encryption_key` is good, but the plaintext password and derived material may still exist in other objects.
- **LOW**: Header-cleartext disclosure is accepted by decision, but the plan should be careful not to overstate "end-to-end" confidentiality.

#### Suggestions
- Redesign the negotiation flow so encrypted credentials are possible without breaking fallback. The cleanest option is to advertise server encryption support in the auth challenge, not only after auth reply.
- If opportunistic fallback is required, define exact behavior.
- Make `SEC-01` explicit in tests. Add an integration test that proves `MESSAGE_CLIENT_AUTH_USER` is encrypted on capable servers before auth completion.
- Split client and server work into separate plans or explicit deliverables.
- Specify send/receive mechanics precisely.
- Standardize failure handling for decrypt errors to reduce padding-oracle leakage.

#### Risk Assessment
**HIGH**. The implementation surface is understandable, but the current handshake plan has a protocol-ordering flaw that blocks the phase goal. Until encrypted credentials plus graceful fallback are specified coherently, this wave is high risk.

---

### Overall Assessment (Codex)

The two-wave split is good, but the phase is not yet fully plan-complete because the protocol negotiation design is still internally inconsistent with the stated success criteria. Plan 15-01 is mostly sound after test corrections. Plan 15-02 needs revision before execution, specifically around when encryption becomes active, how legacy fallback works without breaking `SEC-01`, and how server-side support is actually delivered.

---

## Consensus Summary

### Agreed Strengths
- Two-wave split (crypto module first, integration second) is architecturally correct
- TDD approach for security-critical crypto code is appropriate
- Threat model presence is positive
- OpenSSL EVP is the right abstraction level

### Agreed Concerns
1. **Protocol ordering flaw (HIGH)**: Key derivation happens after auth reply, but SEC-01 requires credentials in `MESSAGE_CLIENT_AUTH_USER` to be encrypted — which is sent BEFORE auth reply. The encryption negotiation timeline doesn't protect the auth message itself.
2. **Zero-length passthrough semantics (HIGH)**: The plan treats zero-length payloads as passthrough (no encryption), but this is a design choice that needs explicit justification vs. encrypting the empty payload.
3. **Server-side delivery gap (HIGH)**: ninbot server code may not be in this repository, making Part G under-scoped.
4. **Downgrade attack surface (MEDIUM)**: Silent fallback without explicit rules could allow active attackers to force unencrypted connections.
5. **Test assumptions about tamper detection (MEDIUM)**: CBC without MAC means corrupted ciphertext may produce garbage plaintext rather than reliably failing.

### Divergent Views
- None (single reviewer)

---

*Review conducted: 2026-04-11*
*Reviewer: Codex (OpenAI)*
