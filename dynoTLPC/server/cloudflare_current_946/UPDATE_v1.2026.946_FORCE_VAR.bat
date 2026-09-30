@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title DynoTL Cloud Update v1.2026.946 - FORCE APP_VERSION

set "TARGET_VERSION=v1.2026.946"

echo ================================================
echo   DynoTL Cloud Update %TARGET_VERSION%
echo   FORCE APP_VERSION DURING WRANGLER DEPLOY
echo ================================================
echo.

if not exist "wrangler.jsonc" (
  echo [ERROR] wrangler.jsonc was not found.
  echo Put this BAT and the update_payload folder inside your existing DynoTL_Cloudflare_Server folder.
  pause
  exit /b 1
)
if not exist "src\index.js" (
  echo [ERROR] src\index.js was not found. This is not the correct server folder.
  pause
  exit /b 1
)
if not exist "update_payload\app_payload.html" (
  echo [ERROR] update_payload\app_payload.html was not found.
  pause
  exit /b 1
)

if not exist "backup" mkdir "backup"
if exist "src\app_payload.html" copy /y "src\app_payload.html" "backup\app_payload_before_%TARGET_VERSION%.html" >nul
copy /y "update_payload\app_payload.html" "src\app_payload.html" >nul
if errorlevel 1 (
  echo [ERROR] Could not copy app_payload.html.
  pause
  exit /b 1
)

echo [OK] %TARGET_VERSION% payload installed locally.

rem Best-effort update of the local Wrangler config for readability.
rem The deploy command below ALSO forces APP_VERSION, so deployment does not depend on this regex succeeding.
powershell -NoProfile -ExecutionPolicy Bypass -Command "$p='wrangler.jsonc'; $s=[IO.File]::ReadAllText($p); if($s -match '(?m)([\"'']?APP_VERSION[\"'']?\s*:\s*[\"''])[^\"'']*([\"''])'){ $s=[regex]::Replace($s,'(?m)([\"'']?APP_VERSION[\"'']?\s*:\s*[\"''])[^\"'']*([\"''])','$1v1.2026.946$2',1) }; [IO.File]::WriteAllText($p,$s,(New-Object Text.UTF8Encoding($false)))"

if exist "tools\verify_cloud.mjs" powershell -NoProfile -ExecutionPolicy Bypass -Command "$p='tools\verify_cloud.mjs'; $s=[IO.File]::ReadAllText($p); $s=[regex]::Replace($s,'v1\.2026\.\d+','v1.2026.946'); [IO.File]::WriteAllText($p,$s,(New-Object Text.UTF8Encoding($false)))"

echo.
echo === DEPLOYING TO CLOUDFLARE ===
echo [INFO] Forcing APP_VERSION=%TARGET_VERSION% in Wrangler deploy command.
echo.

call npx wrangler deploy --config "wrangler.jsonc" --var "APP_VERSION:%TARGET_VERSION%"
if errorlevel 1 (
  echo.
  echo [ERROR] Deployment failed. Your previous app backup is still in the backup folder.
  pause
  exit /b 1
)

echo.
echo ================================================
echo [DONE] DynoTL %TARGET_VERSION% deployed.
echo.
echo CHECK THE WRANGLER OUTPUT ABOVE.
echo It MUST show:
echo   env.APP_VERSION ("%TARGET_VERSION%")
echo.
echo If it shows another version, DO NOT trust this deployment.
echo ================================================
echo.
echo === LIVE SERVER CHECK ===
curl -s --max-time 10 https://dynotl-cloud.dynotl-quyen.workers.dev
echo.
echo Expected app_version: %TARGET_VERSION%
echo.
pause