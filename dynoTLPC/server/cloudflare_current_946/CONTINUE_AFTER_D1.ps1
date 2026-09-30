$ErrorActionPreference = 'Continue'
Set-Location $PSScriptRoot
$logPath = Join-Path $PSScriptRoot 'DEPLOY_LOG.txt'
try { Start-Transcript -Path $logPath -Append -Force | Out-Null } catch {}

function Step($s) { Write-Host "`n=== $s ===" -ForegroundColor Cyan }
function Fail($s) {
  Write-Host "`nLOI: $s" -ForegroundColor Red
  try { Stop-Transcript | Out-Null } catch {}
  Read-Host 'Nhan Enter de quay ve CMD' | Out-Null
  exit 1
}

Step 'Kiem tra dung thu muc DynoTL Cloud'
$required = @('wrangler.jsonc','src\index.js','schema.sql','private\DO_NOT_SHARE_signing_seed.txt','private\DO_NOT_SHARE_session_secret.txt','private\DO_NOT_SHARE_admin_token.txt')
foreach ($f in $required) {
  if (-not (Test-Path (Join-Path $PSScriptRoot $f))) {
    Fail "Khong tim thay $f. Hay dat 2 file CONTINUE_* vao CHINH thu muc DynoTL_Cloudflare_Server da chay DEPLOY.bat luc nay."
  }
}

Step 'Kiem tra Node / Wrangler / dang nhap'
if (-not (Get-Command node -ErrorAction SilentlyContinue)) { Fail 'Khong tim thay Node.js.' }
Write-Host (node --version)
& npx wrangler whoami
if ($LASTEXITCODE -ne 0) {
  Write-Host 'Wrangler chua dang nhap. Dang mo login...' -ForegroundColor Yellow
  & npx wrangler login
  if ($LASTEXITCODE -ne 0) { Fail 'Dang nhap Cloudflare that bai.' }
}

$configText = Get-Content (Join-Path $PSScriptRoot 'wrangler.jsonc') -Raw
if ($configText -notmatch 'd1_databases') {
  Fail 'wrangler.jsonc chua co D1 binding. Day khong phai thu muc vua tao D1 thanh cong. Hay chay file nay trong dung thu muc cu.'
}

Step 'Deploy Worker lan dau - neu Cloudflare hoi workers.dev subdomain thi chon Y/Yes'
& npx wrangler deploy
if ($LASTEXITCODE -ne 0) { Fail 'Deploy Worker lan dau that bai. Loi nam ngay phia tren va da duoc ghi vao DEPLOY_LOG.txt.' }

$signingSeed = (Get-Content (Join-Path $PSScriptRoot 'private\DO_NOT_SHARE_signing_seed.txt') -Raw).Trim()
$sessionSecret = (Get-Content (Join-Path $PSScriptRoot 'private\DO_NOT_SHARE_session_secret.txt') -Raw).Trim()
$adminToken = (Get-Content (Join-Path $PSScriptRoot 'private\DO_NOT_SHARE_admin_token.txt') -Raw).Trim()

Step 'Nap khoa bi mat len Cloudflare Secrets'
$signingSeed | & npx wrangler secret put SIGNING_SEED_HEX
if ($LASTEXITCODE -ne 0) { Fail 'Khong nap duoc SIGNING_SEED_HEX.' }
$sessionSecret | & npx wrangler secret put SESSION_SECRET_HEX
if ($LASTEXITCODE -ne 0) { Fail 'Khong nap duoc SESSION_SECRET_HEX.' }
$adminToken | & npx wrangler secret put ADMIN_TOKEN
if ($LASTEXITCODE -ne 0) { Fail 'Khong nap duoc ADMIN_TOKEN.' }

Step 'Deploy ban chinh thuc'
# Chay qua cmd de stderr cua Wrangler khong lam PowerShell 5 dung dot ngot.
$deployLines = & cmd.exe /d /s /c "npx wrangler deploy 2>&1"
$deployCode = $LASTEXITCODE
$deployOut = ($deployLines | ForEach-Object { [string]$_ }) -join "`n"
Write-Host $deployOut
if ($deployCode -ne 0) { Fail 'Deploy cuoi that bai.' }

$m = [regex]::Match($deployOut, 'https://[a-zA-Z0-9.-]+\.workers\.dev')
if ($m.Success) {
  $serverUrl = $m.Value
} else {
  $serverUrl = Read-Host 'Dan URL workers.dev vua hien o tren vao day (vd https://dynotl-cloud.tenban.workers.dev)'
}
$serverUrl = $serverUrl.Trim().TrimEnd('/')
if ($serverUrl -notmatch '^https://') { Fail 'URL khong hop le.' }
Set-Content -Path (Join-Path $PSScriptRoot 'SERVER_URL.txt') -Value $serverUrl -Encoding ASCII

Step 'Ping server'
try {
  $ping = Invoke-RestMethod -Uri ($serverUrl + '/api/ping') -Method Get -TimeoutSec 30
  Write-Host ("OK - " + $ping.service + " - " + $ping.app_version) -ForegroundColor Green
} catch {
  Write-Host ("Ping chua thanh cong: " + $_.Exception.Message) -ForegroundColor Yellow
  Write-Host 'Neu day la workers.dev moi tao lan dau, co the doi 30-60 giay roi thu URL tren trinh duyet.' -ForegroundColor Yellow
}

Step 'Self-test DynoTL'
if (Test-Path (Join-Path $PSScriptRoot 'tools\verify_cloud.mjs')) {
  & node (Join-Path $PSScriptRoot 'tools\verify_cloud.mjs') $serverUrl
  if ($LASTEXITCODE -ne 0) {
    Write-Host 'Self-test chua pass. CHUA doi launcher sang cloud. Gui DEPLOY_LOG.txt cho toi.' -ForegroundColor Red
  } else {
    Write-Host 'DYNOTL CLOUD PROTOCOL: OK' -ForegroundColor Green
  }
} else {
  Write-Host 'Khong co tools\verify_cloud.mjs, bo qua self-test.' -ForegroundColor Yellow
}

Write-Host "`nSERVER: $serverUrl" -ForegroundColor Green
Write-Host "ADMIN : $serverUrl/admin" -ForegroundColor Green
Write-Host 'SERVER_URL.txt da duoc tao.' -ForegroundColor Green
try { Stop-Transcript | Out-Null } catch {}
Read-Host 'XONG - Nhan Enter de quay ve CMD' | Out-Null
exit 0