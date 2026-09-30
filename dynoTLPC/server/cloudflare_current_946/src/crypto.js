const enc = new TextEncoder();
const dec = new TextDecoder();

export function utf8(s) { return enc.encode(String(s)); }
export function utf8String(bytes) { return dec.decode(bytes); }

export function concatBytes(...parts) {
  const total = parts.reduce((n, p) => n + p.length, 0);
  const out = new Uint8Array(total);
  let o = 0;
  for (const p of parts) { out.set(p, o); o += p.length; }
  return out;
}

export function hexToBytes(hex) {
  hex = String(hex).trim();
  if (hex.length % 2 || !/^[0-9a-fA-F]*$/.test(hex)) throw new Error("bad hex");
  const out = new Uint8Array(hex.length / 2);
  for (let i = 0; i < out.length; i++) out[i] = parseInt(hex.slice(i * 2, i * 2 + 2), 16);
  return out;
}

export function bytesToHex(bytes) {
  let s = "";
  for (const b of bytes) s += b.toString(16).padStart(2, "0");
  return s;
}

export function bytesToB64(bytes) {
  // Chunked to avoid argument/string limits on the ~450 KB Dyno payload.
  const CHUNK = 0x6000;
  let binary = "";
  for (let i = 0; i < bytes.length; i += CHUNK) {
    const chunk = bytes.subarray(i, Math.min(i + CHUNK, bytes.length));
    binary += String.fromCharCode(...chunk);
  }
  return btoa(binary);
}

export function b64ToBytes(s) {
  const raw = atob(String(s));
  const out = new Uint8Array(raw.length);
  for (let i = 0; i < raw.length; i++) out[i] = raw.charCodeAt(i);
  return out;
}

export function bytesToB64Url(bytes) {
  return bytesToB64(bytes).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/g, "");
}

export function b64UrlToBytes(s) {
  s = String(s).replace(/-/g, "+").replace(/_/g, "/");
  while (s.length % 4) s += "=";
  return b64ToBytes(s);
}

export async function sha256Bytes(data) {
  const view = typeof data === "string" ? utf8(data) : data;
  return new Uint8Array(await crypto.subtle.digest("SHA-256", view));
}

export async function sha256Hex(data) { return bytesToHex(await sha256Bytes(data)); }

export function randomBytes(n) {
  const out = new Uint8Array(n);
  crypto.getRandomValues(out);
  return out;
}

export function randomB64Url(n = 24) { return bytesToB64Url(randomBytes(n)); }

export function normalizeKey(key) { return String(key ?? "").trim().toUpperCase(); }

// Exact DynoTL launcher/server derivation recovered from the supplied binaries:
// SHA256("DynoTL-session-v1\\0" || ECDH_shared || "\\0" || client_nonce || "\\0" || hwid)
export async function deriveDynoSessionKey(sharedSecret, clientNonce, hwid) {
  const material = concatBytes(
    utf8("DynoTL-session-v1"), new Uint8Array([0]),
    sharedSecret,
    new Uint8Array([0]), utf8(clientNonce),
    new Uint8Array([0]), utf8(hwid)
  );
  return sha256Bytes(material);
}

export function activationCanonical({clientNonce, hwid, expiresAt, serverPubB64, payloadNonceB64, payloadSha256, cipherSha256, sessionToken}) {
  return [
    "ACT1", clientNonce, hwid, String(expiresAt), serverPubB64,
    payloadNonceB64, payloadSha256, cipherSha256, sessionToken
  ].join("\n");
}

export function heartbeatCanonical({nonce, hwid, expiresAt, sessionToken}) {
  return ["HB1", nonce, hwid, String(expiresAt), sessionToken].join("\n");
}

export function ed25519Pkcs8FromSeed(seed32) {
  if (seed32.length !== 32) throw new Error("Ed25519 seed must be 32 bytes");
  const prefix = hexToBytes("302e020100300506032b657004220420");
  return concatBytes(prefix, seed32);
}

export async function importEd25519PrivateFromSeedHex(seedHex) {
  const pkcs8 = ed25519Pkcs8FromSeed(hexToBytes(seedHex));
  return crypto.subtle.importKey("pkcs8", pkcs8, {name: "Ed25519"}, false, ["sign"]);
}

export async function signEd25519B64(privateKey, text) {
  const sig = await crypto.subtle.sign({name: "Ed25519"}, privateKey, utf8(text));
  return bytesToB64(new Uint8Array(sig));
}

export async function importX25519PublicB64(clientPubB64) {
  const raw = b64ToBytes(clientPubB64);
  if (raw.length !== 32) throw new Error("client_pub must decode to 32 bytes");
  return crypto.subtle.importKey("raw", raw, {name: "X25519"}, false, []);
}

export async function generateX25519() {
  return crypto.subtle.generateKey({name: "X25519"}, true, ["deriveBits"]);
}

export async function x25519Shared(privateKey, publicKey) {
  return new Uint8Array(await crypto.subtle.deriveBits({name: "X25519", public: publicKey}, privateKey, 256));
}

export async function aesGcmEncryptRaw(key32, nonce12, plaintext, aad) {
  const key = await crypto.subtle.importKey("raw", key32, {name: "AES-GCM"}, false, ["encrypt"]);
  const out = await crypto.subtle.encrypt(
    {name: "AES-GCM", iv: nonce12, additionalData: aad, tagLength: 128},
    key, plaintext
  );
  return new Uint8Array(out); // ciphertext || 16-byte tag (Go-compatible)
}

export async function aesGcmDecryptRaw(key32, nonce12, ciphertextTag, aad) {
  const key = await crypto.subtle.importKey("raw", key32, {name: "AES-GCM"}, false, ["decrypt"]);
  const out = await crypto.subtle.decrypt(
    {name: "AES-GCM", iv: nonce12, additionalData: aad, tagLength: 128},
    key, ciphertextTag
  );
  return new Uint8Array(out);
}

export async function importHmacKeyHex(secretHex) {
  const raw = hexToBytes(secretHex);
  if (raw.length < 32) throw new Error("SESSION_SECRET_HEX must be at least 32 bytes");
  return crypto.subtle.importKey("raw", raw, {name: "HMAC", hash: "SHA-256"}, false, ["sign", "verify"]);
}

export async function hmacB64Url(key, text) {
  const sig = await crypto.subtle.sign("HMAC", key, utf8(text));
  return bytesToB64Url(new Uint8Array(sig));
}

export async function verifyHmacB64Url(key, text, sigB64Url) {
  let sig;
  try { sig = b64UrlToBytes(sigB64Url); } catch { return false; }
  return crypto.subtle.verify("HMAC", key, sig, utf8(text));
}