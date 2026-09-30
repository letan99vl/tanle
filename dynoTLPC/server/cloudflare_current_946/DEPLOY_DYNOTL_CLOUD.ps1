$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

function Step($s) { Write-Host "`n=== $s ===" -ForegroundColor Cyan }
function Fail($s) { Write-Host "`nLOI: $s" -ForegroundColor Red; Read-Host 'Nhan Enter de dong'; exit 1 }

Step 'Kiem tra Node.js'
if (-not (Get-Command node -ErrorAction SilentlyContinue)) {
  Fail 'Chua co Node.js. Cai Node.js LTS tu https://nodejs.org roi chay lai file nay.'
}
if (-not (Get-Command npm -ErrorAction SilentlyContinue)) { Fail 'Khong tim thay npm.' }
Write-Host (node --version)

Step 'Cai Wrangler'
& npm install --no-audit --no-fund
if ($LASTEXITCODE -ne 0) { Fail 'npm install that bai.' }

Step 'Dang nhap Cloudflare'
& npx wrangler login
if ($LASTEXITCODE -ne 0) { Fail 'Dang nhap Cloudflare that bai.' }

$configText = Get-Content (Join-Path $PSScriptRoot 'wrangler.jsonc') -Raw
if ($configText -notmatch '"d1_databases"') {
  Step 'Tao D1 database dynotl-cloud tai khu vuc APAC'
  & npx wrangler d1 create dynotl-cloud --location apac --binding DB --update-config
  if ($LASTEXITCODE -ne 0) {
    Fail 'Khong tao duoc D1. Neu ban da tung tao dynotl-cloud, xoa database cu hoac them binding DB vao wrangler.jsonc.'
  }
} else {
  Write-Host 'Da co D1 binding trong wrangler.jsonc - bo qua buoc tao database.' -ForegroundColor Yellow
}

Step 'Tao bang license trong D1'
& npx wrangler d1 execute dynotl-cloud --remote --file=./schema.sql --yes
if ($LASTEXITCODE -ne 0) { Fail 'Khong tao duoc schema D1.' }

Step 'Deploy Worker lan dau'
$firstDeploy = (& npx wrangler deploy 2>&1 | Tee-Object -Variable firstLines) -join "`n"
Write-Host $firstDeploy
if ($LASTEXITCODE -ne 0) { Fail 'Deploy Worker that bai.' }

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
$deployOut = (& npx wrangler deploy 2>&1 | Tee-Object -Variable deployLines) -join "`n"
Write-Host $deployOut
if ($LASTEXITCODE -ne 0) { Fail 'Deploy cuoi that bai.' }

$m = [regex]::Match($deployOut, 'https://[a-zA-Z0-9.-]+\.workers\.dev')
if (-not $m.Success) {
  $serverUrl = Read-Host 'Khong tu tim thay URL. Dan URL workers.dev vua hien o tren vao day'
} else {
  $serverUrl = $m.Value
}
$serverUrl = $serverUrl.Trim().TrimEnd('/')
Set-Content -Path (Join-Path $PSScriptRoot 'SERVER_URL.txt') -Value $serverUrl -Encoding ASCII

Step 'Kiem tra server'
try {
  $ping = Invoke-RestMethod -Uri ($serverUrl + '/api/ping') -Method Get -TimeoutSec 20
  Write-Host ("OK - " + $ping.service + " - " + $ping.app_version) -ForegroundColor Green
} catch {
  Write-Host 'Worker da deploy nhung ping chua thanh cong. Thu mo URL tren trinh duyet.' -ForegroundColor Yellow
}

Step 'Tu test giao thuc DynoTL tren Cloudflare'
& node (Join-Path $PSScriptRoot 'tools\verify_cloud.mjs') $serverUrl
if ($LASTEXITCODE -ne 0) { Fail 'Self-test activate/heartbeat that bai. Chua nen doi launcher sang server moi.' }

Write-Host "`nSERVER: $serverUrl" -ForegroundColor Green
Write-Host "ADMIN : $serverUrl/admin" -ForegroundColor Green
Write-Host 'ADMIN TOKEN nam tai: private\DO_NOT_SHARE_admin_token.txt' -ForegroundColor Yellow

$ans = Read-Host 'Muon doi launcher DynoTL tren may nay sang server Cloudflare luon? (Y/N)'
if ($ans -match '^[Yy]') {
  & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'SET_LAUNCHER_SERVER.ps1') -ServerUrl $serverUrl
}

try { Start-Process ($serverUrl + '/admin') } catch {}
Write-Host "`nXONG. Nhan Enter de dong." -ForegroundColor Green
Read-Host | Out-Null