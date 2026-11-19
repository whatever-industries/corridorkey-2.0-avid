"""Persistent, local CorridorKey frame worker for the macOS AVX adapter.

Request: little-endian uint32 width and height, then packed float32
scene-linear Rec.709 RGB samples in top-to-bottom row order.
Reply: little-endian uint32 status and byte count, then packed float32
straight RGBA with a linear coverage alpha (status 0) or UTF-8 error (status 1).
One request may be in flight. Diagnostics are written to stderr.
"""

import argparse
from collections import OrderedDict
from contextlib import contextmanager, nullcontext
import hashlib
import os
from pathlib import Path
import struct
import sys
import tempfile
import traceback


HEADER = struct.Struct("<II")


class NonFiniteFrame(RuntimeError):
    """The Apple GPU returned unusable samples for an otherwise valid frame."""


@contextmanager
def blocking_mps_uploads(torch):
    # The pinned runtime passes temporary CPU tensors to asynchronous MPS
    # copies. PyTorch can release their storage before the blit reads it
    # (pytorch/pytorch#189690). A later stream fence cannot restore that data.
    # Scope the workaround to inference in this dedicated worker process.
    original_to = torch.Tensor.to

    def safe_to(tensor, *args, **kwargs):
        if kwargs.get("non_blocking") and tensor.device.type == "cpu":
            device = kwargs.get("device", args[0] if args else None)
            device_type = device.type if isinstance(device, torch.device) else device
            if isinstance(device_type, str) and device_type.split(":", 1)[0] == "mps":
                kwargs["non_blocking"] = False
        return original_to(tensor, *args, **kwargs)

    torch.Tensor.to = safe_to
    try:
        yield
    finally:
        torch.Tensor.to = original_to


def synchronize_mps_stages(runtime, torch):
    # The pinned MPS port can emit NaNs with asynchronous stage boundaries.
    # Keep execution on Metal and wait for each inference stage to finish.
    original_autocast = runtime._autocast

    @contextmanager
    def synchronized_autocast(device):
        torch.mps.synchronize()
        autocast = nullcontext() if runtime._inference_dtype(device) == torch.float32 else original_autocast(device)
        with autocast:
            yield
        torch.mps.synchronize()

    runtime._autocast = synchronized_autocast


class FrameKeyer:
    def __init__(self, source_root, asset_root):
        self.source_root = source_root
        self.asset_root = asset_root
        self.model = None
        self.mps_configured = False

    def __call__(self, width, height, payload):
        import torch
        from corridorkey import runtime

        try:
            return self.key_frame(width, height, payload)
        except NonFiniteFrame as error:
            print(f"{error} Reloading the model and retrying in float32 on MPS.", file=sys.stderr)
            original_dtype = runtime._inference_dtype
            runtime._inference_dtype = lambda device: torch.float32
            try:
                return self.key_frame(width, height, payload)
            finally:
                runtime._inference_dtype = original_dtype

    def key_frame(self, width, height, payload):
        import cv2
        import numpy as np
        import torch
        from corridorkey import runtime

        rgb = np.frombuffer(payload, dtype="<f4").reshape(height, width, 3)
        if not np.isfinite(rgb).all():
            raise ValueError("The input frame contains non-finite RGB samples.")

        if self.model is None:
            paths = runtime.RuntimePaths.from_package(self.asset_root)
            paths.package_root = self.source_root
            paths.validate()
            device = runtime.select_accelerator("mps")
            if not self.mps_configured:
                synchronize_mps_stages(runtime, torch)
                self.mps_configured = True
            self.model = runtime.load_model(paths, device=device)

        try:
            with tempfile.TemporaryDirectory(prefix="corridorkey-frame-") as directory:
                root = Path(directory)
                input_dir = root / "plate"
                input_dir.mkdir()
                frame = input_dir / "frame.exr"
                if not cv2.imwrite(str(frame), cv2.cvtColor(rgb, cv2.COLOR_RGB2BGR)):
                    raise OSError("Could not write the temporary input frame.")

                output_dir = root / "output"
                config = runtime.InferenceConfig(
                    hann_chunk=1,
                    hann_stride=1,
                    carry_hint=False,
                    low_vram=True,
                    outputs={"cutout"},
                    # Avid applies the selected despill after the matte cache.
                    despill_strength=0.0,
                    frame_dir_linear=True,
                    output_format="exr",
                    exr_dtype="float",
                    temp_dir=root / "scratch",
                )
                with blocking_mps_uploads(torch):
                    runtime.run_inference(
                        self.model, str(input_dir), str(output_dir), config, runtime.WriterConfig(),
                        initial_hint_path=None, hint_video_path=None,
                    )
                cutout = output_dir / "plate_cutout_exr" / "cutout_000000.exr"
                bgra = cv2.imread(str(cutout), cv2.IMREAD_UNCHANGED)
                if (
                    bgra is None
                    or bgra.shape != (height, width, 4)
                    or bgra.dtype != np.float32
                ):
                    raise RuntimeError("CorridorKey did not produce a valid float RGBA frame.")
                if not np.isfinite(bgra).all():
                    raise NonFiniteFrame("Apple GPU inference returned non-finite pixels.")
                return cv2.cvtColor(bgra, cv2.COLOR_BGRA2RGBA).astype("<f4", copy=False).tobytes()
        except Exception:
            # Upstream inference moves model stages between devices. Reload
            # after failures so a retry always starts from a known state.
            self.model = None
            raise


