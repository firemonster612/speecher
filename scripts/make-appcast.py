#!/usr/bin/env python3
"""Render a one-item Sparkle appcast from release artifacts."""

import argparse
from email.utils import formatdate
from html import escape
from pathlib import Path
import xml.etree.ElementTree as ET


SPARKLE_NAMESPACE = "http://www.andymatuschak.org/xml-namespaces/sparkle"
ET.register_namespace("sparkle", SPARKLE_NAMESPACE)


def signature_attributes(path: Path | None) -> dict[str, str]:
    if path is None or not path.exists():
        return {}
    values = {}
    for line in path.read_text().splitlines():
        key, separator, value = line.partition("=")
        if separator:
            values[key] = value
    if "length" not in values:
        raise ValueError(f"Malformed signature file {path}: missing length")
    attributes = {"length": values["length"]}
    if "sparkle:edSignature" in values:
        attributes[f"{{{SPARKLE_NAMESPACE}}}edSignature"] = values["sparkle:edSignature"]
    return attributes


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--channel", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--build-number", required=True)
    parser.add_argument("--pub-date", default=formatdate(usegmt=True))
    parser.add_argument("--dmg-url", required=True)
    parser.add_argument("--signature", type=Path)
    parser.add_argument("--notes-file", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    rss = ET.Element("rss", {"version": "2.0"})
    channel = ET.SubElement(rss, "channel")
    ET.SubElement(channel, "title").text = f"Speecher {args.channel} updates"
    ET.SubElement(channel, "link").text = "https://github.com/firemonster612/speecher/releases"
    ET.SubElement(channel, "description").text = f"Speecher {args.channel} updates"
    item = ET.SubElement(channel, "item")
    ET.SubElement(item, "title").text = args.version
    ET.SubElement(item, "pubDate").text = args.pub_date
    ET.SubElement(item, f"{{{SPARKLE_NAMESPACE}}}version").text = args.build_number
    ET.SubElement(item, f"{{{SPARKLE_NAMESPACE}}}shortVersionString").text = args.version
    description_marker = "SPEECHER_RELEASE_NOTES"
    if args.notes_file is not None:
        ET.SubElement(item, "description").text = description_marker
    enclosure = {
        "url": args.dmg_url,
        "length": "0",
        "type": "application/x-apple-diskimage",
    }
    enclosure.update(signature_attributes(args.signature))
    ET.SubElement(item, "enclosure", enclosure)

    xml = ET.tostring(rss, encoding="utf-8", xml_declaration=True).decode()
    if args.notes_file is not None:
        notes = escape(args.notes_file.read_text(encoding="utf-8"), quote=False)
        notes = notes.replace("]]>", "]]&gt;")
        xml = xml.replace(
            f"<description>{description_marker}</description>",
            f"<description><![CDATA[<pre>{notes}</pre>]]></description>",
        )
    args.output.write_text(xml, encoding="utf-8")


if __name__ == "__main__":
    main()
