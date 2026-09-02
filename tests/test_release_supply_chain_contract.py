#!/usr/bin/env python3
"""Regression contracts for the release privilege and download trust boundaries."""

from __future__ import annotations

import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
WORKFLOW_PATH = ROOT / ".github" / "workflows" / "release.yml"
WORKFLOW = WORKFLOW_PATH.read_text(encoding="utf-8") if WORKFLOW_PATH.is_file() else ""
BOOTSTRAP = (ROOT / "scripts" / "bootstrap-third-party.ps1").read_text(
    encoding="utf-8"
)


def job_block(name: str, next_name: str | None = None) -> str:
    start = WORKFLOW.index(f"  {name}:\n")
    end = WORKFLOW.index(f"  {next_name}:\n", start) if next_name else len(WORKFLOW)
    return WORKFLOW[start:end]


class ReleaseSupplyChainContractTests(unittest.TestCase):
    @unittest.skipUnless(WORKFLOW_PATH.is_file(), "release workflow has not been added yet")
    def test_build_is_read_only_and_publish_is_the_only_write_job(self) -> None:
        build = job_block("build", "publish")
        publish = job_block("publish")

        self.assertRegex(WORKFLOW, r"(?m)^permissions:\n  contents: read$")
        self.assertRegex(build, r"(?m)^    permissions:\n      contents: read$")
        self.assertNotIn("contents: write", build)
        self.assertRegex(publish, r"(?m)^    permissions:\n      contents: write$")
        self.assertIn("needs: build", publish)

    @unittest.skipUnless(WORKFLOW_PATH.is_file(), "release workflow has not been added yet")
    def test_publish_consumes_artifact_without_checkout_or_build_steps(self) -> None:
        publish = job_block("publish")

        self.assertIn("actions/download-artifact@", publish)
        self.assertIn("softprops/action-gh-release@", publish)
        self.assertNotIn("actions/checkout@", publish)
        self.assertNotIn("setup-msbuild@", publish)
        self.assertNotIn("build-installer-x64.ps1", publish)
        self.assertNotIn("test-runtime-loudness.ps1", publish)
        self.assertIn("Get-FileHash", publish)

    @unittest.skipUnless(WORKFLOW_PATH.is_file(), "release workflow has not been added yet")
    def test_manual_release_does_not_checkout_a_nonexistent_tag(self) -> None:
        build = job_block("build", "publish")
        checkout_step = build.split("- name: Checkout repository", maxsplit=1)[1].split(
            "- name:", maxsplit=1
        )[0]

        self.assertNotRegex(checkout_step, r"(?m)^\s+ref:")
        self.assertNotIn("inputs.tag_name", checkout_step)
        self.assertIn("target_commitish: ${{ needs.build.outputs.commit_sha }}", WORKFLOW)

    @unittest.skipUnless(WORKFLOW_PATH.is_file(), "release workflow has not been added yet")
    def test_existing_release_tag_must_match_the_built_commit(self) -> None:
        build = job_block("build", "publish")

        self.assertIn("git rev-parse HEAD", build)
        self.assertIn("git ls-remote --tags origin $tagRef $peeledTagRef", build)
        self.assertIn('$peeledTagRef = "$tagRef^{}"', build)
        self.assertIn("not checked-out commit $sourceCommit", build)
        self.assertIn('"commit_sha=$sourceCommit"', build)
        self.assertNotIn("SOURCE_COMMIT: ${{ github.sha }}", build)

    @unittest.skipUnless(WORKFLOW_PATH.is_file(), "release workflow has not been added yet")
    def test_same_tag_release_attempts_are_serialized(self) -> None:
        self.assertIn(
            "group: release-${{ github.event_name == 'workflow_dispatch' "
            "&& inputs.tag_name || github.ref_name }}",
            WORKFLOW,
        )
        self.assertIn("cancel-in-progress: false", WORKFLOW)

    @unittest.skipUnless(WORKFLOW_PATH.is_file(), "release workflow has not been added yet")
    def test_every_action_is_pinned_to_a_full_commit(self) -> None:
        actions = re.findall(r"(?m)^\s*uses:\s*([^\s#]+)", WORKFLOW)
        self.assertGreaterEqual(len(actions), 5)
        for action in actions:
            with self.subTest(action=action):
                self.assertRegex(action, r"^[^@]+@[0-9a-f]{40}$")

    def test_nsis_archive_is_verified_before_extraction(self) -> None:
        expected = "C7D27F780DDB6CFFB4730138CD1591E841F4B7EDB155856901CDF5F214394FA1"
        self.assertIn(f'[string] $NsisSha256 = "{expected}"', BOOTSTRAP)
        self.assertIn("Get-FileHash -LiteralPath $downloadZip -Algorithm SHA256", BOOTSTRAP)
        self.assertIn("[StringComparer]::OrdinalIgnoreCase.Equals", BOOTSTRAP)
        self.assertIn('curl.exe -L --fail --retry 3 --proto "=https"', BOOTSTRAP)
        self.assertIn('--proto-redir "=https"', BOOTSTRAP)
        self.assertLess(
            BOOTSTRAP.index("Get-FileHash -LiteralPath $downloadZip"),
            BOOTSTRAP.index("Expand-Archive -LiteralPath $zip"),
        )

    def test_aqtinstall_top_level_wheel_is_content_pinned(self) -> None:
        expected = "E88DBD87226F276FDD5D05347A44578D390D93A7F176E9476FBDA0A7C9635F69"
        self.assertIn(f'[string] $AqtInstallSha256 = "{expected}"', BOOTSTRAP)
        self.assertIn("--no-deps --only-binary=:all:", BOOTSTRAP)
        self.assertIn("Get-FileHash -LiteralPath $aqtWheel -Algorithm SHA256", BOOTSTRAP)
        self.assertIn("$actualAqtSha256, $AqtInstallSha256", BOOTSTRAP)
        self.assertIn("transitive Python", BOOTSTRAP)
        self.assertIn("not content-locked", BOOTSTRAP)


if __name__ == "__main__":
    unittest.main()
