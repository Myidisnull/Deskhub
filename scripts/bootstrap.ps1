$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')

$clangFormatVersion = '22.1.3'
$clangTidyVersion = '22.1.8'
$ktlintVersion = '1.5.0'
$ktlintSha256 = 'A16BE01DCC480AAB2F55F444B620142152F66E31564B3B9376506D624C28A2AD'
$swiftformatVersion = '0.62.1'
$swiftformatMsiSha256 = 'DDE120147AADAD9271831D37919A6567E2C9EC03B22D075DE4CC5E2F2F3D8F25'

if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    throw "winget not found. Install 'App Installer' from Microsoft Store, then re-run."
}

function Install-IfMissing([string]$Cmd, [string]$WingetId, [string]$Label) {
    if (Get-Command $Cmd -ErrorAction SilentlyContinue) {
        Write-Host "[ok]      $Label ($((Get-Command $Cmd).Source))"
        return $false
    }
    Write-Host "[install] $Label ($WingetId)..."
    & winget install --id $WingetId --exact --accept-source-agreements --accept-package-agreements
    if ($LASTEXITCODE -ne 0) { throw "winget failed installing $WingetId (exit $LASTEXITCODE)" }
    return $true
}

$restartNote = $false

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsOk = $false
if (Test-Path $vswhere) {
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($vs) {
        $vsOk = $true; Write-Host "[ok]      Visual Studio C++ toolchain ($vs)"
        if (Test-Path (Join-Path $vs 'VC\Tools\Llvm\x64\bin\clang++.exe')) {
            Write-Host "[ok]      LLVM in VS (clang++ + llvm-cov)"
        } else {
            Write-Host "[action]  VS is missing the LLVM component (needed by 'make coverage')."
            Write-Host "          Open Visual Studio Installer -> Modify -> add 'C++ Clang tools for Windows'."
        }
    }
}
if (-not $vsOk) {
    Write-Host "[install] Visual Studio 2026 Build Tools (C++ workload + Clang)..."
    winget install --id Microsoft.VisualStudio.BuildTools --exact --accept-source-agreements --accept-package-agreements `
        --override "--passive --wait --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Llvm.Clang --includeRecommended"
    if ($LASTEXITCODE -ne 0) { throw "winget failed installing VS Build Tools (exit $LASTEXITCODE)" }
    $restartNote = $true
}

if (Install-IfMissing 'rustc' 'Rustlang.Rustup' 'Rust (builds quiche, the QUIC transport)') { $restartNote = $true }
$cargoBin = Join-Path $env:USERPROFILE '.cargo\bin'
if (Test-Path $cargoBin) { $env:PATH = "$cargoBin;$env:PATH" }
$rustHost = ''
$rustc = Get-Command rustc -ErrorAction SilentlyContinue
if ($rustc) {
    $hostLine = & $rustc.Source -vV | Select-String '^host:'
    if ($hostLine) { $rustHost = ($hostLine.ToString() -replace '^host:\s*', '') }
}
if ($rustHost -and $rustHost -ne 'x86_64-pc-windows-msvc') {
    Write-Host "[action]  Rust host is '$rustHost' - quiche needs the MSVC toolchain."
    Write-Host "          rustup default stable-x86_64-pc-windows-msvc"
}

$nasmDir = Join-Path $env:LOCALAPPDATA 'bin\NASM'
if (Install-IfMissing 'nasm' 'NASM.NASM' 'NASM (assembles BoringSSL)') { $restartNote = $true }
if (-not (Get-Command nasm -ErrorAction SilentlyContinue)) {
    if (Test-Path (Join-Path $nasmDir 'nasm.exe')) {
        $env:PATH = "$nasmDir;$env:PATH"
        Write-Host "[action]  Add '$nasmDir' to your PATH - the NASM installer does not."
    } else {
        Write-Host "[action]  nasm not found after install - 'build-quiche.sh windows' will fail without it."
    }
}

if (Install-IfMissing 'make' 'GnuWin32.Make' 'GNU make') { $restartNote = $true }
if (Install-IfMissing 'java' 'EclipseAdoptium.Temurin.17.JDK' 'JDK 17 (Temurin)') { $restartNote = $true }

$clangFormatOk = $false
$cf = Get-Command clang-format -ErrorAction SilentlyContinue
if ($cf -and (((& $cf.Source --version) -join ' ') -match [regex]::Escape($clangFormatVersion))) {
    Write-Host "[ok]      clang-format $clangFormatVersion ($($cf.Source))"
    $clangFormatOk = $true
}
if (-not $clangFormatOk) {
    $pipx = Get-Command pipx -ErrorAction SilentlyContinue
    $py = Get-Command py -ErrorAction SilentlyContinue
    if ($pipx) {
        Write-Host "[install] clang-format $clangFormatVersion (pipx)..."
        & $pipx.Source install --force "clang-format==$clangFormatVersion"
        if ($LASTEXITCODE -ne 0) { throw "pipx failed installing clang-format (exit $LASTEXITCODE)" }
        & $pipx.Source ensurepath
        $restartNote = $true
    } elseif ($py) {
        Write-Host "[install] clang-format $clangFormatVersion (pip --user)..."
        & $py.Source -m pip install --user --upgrade "clang-format==$clangFormatVersion"
        if ($LASTEXITCODE -ne 0) { throw "pip failed installing clang-format (exit $LASTEXITCODE)" }
        $restartNote = $true
    } else {
        Write-Host "[action]  clang-format $clangFormatVersion not found - CI enforces this exact version."
        Write-Host "          Install Python 3 (winget install Python.Python.3.12), reopen the terminal,"
        Write-Host "          then re-run bootstrap (or: pipx install clang-format==$clangFormatVersion)."
    }
}

$clangTidyOk = $false
$ct = Get-Command clang-tidy -ErrorAction SilentlyContinue
if ($ct -and (((& $ct.Source --version) -join ' ') -match [regex]::Escape($clangTidyVersion))) {
    Write-Host "[ok]      clang-tidy $clangTidyVersion ($($ct.Source))"
    $clangTidyOk = $true
}
if (-not $clangTidyOk) {
    $pipx = Get-Command pipx -ErrorAction SilentlyContinue
    $py = Get-Command py -ErrorAction SilentlyContinue
    if ($pipx) {
        Write-Host "[install] clang-tidy $clangTidyVersion (pipx, what 'make lint-tidy' runs)..."
        & $pipx.Source install --force "clang-tidy==$clangTidyVersion"
        if ($LASTEXITCODE -ne 0) { throw "pipx failed installing clang-tidy (exit $LASTEXITCODE)" }
        & $pipx.Source ensurepath
        $restartNote = $true
    } elseif ($py) {
        Write-Host "[install] clang-tidy $clangTidyVersion (pip --user)..."
        & $py.Source -m pip install --user --upgrade "clang-tidy==$clangTidyVersion"
        if ($LASTEXITCODE -ne 0) { throw "pip failed installing clang-tidy (exit $LASTEXITCODE)" }
        $restartNote = $true
    } else {
        Write-Host "[action]  clang-tidy $clangTidyVersion not found - CI enforces this exact version."
        Write-Host "          Install Python 3 (winget install Python.Python.3.12), reopen the terminal,"
        Write-Host "          then re-run bootstrap (or: pipx install clang-tidy==$clangTidyVersion)."
    }
}

if (Test-Path (Join-Path $root '.git')) {
    $subStatus = git -C $root submodule status
    if ($subStatus -match '(?m)^-') {
        Write-Host "[install] git submodules (nvenc headers)..."
        git -C $root submodule update --init
        if ($LASTEXITCODE -ne 0) { throw "git submodule update failed (exit $LASTEXITCODE)" }
    } else {
        Write-Host "[ok]      git submodules"
    }
}

$toolsDir = Join-Path $root 'tools'
New-Item -ItemType Directory -Force -Path $toolsDir | Out-Null

$ktlintJar = Join-Path $toolsDir 'ktlint.jar'
$ktlintVerFile = Join-Path $toolsDir 'ktlint.jar.version'
$ktlintCurrent = if (Test-Path $ktlintVerFile) { (Get-Content $ktlintVerFile -Raw).Trim() } else { '' }
if ((Test-Path $ktlintJar) -and ($ktlintCurrent -eq $ktlintVersion)) {
    Write-Host "[ok]      ktlint $ktlintVersion ($ktlintJar)"
} else {
    Write-Host "[install] ktlint $ktlintVersion..."
    Invoke-WebRequest -Uri "https://github.com/pinterest/ktlint/releases/download/$ktlintVersion/ktlint" -OutFile $ktlintJar
    if ((Get-FileHash $ktlintJar -Algorithm SHA256).Hash -ne $ktlintSha256) {
        Remove-Item $ktlintJar -Force
        throw "ktlint download failed the checksum check."
    }
    Set-Content -Path $ktlintVerFile -Value $ktlintVersion
}

$swiftformatExe = Join-Path $toolsDir 'swiftformat.exe'
$sfOnPath = (Get-Command swiftformat -ErrorAction SilentlyContinue).Source
if ($sfOnPath) {
    Write-Host "[ok]      swiftformat ($sfOnPath)"
} elseif (Test-Path $swiftformatExe) {
    Write-Host "[ok]      swiftformat ($swiftformatExe)"
} else {
    Write-Host "[install] SwiftFormat $swiftformatVersion..."
    $msi = Join-Path $env:TEMP 'SwiftFormat.amd64.msi'
    $ext = Join-Path $env:TEMP 'SwiftFormatMsiExtract'
    Invoke-WebRequest -Uri "https://github.com/nicklockwood/SwiftFormat/releases/download/$swiftformatVersion/SwiftFormat.amd64.msi" -OutFile $msi
    if ((Get-FileHash $msi -Algorithm SHA256).Hash -ne $swiftformatMsiSha256) {
        Remove-Item $msi -Force
        throw "SwiftFormat download failed the checksum check."
    }
    Start-Process msiexec -ArgumentList "/a `"$msi`" /qn TARGETDIR=`"$ext`"" -Wait
    Copy-Item (Join-Path $ext 'PFiles64\nicklockwood\SwiftFormat\swiftformat.exe') $swiftformatExe
    Remove-Item $msi -Force
    Remove-Item $ext -Recurse -Force
}

