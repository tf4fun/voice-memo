"""Exercise private-file parsing without Go, network, or a real credential."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ServerLauncherTest(unittest.TestCase):
    def run_config(self, contents):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            config = root / "server.env"
            marker = root / "must-not-exist"
            if contents is not None:
                config.write_bytes(contents.replace("{marker}", str(marker)).encode())
            env = dict(os.environ, MEMO_ENV_FILE=str(config))
            result = subprocess.run(
                ["bash", str(ROOT / "scripts/run-server.sh"), "--check-env"],
                env=env, capture_output=True, text=True, check=False,
            )
            self.assertFalse(marker.exists(), "Configuration was executed as shell code")
            self.assertNotIn("private-test-value", result.stdout + result.stderr)
            return result

    def test_literal_values_and_line_endings(self):
        value = "$(touch {marker})private-test-value=="
        for content in (
            "# comment\nMEMO_ASR_API_KEY=" + value,
            'MEMO_ASR_API_KEY="' + value + '"\r\n',
            "  # comment\n\nMEMO_ASR_API_KEY='" + value + "'\n",
            "MEMO_ASR_PROVIDER=fake\nMEMO_ADDR=127.0.0.1:8080\n",
            "MEMO_MDNS_INTERFACE=en0\n",
        ):
            result = self.run_config(content)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("PASS", result.stdout)

    def test_bad_input_and_unknown_names_do_not_reveal_values(self):
        for content in (
            "private-test-value",
            "MEMO_UNKNOWN=private-test-value\n",
            "PATH=private-test-value\n",
            "export MEMO_ASR_API_KEY=private-test-value\n",
        ):
            result = self.run_config(content)
            self.assertNotEqual(result.returncode, 0)

    def test_missing_file_is_actionable(self):
        result = self.run_config(None)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(".env.example", result.stderr)


if __name__ == "__main__":
    unittest.main()
