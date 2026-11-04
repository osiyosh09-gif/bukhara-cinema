"""Reads the phase 15a sample files with the OpenTimelineIO reference library.

    python tests/check_interchange.py <dir>

<dir> holds sample.otio, sample.xml and sample.edl written by
`tst_phase15a writeSamplesForReferenceCheck` (BC_INTERCHANGE_OUT=<dir>).
Needs: pip install opentimelineio otio-fcp-adapter otio-cmx3600-adapter

The expected edit is the sample sequence of tests/tst_phase15a.cpp (25 fps);
every reader must find the clips at the same places (cut to cut) with the
same source frames. A different reader understanding the files the same way
is the check, not our own parser.
"""
import sys
from pathlib import Path

import opentimelineio as otio

RATE = 25
# (start on the track, duration, first source frame, file name)
V1 = [(0, 100, 25, "a b.mp4"), (100, 100, 50, "b.mov"), (300, 50, 0, "b.mov"), (400, 50, 10, "a b.mp4")]
V2 = [(200, 50, 0, "c.png")]
A1 = V1
DISSOLVE = (10, 10)  # frames before / after the cut at 100 on V1 and A1

errors = []


def frames(t):
    return int(round(t.value_rescaled_to(RATE)))


def clips_of(track):
    out = []
    for child in track:
        if isinstance(child, otio.schema.Clip):
            r = track.range_of_child(child)
            out.append((frames(r.start_time), frames(r.duration), frames(child.source_range.start_time), child))
    return out


def check_track(label, track, expected, check_names=True):
    got = clips_of(track)
    if len(got) != len(expected):
        errors.append(f"{label}: {len(got)} clips, expected {len(expected)}: {[g[:3] for g in got]}")
        return
    for (s, d, i, clip), (es, ed, ei, name) in zip(got, expected):
        if (s, d, i) != (es, ed, ei):
            errors.append(f"{label}: clip {clip.name!r} at {s}+{d} from {i}, expected {es}+{ed} from {ei}")
        ref = clip.media_reference
        url = getattr(ref, "target_url", "") or ""
        if check_names and name not in clip.name and name.replace(" ", "%20") not in url and name not in url:
            errors.append(f"{label}: clip {clip.name!r} ({url}) is not {name}")


def check_dissolve(label, track):
    ts = [c for c in track if isinstance(c, otio.schema.Transition)]
    if len(ts) != 1:
        errors.append(f"{label}: {len(ts)} transitions, expected 1")
        return
    got = (frames(ts[0].in_offset), frames(ts[0].out_offset))
    if got != DISSOLVE:
        errors.append(f"{label}: dissolve {got}, expected {DISSOLVE}")


def kinds(tl, kind):
    return [t for t in tl.tracks if t.kind == kind]


def check_full(label, tl):
    video, audio = kinds(tl, otio.schema.TrackKind.Video), kinds(tl, otio.schema.TrackKind.Audio)
    if len(video) < 2:
        errors.append(f"{label}: {len(video)} video tracks, expected 2")
        return
    check_track(f"{label} V1", video[0], V1)
    check_dissolve(f"{label} V1", video[0])
    check_track(f"{label} V2", video[1], V2)
    if not audio:
        errors.append(f"{label}: no audio track")
        return
    check_track(f"{label} A1", audio[0], A1)
    check_dissolve(f"{label} A1", audio[0])


def main():
    d = Path(sys.argv[1])
    print("opentimelineio", otio.__version__)

    tl = otio.adapters.read_from_file(str(d / "sample.otio"))
    check_full("OTIO", tl)
    markers = list(tl.tracks.markers)
    if len(markers) != 1 or frames(markers[0].marked_range.start_time) != 120:
        errors.append(f"OTIO: markers {[(m.name, m.marked_range) for m in markers]}")
    # It must survive a round trip through the library's own writer.
    otio.adapters.read_from_string(otio.adapters.write_to_string(tl, "otio_json"), "otio_json")

    check_full("FCP XML", otio.adapters.read_from_file(str(d / "sample.xml"), adapter_name="fcp_xml"))

    edl = otio.adapters.read_from_file(str(d / "sample.edl"), adapter_name="cmx_3600", rate=RATE)
    video = kinds(edl, otio.schema.TrackKind.Video)
    if len(video) != 1:
        errors.append(f"EDL: {len(video)} video tracks, expected 1")
    else:
        check_track("EDL V1", video[0], V1)

    if errors:
        print("\n".join(errors))
        return 1
    print("OpenTimelineIO reads the OTIO, FCP XML and EDL files as expected")
    return 0


if __name__ == "__main__":
    sys.exit(main())
