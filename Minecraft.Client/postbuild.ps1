param(
    [string]$OutDir,
    [string]$ProjectDir
)

Write-Host "Post-build script started. Output Directory: $OutDir, Project Directory: $ProjectDir"

$directories = @(
    "music",
    "Windows64\GameHDD",
    "Common\Media",
    "Common\res",
    "Common\Trial",
    "Common\Tutorial",
    "Windows64Media"
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
    @{ Source = "music";           Dest = "music" },
    @{ Source = "Common\Media";    Dest = "Common\Media" },
    @{ Source = "Common\res";      Dest = "Common\res" },
    @{ Source = "Common\Trial";    Dest = "Common\Trial" },
    @{ Source = "Common\Tutorial"; Dest = "Common\Tutorial" },
    @{ Source = "Windows64\GameHDD"; Dest = "Windows64\GameHDD" },
    @{ Source = "Windows64\Sound";  Dest = "Windows64\Sound" },
    @{ Source = "Windows64Media";  Dest = "Windows64Media" }
)

foreach ($copy in $copies) {
    $src = Join-Path $ProjectDir $copy.Source
    $dst = Join-Path $OutDir $copy.Dest

    if (Test-Path $src) {
        # Copy the files using xcopy, forcing overwrite and suppressing errors, and only copying if the source is newer than the destination
		xcopy /q /y /i /s /e /d "$src" "$dst" 2>$null
    }
}

git restore "**/BuildVer.h"