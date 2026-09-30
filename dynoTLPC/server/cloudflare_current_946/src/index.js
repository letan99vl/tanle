import appHtml from "./app_payload.html";
import adminHtml from "./admin.html";
import {
  utf8, utf8String, bytesToB64, bytesToB64Url, b64UrlToBytes,
  sha256Bytes, sha256Hex, randomBytes, randomB64Url, normalizeKey,
  deriveDynoSessionKey, activationCanonical, heartbeatCanonical,
  importEd25519PrivateFromSeedHex, signEd25519B64,
  importX25519PublicB64, generateX25519, x25519Shared,
  aesGcmEncryptRaw, importHmacKeyHex, hmacB64Url, verifyHmacB64Url
} from "./crypto.js";

const APP_BYTES = utf8(appHtml);
let appShaPromise;
let signingKeyCache = { secret: null, promise: null };
let hmacKeyCache = { secret: null, promise: null };

function nowSec() { return Math.floor(Date.now() / 1000); }

function json(data, status = 200, extraHeaders = {}) {
  return new Response(JSON.stringify(data), {
    status,
    headers: {
      "content-type": "application/json; charset=utf-8",
      "cache-control": "no-store, no-cache, must-revalidate",
      "pragma": "no-cache",
      ...extraHeaders,
    },
  });
}

function html(body, status = 200) {
  return new Response(body, {
    status,
    headers: {
      "content-type": "text/html; charset=utf-8",
      "cache-control": "no-store",
      "x-content-type-options": "nosniff",
      "referrer-policy": "no-referrer",
    },
  });
}

function text(body, status = 200) {
  return new Response(body, {status, headers: {"content-type": "text/plain; charset=utf-8", "cache-control": "no-store"}});
}

function cleanHwid(v) {
  const s = String(v ?? "").trim().toLowerCase();
  return /^[0-9a-f]{64}$/.test(s) ? s : null;
}

function safeNonce(v) {
  const s = String(v ?? "").trim();
  // Accept both standard Base64 (+/ and optional =) and URL-safe Base64 (-_).
  // The DynoTL launcher has used Base64 nonces in more than one build.
  return /^[A-Za-z0-9+\/_=-]{16,200}$/.test(s) ? s : null;
}

function safeVersion(v) {
  return String(v ?? "").replace(/[\r\n\0]/g, "").slice(0, 80);
}

async function readJson(request, maxBytes = 64 * 1024) {
  const len = Number(request.headers.get("content-length") || 0);
  if (len > maxBytes) throw new Error("body_too_large");
  const t = await request.text();
  if (t.length > maxBytes) throw new Error("body_too_large");
  return JSON.parse(t || "{}");
}

function requireConfig(env) {
  const missing = [];
  if (!env.DB) missing.push("DB");
  if (!env.SIGNING_SEED_HEX) missing.push("SIGNING_SEED_HEX");
  if (!env.SESSION_SECRET_HEX) missing.push("SESSION_SECRET_HEX");
  if (!env.ADMIN_TOKEN) missing.push("ADMIN_TOKEN");
  if (missing.length) throw new Error("missing_config:" + missing.join(","));
}

async function signingKey(env) {
  if (signingKeyCache.secret !== env.SIGNING_SEED_HEX) {
    signingKeyCache = {secret: env.SIGNING_SEED_HEX, promise: importEd25519PrivateFromSeedHex(env.SIGNING_SEED_HEX)};
  }
  return signingKeyCache.promise;
}

async function sessionHmacKey(env) {
  if (hmacKeyCache.secret !== env.SESSION_SECRET_HEX) {
    hmacKeyCache = {secret: env.SESSION_SECRET_HEX, promise: importHmacKeyHex(env.SESSION_SECRET_HEX)};
  }
  return hmacKeyCache.promise;
}

async function appSha256() {
  if (!appShaPromise) appShaPromise = sha256Hex(APP_BYTES);
  return appShaPromise;
}

async function keyHash(key) { return sha256Hex(normalizeKey(key)); }

