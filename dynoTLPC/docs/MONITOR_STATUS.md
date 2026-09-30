# DynoTL Monitor / Presence Status

## Surviving client artifact

`DynoTL_Setup_Universal_Monitor_v1.exe` contains the universal installer concept: install DynoTL from zero when missing, otherwise add/update Monitor. `DynoTLMonitor.exe` was observed in a working client installation.

## Intended monitor behavior recorded from prior work

- customer online/offline state;
- last seen timestamp;
- latest running executable names;
- monitor starts with DynoTL and exits when DynoTL closes;
- server stores the latest snapshot rather than full process history;
- intended privacy boundary: executable names only, not window titles/file contents.

## Important gap

The exact `UPDATE_MONITOR_SERVER_v1` / rollback source package is not present in the surviving files available when this archive was built. The recovered `src/index.js` in the old Cloudflare server backup contains the license/admin routes but not the later monitor presence/process routes.

Therefore:

- preserve the Monitor client EXE;
- do not claim `server/cloudflare_current_946` already supports Monitor presence;
- if monitor functionality is required, recover the old patch from another machine/backup or re-implement it as a separate additive API without changing `/api/activate`, `/api/heartbeat`, payload encryption or license routes.