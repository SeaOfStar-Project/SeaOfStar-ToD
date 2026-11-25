# analysis

Working notes, symbol maps, and dumps. Everything except this README is gitignored.

`tools/extract-disc.ps1` writes:

| File | Contents |
|---|---|
| `disc-manifest.json` | Disc hash, DOL header, sections, modules, middleware markers, inventory |

`tools/recompile.ps1` writes:

| File | Contents |
|---|---|
| `recompile-stats.json` | Backend, title ID, output path, file counts, generated size |

Files to add by hand as the project progresses:

| File | Contents |
|---|---|
| `functions.csv` | Address, size, provisional name, callers |
| `sdk-calls.md` | SDK functions found and their replacement status |
| `audio-notes.md` | CRI/CPK/Sofdec findings, ucode, sample rates |
| `rel-layout.md` | RSO/REL modules and load addresses |
| `smc.md` | Self-modifying-code candidates and patches |
| `dolphin/` | FIFO dumps (`.dff`), frame dumps, audio dumps, savestates |

## Verified baseline

- Disc ID: `SO3EE9` (USA)
- DOL entry point: `0x80004050`
- Text: `0x80004000` + `0x80009180`, 5.29 MB total
- BSS: `0x805EA980`, 861,884 bytes
- No `*.rel` modules
- One RSO module: `files/Sofdec.rso` (552,192 bytes)
- Middleware markers in `main.dol`: Sofdec, CRI, CPK, ADX, HCA, AX, WPAD, GX, IOS, NAND,
  DVD, Revolution
