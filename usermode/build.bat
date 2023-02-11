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
@git reset --hard 7ead71739371ffc036883b9ee89318f5c368f4d4
@git apply ..\mesa-virgl-icd-for-windows.patch
@meson setup build -Dgallium-drivers=virgl -Dprefix=%1
@cd build
@meson install
@echo build finished.

:end
@pause
