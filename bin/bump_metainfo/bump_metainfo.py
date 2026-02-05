#!/usr/bin/env python3
import argparse
import xml.etree.ElementTree as ET
from defusedxml.ElementTree import parse
from datetime import datetime, timezone


# Indent by 2 spaces to align with xml formatting.
def indent(elem, level=0):
    i = "\n" + level * "  "
    if len(elem):
        if not elem.text or not elem.text.strip():
            elem.text = i + "  "
        for child in elem:
            indent(child, level + 1)
        if not child.tail or not child.tail.strip():
            child.tail = i
    if level and (not elem.tail or not elem.tail.strip()):
        elem.tail = i


def main():
    parser = argparse.ArgumentParser(
        description="Prepend new release entry to metainfo.xml file.")
    parser.add_argument("--file", help="Path to the metainfo.xml file",
                        default="org.meshtastic.meshtasticd.metainfo.xml")
    parser.add_argument("version", help="Version string (e.g. 2.6.4)")
    parser.add_argument("--date", help="Release date (YYYY-MM-DD), defaults to today",
                        default=datetime.now(timezone.utc).date().isoformat())

    args = parser.parse_args()

    tree = parse(args.file)
    root = tree.getroot()

    releases = root.find('releases')
    if releases is None:
        raise RuntimeError("<releases> element not found in XML.")

    existing_versions = {
        release.get('version'): release
        for release in releases.findall('release')
    }
    existing_release = existing_versions.get(args.version)

    if existing_release is not None:
        if not existing_release.get('date'):
            print(f"Version {args.version} found without date. Adding date...")
            existing_release.set('date', args.date)
        else:
            print(
                f"Version {args.version} is already present with date, skipping insertion.")
    else:
        new_release = ET.Element('release', {
            'version': args.version,
            'date': args.date
        })
        url = ET.SubElement(new_release, 'url', {'type': 'details'})
        url.text = f"https://github.com/meshtastic/firmware/releases?q=tag%3Av{args.version}"

        releases.insert(0, new_release)

        indent(releases, level=1)
        releases.tail = "\n"

        print(f"Inserted new release: {args.version}")

    tree.write(args.file, encoding='UTF-8', xml_declaration=True)


if __name__ == "__main__":
    main()
