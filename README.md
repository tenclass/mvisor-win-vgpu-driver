# Mvisor Windows Guest VGPU Driver

## Abstract
1. A full Windows guest OpenGL driver implemention for the <b>Mvisor virtio-vgpu device</b>, it provides OpenGL 4.2 by delivering the Mesa virgl opengl render commands from guest to host, then the Virglrenderer model would help to consume these commands on the host.
2. We have tested it by using <b>Cinema4D</b> and <b>GPUTest</b> on windows 10 guest, the driver worked very nice. 
3. We can use Mvisor+VGPU to create a VM with OpenGL acceleration, <b>regardless of the limitations of graphics card virtualization</b>.
By the way, we have created 70 VMs on a single T4 card with 16G video memory, each running Cinema 4D rendering, the operation of the VM (Virtual Machine) was still very smooth.

## Screenshot
<img src="./docs/Screenshot from 2023-08-22 18-08-36.png" width="960">
<img src="./docs/Screenshot from 2023-08-22 18-07-42.png" width="960">
<img src="./docs/Screenshot from 2023-08-22 18-12-59.png" width="960">

## Compile

### User Model Driver
&nbsp;&nbsp;&nbsp;&nbsp;Build Environment: vs2019 or MinGW-W64

#### Steps:
1. Run build.bat in the usermode directory, it will download the Mesa project, patch it, and build it automatically
2. After building, you will get <b>MvisorVGPUx64.dll</b> and <b>opengl32.dll</b> in the build directory.

### Kernel Model Driver
&nbsp;&nbsp;&nbsp;&nbsp;Build Environment: vs2019 + wdk10.0

&nbsp;&nbsp;&nbsp;&nbsp;It's a WDF kernel model driver, after building, you would get <b>vgpu.sys</b>, <b>vgpu.inf</b> and <b>vgpu.cat</b> in the build directory.

## Install
1. Change you guest vm to <b>test-sign mode</b>, otherwise the driver would not work because the windows driver sign-check.
2. Just run <b>install.bat</b> in out release package, it will help you to prepare the environment and install the drivers.

## Current Status
1. We chose Direct-IO as the data transport type between usermode and kernelmode, but using Nether-IO would get better performance than Direct-IO
2. We have implemented all the features supported on linux host, but the blob feature was not supported in vm migration.

##  References
1. https://github.com/Keenuts/virtio-gpu-win-icd
2. https://github.com/kjliew/qemu-3dfx
3. https://gitlab.freedesktop.org/mesa/mesa