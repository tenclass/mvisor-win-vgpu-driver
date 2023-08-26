# Mvisor Windows Guest VGPU Driver

## Abstract
- A full Windows guest OpenGL driver implemention for the [Mvisor](https://github.com/tenclass/mvisor) <b>virtio-vgpu device</b>, it provides <b>OpenGL 4.2</b> by translating OpenGL api requests to Mesa Virgl Render Commands, and then delivering these commands from guest application to [Virglrenderer](https://gitlab.freedesktop.org/virgl/virglrenderer) on the host.
- We have tested it by using <b>Cinema4D</b> and <b>[GPUTest](https://www.geeks3d.com/gputest/)</b> on windows 10 guest, the driver worked very nice. 
- We can use Mvisor+VGPU to create a VM with OpenGL acceleration, <b>regardless of the limitations of graphics card virtualization</b>.
By the way, we have created 70 VMs on a single T4 card with 16G video memory, each running Cinema 4D rendering, the operation of the VM (Virtual Machine) was still very smooth.

## Screenshot
### GpuTest
<img src="./docs/Screenshot from 2023-08-22 18-08-36.png" width="960">
<img src="./docs/Screenshot from 2023-08-22 18-12-59.png" width="960">

## Cinema4D
<img src="./docs/Screenshot from 2023-08-22 18-07-42.png" width="960">

## Compile

### User Mode Driver
&nbsp;&nbsp;&nbsp;&nbsp;Build Environment: VS2019 or MinGW-W64

&nbsp;&nbsp;&nbsp;&nbsp;Run <b>build.bat</b> in the usermode directory, it will download the Mesa project, patch it, and build it automatically. After building, you will get <b>MvisorVGPUx64.dll</b> and <b>opengl32.dll</b> in the build directory.

### Kernel Mode Driver
&nbsp;&nbsp;&nbsp;&nbsp;Build Environment: VS2019 + WDK10.0

&nbsp;&nbsp;&nbsp;&nbsp;It's a WDF kernel mode driver, after building, you will get <b>vgpu.sys</b>, <b>vgpu.inf</b> and <b>vgpu.cat</b> in the build directory.

## Install
1. Change you guest VM to <b>test-sign mode</b>, otherwise the driver would not work because of the windows driver sign-check.
```c
bcdedit.exe /set testsigning on
```
2. Just run <b>install.bat</b> in our release package, it will help you to prepare the environment and install the drivers.

## Current Status
- You need to add this part of config to let Mvisor create VM with vgpu device.
```c
  - class: virtio-vgpu
    memory: 1G
    staging: Yes
    blob: Yes
    node: /dev/dri/renderD128
```
- We choose Direct-IO as the data transport type between usermode and kernelmode in guest vm now, but using Nether-IO may get better performance.
- We have implemented all the features supported on linux host, but the blob feature was not supported in VM migration.
- In order to use blob feature, you may need to patch the vrend_state.inferred_gl_caching_type in [Virglrenderer](https://gitlab.freedesktop.org/virgl/virglrenderer) to let your guest driver get VIRGL_CAP_ARB_BUFFER_STORAGE. 
```c
   if (has_feature(feat_arb_buffer_storage) && !vrend_state.use_external_blob) {
      const char *vendor = (const char *)glGetString(GL_VENDOR);
      bool is_mesa = ((strstr(renderer, "Mesa") != NULL) || (strstr(renderer, "DRM") != NULL) ||
                      (strstr(renderer, "llvmpipe") != NULL));
      /*
       * Intel GPUs (aside from Atom, which doesn't expose GL4.5) are cache-coherent.
       * Mesa AMDGPUs use write-combine mappings for coherent/persistent memory (see
       * RADEON_FLAG_GTT_WC in si_buffer.c/r600_buffer_common.c). For Nvidia, we can guess and
       * check.  Long term, maybe a GL extension or using VK could replace these heuristics.
       *
       * Note Intel VMX ignores the caching type returned from virglrenderer, while AMD SVM and
       * ARM honor it.
       */
      if (is_mesa) {
         if (strstr(vendor, "Intel") != NULL)
            vrend_state.inferred_gl_caching_type = VIRGL_RENDERER_MAP_CACHE_CACHED;
         else if (strstr(vendor, "AMD") != NULL)
            vrend_state.inferred_gl_caching_type = VIRGL_RENDERER_MAP_CACHE_WC;
         else if (strstr(vendor, "Mesa") != NULL)
            vrend_state.inferred_gl_caching_type = VIRGL_RENDERER_MAP_CACHE_CACHED;
      } else {
         /* This is an educated guess since things don't explode with VMX + Nvidia. */
         if (strstr(renderer, "NVIDIA") != NULL)
            vrend_state.inferred_gl_caching_type = VIRGL_RENDERER_MAP_CACHE_UNCACHED;
      }

      if (vrend_state.inferred_gl_caching_type)
         caps->v2.capability_bits |= VIRGL_CAP_ARB_BUFFER_STORAGE;
   }

```

##  References
- https://github.com/Keenuts/virtio-gpu-win-icd
- https://github.com/kjliew/qemu-3dfx
- https://gitlab.freedesktop.org/mesa/mesa
- https://gitlab.freedesktop.org/virgl/virglrenderer
