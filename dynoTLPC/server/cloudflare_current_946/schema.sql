CREATE TABLE IF NOT EXISTS licenses (
  key_hash TEXT PRIMARY KEY,
  key_value TEXT NOT NULL UNIQUE,
  note TEXT NOT NULL DEFAULT '',
  duration_days INTEGER NOT NULL,
  created_at INTEGER NOT NULL,
  activated_at INTEGER,
  expires_at INTEGER,
  hwid_hash TEXT,
  revoked INTEGER NOT NULL DEFAULT 0,
  session_epoch INTEGER NOT NULL DEFAULT 0,
  last_activation_at INTEGER,
  launcher_version TEXT NOT NULL DEFAULT ''
);

CREATE INDEX IF NOT EXISTS idx_licenses_created_at ON licenses(created_at DESC);
CREATE INDEX IF NOT EXISTS idx_licenses_expires_at ON licenses(expires_at);
CREATE INDEX IF NOT EXISTS idx_licenses_hwid_hash ON licenses(hwid_hash);