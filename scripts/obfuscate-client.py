"""Writes the published clients file the app fetches at run time.

The Google client comes from GOOGLE_CLIENT_ID and GOOGLE_CLIENT_SECRET; the values are
obfuscated with the key in source/auth/ClientCredentials.cpp, which Deobfuscate() reverses. This
hides them from leaked-credential scanners only: the key ships with the app.

    GOOGLE_CLIENT_ID=... GOOGLE_CLIENT_SECRET=... python3 scripts/obfuscate-client.py > clients.json
"""

import json
import os
import pathlib
import re
import sys

SOURCE = (
    pathlib.Path(__file__).resolve().parent.parent / "source" / "auth" / "ClientCredentials.cpp"
)


def obfuscation_key():
    """The key as the app holds it, read from its source so the two never differ."""
    match = re.search(r'kObfuscationKey\[\] = "([0-9a-f]+)";', SOURCE.read_text(encoding="utf-8"))
    if not match:
        sys.exit(f"No kObfuscationKey in {SOURCE}")
    return match.group(1).encode("ascii")


def obfuscate(text, key):
    """text XORed with key, as hex."""
    data = text.encode("utf-8")
    return bytes(byte ^ key[i % len(key)] for i, byte in enumerate(data)).hex()


def main():
    client_id = os.environ.get("GOOGLE_CLIENT_ID", "")
    client_secret = os.environ.get("GOOGLE_CLIENT_SECRET", "")
    if not client_id or not client_secret:
        sys.exit("Set GOOGLE_CLIENT_ID and GOOGLE_CLIENT_SECRET.")
    key = obfuscation_key()
    clients = {
        "google": {
            "client_id": obfuscate(client_id, key),
            "client_secret": obfuscate(client_secret, key),
        }
    }
    json.dump(clients, sys.stdout, indent=2)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
