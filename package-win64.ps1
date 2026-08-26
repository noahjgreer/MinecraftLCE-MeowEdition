# package-win64.ps1
#
# Assembles a clean, self-contained Windows x64 build that can be handed to
# someone else, from the staged build output in Minecraft.Client\bin\x64\<cfg>.
#
# The post-build step (Minecraft.Client\postbuild.ps1) already makes the build
# output directory complete and runnable. This script exists only to strip the
# things that should not be shared - the 100MB+ .pch, the .pdb, local save data
# - and to verify nothing required is missing before you send it out.
#
#   .\package-win64.ps1                     # -> Builds\Windows-x64-Release
#   .\package-win64.ps1 -Zip                # also produces a .zip alongside it
#   .\package-win64.ps1 -Configuration Debug
#
# Note the game needs no VC++ redistributable: Release|x64 builds with
# RuntimeLibrary=MultiThreaded (/MT), so the CRT is linked statically.

param(
    [string]$Configuration = "Release",
    [string]$DestDir,
    [switch]$Zip
)

$ErrorActionPreference = "Stop"

$root    = $PSScriptRoot
$srcDir  = Join-Path $root "Minecraft.Client\bin\x64\$Configuration"
if (-not $DestDir) { $DestDir = Join-Path $root "Builds\Windows-x64-$Configuration" }

if (-not (Test-Path $srcDir)) {
    throw "Build output not found: $srcDir`nBuild MinecraftPC.sln at $Configuration|x64 first."
}

Write-Host "Packaging $srcDir"
Write-Host "       -> $DestDir"

# Developer-only artefacts, and local state that must not follow the build to
# someone else's machine. GameHDD is this machine's save data.
$excludeFiles = @("*.pch", "*.pdb", "*.ilk", "*.exp", "*.lib", "*.iobj", "*.ipdb", "*.log")
$excludeDirs  = @("Windows64\GameHDD")

New-Item -ItemType Directory -Path $DestDir -Force | Out-Null

# /MIR mirrors, so the destination is an exact copy and stale files from an
# older package do not linger. Everything under $DestDir is disposable.
$roboArgs = @($srcDir, $DestDir, "/MIR", "/NFL", "/NDL", "/NJH", "/NJS", "/NP", "/R:1", "/W:1")
$roboArgs += "/XF"; $roboArgs += $excludeFiles
$roboArgs += "/XD"; $roboArgs += ($excludeDirs | ForEach-Object { Join-Path $srcDir $_ })

& robocopy @roboArgs | Out-Null

# robocopy uses a bitmask: <8 is success, >=8 is a real failure.
if ($LASTEXITCODE -ge 8) {
    throw "robocopy failed with exit code $LASTEXITCODE"
}

# /MIR removes files the source no longer has, but a file matched by /XF is
# excluded from that comparison entirely - so an excluded file already sitting
# in the destination is neither copied nor deleted, and survives forever. Sweep
# them explicitly, or a stale 58MB .pdb from an earlier package ships anyway.
foreach ($pattern in $excludeFiles) {
    Get-ChildItem -Path $DestDir -Filter $pattern -Recurse -File -ErrorAction SilentlyContinue |
        ForEach-Object {
            Write-Host "Removing stale artefact: $($_.FullName.Substring($DestDir.Length + 1))"
            Remove-Item $_.FullName -Force
        }
}

# Same trap for /XD. This one matters more: GameHDD is local save data, and
# without this sweep every package would carry this machine's worlds to
# whoever the build is sent to.
foreach ($dir in $excludeDirs) {
    $stale = Join-Path $DestDir $dir
    if (Test-Path $stale) {
        Write-Host "Removing local state: $dir"
        Remove-Item $stale -Recurse -Force
    }
}

# An empty save directory so the game has somewhere to write on first run.
New-Item -ItemType Directory -Path (Join-Path $DestDir "Windows64\GameHDD") -Force | Out-Null

# Verify. Everything below is needed for the process to start or for audio to
# work at all - see docs\systems\windows-x64-audio.md.
$required = @(
    "Minecraft.Client.exe",
    "mss64.dll",
    "iggy_w64.dll",
    "Durango\Sound\Minecraft.msscmp",
    "redist64\binkawin64.asi",
    "redist64\mss64dolby.flt",
    "redist64\mss64ds3d.flt",
    "redist64\mss64dsp.flt",
    "redist64\mss64eax.flt",
    "redist64\mss64srs.flt",
    "music\music\calm1.binka",
    "music\cds\cat.binka",
    "Common\Media\platformskinHD.swf",
    "Common\res",
    "Windows64Media"
)

$missing = $required | Where-Object { -not (Test-Path (Join-Path $DestDir $_)) }
if ($missing) {
    Write-Host ""
    Write-Warning "PACKAGE IS INCOMPLETE - do not share it. Missing:"
    $missing | ForEach-Object { Write-Warning "    $_" }
    exit 1
}

$size  = (Get-ChildItem $DestDir -Recurse -File | Measure-Object -Property Length -Sum).Sum
$count = (Get-ChildItem $DestDir -Recurse -File).Count
Write-Host ("Package OK: {0} files, {1:N1} MB" -f $count, ($size / 1MB))

if ($Zip) {
    $zipPath = "$DestDir.zip"
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
    Write-Host "Compressing to $zipPath ..."
    Compress-Archive -Path (Join-Path $DestDir "*") -DestinationPath $zipPath
    $zipSize = (Get-Item $zipPath).Length
    Write-Host ("Zip written: {0:N1} MB" -f ($zipSize / 1MB))
}

# robocopy leaves a non-zero $LASTEXITCODE behind even on success (3 = files
# copied + extras removed), which would otherwise look like a failed script.
exit 0
