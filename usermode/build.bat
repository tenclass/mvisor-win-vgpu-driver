@REM Run it in a VS build command window
@echo start building
@echo mesa install path=%1

@if {%1} == {} (
    @echo build failed, invalid param.
    @echo usage: build.bat [install path]
	@echo build failed.
	goto end
)

@if exist mesa (
	@echo delete old mesa source.
    rd /s/q mesa
)

@git clone https://gitlab.freedesktop.org/mesa/mesa.git
@cd mesa
@git reset --hard ad459054ed9900a0fa4672fd7a72a44a326bfdf3
@git apply ..\mesa-virgl-icd-for-windows.patch
@meson setup build -Dbuildtype=release -Dgallium-drivers=virgl -Dgallium-windows-dll-name=TenclassVGPUx64 -Dllvm=disabled -Dzlib=disabled -Dprefix=%1
@meson install -C build
@echo build finished.

:end
@pause
