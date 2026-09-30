@echo off
setlocal EnableExtensions
chcp 65001 >nul
TITLE DynoTL Firmware Server Check

set "FW=https://dynotl-firmware.dynotl-quyen.workers.dev"
set "META=%FW%/latest.json"
set "BIN=%FW%/firmware.bin"
set "ESP=https://unpkg.com/esptool-js@0.6.1/bundle.js"

echo ============================================================
echo DynoTL ESP32 Firmware Server Check
echo ============================================================
echo.
echo Firmware server: %FW%
echo.

echo [1/4] DNS check...
nslookup dynotl-firmware.dynotl-quyen.workers.dev 2>&1

echo.
echo [2/4] Checking latest.json...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
 "$ProgressPreference='SilentlyContinue'; try { $u='%META%?t=' + [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds(); $r=Invoke-WebRequest -UseBasicParsing -Uri $u -TimeoutSec 15; Write-Host ('HTTP: ' + [int]$r.StatusCode); Write-Host $r.Content; $m=$r.Content ^| ConvertFrom-Json; if(-not $m.version){throw 'Missing version'}; if(-not $m.sha256){throw 'Missing sha256'}; Write-Host ('VERSION: ' + $m.version); Write-Host ('SIZE: ' + $m.size); Write-Host ('OFFSET: ' + $m.offset + ' (expected 65536 / 0x10000)'); Write-Host ('CHIP: ' + $m.chip); Write-Host ('SHA256: ' + $m.sha256); exit 0 } catch { Write-Host ('ERROR: ' + $_.Exception.Message); if($_.Exception.Response){ try { Write-Host ('HTTP: ' + [int]$_.Exception.Response.StatusCode.value__) } catch {} }; exit 2 }"
set META_RC=%ERRORLEVEL%

echo.
echo [3/4] Checking firmware.bin and SHA-256...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
 "$ProgressPreference='SilentlyContinue'; $tmp=Join-Path $env:TEMP 'dynotl_firmware_check.bin'; try { $mr=Invoke-WebRequest -UseBasicParsing -Uri ('%META%?t=' + [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()) -TimeoutSec 15; $m=$mr.Content ^| ConvertFrom-Json; Invoke-WebRequest -UseBasicParsing -Uri ('%BIN%?t=' + [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()) -OutFile $tmp -TimeoutSec 30; $f=Get-Item $tmp; $h=(Get-FileHash -Algorithm SHA256 $tmp).Hash.ToLower(); Write-Host ('DOWNLOADED: ' + $f.Length + ' bytes'); Write-Host ('SHA256: ' + $h); if($m.size -and [int64]$m.size -ne $f.Length){Write-Host ('FAIL: size mismatch; server metadata=' + $m.size); exit 3}; if($m.sha256 -and $m.sha256.ToLower() -ne $h){Write-Host 'FAIL: SHA-256 mismatch'; exit 4}; Write-Host 'BIN + SHA256: OK'; Remove-Item $tmp -Force -ErrorAction SilentlyContinue; exit 0 } catch { Write-Host ('ERROR: ' + $_.Exception.Message); Remove-Item $tmp -Force -ErrorAction SilentlyContinue; exit 2 }"
set BIN_RC=%ERRORLEVEL%

echo.
echo [4/4] Checking esptool-js dependency...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
 "$ProgressPreference='SilentlyContinue'; try { $r=Invoke-WebRequest -UseBasicParsing -Uri '%ESP%' -TimeoutSec 20; Write-Host ('HTTP: ' + [int]$r.StatusCode); Write-Host ('SIZE: ' + $r.RawContentLength + ' bytes'); if($r.RawContentLength -lt 1000){throw 'esptool-js response is unexpectedly small'}; Write-Host 'ESPTOOL-JS: OK'; exit 0 } catch { Write-Host ('ERROR: ' + $_.Exception.Message); if($_.Exception.Response){ try { Write-Host ('HTTP: ' + [int]$_.Exception.Response.StatusCode.value__) } catch {} }; exit 2 }"
set ESP_RC=%ERRORLEVEL%

echo.
echo ============================================================
echo RESULT
echo ============================================================
if "%META_RC%"=="0" (echo latest.json     : OK) else (echo latest.json     : FAIL)
if "%BIN_RC%"=="0"  (echo firmware.bin   : OK) else (echo firmware.bin   : FAIL)
if "%ESP_RC%"=="0"  (echo esptool-js     : OK) else (echo esptool-js     : FAIL)
echo.
if "%META_RC%"=="0" if "%BIN_RC%"=="0" if "%ESP_RC%"=="0" (
  echo SERVER SIDE LOOKS OK.
  echo If DynoTL still cannot flash, the problem is likely Web Serial / COM / BOOT mode.
) else (
  echo ONE OR MORE SERVER/NETWORK CHECKS FAILED.
  echo Send a screenshot of this window to ChatGPT.
)
echo.
pause
endlocal