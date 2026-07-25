# =============================================================================
# publish.ps1 — đóng gói Deskhub (WinUI3) thành MỘT thư mục chạy được + file .zip.
#
# Làm hai việc theo thứ tự:
#   1. Build lớp native Release (CMake) -> deskhub_native.dll (Release, không phụ thuộc
#      CRT debug).
#   2. dotnet publish self-contained (WindowsPackageType=None + WindowsAppSDKSelfContained)
#      -> thư mục chạy được không cần cài .NET / Windows App SDK, rồi nén .zip.
#
# Yêu cầu: chạy trong "Developer PowerShell for VS" (có cmake + vcvars trên PATH), và
# đã cài .NET 9 SDK + workload Windows App SDK. Chạy từ thư mục này:
#   ./publish.ps1
# =============================================================================
$ErrorActionPreference = 'Stop'

$RepoRoot = (Resolve-Path "$PSScriptRoot\..\..\..").Path
$OutDir   = "$PSScriptRoot\publish"
$Rid      = 'win-x64'

Write-Host "== 1/3: build native (Release) ==" -ForegroundColor Cyan
Push-Location $RepoRoot
try {
    cmake --preset x64-release
    cmake --build --preset x64-release --target deskhub_native
}
finally { Pop-Location }

Write-Host "== 2/3: dotnet publish (self-contained) ==" -ForegroundColor Cyan
if (Test-Path $OutDir) { Remove-Item $OutDir -Recurse -Force }
dotnet publish "$PSScriptRoot\Deskhub.csproj" -c Release -r $Rid --self-contained true -o $OutDir

# deskhub_native.dll do .csproj tự chép (đường dẫn preset x64-release). Kiểm tra cho chắc.
if (-not (Test-Path "$OutDir\deskhub_native.dll")) {
    Copy-Item "$RepoRoot\out\build\x64-release\client\windows\cpp\deskhub_native.dll" $OutDir
}

Write-Host "== 3/3: zip ==" -ForegroundColor Cyan
$Zip = "$PSScriptRoot\Deskhub-$Rid.zip"
if (Test-Path $Zip) { Remove-Item $Zip -Force }
Compress-Archive -Path "$OutDir\*" -DestinationPath $Zip

Write-Host "Done -> $Zip" -ForegroundColor Green
