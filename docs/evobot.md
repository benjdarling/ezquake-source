# EvoBot local-server integration

ezQuake links the canonical portable EvoBot core from the `external/evobot`
submodule. The core owns bot lifecycle state, convex navigation generation,
`.botnav` persistence, and OBJ export. `src/evobot_ezq_adapter.c` translates
between portable EvoBot values and ezQuake's local QuakeWorld server state.

The collision hulls, traces, entity fields, fake-client physics, and PR1/PR2
game-code calls follow the same QuakeWorld semantics as the MVDSV host. The
adapter itself remains ezQuake-specific because local-server teardown does not
end the client process, filesystem writes use ezQuake's writable game path, and
full EvoBot shutdown occurs only during process shutdown.

## Build and run

Configure and build from PowerShell:

```powershell
cmake --preset msbuild-x64
cmake --build --preset msbuild-x64-debug
```

Launch the local E1M1 development configuration with
`run-evobot-local.cmd`, or run:

```powershell
build-msbuild-x64\Debug\ezquake.exe -basedir K:\development\EvoBot\server -game evosp -progtype 0 +set sv_progsname spprogs +set deathmatch 0 +set coop 1 +set skill 1 +map e1m1
```

The launcher also detects `build-msbuild-x64-vs2022\Debug\ezquake.exe` when
the installed toolchain is Visual Studio 2022 rather than the preset's newer
Visual Studio generator.

The local console exposes:

```text
evobot_version
evobot_status
evobot_add <name>
evobot_remove <name>
evobot_nav_generate
evobot_nav_status
evobot_nav_save
evobot_nav_load [map]
evobot_nav_clear
evobot_nav_export_obj
```

Debug rendering, reachabilities, routing, movement, and AI are not part of
this integration.
