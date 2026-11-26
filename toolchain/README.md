# Experimental open QNX cross-build

The open toolchain builds QNX 6.5 x86 programs without the QNX SDP or a VM. It uses pinned GNU GCC 4.4.2 and
binutils, project-written headers and runtime support, and **link-only stubs** for QNX and HMI libraries.
The stubs are not included in build outputs.

On Linux with Docker, Python 3, Git, curl, `sha256sum` and yasm 1.3.0:

```sh
toolchain/setup.sh
git clone https://github.com/Sq00ky/Supra-iDrive6-Android-Auto .local/reference-nbt-aad
git -C .local/reference-nbt-aad checkout 41975f269b065dbdbc77af9f568387cd22b9ed27
python3 scripts/build_open.py
```

The default build prepares the same pinned NBT-AAD overlay and patches as `build_all.py`, downloads the pinned
Mbed TLS release, and prepares FFmpeg for `nbt_decoder`. Output is in `build/open-build-<time>-<id>/programs`.
`--programs` selects a space-separated subset; `--output-dir build/<new-directory>` selects the output location.
`accessory_window` requires QNX resource-manager headers and is not built by this toolchain. `--reuse` can supply
missing components from an existing payload.

The host suite uses the same pinned reference and Mbed TLS source, and creates disposable TLS certificates:

```sh
python3 -m pip install cryptography==44.0.3
python3 scripts/test_host.py
python3 scripts/test_host.py --sanitize
python3 -m unittest discover -s scripts -p 'test_*.py' -v
```

CI checks ELF class and architecture and retains the experimental outputs, build log, hashes and ELF metadata for
14 days. The open headers and stub-linked artifacts do **not** establish ABI compatibility or vehicle safety.
Use the QNX VM build for supported vehicle deployments; host and cross-build tests are not car tests.
