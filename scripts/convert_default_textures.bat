:: run from project dir
set TEXCONV=%1
set OUTDIR=%2
%TEXCONV% -f BC7_UNORM_SRGB -srgb -o %OUTDIR% -y -sepalpha -keepcoverage 0.75 -dx10 -nologo resources\textures\black.png
%TEXCONV% -f BC7_UNORM -srgb -o %OUTDIR% -y -sepalpha -keepcoverage 0.75 -dx10 -nologo resources\textures\white.png resources\textures\normal.png resources\textures\yellow.png
