@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"
if not exist build-mingw mkdir build-mingw
icacls "build-mingw" /setintegritylevel "(OI)(CI)H" /T /Q
if errorlevel 1 goto :failed
powershell.exe -NoProfile -Command "$buildBody = (Get-Content -LiteralPath 'build.bat' -Raw -Encoding UTF8) -split '(?m)^# POWERSHELL BUILD\r?\n', 2; & ([scriptblock]::Create($buildBody[1]))"
if errorlevel 1 goto :failed
if not exist "build-mingw\ui" mkdir "build-mingw\ui"
xcopy /y /i "code\ui\web\*" "build-mingw\ui\"
if errorlevel 1 goto :failed
if "%~1"=="--build-only" exit /b 0
cd /d "build-mingw"
"mss016.exe"
set "MSS_BUILD_STATUS=%errorlevel%"
pause
exit /b %MSS_BUILD_STATUS%
:failed
if "%~1"=="--build-only" exit /b 1
pause
exit /b 1

# POWERSHELL BUILD
$ErrorActionPreference = 'Stop'
$compiler = 'D:/codeenvset/mingw64/bin/g++.exe'
$flags = '-std=gnu++20 -O3 -march=x86-64 -fomit-frame-pointer -Wall -Wextra -pedantic -Icode'
$root = (Get-Location).Path
$sources = @(Get-ChildItem -LiteralPath 'code' -Recurse -Filter '*.cpp' | Sort-Object FullName | ForEach-Object { $_.FullName.Substring($root.Length + 1).Replace('\', '/') } | Where-Object { -not $_.StartsWith('code/tools/') })
foreach ($source in $sources) { [void][System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName("build-mingw/obj/$source.o")) }

function Write-ChangedFile([string]$path, [string]$content) {
    if (-not [System.IO.File]::Exists($path) -or [System.IO.File]::ReadAllText($path) -ne $content) { [System.IO.File]::WriteAllText($path, $content, [System.Text.UTF8Encoding]::new($false)); return $true }
    return $false
}

# 参数变化使目标文件失效；源码列表变化只触发必要的编译及重新链接。
$flagsChanged = Write-ChangedFile 'build-mingw/compile.flags' "$compiler`n$flags`n"
$rules = "CXX := $compiler`nCXXFLAGS := $flags`nSOURCES := $($sources -join ' ')`n" + @'
OBJECTS := $(SOURCES:%.cpp=build-mingw/obj/%.o)
.DEFAULT_GOAL := build-mingw/mss016.exe
.DELETE_ON_ERROR:

$(OBJECTS): build-mingw/compile.flags $(CXX)

build-mingw/mss016.exe: $(OBJECTS) build-mingw/Makefile
	@echo LINK $@
	@"$(CXX)" $(OBJECTS) -ldbghelp -lws2_32 -lshell32 -luser32 -lole32 -lshlwapi -o "$@"

build-mingw/obj/%.o: %.cpp
	@echo CXX $<
	@"$(CXX)" $(CXXFLAGS) -MMD -MP -MF "$(@:.o=.d)" -MQ "$@" -c "$<" -o "$@"

-include $(OBJECTS:.o=.d)
'@
$rulesChanged = Write-ChangedFile 'build-mingw/Makefile' ($rules + "`n")
# 同一秒内增删源码或改参数时，也必须触发对应构建。
$makeArgs = @('-f', 'build-mingw/Makefile', '-j5', '--output-sync=target', '--no-print-directory')
if ($flagsChanged) { $makeArgs += @('-W', 'build-mingw/compile.flags') }
if ($rulesChanged) { $makeArgs += @('-W', 'build-mingw/Makefile') }
& 'D:/codeenvset/mingw64/bin/mingw32-make.exe' @makeArgs
exit $LASTEXITCODE
