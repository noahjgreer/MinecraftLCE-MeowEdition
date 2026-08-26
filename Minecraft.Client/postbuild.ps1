param(
    [string]$OutDir,
    [string]$ProjectDir,
    [string]$SolutionDir
)

Write-Host "Post-build script started. Output Directory: $OutDir, Project Directory: $ProjectDir"

# 4J Meow - $SolutionDir was added after this script already existed, so fall back
# to deriving it rather than failing on a stale PostBuildEvent command line.
if (-not $SolutionDir) {
    $SolutionDir = (Resolve-Path (Join-Path $ProjectDir "..")).Path
}
if (-not $SolutionDir.EndsWith("\")) { $SolutionDir += "\" }

$directories = @(
    "music",
    "Windows64\GameHDD",
    "Common\Media",
    "Common\res",
    "Common\Trial",
    "Common\Tutorial",
    "Windows64Media",
    "Durango\Sound",
    "redist64"
)

foreach ($dir in $directories) {
    New-Item -ItemType Directory -Path (Join-Path $OutDir $dir) -Force | Out-Null
}

# 4J Meow - Install the platform-specific UI skin.
#
# skinHD.swf carries an ImportAssets2 reference to "platformskinHD.swf", which
# is where the per-platform button art, logo and panorama backgrounds live.
# 4J generated that file by hand with Media\CopyPlatformSkin.bat, and it had
# never been run in this tree - so platformskin.swf and platformskinHD.swf did
# not exist anywhere and the import resolved to nothing. Doing it here means it
# cannot be forgotten again.
$platformSkins = @(
    @{ Source = "Windows64Media\Media\skinWin.swf";   Dest = "Common\Media\platformskin.swf" },
    @{ Source = "Windows64Media\Media\skinHDWin.swf"; Dest = "Common\Media\platformskinHD.swf" }
)

foreach ($skin in $platformSkins) {
    $src = Join-Path $ProjectDir $skin.Source
    $dst = Join-Path $ProjectDir $skin.Dest
    if (Test-Path $src) {
        if ((-not (Test-Path $dst)) -or ((Get-Item $src).LastWriteTime -gt (Get-Item $dst).LastWriteTime)) {
            Write-Host "Installing platform skin: $($skin.Source) -> $($skin.Dest)"
            Copy-Item -Path $src -Destination $dst -Force
        }
    } else {
        Write-Warning "Platform skin source missing: $src"
    }
}


$copies = @(
    @{ Source = "music";             Dest = "music" },
    @{ Source = "Common\Media";      Dest = "Common\Media" },
    @{ Source = "Common\res";        Dest = "Common\res" },
    @{ Source = "Common\Trial";      Dest = "Common\Trial" },
    @{ Source = "Common\Tutorial";   Dest = "Common\Tutorial" },
    @{ Source = "Windows64Media";    Dest = "Windows64Media" },

    # 4J Meow - Miles Sound System runtime.
    #
    # SoundEngine::init() calls AIL_set_redist_directory("redist64") and then
    # AIL_add_soundbank("Durango\Sound\Minecraft.msscmp"). Both paths are
    # relative to the working directory. If the soundbank is not found, init()
    # does NOT degrade gracefully - it calls AIL_close_digital_driver() and
    # AIL_shutdown(), so the game goes completely silent, streamed .binka music
    # included. Neither of these was being staged, which is exactly what
    # happened. See docs/systems/windows-x64-audio.md.
    #
    # The soundbank path really does say "Durango" on the Windows build; it is
    # a 4J leftover from sharing sound assets with the Xbox One target. Do not
    # "tidy" the folder name here without changing m_szSoundPath to match.
    @{ Source = "Durango\Sound";     Dest = "Durango\Sound" },
    @{ Source = "redist64";          Dest = "redist64" }
)

foreach ($copy in $copies) {
    $src = Join-Path $ProjectDir $copy.Source
    $dst = Join-Path $OutDir $copy.Dest

    if (Test-Path $src) {
        # Copy the files using xcopy, forcing overwrite and suppressing errors, and only copying if the source is newer than the destination
		xcopy /q /y /i /s /e /d "$src" "$dst" 2>$null
    } else {
        Write-Warning "Content source missing, not staged: $src"
    }
}

# 4J Meow - Middleware DLLs the exe imports directly.
#
# dumpbin /dependents on Minecraft.Client.exe lists exactly two non-OS imports:
# mss64.dll (Miles) and iggy_w64.dll (Iggy/Flash UI). Without them the process
# will not start at all - this is a hard loader dependency, not a soft failure.
#
# Source is $(SolutionDir)x64\Release\, which despite the name is NOT build
# output: OutDir/IntDir both live under Minecraft.Client\bin\. It is 4J's
# runtime-support staging folder and it is git-tracked. It is also the only
# place in the tree holding mss64.dll, and it holds the redist64 provider set
# that matches it.
#
# Note there is a second, older iggy_w64.dll at
# Windows64\Iggy\lib\redist64\ (201 exports vs 203). Both satisfy the 41 Iggy
# symbols the exe imports, but the x64\Release copy is the one that has been
# shipping, so prefer it.
#
# The CRT is NOT needed alongside these: Release|x64 builds with
# RuntimeLibrary=MultiThreaded (/MT), so there is no VCRUNTIME140/MSVCP140
# dependency and end users do not need the VC++ redistributable installed.
$runtimeDlls = @("mss64.dll", "iggy_w64.dll")
$dllSource = Join-Path $SolutionDir "x64\Release"

foreach ($dll in $runtimeDlls) {
    $src = Join-Path $dllSource $dll
    $dst = Join-Path $OutDir $dll
    if (Test-Path $src) {
        if ((-not (Test-Path $dst)) -or ((Get-Item $src).Length -ne (Get-Item $dst).Length)) {
            Write-Host "Staging runtime DLL: $dll"
            Copy-Item -Path $src -Destination $dst -Force
        }
    } else {
        Write-Warning "Runtime DLL missing, game will not start: $src"
    }
}

# 4J Meow - Fail loudly rather than shipping something that cannot run.
# Everything listed here is required for the process to start or for audio to
# work at all. A warning in the build log is easy to miss, so summarise.
$required = @(
    "Minecraft.Client.exe",
    "mss64.dll",
    "iggy_w64.dll",
    "Durango\Sound\Minecraft.msscmp",
    "redist64\binkawin64.asi",
    "Common\Media\platformskinHD.swf"
)
$missing = $required | Where-Object { -not (Test-Path (Join-Path $OutDir $_)) }
if ($missing) {
    Write-Warning "=========================================================="
    Write-Warning "INCOMPLETE BUILD - the following runtime files are missing:"
    $missing | ForEach-Object { Write-Warning "    $_" }
    Write-Warning "=========================================================="
} else {
    Write-Host "Runtime staging complete - $OutDir is ready to run."
}

git restore "**/BuildVer.h"
