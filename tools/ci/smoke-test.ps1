# 本地冒烟测试(构建产物快速功能验证, CI 产物功能测试的本地等价物)
# 用法: pwsh tools/ci/smoke-test.ps1 [-Binary <GitHubDesktop2Chinese.exe 路径>]
# 默认自动探测 out\build\*\Release\ 与 build\Release\ 下的产物, 取最新者
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$Binary
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

# ── 定位被测二进制 ──
if (-not $Binary) {
    $candidates = @()
    foreach ($pattern in @(
        "out\build\*\Release\GitHubDesktop2Chinese.exe",
        "out\build\*\GitHubDesktop2Chinese.exe",
        "build\Release\GitHubDesktop2Chinese.exe")) {
        $candidates += Get-Item (Join-Path $repoRoot $pattern) -ErrorAction SilentlyContinue
    }
    if (-not $candidates) {
        Write-Error "未找到待测二进制: 请先构建, 或用 -Binary 指定 GitHubDesktop2Chinese.exe 路径"
        exit 1
    }
    $Binary = ($candidates | Sort-Object LastWriteTime -Descending | Select-Object -First 1).FullName
}
if (-not (Test-Path -LiteralPath $Binary)) {
    Write-Error "二进制不存在: $Binary"
    exit 1
}
Write-Host "被测二进制: $Binary"

$failed = 0

# ── T1: --help 应 exit 0 ──
& $Binary --help *> $null
if ($LASTEXITCODE -eq 0) {
    Write-Host "T1 --help exit 0: PASS"
} else {
    Write-Host "T1 --help exit=${LASTEXITCODE}: FAIL"
    $failed++
}

# ── T2: 非法参数应被拒绝(exit != 0) ──
& $Binary thisargdoesnotexist *> $null
if ($LASTEXITCODE -ne 0) {
    Write-Host "T2 非法参数被拒绝: PASS"
} else {
    Write-Host "T2 非法参数未被拒绝: FAIL"
    $failed++
}

# ── T3: 完整汉化流程(fixture + 仓库内映射, 不联网)应 exit 0 且到达 renderer 完成 ──
$fixture = Join-Path ([System.IO.Path]::GetTempPath()) ("ghd2c-smoke-" + [System.IO.Path]::GetRandomFileName())
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
try {
    Set-Content -LiteralPath (Join-Path $fixture "index.html") -Value "<html></html>" -NoNewline
    Set-Content -LiteralPath (Join-Path $fixture "main.js") -Value "// fixture main" -NoNewline
    Set-Content -LiteralPath (Join-Path $fixture "renderer.js") -Value "// fixture renderer" -NoNewline
    $json = Join-Path $fixture "localization.json"
    Copy-Item (Join-Path $repoRoot "json\localization.json") $json
    $out = '' | & $Binary -g $fixture -j $json --nopause 2>&1 | Out-String
    $code = $LASTEXITCODE
    if ($code -eq 0 -and $out.Contains("renderer.js 文件汉化结束")) {
        Write-Host "T3 完整汉化流程 exit 0: PASS"
    } else {
        Write-Host "T3 完整汉化流程 exit=${code}: FAIL"
        Write-Host $out
        $failed++
    }

    # ── T4: --rollback 应从 .bak 还原内容 ──
    Set-Content -LiteralPath (Join-Path $fixture "main.js") -Value "// patched" -NoNewline
    Set-Content -LiteralPath (Join-Path $fixture "main.js.bak") -Value "// original" -NoNewline
    Set-Content -LiteralPath (Join-Path $fixture "renderer.js") -Value "// patched" -NoNewline
    Set-Content -LiteralPath (Join-Path $fixture "renderer.js.bak") -Value "// original" -NoNewline
    $out = '' | & $Binary -g $fixture --nopause --rollback 2>&1 | Out-String
    $code = $LASTEXITCODE
    $mainRestored = (Get-Content -LiteralPath (Join-Path $fixture "main.js") -Raw) -eq "// original"
    $rendererRestored = (Get-Content -LiteralPath (Join-Path $fixture "renderer.js") -Raw) -eq "// original"
    if ($code -eq 0 -and $mainRestored -and $rendererRestored) {
        Write-Host "T4 --rollback 还原备份: PASS"
    } else {
        Write-Host "T4 --rollback exit=${code} main还原=$mainRestored renderer还原=${rendererRestored}: FAIL"
        Write-Host $out
        $failed++
    }
} finally {
    Remove-Item -LiteralPath $fixture -Recurse -Force -ErrorAction SilentlyContinue
}

if ($failed -gt 0) {
    Write-Host ""
    Write-Host "❌ 冒烟测试失败: $failed 项"
    exit 1
}
Write-Host ""
Write-Host "✅ 冒烟测试全部通过"
exit 0