async function makeSessionToken(env, license) {
  const bodyObj = {
    v: 1,
    kh: license.key_hash,
    hw: license.hwid_hash,
    exp: Number(license.expires_at),
    ep: Number(license.session_epoch || 0),
    j: randomB64Url(12),
  };
  const body = bytesToB64Url(utf8(JSON.stringify(bodyObj)));
  const sig = await hmacB64Url(await sessionHmacKey(env), body);
  return `v1.${body}.${sig}`;
}

async function parseSessionToken(env, token) {
  try {
    const parts = String(token ?? "").split(".");
    if (parts.length !== 3 || parts[0] !== "v1") return null;
    const ok = await verifyHmacB64Url(await sessionHmacKey(env), parts[1], parts[2]);
    if (!ok) return null;
    const obj = JSON.parse(utf8String(b64UrlToBytes(parts[1])));
    if (obj?.v !== 1 || !/^[0-9a-f]{64}$/.test(obj.kh || "") || !/^[0-9a-f]{64}$/.test(obj.hw || "")) return null;
    if (!Number.isInteger(obj.exp) || !Number.isInteger(obj.ep)) return null;
    return obj;
  } catch {
    return null;
  }
}

async function handlePing(request, env) {
  return json({ok: true, service: "DynoTL Cloud", server_time: nowSec(), app_version: env.APP_VERSION || "unknown"});
}

async function handleActivate(request, env) {
  if (request.method !== "POST") return json({ok: false, error: "method_not_allowed"}, 405);
  requireConfig(env);

  let body;
  try { body = await readJson(request); }
  catch { return json({ok: false, error: "bad_request"}); }

  const key = normalizeKey(body.key);
  const hwid = cleanHwid(body.hwid);
  const clientNonce = safeNonce(body.client_nonce);
  const launcherVersion = safeVersion(body.launcher_version);
  const clientPubB64 = String(body.client_pub ?? "").trim();

  if (!key) return json({ok: false, error: "invalid_key"});
  if (!hwid) return json({ok: false, error: "bad_hwid"});
  if (!clientNonce) return json({ok: false, error: "bad_client_nonce"});

  let clientPub;
  try { clientPub = await importX25519PublicB64(clientPubB64); }
  catch { return json({ok: false, error: "bad_client_pub"}); }

  const kh = await keyHash(key);
  let lic = await env.DB.prepare("SELECT * FROM licenses WHERE key_hash = ?1").bind(kh).first();
  if (!lic) return json({ok: false, error: "invalid_key"});
  if (Number(lic.revoked)) return json({ok: false, error: "revoked_or_expired"});

  const now = nowSec();

  // First successful activation binds this license to the machine and starts its duration.
  if (!lic.hwid_hash) {
    const exp = now + Math.max(1, Number(lic.duration_days || 1)) * 86400;
    await env.DB.prepare(`
      UPDATE licenses
      SET hwid_hash = ?1, activated_at = ?2, expires_at = ?3,
          last_activation_at = ?2, launcher_version = ?4
      WHERE key_hash = ?5 AND hwid_hash IS NULL AND revoked = 0
    `).bind(hwid, now, exp, launcherVersion, kh).run();
    lic = await env.DB.prepare("SELECT * FROM licenses WHERE key_hash = ?1").bind(kh).first();
  }

  if (!lic || Number(lic.revoked)) return json({ok: false, error: "revoked_or_expired"});
  if (String(lic.hwid_hash || "").toLowerCase() !== hwid) return json({ok: false, error: "hwid_mismatch"});
  if (!lic.expires_at || Number(lic.expires_at) <= now) return json({ok: false, error: "revoked_or_expired"});

  await env.DB.prepare("UPDATE licenses SET last_activation_at = ?1, launcher_version = ?2 WHERE key_hash = ?3")
    .bind(now, launcherVersion, kh).run();

  // Reload epoch in case admin kicked/reset the key between reads.
  lic = await env.DB.prepare("SELECT * FROM licenses WHERE key_hash = ?1").bind(kh).first();
  const expiresAt = Number(lic.expires_at);
  const sessionToken = await makeSessionToken(env, lic);

  try {
    const serverPair = await generateX25519();
    const shared = await x25519Shared(serverPair.privateKey, clientPub);
    const sessionKey = await deriveDynoSessionKey(shared, clientNonce, hwid);
    const payloadNonce = randomBytes(12);
    const aadText = `v1|${clientNonce}|${hwid}|${expiresAt}|${sessionToken}`;
    const cipher = await aesGcmEncryptRaw(sessionKey, payloadNonce, APP_BYTES, utf8(aadText));

    const serverPubRaw = new Uint8Array(await crypto.subtle.exportKey("raw", serverPair.publicKey));
    const serverPubB64 = bytesToB64(serverPubRaw);
    const payloadNonceB64 = bytesToB64(payloadNonce);
    const payloadSha = await appSha256();
    const cipherSha = await sha256Hex(cipher);
    const canonical = activationCanonical({
      clientNonce, hwid, expiresAt, serverPubB64,
      payloadNonceB64, payloadSha256: payloadSha,
      cipherSha256: cipherSha, sessionToken,
    });
    const signature = await signEd25519B64(await signingKey(env), canonical);

    return json({
      ok: true,
      expires_at: expiresAt,
      server_time: now,
      server_pub: serverPubB64,
      payload_nonce: payloadNonceB64,
      payload_sha256: payloadSha,
      cipher_sha256: cipherSha,
      session_token: sessionToken,
      signature,
      payload: bytesToB64(cipher),
    });
  } catch (e) {
    console.error("activate crypto error", e?.stack || e);
    return json({ok: false, error: "server_crypto_error"});
  }
}

