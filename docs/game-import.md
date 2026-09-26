# Game imports on iOS

The library **+** menu provides two game import actions:

- **Import game (.vpk / .zip / .pkg)** selects an archive/package.
- **Import game folder (NoNpDrm)** selects a directory in Files. Select the game
  directory containing `sce_sys/param.sfo`, or a parent containing several games,
  updates and DLC. The app copies the selection; it does not modify the original.

Install system firmware/fonts first. Supported installation does not imply every
Vita game runs: CPU instruction support, HLE and graphics compatibility still
apply. The experimental IR interpreter cannot run general retail games; use JIT
when available for those games.

## VPK and ZIP

VPK is a ZIP container. Already-decrypted/homebrew packages need
`sce_sys/param.sfo` and `eboot.bin` at the application root; a wrapper folder is
also accepted. ZIPs may contain multiple application roots discovered through
PARAM.SFO. Updates are installed under `ux0/patch/TITLE_ID`; install their base
first or include the base in the same archive. DLC is installed separately under
`ux0/addcont/TITLE_ID/CONTENT_ID_SUFFIX`, preserving other DLC for that game.

Password-encrypted ZIP entries, corrupt/truncated entries, unsafe paths,
duplicate paths, conflicting destinations and malformed metadata are rejected.
Unsupported legacy dump modifications are not converted by this importer.

## NoNpDrm

Encrypted content is identified by its `sce_pfs` directory and prepared with the
existing Vita3K PFS decoder. The importer uses an exact matching license from:

1. `sce_sys/package/work.bin` inside the content root;
2. a sibling `license/.../FULL_CONTENT_ID.rif` in a full dump;
3. a license imported earlier with **Import license (work.bin)**.

A license must be a complete 512-byte RIF/work.bin with a valid matching Content
ID. A patch can share its base game's Content ID; each DLC uses its own license.
The filename alone does not establish that the license matches. The PFS decoder
validates whether the key can actually decrypt the content.

Example ZIP/directory layout:

```text
app/PCSE00001/sce_sys/param.sfo
app/PCSE00001/sce_sys/package/work.bin
app/PCSE00001/sce_pfs/...
app/PCSE00001/eboot.bin
patch/PCSE00001/sce_sys/param.sfo
patch/PCSE00001/sce_pfs/...
addcont/PCSE00001/DLC0000000000001/sce_sys/param.sfo
addcont/PCSE00001/DLC0000000000001/sce_sys/package/work.bin
addcont/PCSE00001/DLC0000000000001/sce_pfs/...
```

If a license is missing, import it and select the game again. Missing/wrong
licenses and failed decryption reject the new installation before replacing the
previous content. License import also prepares matching encrypted app/update/DLC
left by older versions, on a worker thread. Successful imports refresh the library.

## Storage and verification

Imports stream files from disk. Directory imports temporarily write an
uncompressed ZIP on disk to reuse the archive transaction, and remove it afterward.
Allow storage for the selection copy, staging, decrypted output and the existing
installation until commit completes. This avoids retaining entire games in RAM,
but uses extra disk space. Archives are limited to 100,000 entries and 32 GiB of
uncompressed data; metadata is bounded at 16 MiB.

The host test `test_archive_install` uses real miniz, SFO parsing, license
validation, archive transactions and the iOS preparation callback. Its PFS decoder
is synthetic, testing success/failure/exception and replacement behavior. Real
NoNpDrm cryptography, Files-provider behavior and game launch require an iPhone
with a valid dump/license; host fixtures do not establish game compatibility.

```sh
PYTHONPATH=.ci/tests python3 -m unittest test_archive_install test_ios_install_and_cache -v
```
