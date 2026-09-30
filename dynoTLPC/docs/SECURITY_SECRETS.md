# Security / Secrets — DO NOT COMMIT

The recovered server deploy scripts expect three secrets:

- `SIGNING_SEED_HEX`
- `SESSION_SECRET_HEX`
- `ADMIN_TOKEN`

The old server folder referred to local private files:

- `private/DO_NOT_SHARE_signing_seed.txt`
- `private/DO_NOT_SHARE_session_secret.txt`
- `private/DO_NOT_SHARE_admin_token.txt`

These files are intentionally **not included** in the cleaned GitHub-ready source and should never be put in a public repository.

Also excluded:

- `.wrangler/` local auth/cache
- `node_modules/`
- Wrangler account cache

The D1 database ID in `wrangler.jsonc` is infrastructure metadata, not the private signing secret, but keep deployment access controlled.

## Recovery warning

The Ed25519 signing seed is compatibility-critical. Existing launchers pin the corresponding public key. Losing/changing the signing seed may require rebuilding/replacing customer launchers.