async function handleHeartbeat(request, env) {
  if (request.method !== "POST") return json({ok: false, error: "method_not_allowed"}, 405);
  requireConfig(env);

  let body;
  try { body = await readJson(request); }
  catch { return json({ok: false, error: "bad_request"}); }

  const sessionToken = String(body.session_token ?? "").trim();
  const hwid = cleanHwid(body.hwid);
  const nonce = safeNonce(body.nonce);
  if (!sessionToken || !hwid || !nonce) return json({ok: false, error: "bad_heartbeat"});

  const token = await parseSessionToken(env, sessionToken);
  if (!token) return json({ok: false, error: "invalid_session"});
  if (token.hw !== hwid) return json({ok: false, error: "hwid_mismatch"});

  const now = nowSec();
  if (token.exp <= now) return json({ok: false, error: "expired"});

  const lic = await env.DB.prepare("SELECT key_hash, hwid_hash, expires_at, revoked, session_epoch FROM licenses WHERE key_hash = ?1")
    .bind(token.kh).first();
  if (!lic || Number(lic.revoked)) return json({ok: false, error: "revoked_or_expired"});
  if (String(lic.hwid_hash || "").toLowerCase() !== hwid) return json({ok: false, error: "hwid_mismatch"});
  if (Number(lic.expires_at || 0) <= now) return json({ok: false, error: "expired"});
  if (Number(lic.session_epoch || 0) !== Number(token.ep)) return json({ok: false, error: "session_revoked"});
  if (Number(lic.expires_at) !== Number(token.exp)) return json({ok: false, error: "session_revoked"});

  const expiresAt = Number(lic.expires_at);
  const canonical = heartbeatCanonical({nonce, hwid, expiresAt, sessionToken});
  const signature = await signEd25519B64(await signingKey(env), canonical);
  return json({ok: true, expires_at: expiresAt, signature});
}

function adminAuthorized(request, env) {
  const auth = request.headers.get("authorization") || "";
  if (!auth.startsWith("Bearer ")) return false;
  const provided = auth.slice(7).trim();
  return provided.length >= 20 && provided === String(env.ADMIN_TOKEN || "");
}

const KEY_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
function generateLicenseKey() {
  const bytes = randomBytes(16);
  let s = "";
  for (let i = 0; i < 16; i++) s += KEY_ALPHABET[bytes[i] % KEY_ALPHABET.length];
  return `DTL-${s.slice(0,4)}-${s.slice(4,8)}-${s.slice(8,12)}-${s.slice(12,16)}`;
}

