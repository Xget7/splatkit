#!/usr/bin/env python3
"""Turn photos of a place into a walkable world with the World Labs API.

    export WORLDLABS_API_KEY=...
    scripts/generate_world.py --out house --prompt "my living room" photos/*.jpg

Uploads the photos, generates a multi image world, polls until it is done and downloads
the 500k and full resolution SPZ files plus the collider GLB into --out.
Photos are assigned azimuths evenly around the circle in the order given, so shoot them
turning in one direction. Only the standard library is used.
"""

import argparse
import json
import mimetypes
import os
import sys
import time
import urllib.request

BASE = "https://api.worldlabs.ai"


def request(key, method, path, body=None, headers=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(BASE + path, data=data, method=method)
    req.add_header("WLT-Api-Key", key)
    if body is not None:
        req.add_header("Content-Type", "application/json")
    for name, value in (headers or {}).items():
        req.add_header(name, value)
    with urllib.request.urlopen(req) as response:
        return json.loads(response.read() or b"{}")


def upload(key, path):
    name = os.path.basename(path)
    extension = name.rsplit(".", 1)[-1].lower()
    prepared = request(key, "POST", "/marble/v1/media-assets:prepare_upload",
                       {"file_name": name, "kind": "image", "extension": extension})
    info = prepared["upload_info"]
    headers = dict(info.get("required_headers") or {})
    headers.setdefault("Content-Type", mimetypes.guess_type(name)[0] or "application/octet-stream")
    with open(path, "rb") as f:
        req = urllib.request.Request(info["upload_url"], data=f.read(), method=info.get("upload_method", "PUT"))
        for k, v in headers.items():
            req.add_header(k, v)
        urllib.request.urlopen(req).read()
    return prepared["media_asset"]["id"]


def find(obj, key):
    """First value for `key` anywhere in a nested JSON object."""
    if isinstance(obj, dict):
        if key in obj:
            return obj[key]
        for value in obj.values():
            found = find(value, key)
            if found is not None:
                return found
    if isinstance(obj, list):
        for value in obj:
            found = find(value, key)
            if found is not None:
                return found
    return None


def download(url, path):
    with urllib.request.urlopen(url) as response, open(path, "wb") as out:
        while chunk := response.read(1 << 20):
            out.write(chunk)
    print(f"  {path}: {os.path.getsize(path) / 1e6:.1f} MB")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("photos", nargs="+")
    parser.add_argument("--out", required=True, help="output directory")
    parser.add_argument("--prompt", default="", help="text that describes the place")
    parser.add_argument("--name", default="react-native-splat world")
    args = parser.parse_args()
    key = os.environ.get("WORLDLABS_API_KEY")
    if not key:
        sys.exit("set WORLDLABS_API_KEY")
    os.makedirs(args.out, exist_ok=True)

    print(f"uploading {len(args.photos)} photos")
    views = []
    for i, photo in enumerate(args.photos):
        asset_id = upload(key, photo)
        azimuth = round(360.0 * i / len(args.photos))
        views.append({"azimuth": azimuth, "content": {"source": "media_asset", "media_asset_id": asset_id}})
        print(f"  {photo} -> {asset_id} at {azimuth} deg")

    body = {"display_name": args.name,
            "world_prompt": {"type": "multi-image", "multi_image_prompt": views, "text_prompt": args.prompt}}
    operation = request(key, "POST", "/marble/v1/worlds:generate", body)
    op_id = operation["operation_id"]
    print(f"generating, operation {op_id} (about five minutes)")
    while not operation.get("done"):
        time.sleep(15)
        operation = request(key, "GET", f"/marble/v1/operations/{op_id}")
        print("  waiting")
    if operation.get("error"):
        sys.exit(f"generation failed: {operation['error']}")
    with open(os.path.join(args.out, "world.json"), "w") as f:
        json.dump(operation, f, indent=2)

    spz_urls = find(operation, "spz_urls") or {}
    collider = find(operation, "collider_mesh_url")
    print("downloading")
    for tier in ("500k", "full_res"):
        if tier in spz_urls:
            download(spz_urls[tier], os.path.join(args.out, f"world_{tier}.spz"))
    if collider:
        download(collider, os.path.join(args.out, "collider.glb"))
    else:
        print("  no collider mesh in the response; the world loads in fly mode")
    print("push and walk:")
    print(f"  adb push {args.out}/world_500k.spz {args.out}/collider.glb /sdcard/Android/data/com.splatkit.devapp/files/")
    print("  adb shell am start -n com.splatkit.devapp/.MainActivity --es world world_500k.spz --es collider collider.glb")


if __name__ == "__main__":
    main()
