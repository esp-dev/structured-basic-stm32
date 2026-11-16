# Third-party components

The port uses the following external code when building MCU firmware.
The source checkout expects the user to supply STM32CubeF4 v1.27.1 and
the Arm GNU toolchain.

| Component | License or notice supplied with the dependency |
| --- | --- |
| STM32F4 HAL | `Drivers/STM32F4xx_HAL_Driver/LICENSE.md` in the GitHub SDK (`LICENSE.txt` in the installed package) |
| Arm CMSIS | `Drivers/CMSIS/LICENSE.txt` in STM32CubeF4 |
| STM32F4 device definitions and startup | `Drivers/CMSIS/Device/ST/STM32F4xx/License.md` in the GitHub SDK (`LICENSE.txt` in the installed package) |
| FatFs R0.12c | `Middlewares/Third_Party/FatFS/st_license.txt` and the FatFs source headers in STM32CubeF4 (`FatFs` in the installed package) |
| Newlib and GCC runtimes | License and notices supplied with the selected Arm GNU toolchain |

Check those notices when preparing firmware binaries for redistribution.
The [project license](LICENSE) covers the original port code. The interpreter
submodule has its own license file.
