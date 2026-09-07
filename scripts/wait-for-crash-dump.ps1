param(
    [Parameter(Mandatory = $true)][string]$Folder,
    [int]$AppearSeconds = 90,
    [int]$SettleSeconds = 240
)

$ErrorActionPreference = 'Stop'

function Get-DumpBytes {
    $files = @(Get-ChildItem -Path (Join-Path $Folder '*.dmp') -ErrorAction SilentlyContinue)
    if ($files.Count -eq 0) { return $null }
    return [int64](($files | Measure-Object -Property Length -Sum).Sum)
}

$deadline = (Get-Date).AddSeconds($AppearSeconds)
while ($null -eq (Get-DumpBytes)) {
    if ((Get-Date) -ge $deadline) {
        Write-Host "wait-for-crash-dump: no .dmp appeared in $Folder within $AppearSeconds s. Windows Error Reporting writes these out of process, so either LocalDumps is not configured for this job or the process died in a way WER does not report - without the dump the crashing frame is all this run can ever tell you."
        exit 0
    }
    Start-Sleep -Seconds 2
}

$deadline = (Get-Date).AddSeconds($SettleSeconds)
$previous = -1
while ($true) {
    $bytes = Get-DumpBytes
    if ($bytes -eq $previous) {
        Write-Host "wait-for-crash-dump: $Folder holds $bytes byte(s) of dump, no longer growing."
        exit 0
    }
    if ((Get-Date) -ge $deadline) {
        Write-Host "wait-for-crash-dump: $Folder was still growing after $SettleSeconds s, uploading it at $bytes byte(s) anyway - a truncated dump still names the crashing thread."
        exit 0
    }
    $previous = $bytes
    Start-Sleep -Seconds 5
}
