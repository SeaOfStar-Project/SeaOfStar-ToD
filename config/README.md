# config

`game.example.json` is tracked. Copy it to `game.json` and edit the paths for your machine:

```powershell
Copy-Item config\game.example.json config\game.json
```

`game.json` is gitignored because it contains local paths and your disc location.

## Save import keys (`sd-keys.txt`)

`tools\import-save.ps1` and `tools\wii-save-extract.py` decrypt a Wii `data.bin` save export
with the console's SD key and SD IV. These keys are not shipped with the project. Supply them
from your own console (for example dumped with BootMii or a keys.bin tool), in any one of
these ways (first match wins):

1. `--sd-key <32 hex> --sd-iv <32 hex>` on `wii-save-extract.py`
2. environment variables `WII_SD_KEY` and `WII_SD_IV` (these also reach the script when you run `import-save.ps1`)
3. `config\sd-keys.txt` with two lines, 32 hex characters each:

```
sd_key=<32 hex characters>
sd_iv=<32 hex characters>
```

`sd-keys.txt` is gitignored. Without keys the script exits with an error. Unpacked save
folders (`-Source`) need no keys.

## Fields

| Field | Meaning |
|---|---|
| `game.titleId` | Six-character Wii title ID: `SO3E01` (USA), `SO3P01` (PAL), `SO3J01` (Japan) |
| `game.disc` | Path to your own ISO/WBFS/RVZ dump, relative to the repo root |
| `game.extracted` | Where `wit`/`nodtool`/Dolphin extracts the disc |
| `recomp.mainDol` | `sys/main.dol` inside the extracted tree |
| `recomp.relDir` | Folder containing `*.rel` modules, if the game uses them |
| `recomp.output` | Root output folder for generated code (gitignored) |
| `recomp.map` | Optional linker MAP or symbol map for readable function names |
| `recomp.config` | Optional DolRecomp patch TOML (`--config`), e.g. a `patches.toml` of your own |
| `runtime.backend` | `custom` (this repo's runtime) or `moderngekko` (external template) |
| `runtime.graphics` | `vulkan`, `d3d11`, or `opengl` — backend preference, not a guarantee |
| `runtime.scale` | Internal resolution multiplier, once the renderer supports it |
| `runtime.widescreen` | Enable the widescreen patch, once it exists |
| `runtime.unlockFps` | Leave `false` until frame-rate coupling has been audited |

The scripts fall back to `game.example.json` with a warning if `game.json` is absent, but
extraction and recompilation will fail until the paths point at a real dump.
