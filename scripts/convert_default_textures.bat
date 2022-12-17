:: run from project dir
:: cmd.exe /c scripts\\convert_default_textures.bat "D:\dev\tools\texconv" "resources\output\textures"
set TEXCONV=%1
set OUTDIR=%2
%TEXCONV% -f BC7_UNORM_SRGB -srgb -o %OUTDIR% -y -sepalpha -keepcoverage 0.75 -dx10 -nologo resources\textures\black.png resources\textures\white.png
%TEXCONV% -f BC7_UNORM -srgb -o %OUTDIR% -y -sepalpha -keepcoverage 0.75 -dx10 -nologo resources\textures\normal.png resources\textures\yellow.png
