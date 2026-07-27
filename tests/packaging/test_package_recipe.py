"""The AUR recipe: one signed, source-built package with a hard Omarchy 4 dependency."""

from __future__ import annotations

import json
import shutil
import subprocess
import unittest

from installed import REPOSITORY_ROOT

PKGBUILD = REPOSITORY_ROOT / "packaging" / "PKGBUILD"

# Reading the recipe's own variables, rather than grepping its text, keeps the
# journey honest about what makepkg would see. Every array is passed whole, so
# an assertion never sees just its first element.
_ARRAYS = (
    "arch",
    "depends",
    "makedepends",
    "optdepends",
    "source",
    "validpgpkeys",
    "sha256sums",
)

RECIPE_AS_JSON = r"""
source "$1"
python3 - "$pkgname" "$pkgver" "$install" <<'PY' \
  "${arch[@]}" --- "${depends[@]}" --- "${makedepends[@]}" --- \
  "${optdepends[@]}" --- "${source[@]}" --- "${validpgpkeys[@]}" --- \
  "${sha256sums[@]}"
import json, sys
names = %r
head, rest = sys.argv[1:4], sys.argv[4:]
groups, current = [], []
for item in rest:
    if item == "---":
        groups.append(current)
        current = []
    else:
        current.append(item)
groups.append(current)
recipe = {"pkgname": head[0], "pkgver": head[1], "install": head[2]}
recipe.update(dict(zip(names, groups)))
print(json.dumps(recipe))
PY
""" % (_ARRAYS,)


def recipe() -> dict:
    result = subprocess.run(
        ["bash", "-c", RECIPE_AS_JSON, "bash", str(PKGBUILD)],
        capture_output=True,
        text=True,
        check=True,
    )
    return json.loads(result.stdout)


def printsrcinfo(check: bool = True) -> subprocess.CompletedProcess[str]:
    """What makepkg makes of the recipe, or a skip if makepkg is absent."""
    if shutil.which("makepkg") is None:
        raise unittest.SkipTest("makepkg is not installed")
    return subprocess.run(
        ["makepkg", "--printsrcinfo", "-p", str(PKGBUILD)],
        cwd=PKGBUILD.parent,
        capture_output=True,
        text=True,
        check=check,
    )


class PackageRecipe(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        if not PKGBUILD.is_file():
            raise unittest.SkipTest(f"no PKGBUILD at {PKGBUILD}")
        cls.recipe = recipe()
        cls.text = PKGBUILD.read_text(encoding="utf-8")

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

    def test_the_released_tarball_has_its_digest_pinned(self) -> None:
        """A signature proves who built the tarball; the digest pins which one."""
        sums = dict(zip(self.recipe["source"], self.recipe["sha256sums"]))
        self.assertEqual(len(sums), len(self.recipe["source"]), "a source has no sha256sum")
        for item, digest in sums.items():
            if item.endswith((".sig", ".asc")):
                # A detached signature carries no digest of its own.
                self.assertEqual(digest, "SKIP", f"{item} should not be digested")
            else:
                self.assertRegex(
                    digest, r"^[0-9a-f]{64}$", f"{item} has no pinned sha256 digest"
                )

    def test_the_package_depends_hard_on_omarchy(self) -> None:
        depends = self.recipe["depends"]
        omarchy = [item for item in depends if item.startswith("omarchy")]
        self.assertTrue(omarchy, f"omarchy is not a dependency: {depends}")
        self.assertTrue(
            any(">=4" in item for item in omarchy),
            f"the Omarchy 4/Quattro floor is not pinned: {omarchy}",
        )
        # Hard dependency, so it may not also be offered as an optional one.
        self.assertNotIn(
            "omarchy",
            " ".join(self.recipe["optdepends"]),
            "omarchy is also listed as an optdepend",
        )

    def test_the_package_carries_no_install_scriptlet(self) -> None:
        """No Omarchy hooks or launcher refresh for ordinary operation.

        What the package actually puts on disk is asserted by the installed
        footprint journey; this only rules out the scriptlet makepkg would run.
        """
        self.assertEqual(self.recipe["install"], "")

    def test_the_recipe_passes_makepkg_parsing(self) -> None:
        result = printsrcinfo(check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("pkgname = omachess", result.stdout)

    def test_the_recipe_and_its_srcinfo_agree(self) -> None:
        srcinfo = PKGBUILD.parent / ".SRCINFO"
        self.assertTrue(srcinfo.is_file(), "the AUR package needs a committed .SRCINFO")
        result = printsrcinfo()
        self.assertEqual(
            srcinfo.read_text(encoding="utf-8").strip(),
            result.stdout.strip(),
            "regenerate packaging/.SRCINFO with makepkg --printsrcinfo",
        )


if __name__ == "__main__":
    unittest.main()
