# IDE environment doctor

`doctor.pp` is a tiny PunPun-native probe used by editor tooling to understand the host environment without duplicating PunPun runtime assumptions in each IDE.

Run it with a current compiler:

```sh
ppc run tools/ide/doctor.pp
```

It prints eight newline-delimited fields in this order:

1. protocol marker: `PUNPUN_IDE_DOCTOR_V1`
2. `system_platform()`
3. current working directory
4. `PATH`
5. `PPC_RUNTIME`
6. `PPC_STDLIB`
7. `CC`
8. `CXX`

The protocol is intentionally simple so IDEs can consume it before a richer JSON stdlib surface exists. The probe is written in PunPun and uses only `std.system`, so it also doubles as a quick verification that the installed compiler can locate the runtime and standard library correctly.
