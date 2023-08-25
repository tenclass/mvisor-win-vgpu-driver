# Mvisor Windows VGPU Driver

## Abstract
1. A full Windows guest OpenGL driver implemention for the <b>Mvisor virtio-vgpu device</b>.
it deliver Mesa virgl opengl render commands from guest to host, then the Virglrenderer model would help to consume these commands on the host.
2. This driver provides OpenGL 4.2 on windows 10 guest, we tested it by using <b>Cinema4D</b> and <b>GPUTest</b>, and the driver worked very nice. 
3. We can use Mvisor+VGPU to create a VM with OpenGL acceleration, <b>regardless of the limitations of graphics card virtualization</b>.

## Screenshot
<img src="./docs/Screenshot from 2023-08-22 18-08-36.png" width="960">
<img src="./docs/Screenshot from 2023-08-22 18-07-42.png" width="960">
<img src="./docs/Screenshot from 2023-08-22 18-12-59.png" width="960">

## Compile & Run

### User Model Driver
Build Environment: vs2019 + sdk19041

Usage:
1. Set environment variables CLINK_HOST for server address
2. Set environment variables CLINK_PORT for server port
3. Set environment variables CLINK_NET for transport protocol, tcp or kcp
4. Set environment variables CLINK_UUID with uuid string
5. Put dlls into the path where the execute file exists(maybe it was loaded by other dll), then the dll would be loaded by the program automatically

### Kernel Model Driver
mkdir build && meson compile -c build<br>
Tips: Cuda 12.1 or newer needs to be installed completely on the host

Usage:<br>
`./clink --port 9999 --net tcp --uuid 60fe43ab-6860-4317-88cc-968ee3c3f3ad`

## Current Status
1. We handled about 70 functions of cuda 12.1 driver api to make cinema4d oc-render work on any computer without nvidia graphics card
2. It is proved that we can use fake cuda dll to supply cuda render engine for application on windows
3. By the way, if it can work properly over a network, then it should certainly be able to operate correctly within a guest/host environment on a virtual machine

##  References