class MatteCache:
    """Cache full-precision foreground before despill and matte for this worker's lifetime."""

    def __init__(self, key_frame, max_bytes=4 * 1024**3, max_frames=128):
        self.key_frame = key_frame
        self.max_bytes = max_bytes
        self.max_frames = max_frames
        self.entries = OrderedDict()
        self.total_bytes = 0

    def _remove(self, key):
        stream, size = self.entries.pop(key)
        self.total_bytes -= size
        stream.close()

    def close(self):
        while self.entries:
            self._remove(next(iter(self.entries)))

    def __call__(self, width, height, payload):
        key = (width, height, hashlib.sha256(payload).digest())
        size = width * height * 4 * 4
        if key in self.entries:
            stream, _ = self.entries[key]
            try:
                stream.seek(0)
                rgba = stream.read(size)
                if len(rgba) == size:
                    self.entries.move_to_end(key)
                    print(f"CorridorKey matte cache hit ({width}x{height}).", file=sys.stderr)
                    return rgba
            except OSError:
                pass
            self._remove(key)

        rgba = self.key_frame(width, height, payload)
        if len(rgba) == size and size <= self.max_bytes and self.max_frames > 0:
            self._store(key, rgba)
        return rgba

    def _store(self, key, rgba):
        while (
            len(self.entries) >= self.max_frames
            or self.total_bytes + len(rgba) > self.max_bytes
        ):
            self._remove(next(iter(self.entries)))

        # macOS unlinks TemporaryFile on open. The OS also reclaims these
        # files if the worker is interrupted or Avid exits unexpectedly.
        stream = None
        try:
            stream = tempfile.TemporaryFile(buffering=0)
            if stream.write(rgba) == len(rgba):
                self.entries[key] = (stream, len(rgba))
                self.total_bytes += len(rgba)
                return
        except OSError:
            pass
        if stream is not None:
            stream.close()


def write_reply(stream, status, payload):
    stream.write(HEADER.pack(status, len(payload)))
    stream.write(payload)
    stream.flush()


def serve(input_stream, output_stream, key_frame):
    while True:
        header = input_stream.read(HEADER.size)
        if not header:
            return
        if len(header) != HEADER.size:
            write_reply(output_stream, 1, b"Incomplete frame header.")
            return
        width, height = HEADER.unpack(header)
        if not 0 < width <= 1920 or not 0 < height <= 1080:
            write_reply(output_stream, 1, b"Supported frame sizes are at most 1920x1080.")
            return
        payload_size = width * height * 3 * 4
        frame = input_stream.read(payload_size)
        if len(frame) != payload_size:
            write_reply(output_stream, 1, b"Incomplete RGB frame.")
            return
        try:
            result = key_frame(width, height, frame)
            if len(result) != width * height * 4 * 4:
                raise RuntimeError("Unexpected RGBA byte count from CorridorKey.")
        except Exception as error:
            traceback.print_exc()
            write_reply(output_stream, 1, str(error).encode("utf-8", errors="replace"))
        else:
            write_reply(output_stream, 0, result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--asset-root", type=Path)
    args = parser.parse_args()

    source_root = args.source_root.resolve()
    asset_root = (args.asset_root or source_root).resolve()
    if not (source_root / "corridorkey" / "runtime.py").is_file():
        parser.error(f"No CorridorKey runtime in {source_root}")
    sys.path.insert(0, str(source_root))
    os.environ.update(
        PYTORCH_ENABLE_MPS_FALLBACK="0",
        OPENCV_IO_ENABLE_OPENEXR="1",
        HF_HUB_OFFLINE="1",
        TRANSFORMERS_OFFLINE="1",
    )

    # Preserve a private copy of the original stdout for the frame protocol.
    # Redirect the file descriptor as well as Python prints so native library
    # output also goes to stderr.
    with os.fdopen(os.dup(sys.stdout.fileno()), "wb") as protocol_out:
        os.dup2(sys.stderr.fileno(), sys.stdout.fileno())
        cache = MatteCache(FrameKeyer(source_root, asset_root))
        try:
            serve(sys.stdin.buffer, protocol_out, cache)
        finally:
            cache.close()


if __name__ == "__main__":
    main()
