#!/usr/bin/env python3
import argparse
import xml.etree.ElementTree as ET
from defusedxml.ElementTree import parse
from datetime import datetime, timezone


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
    parser.add_argument("version", help="Version string (e.g. v2.6.4.b89355f)")
    parser.add_argument("--date", help="Release date (YYYY-MM-DD), defaults to today",
                        default=datetime.now(timezone.utc).date().isoformat())

    args = parser.parse_args()

    tree = parse(args.file)
    root = tree.getroot()

    releases = root.find('releases')
    if releases is None:
        raise RuntimeError("<releases> element not found in XML.")

    existing_versions = [release.get('version')
                         for release in releases.findall('release')]
    if args.version in existing_versions:
        print(
            f"Version {args.version} is already present, skipping insertion.")
        return

    new_release = ET.Element('release', {
        'version': args.version,
        'date': args.date
    })
    url = ET.SubElement(new_release, 'url', {'type': 'details'})
    url.text = "https://github.com/meshtastic/firmware/releases"

    releases.insert(0, new_release)

    indent(releases, level=1)
    releases.tail = "\n"

    tree.write(args.file, encoding='UTF-8', xml_declaration=True)
    print(f"Inserted new release: {args.version}")

    with open(args.file, 'r+', encoding='UTF-8') as file:
        content = file.read()
        content = content.replace("<?xml version='1.0'", '<?xml version="1.0"')
        content = content.replace("encoding='UTF-8'", 'encoding="UTF-8"')
        file.seek(0)
        file.write(content)
        file.truncate()


if __name__ == "__main__":
    main()
