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
@git reset --hard 56108b411fd763d9fcba56bad72ddff11cb0b95a
@git apply ..\mesa-virgl-icd-for-windows.patch
@meson setup build -Dgallium-drivers=virgl -Dgallium-windows-dll-name=TenclassVGPUx64 -Dprefix=%1
@cd build
@meson install
@echo build finished.

:end
@pause