async function adminApi(request, env, path) {
  requireConfig(env);
  if (!adminAuthorized(request, env)) return json({ok: false, error: "unauthorized"}, 401);

  if (path === "/admin/api/ping") return json({ok: true, app_version: env.APP_VERSION || "unknown", server_time: nowSec()});

  if (path === "/admin/api/list") {
    const url = new URL(request.url);
    const q = (url.searchParams.get("q") || "").trim();
    let result;
    if (q) {
      const like = `%${q.slice(0, 80)}%`;
      result = await env.DB.prepare(`
        SELECT * FROM licenses
        WHERE key_value LIKE ?1 OR note LIKE ?1 OR hwid_hash LIKE ?1
        ORDER BY created_at DESC LIMIT 300
      `).bind(like).all();
    } else {
      result = await env.DB.prepare("SELECT * FROM licenses ORDER BY created_at DESC LIMIT 300").all();
    }
    return json({ok: true, licenses: result.results || []});
  }

  if (request.method !== "POST") return json({ok: false, error: "method_not_allowed"}, 405);
  let body;
  try { body = await readJson(request, 16 * 1024); }
  catch { return json({ok: false, error: "bad_request"}, 400); }

  if (path === "/admin/api/create") {
    const days = Math.floor(Number(body.duration_days));
    if (!Number.isFinite(days) || days < 1 || days > 36500) return json({ok: false, error: "duration_days must be 1..36500"}, 400);
    const note = String(body.note ?? "").trim().slice(0, 160);
    const manual = normalizeKey(body.key || "");
    if (manual && (!/^[A-Z0-9_-]{8,80}$/.test(manual))) return json({ok: false, error: "bad_key_format"}, 400);

    for (let attempt = 0; attempt < 8; attempt++) {
      const key = manual || generateLicenseKey();
      const kh = await keyHash(key);
      try {
        await env.DB.prepare(`
          INSERT INTO licenses(key_hash,key_value,note,duration_days,created_at,revoked,session_epoch)
          VALUES(?1,?2,?3,?4,?5,0,0)
        `).bind(kh, key, note, days, nowSec()).run();
        return json({ok: true, key, duration_days: days, note});
      } catch (e) {
        if (manual) return json({ok: false, error: "key_already_exists"}, 409);
        if (attempt === 7) throw e;
      }
    }
  }

  const kh = String(body.key_hash ?? "").toLowerCase();
  if (!/^[0-9a-f]{64}$/.test(kh)) return json({ok: false, error: "bad_key_hash"}, 400);

  if (path === "/admin/api/revoke") {
    await env.DB.prepare("UPDATE licenses SET revoked=1, session_epoch=session_epoch+1 WHERE key_hash=?1").bind(kh).run();
    return json({ok: true});
  }
  if (path === "/admin/api/unrevoke") {
    await env.DB.prepare("UPDATE licenses SET revoked=0 WHERE key_hash=?1").bind(kh).run();
    return json({ok: true});
  }
  if (path === "/admin/api/kick") {
    await env.DB.prepare("UPDATE licenses SET session_epoch=session_epoch+1 WHERE key_hash=?1").bind(kh).run();
    return json({ok: true});
  }
  if (path === "/admin/api/reset") {
    await env.DB.prepare(`
      UPDATE licenses SET hwid_hash=NULL, activated_at=NULL, expires_at=NULL,
        last_activation_at=NULL, launcher_version='', session_epoch=session_epoch+1
      WHERE key_hash=?1
    `).bind(kh).run();
    return json({ok: true});
  }
  if (path === "/admin/api/delete") {
    await env.DB.prepare("DELETE FROM licenses WHERE key_hash=?1").bind(kh).run();
    return json({ok: true});
  }

  return json({ok: false, error: "not_found"}, 404);
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    try {
      if (url.pathname === "/" || url.pathname === "/api/ping") return handlePing(request, env);
      if (url.pathname === "/api/activate") return handleActivate(request, env);
      if (url.pathname === "/api/heartbeat") return handleHeartbeat(request, env);
      if (url.pathname === "/admin") return html(adminHtml);
      if (url.pathname.startsWith("/admin/api/")) return adminApi(request, env, url.pathname);
      if (url.pathname === "/api/version") return json({ok: true, version: env.APP_VERSION || "unknown", server_time: nowSec()});
      return text("DynoTL Cloud Server\n", 404);
    } catch (e) {
      console.error("request error", e?.stack || e);
      if (String(e?.message || "").startsWith("missing_config:")) return json({ok:false,error:String(e.message)}, 500);
      return json({ok: false, error: "server_error"}, 500);
    }
  },
};