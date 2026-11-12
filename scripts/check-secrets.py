"""Refuses files that contain something shaped like a credential.

Cloud keys reach the build from the repository's secrets only. Run on every
tracked file in CI: python3 scripts/check-secrets.py $(git ls-files)
"""

import re
import sys

PATTERNS = [
    re.compile(r"GOCSPX-[A-Za-z0-9_-]{20,}"),  # Google OAuth client secret
    re.compile(r"\bAIza[0-9A-Za-z_-]{35}"),  # Google API key
    re.compile(r"\bya29\.[0-9A-Za-z_-]{20,}"),  # Google access token
    re.compile(r"\b1//0[0-9A-Za-z_-]{30,}"),  # Google refresh token
    re.compile(r"\b[0-9]{6,}-[0-9a-z]{32}\.apps\.googleusercontent\.com"),  # Google client ID
    re.compile(r"\b[A-Za-z0-9_~.-]{3}\dQ~[A-Za-z0-9_~.-]{31,34}"),  # Microsoft client secret
    re.compile(r"\bgh[pousr]_[A-Za-z0-9]{36}\b"),  # GitHub token
    re.compile(r"-----BEGIN [A-Z ]*PRIVATE KEY-----"),
]
SKIP = re.compile(r"\.(png|jpg|ttf|pkg)$", re.IGNORECASE)


def scan(path):
    """Report each line of path that matches a pattern; return whether any did."""
    found = False
    with open(path, encoding="utf-8", errors="replace") as handle:
        for number, line in enumerate(handle, 1):
            if any(pattern.search(line) for pattern in PATTERNS):
                # Only the position: the report must not repeat the secret.
                print(f"{path}:{number}: looks like a credential")
                found = True
    return found


def main(paths):
    failed = [path for path in paths if not SKIP.search(path) and scan(path)]
    if failed:
        print("Refusing: credentials belong in the repository's secrets, never in files.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
