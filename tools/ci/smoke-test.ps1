# 冒烟测试(构建产物快速功能验证)
# 本地与 CI Build job Windows 产物功能测试共用同一用例源(#178, 消除两套平行维护漂移)
# 用法: pwsh tools/ci/smoke-test.ps1 [-Binary <TuPigGHD2Chinese.exe 路径>] [-Force]
# 默认自动探测 out\build\*\Release\ 与 build\Release\ 下的产物, 取最新者
# -Force: T3 完整汉化流程追加 --force(CI 构建版本 ci-<sha> 解析为 0.0.0 低于 minversion,
#         --nopause 下默认中止; 本地默认版本为 Dev 状态跳过门禁, 无需此开关)
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$Binary,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

# #136: Windows PowerShell 5.1 在任何 stderr 重定向下把 native stderr 包装为 ErrorRecord,
# EAP=Stop 时升级为终止错误 —— T2(CLI11 非法参数经 stderr 输出)会中止整个脚本。
# 局部降级 EAP 包裹 native 调用, 调用后恢复; $LASTEXITCODE 不受影响(实测 5.1/pwsh 双通过)。
# pwsh 7 默认 $PSNativeCommandUseErrorActionPreference=false 不受影响, 但统一走此封装保持一致
function Invoke-Native {
    param([Parameter(Mandatory)][scriptblock]$Block)
    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { & $Block } finally { $ErrorActionPreference = $prevEap }
}

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

# ── 定位被测二进制 ──
if (-not $Binary) {
    $candidates = @()
    foreach ($pattern in @(
        "out\build\*\Release\TuPigGHD2Chinese.exe",
        "build\Release\TuPigGHD2Chinese.exe")) {
        $candidates += Get-Item (Join-Path $repoRoot $pattern) -ErrorAction SilentlyContinue
    }
    if (-not $candidates) {
        # #136: Write-Error 在 EAP=Stop 下直接抛终止错误, 原 exit 1 不可达; throw 语义等价且明确
        throw "未找到待测二进制: 请先构建, 或用 -Binary 指定 TuPigGHD2Chinese.exe 路径"
    }
    $Binary = ($candidates | Sort-Object LastWriteTime -Descending | Select-Object -First 1).FullName
}
if (-not (Test-Path -LiteralPath $Binary)) {
    throw "二进制不存在: $Binary"
}
Write-Host "被测二进制: $Binary"

$failed = 0

# ── T1: --help 应 exit 0 ──
Invoke-Native { & $Binary --help *> $null }
if ($LASTEXITCODE -eq 0) {
    Write-Host "T1 --help exit 0: PASS"
} else {
    Write-Host "T1 --help exit=${LASTEXITCODE}: FAIL"
    $failed++
}

# ── T2: 非法参数应被拒绝(exit != 0) ──
Invoke-Native { & $Binary thisargdoesnotexist *> $null }
if ($LASTEXITCODE -ne 0) {
    Write-Host "T2 非法参数被拒绝: PASS"
} else {
    Write-Host "T2 非法参数未被拒绝: FAIL"
    $failed++
}

# ── T3: 完整汉化流程(fixture + 仓库内映射, 不联网)应 exit 0 且到达 renderer 完成 ──
# fixture 名含中文: 汉化目标用户路径普遍含中文, 保留中文路径覆盖(原 CI 内联用例等价迁移)
$fixture = Join-Path ([System.IO.Path]::GetTempPath()) ("ghd2c-冒烟测试-" + [System.IO.Path]::GetRandomFileName())
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
try {
    Set-Content -LiteralPath (Join-Path $fixture "index.html") -Value "<html></html>" -NoNewline
    Set-Content -LiteralPath (Join-Path $fixture "main.js") -Value "// fixture main" -NoNewline
    Set-Content -LiteralPath (Join-Path $fixture "renderer.js") -Value "// fixture renderer" -NoNewline
    $json = Join-Path $fixture "localization.json"
    Copy-Item (Join-Path $repoRoot "json\localization.json") $json
    $t3Args = @('-g', $fixture, '-j', $json, '--nopause')
    if ($Force) { $t3Args += '--force' }
    $out = Invoke-Native { '' | & $Binary @t3Args 2>&1 | Out-String }
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
    $out = Invoke-Native { '' | & $Binary -g $fixture --nopause --rollback 2>&1 | Out-String }
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
