"""Build and run hardware-independent tests on a Linux host."""
import argparse
import importlib.util
import os
from pathlib import Path
import struct
import subprocess
import tarfile
import tempfile
import zlib

from build_all import Archive, ROOT, add_mbedtls, add_nbt_aad


def png_chunk(kind, data):
    payload = kind + data
    return struct.pack(">I", len(data)) + payload + struct.pack(">I", zlib.crc32(payload))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true", help="enable AddressSanitizer and UBSan")
    parser.add_argument("--nbt-aad", type=Path, default=ROOT / ".local/reference-nbt-aad")
    args = parser.parse_args()
    reference = args.nbt_aad.resolve()
    flags = ["-std=gnu++0x", "-O1", "-g", "-Wall", "-Wextra", "-pthread"]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]

    with tempfile.TemporaryDirectory(prefix="nbt-host-tests-") as directory:
        work = Path(directory)
        archive_path = work / "sources.tar"
        archive = Archive(archive_path)
        try:
            add_nbt_aad(archive, reference)
            add_mbedtls(archive)
        finally:
            archive.close()
        with tarfile.open(archive_path) as archive:
            archive.extractall(work)
        subprocess.run(["make", "-C", str(work / "mbedtls-3.6.7/library"), "-j2"], check=True,
                       stdout=subprocess.DEVNULL)

        h264 = work / "fixture.h264"
        h264.write_bytes(b"\x00\x00\x00\x01\x67\x42\x00\x1e"
                         b"\x00\x00\x01\x68\xce\x06"
                         b"\x00\x00\x01\x65\x88\x84")
        png = work / "fixture.png"
        png.write_bytes(b"\x89PNG\r\n\x1a\n" +
                        png_chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0)) +
                        png_chunk(b"IDAT", zlib.compress(b"\x00\x00\x00\x00")) + png_chunk(b"IEND", b""))

        ref = work / "reference"
        src = ROOT / "src/android_auto"
        tls = work / "mbedtls-3.6.7"
        includes = ["-I" + str(ref / "src"), "-I" + str(ref / "include"), "-I" + str(tls / "include")]
        core = [ref / "src" / name for name in
                ("aa_session.cpp", "aa_cryptor.cpp", "audio/audio_acks.cpp", "sdr_builder.cpp", "crash_handler.cpp")]
        libraries = [tls / "library" / name for name in ("libmbedtls.a", "libmbedx509.a", "libmbedcrypto.a")]

        spec = importlib.util.spec_from_file_location("tls_fixtures", ROOT / "scripts/Test-QnxTls.py")
        fixtures = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(fixtures)
        for name, data in fixtures.fixtures().items():
            (work / name).write_bytes(data)

        tests = [
            ("annexb", [src / "test_h264_annexb.cpp"], [h264, work / "normalized.h264"], []),
            ("frame_packet", [ROOT / "experiments/png-pipeline/test_frame_packet.cpp"], [png], []),
            ("session_protocol", [src / "test_session_protocol.cpp", *core], [], libraries + ["-ldl"]),
            ("video_capture", [src / "test_video_capture.cpp", src / "aa_video_capture.cpp",
                               src / "aa_media_out.cpp", src / "aa_mic.cpp", *core], [], libraries + ["-ldl"]),
        ]
        upstream = reference / "milestone-7/tests"
        for name, sources in {
            "test_sdr": ["sdr_builder.cpp"],
            "test_sdr_drop": ["sdr_builder.cpp"],
            "test_audio_format": ["sdr_builder.cpp"],
            "test_audio_channels": [],
            "test_audio_acks": ["audio/audio_acks.cpp"],
            "test_media_ack": [],
            "test_media_msgs": [],
            "test_session_owner": [],
        }.items():
            tests.append((name, [upstream / (name + ".cpp"), *(ref / "src" / s for s in sources)], [], []))
        for name, sources, arguments, extra in tests:
            executable = work / name
            print("==", name, flush=True)
            subprocess.run([os.environ.get("CXX", "g++"), *flags, *includes, *map(str, sources),
                            *map(str, extra), "-o", str(executable)], check=True)
            subprocess.run([str(executable), *map(str, arguments)], cwd=work, check=True)


if __name__ == "__main__":
    main()
