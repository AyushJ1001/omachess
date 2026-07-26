"""The AUR recipe: one signed, source-built package with a hard Omarchy 4 dependency."""

from __future__ import annotations

import json
import shutil
import subprocess
import unittest

from installed import REPOSITORY_ROOT, read_pkgbuild

PKGBUILD = REPOSITORY_ROOT / "packaging" / "PKGBUILD"

# Reading the recipe's own variables, rather than grepping its text, keeps the
# journey honest about what makepkg would see.
_DUMP = r"""
source "$1"
python3 - "$pkgname" "$pkgver" "$arch" "$install" <<'PY' \
  "${depends[@]}" --- "${makedepends[@]}" --- "${source[@]}" --- "${validpgpkeys[@]}"
import json, sys
head, rest = sys.argv[1:5], sys.argv[5:]
groups, current = [], []
for item in rest:
    if item == "---":
        groups.append(current)
        current = []
    else:
        current.append(item)
groups.append(current)
print(json.dumps({
    "pkgname": head[0], "pkgver": head[1], "arch": head[2], "install": head[3],
    "depends": groups[0], "makedepends": groups[1],
    "source": groups[2], "validpgpkeys": groups[3],
}))
PY
"""


def recipe() -> dict:
    result = subprocess.run(
        ["bash", "-c", _DUMP, "bash", str(PKGBUILD)],
        capture_output=True,
        text=True,
        check=True,
    )
    return json.loads(result.stdout)


class PackageRecipe(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        if not PKGBUILD.is_file():
            raise unittest.SkipTest(f"no PKGBUILD at {PKGBUILD}")
        cls.recipe = recipe()
        cls.text = read_pkgbuild()

    def test_one_package_named_omachess_builds_from_this_repository(self) -> None:
        self.assertEqual(self.recipe["pkgname"], "omachess")
        self.assertEqual(self.recipe["pkgver"], "0.1.0")
        self.assertIn("github.com/AyushJ1001/omachess", " ".join(self.recipe["source"]))
        # Source-built, not a repackaged binary: the recipe compiles the tree.
        self.assertIn("cmake", self.text)
        self.assertIn("cargo", " ".join(self.recipe["makedepends"]))

    def test_the_source_tarball_is_signed_and_the_signing_key_is_pinned(self) -> None:
        self.assertTrue(
            any(item.endswith(".sig") or item.endswith(".asc") for item in self.recipe["source"]),
            f"no detached signature in source=({self.recipe['source']})",
        )
        self.assertTrue(self.recipe["validpgpkeys"], "no validpgpkeys entry pins the signing key")
        for key in self.recipe["validpgpkeys"]:
            self.assertRegex(key, r"^[0-9A-F]{40}$", "validpgpkeys wants a full fingerprint")

    def test_the_package_depends_hard_on_omarchy(self) -> None:
        depends = self.recipe["depends"]
        omarchy = [item for item in depends if item.startswith("omarchy")]
        self.assertTrue(omarchy, f"omarchy is not a dependency: {depends}")
        self.assertTrue(
            any(">=4" in item for item in omarchy),
            f"the Omarchy 4/Quattro floor is not pinned: {omarchy}",
        )
        # Hard dependency, so it may not also appear as optional.
        self.assertNotIn("optdepends", self.text.split("package()")[0].split("depends=")[0])

    def test_the_package_carries_no_install_scriptlet(self) -> None:
        """No Omarchy hooks or launcher refresh for ordinary operation."""
        self.assertEqual(self.recipe["install"], "")
        instructions = [
            line for line in self.text.lower().splitlines() if not line.strip().startswith("#")
        ]
        for word in ("hypr", "omarchy-", "hook"):
            self.assertFalse(
                [line for line in instructions if word in line],
                f"the recipe acts on {word!r}",
            )

    def test_the_recipe_passes_makepkg_parsing(self) -> None:
        if shutil.which("makepkg") is None:
            self.skipTest("makepkg is not installed")
        result = subprocess.run(
            ["makepkg", "--printsrcinfo", "-p", str(PKGBUILD)],
            cwd=PKGBUILD.parent,
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("pkgname = omachess", result.stdout)

    def test_the_recipe_and_its_srcinfo_agree(self) -> None:
        srcinfo = PKGBUILD.parent / ".SRCINFO"
        self.assertTrue(srcinfo.is_file(), "the AUR package needs a committed .SRCINFO")
        if shutil.which("makepkg") is None:
            self.skipTest("makepkg is not installed")
        result = subprocess.run(
            ["makepkg", "--printsrcinfo", "-p", str(PKGBUILD)],
            cwd=PKGBUILD.parent,
            capture_output=True,
            text=True,
            check=True,
        )
        self.assertEqual(
            srcinfo.read_text(encoding="utf-8").strip(),
            result.stdout.strip(),
            "regenerate packaging/.SRCINFO with makepkg --printsrcinfo",
        )


if __name__ == "__main__":
    unittest.main()
