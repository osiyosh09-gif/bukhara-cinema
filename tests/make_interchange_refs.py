"""Writes reference timelines with the OpenTimelineIO library (phase 15b).

    python tests/make_interchange_refs.py <dir>

<dir>/ref.otio, ref.xml (fcp_xml adapter) and ref.edl (cmx_3600 adapter)
hold the same edit at 25 fps; `tst_phase15b readReferenceFiles` with
BC_INTERCHANGE_IN=<dir> imports them. So the importer is checked against
files another program wrote, not only against our own writer.
Needs: pip install opentimelineio otio-fcp-adapter otio-cmx3600-adapter
"""
import sys
from pathlib import Path

import opentimelineio as otio

RATE = 25


def rt(v):
    return otio.opentime.RationalTime(v, RATE)


def tr(start, duration):
    return otio.opentime.TimeRange(rt(start), rt(duration))


def clip(name, start, duration):
    ref = otio.schema.ExternalReference(target_url=f"file:///media/ref/{name}", available_range=tr(0, 250))
    return otio.schema.Clip(name=name, media_reference=ref, source_range=tr(start, duration))


def track(kind, name):
    # a.mov [0,100) from 25 | 20-frame dissolve centred on the cut at 100 |
    # b.mov [100,200) from 50 | gap | c.mov [300,350) from 0
    t = otio.schema.Track(name=name, kind=kind)
    t.append(clip("a.mov", 25, 100))
    t.append(otio.schema.Transition(transition_type=otio.schema.TransitionTypes.SMPTE_Dissolve,
                                    in_offset=rt(10), out_offset=rt(10)))
    t.append(clip("b.mov", 50, 100))
    t.append(otio.schema.Gap(source_range=tr(0, 100)))
    t.append(clip("c.mov", 0, 50))
    return t


def main():
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    full = otio.schema.Timeline(name="Reference")
    full.tracks.append(track(otio.schema.TrackKind.Video, "V1"))
    full.tracks.append(track(otio.schema.TrackKind.Audio, "A1"))
    full.tracks.markers.append(otio.schema.Marker(name="M1", marked_range=tr(120, 0)))
    otio.adapters.write_to_file(full, str(out / "ref.otio"))
    otio.adapters.write_to_file(full, str(out / "ref.xml"), adapter_name="fcp_xml")
    video = otio.schema.Timeline(name="Reference")
    video.tracks.append(track(otio.schema.TrackKind.Video, "V1"))
    otio.adapters.write_to_file(video, str(out / "ref.edl"), adapter_name="cmx_3600")
    for name in ("ref.otio", "ref.xml", "ref.edl"):
        print(name, (out / name).stat().st_size, "bytes")
    print((out / "ref.edl").read_text())


if __name__ == "__main__":
    main()