$sdkRoot = $env:ANDROID_HOME
if (-not $sdkRoot) { $sdkRoot = Join-Path $env:LOCALAPPDATA 'Android\Sdk' }
$sdkmanager = Get-ChildItem -Path (Join-Path $sdkRoot 'cmdline-tools\*\bin\sdkmanager.bat') -ErrorAction SilentlyContinue |
    Select-Object -First 1
if (-not $sdkmanager) {
    Write-Host "[install] Android Studio (bootstrap for SDK)..."
    winget install --id Google.AndroidStudio --exact --accept-source-agreements --accept-package-agreements
    if ($LASTEXITCODE -ne 0) { throw "winget failed installing Android Studio (exit $LASTEXITCODE)" }
    Write-Host "  NOTE: open Android Studio once (installs SDK + cmdline-tools), then re-run bootstrap."
} else {
    Write-Host "[ok]      Android SDK ($sdkRoot)"
    $missing = @('platform-tools', 'platforms\android-37.0', 'ndk\26.1.10909125', 'cmake\3.22.1') |
        Where-Object { -not (Test-Path (Join-Path $sdkRoot $_)) }
    if (-not $missing) {
        Write-Host "[ok]      Android SDK packages (platform 37.0, NDK 26.1.10909125, cmake 3.22.1)"
    } else {
        Write-Host "[install] SDK packages ($($missing -join ', '))..."
        & $sdkmanager.FullName --install 'platform-tools' 'platforms;android-37.0' 'ndk;26.1.10909125' 'cmake;3.22.1'
        if ($LASTEXITCODE -ne 0) { throw "sdkmanager failed (exit $LASTEXITCODE)" }
    }
}

Write-Host ""
Write-Host "bootstrap: DONE"
Write-Host "  Next: 'make' (list every target), 'make test', 'make lint', 'make build-windows'"
if ($restartNote) { Write-Host "  NOTE: open a NEW terminal so PATH changes take effect." }
