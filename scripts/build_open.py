"""Build the head-unit programs with the open QNX 6.5.0 toolchain (no QNX SDP, no VM).

Prepares the sources exactly like scripts/build_all.py (pinned NBT-AAD + overlay + patches, Mbed TLS 3.6.7 with
the QNX config) and runs scripts/build-open.sh in the toolchain container (toolchain/qnx-open). The toolchain must be
built once first, see toolchain/README.md.

Output: build/open-build-<time>-<id>/programs (+ plugin-resources), the same layout as a build_all.py run, so
installer/make_payload.py --built can use it. The HMI plugin is linked against experimental symbol stubs.
accessory_window needs QNX resource-manager headers, which the open sysroot does not have; --reuse copies it
from an existing installer payload instead.

Example:
  python3 scripts/build_open.py --reuse .local/release-payload/NBT-AndroidAuto-Python-2026.11.10.2/payload
  python3 installer/make_payload.py 2026.11.16-open --built build/open-build-<time>-<id> \
      --pem-dir .local/release-payload/NBT-AndroidAuto-Python-2026.11.10.2/payload/rt
"""
import argparse
import shutil
import subprocess
import sys
import tarfile
import time
import uuid
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_all import NBT_AAD_REVISION, PROGRAMS, ROOT, Archive, add_mbedtls, add_nbt_aad, add_repo_sources  # noqa: E402
from ffmpeg_qnx import add_ffmpeg

OPEN_PROGRAMS = ("oneplus_native", "native_service", "decode_runner", "fifo_http", "nbt_knob",
                 "nbt_hwvideo", "nbt_cluster", "nbt_key", "nbt_a2i_beep", "nbt_decoder",
                 "nbt_reset", "libNbtAndroidAuto.so")


def select_programs(value):
    if value is None:
        return OPEN_PROGRAMS
    names = value.split()
    if not names:
        raise SystemExit("--programs must name at least one program")
    unknown = set(names) - set(OPEN_PROGRAMS)
    if unknown:
        raise SystemExit("Unknown open-build program(s): " + ", ".join(sorted(unknown)))
    return tuple(dict.fromkeys(names))


def copy_built_programs(source, destination, selected):
    for name in selected:
        path = source / name
        if not path.is_file() or path.stat().st_size == 0:
            raise SystemExit("Open build did not produce a nonempty " + name)
        shutil.copy(path, destination / name)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--nbt-aad", type=Path, default=ROOT / ".local/reference-nbt-aad",
                        help="NBT-AAD checkout at commit %s" % NBT_AAD_REVISION[:7])
    parser.add_argument("--programs", help="space-separated list (default: all supported programs including HMI plugin)")
    parser.add_argument("--reuse", type=Path,
                        help="installer payload folder (rt/, plugin/) to take programs from that were not built")
    parser.add_argument("--output-dir", type=Path,
                        help="new output directory inside build/ (default: unique timestamped directory)")
    args = parser.parse_args()

    selected = select_programs(args.programs)
    run_id = "open-build-" + time.strftime("%Y%m%dT%H%M%S") + "-" + uuid.uuid4().hex[:8]
    build_dir = (ROOT / "build").resolve()
    out = (ROOT / args.output_dir).resolve() if args.output_dir else build_dir / run_id
    if out == build_dir or build_dir not in out.parents:
        raise SystemExit("--output-dir must be a new directory inside build/")
    out.mkdir(parents=True, exist_ok=False)
    work = out / "work"
    work.mkdir()
    archive_path = out / "source.tar"
    archive = Archive(archive_path)
    add_mbedtls(archive)
    add_nbt_aad(archive, args.nbt_aad.resolve())
    add_repo_sources(archive)
    if "nbt_decoder" in selected:
        add_ffmpeg(archive)
    archive.close()
    with tarfile.open(archive_path) as tar:
        tar.extractall(work)
    shutil.copy(ROOT / "scripts/build-open.sh", work / "build-open.sh")
    if "nbt_decoder" in selected:
        shutil.copy(ROOT / "scripts/build-ffmpeg-qnx.sh", work / "build-ffmpeg-qnx.sh")

    env = ["-e", "PROGRAMS=" + " ".join(selected)]
    rel = work.relative_to(ROOT).as_posix()
    command = ["docker", "run", "--rm", "--platform", "linux/amd64",
               "-v", "%s/build/qnx-open:/opt/qnx-open" % ROOT, "-v", "%s:/repo" % ROOT, "-w", "/repo/" + rel,
               "-e", "PATH=/opt/qnx-open/bin:/usr/bin:/bin", *env, "qnx-open-host", "sh", "build-open.sh"]
    print("building in the toolchain container:", rel, flush=True)
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    log = result.stdout.decode(errors="replace")
    (out / "build.log").write_text(log, encoding="utf-8")
    print(log[-3000:])
    if result.returncode != 0 or "BUILD_OPEN_DONE" not in log:
        raise SystemExit("build failed, full log: " + str(out / "build.log"))

    programs = out / "programs"
    programs.mkdir()
    copy_built_programs(work / "out", programs, selected)
    if args.reuse:
        for name, folder in PROGRAMS.items():
            source = args.reuse / folder / name
            if not (programs / name).exists() and source.is_file():
                if source.stat().st_size == 0:
                    raise SystemExit("Cannot reuse empty program: " + str(source))
                shutil.copy(source, programs / name)
                print("reused from", args.reuse.name + ":", name)
    if args.programs is None and not (programs / "accessory_window").is_file():
        print("NOTE: accessory_window is missing; a complete installer payload requires a compatible existing binary.",
              file=sys.stderr)
    resources = out / "plugin-resources"
    subprocess.run([sys.executable, str(ROOT / "src/idrive/make_aa_entry_resources.py"), str(resources)], check=True)
    shutil.copy(ROOT / "scripts/nbt-aa-boot.sh", programs / "boot.sh")
    shutil.copy(ROOT / "scripts/nbt-aa-restart.sh", programs / "restart.sh")
    print("programs:", programs)
    print("plugin XML/manifest/texts:", resources)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
