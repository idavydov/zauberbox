OpenMV upstream licensing note
==============================

Source location:

- https://github.com/openmv/openmv
- https://github.com/openmv/openmv/blob/master/README.md

Vendored files in this repository derived from OpenMV-related sources:

- `collections.c`
- `collections.h`
- `fmath.h`

Upstream licensing summary copied from the OpenMV repository documentation:

> Licensing
>
> Most of the code in the repository is licensed under the MIT license, with the following exceptions:
>
> Some image library code is licensed under the GPL. This includes AGAST, LSD, and ZBAR. GPL code can be completely disabled in a build by defining OMV_NO_GPL in the imlib_config.h files.
> Third-party libraries and drivers in lib and drivers are licensed under various permissive licenses. Please consult the LICENSE file in each driver/library subdirectory for more details.
> Some drivers, modules, and libraries in OpenMV are proprietary and available for non-commercial use only. These proprietary components can be disabled during the build process. Official OpenMV hardware and licensed devices may use the proprietary code. For commercial licensing options, contact openmv@openmv.io.
