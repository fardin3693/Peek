"""Contract tests for the thin Nautilus Script adapter; no desktop required."""

import contextlib
import html
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock


SCRIPT_PATH = (
    Path(__file__).parents[1] / "integrations" / "nautilus" / "peek-nautilus.py"
)
SPEC = importlib.util.spec_from_file_location("peek_nautilus_under_test", SCRIPT_PATH)
ADAPTER = importlib.util.module_from_spec(SPEC)
# Direct invocation, as well as CTest's -B invocation, must leave the source clean.
with mock.patch.object(sys, "dont_write_bytecode", True):
    SPEC.loader.exec_module(ADAPTER)


class UriTests(unittest.TestCase):
    def test_selection_error_is_a_value_error(self):
        self.assertTrue(issubclass(ADAPTER.SelectionError, ValueError))

    def test_local_file_uris_preserve_path_spelling(self):
        cases = (
            ("file:///tmp/plain.txt", "/tmp/plain.txt"),
            ("file:/tmp/plain.txt", "/tmp/plain.txt"),
            ("file://localhost/tmp/plain.txt", "/tmp/plain.txt"),
            ("file://LoCaLhOsT/tmp/plain.txt", "/tmp/plain.txt"),
            ("file:///", "/"),
            ("file:///tmp/with%20spaces", "/tmp/with spaces"),
            ("file:///tmp/caf%C3%A9%20%E6%97%A5%E6%9C%AC%20%F0%9F%98%80", "/tmp/café 日本 😀"),
            ("file:///tmp/%27%22%3B%24%28%29%60%26%3C%3E", "/tmp/'\";$()`&<>"),
            ("file:///tmp/line%0Abreak%09tab", "/tmp/line\nbreak\ttab"),
            ("file:///tmp/percent%25+plus%2B", "/tmp/percent%+plus+"),
            ("file:///tmp/question%3Fhash%23", "/tmp/question?hash#"),
            ("file:///tmp/link/.././leaf//name", "/tmp/link/.././leaf//name"),
        )
        for uri, expected in cases:
            with self.subTest(uri=uri):
                self.assertEqual(ADAPTER.uri_to_path(uri), expected)

    def test_percent_decoding_happens_exactly_once(self):
        for encoded, decoded in (
            ("%2520", "%20"),
            ("%250A", "%0A"),
            ("%2500", "%00"),
            ("%25ZZ", "%ZZ"),
            ("%252F", "%2F"),
        ):
            with self.subTest(encoded=encoded):
                self.assertEqual(ADAPTER.uri_to_path("file:///tmp/" + encoded), "/tmp/" + decoded)

    def test_nonlocal_or_nonabsolute_uris_are_rejected(self):
        cases = (
            "https://localhost/tmp/file",
            "smb://localhost/tmp/file",
            "trash:///tmp/file",
            "/tmp/file",
            "relative-file",
            "file://remote/tmp/file",
            "file://127.0.0.1/tmp/file",
            "file://localhost:80/tmp/file",
            "file://user@localhost/tmp/file",
            "file://localhost./tmp/file",
            "file:",
            "file://",
            "file://localhost",
            "file:relative",
        )
        for uri in cases:
            with self.subTest(uri=uri):
                with self.assertRaises(ADAPTER.SelectionError):
                    ADAPTER.uri_to_path(uri)

    def test_query_and_fragment_delimiters_are_rejected_even_when_empty(self):
        for suffix in ("?", "#", "?query", "#fragment", "?#", "?query#fragment"):
            with self.subTest(suffix=suffix):
                with self.assertRaises(ADAPTER.SelectionError):
                    ADAPTER.uri_to_path("file:///tmp/file" + suffix)

    def test_raw_ascii_whitespace_and_controls_are_rejected_not_stripped(self):
        for codepoint in (*range(33), 127):
            character = chr(codepoint)
            for uri in (
                character + "file:///tmp/file",
                "file:///tmp/a" + character + "b",
                "file:///tmp/file" + character,
            ):
                with self.subTest(codepoint=codepoint, uri=uri):
                    with self.assertRaises(ADAPTER.SelectionError):
                        ADAPTER.uri_to_path(uri)

    def test_invalid_percent_escapes_and_utf8_are_rejected(self):
        for encoded in (
            "%", "%0", "%GG", "%0G", "%G0", "%20%", "%ff", "%80",
            "%C0%AF", "%E2%28%A1", "%E2%82", "%ED%A0%80", "%F4%90%80%80",
        ):
            with self.subTest(encoded=encoded):
                with self.assertRaises(ADAPTER.SelectionError):
                    ADAPTER.uri_to_path("file:///tmp/" + encoded)

    def test_decoded_nul_is_rejected(self):
        with self.assertRaises(ADAPTER.SelectionError):
            ADAPTER.uri_to_path("file:///tmp/before%00after")


class SelectionTests(unittest.TestCase):
    def test_missing_selection_is_an_error(self):
        for selection in (None, "", "\n"):
            with self.subTest(selection=selection):
                with self.assertRaises(ADAPTER.SelectionError):
                    ADAPTER.selected_path(selection)

    def test_one_optional_trailing_newline_and_original_order(self):
        for selection, expected in (
            ("file:///tmp/z", ("/tmp/z", 1)),
            ("file:///tmp/z\n", ("/tmp/z", 1)),
            ("file:///tmp/z\nfile:///tmp/a", ("/tmp/z", 2)),
            ("file:///tmp/z\nfile:///tmp/a\n", ("/tmp/z", 2)),
            ("file:///tmp/line%0Abreak%09tab\n", ("/tmp/line\nbreak\ttab", 1)),
        ):
            with self.subTest(selection=selection):
                self.assertEqual(ADAPTER.selected_path(selection), expected)

    def test_later_invalid_or_blank_records_are_counted_not_validated_or_filtered(self):
        for selection, count in (
            ("file:///tmp/first\nhttps://remote/invalid", 2),
            ("file:///tmp/first\nfile:///tmp/%GG\n", 2),
            ("file:///tmp/first\n\nfile:///tmp/third\n", 3),
            ("file:///tmp/first\n\n", 2),
        ):
            with self.subTest(selection=selection):
                self.assertEqual(ADAPTER.selected_path(selection), ("/tmp/first", count))

    def test_invalid_or_blank_first_record_never_skips_to_valid_later_record(self):
        for first in ("", "https://remote/invalid", "file:///tmp/%GG", "file:///tmp/raw space"):
            with self.subTest(first=first):
                with self.assertRaises(ADAPTER.SelectionError):
                    ADAPTER.selected_path(first + "\nfile:///tmp/valid\n")


class ExecutableTests(unittest.TestCase):
    def setUp(self):
        self.environment = mock.patch.dict(os.environ, {}, clear=True)
        self.environment.start()
        self.addCleanup(self.environment.stop)
        self.fixtures = tempfile.TemporaryDirectory(prefix="peek-nautilus-home-")
        self.addCleanup(self.fixtures.cleanup)
        self.home = Path(self.fixtures.name)
        home_patch = mock.patch.object(Path, "home", return_value=self.home)
        home_patch.start()
        self.addCleanup(home_patch.stop)
        which_patch = mock.patch.object(shutil, "which", return_value="/from-path/peek")
        self.which = which_patch.start()
        self.addCleanup(which_patch.stop)

    def test_absolute_override_is_literal_and_takes_precedence(self):
        for executable in ("/missing/peek", "/tmp/peek ' quote $HOME ~ --flag"):
            with self.subTest(executable=executable):
                os.environ["PEEK_EXECUTABLE"] = executable
                self.assertEqual(ADAPTER.find_executable(), executable)
                self.which.assert_not_called()

    def test_present_empty_or_relative_override_is_an_error_without_fallback(self):
        for executable in ("", "peek", "./peek", "~/bin/peek", "$HOME/bin/peek"):
            with self.subTest(executable=executable):
                os.environ["PEEK_EXECUTABLE"] = executable
                with self.assertRaises(ADAPTER.SelectionError):
                    ADAPTER.find_executable()
                self.which.assert_not_called()

    def test_home_local_bin_file_precedes_path(self):
        executable = self.home / ".local" / "bin" / "peek"
        executable.parent.mkdir(parents=True)
        executable.write_text("test-owned file\n", encoding="utf-8")
        self.assertEqual(ADAPTER.find_executable(), str(executable))
        self.which.assert_not_called()

    def test_missing_home_candidate_falls_back_to_path(self):
        self.assertEqual(ADAPTER.find_executable(), "/from-path/peek")
        self.which.assert_called_once_with("peek")

    def test_home_candidate_directory_is_not_an_executable_file(self):
        (self.home / ".local" / "bin" / "peek").mkdir(parents=True)
        self.assertEqual(ADAPTER.find_executable(), "/from-path/peek")
        self.which.assert_called_once_with("peek")

    def test_no_executable_is_a_selection_error(self):
        self.which.return_value = None
        with self.assertRaises(ADAPTER.SelectionError):
            ADAPTER.find_executable()


class MainTests(unittest.TestCase):
    def setUp(self):
        patches = (
            mock.patch.dict(os.environ, {"NAUTILUS_SCRIPT_SELECTED_URIS": "file:///tmp/first"}, clear=True),
            mock.patch.object(ADAPTER, "find_executable", return_value="/test/peek"),
            mock.patch.object(ADAPTER, "report_error"),
            mock.patch.object(subprocess, "run", return_value=subprocess.CompletedProcess([], 0, stderr="")),
        )
        started = []
        for patch in patches:
            started.append(patch.start())
            self.addCleanup(patch.stop)
        _, self.find_executable, self.report_error, self.run = started

    def assert_report_contains(self, message):
        self.report_error.assert_called_once()
        self.assertIn(message, self.report_error.call_args.args[0])

    def test_safe_argv_and_synchronous_subprocess_options(self):
        os.environ["NAUTILUS_SCRIPT_SELECTED_URIS"] = "file:///tmp/%27%3B%24%28%29%0A"
        self.assertEqual(ADAPTER.main(), 0)
        self.run.assert_called_once_with(
            ["/test/peek", "--", "/tmp/';$()\n"],
            check=False,
            stderr=subprocess.PIPE,
            text=True,
            errors="replace",
            shell=False,
        )
        self.report_error.assert_not_called()

    def test_multiple_selection_warns_and_launches_only_first_even_if_later_invalid(self):
        os.environ["NAUTILUS_SCRIPT_SELECTED_URIS"] = "file:///tmp/first\ninvalid-later\n"
        stderr = io.StringIO()
        with contextlib.redirect_stderr(stderr):
            self.assertEqual(ADAPTER.main(), 0)
        self.assertTrue(stderr.getvalue().strip())
        self.assertEqual(self.run.call_args.args[0], ["/test/peek", "--", "/tmp/first"])
        self.report_error.assert_not_called()

    def test_bad_selection_returns_two_without_resolving_or_spawning(self):
        for selection in (None, "", "\n", "\nfile:///tmp/valid", "file:///tmp/%GG\nfile:///tmp/valid"):
            with self.subTest(selection=selection):
                self.report_error.reset_mock()
                if selection is None:
                    os.environ.pop("NAUTILUS_SCRIPT_SELECTED_URIS", None)
                else:
                    os.environ["NAUTILUS_SCRIPT_SELECTED_URIS"] = selection
                self.assertEqual(ADAPTER.main(), 2)
                self.report_error.assert_called_once()
                self.find_executable.assert_not_called()
                self.run.assert_not_called()

    def test_executable_resolution_error_returns_one_without_spawning(self):
        self.find_executable.side_effect = ADAPTER.SelectionError("executable unavailable")
        self.assertEqual(ADAPTER.main(), 1)
        self.run.assert_not_called()
        self.assert_report_contains("executable unavailable")

    def test_spawn_oserror_returns_one_with_diagnostic(self):
        self.run.side_effect = OSError("test spawn denied")
        self.assertEqual(ADAPTER.main(), 1)
        self.assert_report_contains("test spawn denied")

    def test_native_failure_including_signal_returns_one_and_preserves_stderr(self):
        for returncode in (1, 2, 17, -15):
            with self.subTest(returncode=returncode):
                self.report_error.reset_mock()
                self.run.return_value = subprocess.CompletedProcess([], returncode, stderr="native <b>failure</b>\n")
                self.assertEqual(ADAPTER.main(), 1)
                self.assert_report_contains("native <b>failure</b>")

    def test_native_failure_without_stderr_still_reports_error(self):
        self.run.return_value = subprocess.CompletedProcess([], -15, stderr="")
        self.assertEqual(ADAPTER.main(), 1)
        self.report_error.assert_called_once()
        self.assertTrue(self.report_error.call_args.args[0].strip())

    def test_each_main_invocation_reads_current_environment(self):
        self.assertEqual(ADAPTER.main(), 0)
        os.environ["NAUTILUS_SCRIPT_SELECTED_URIS"] = "file:///tmp/second"
        self.assertEqual(ADAPTER.main(), 0)
        self.assertEqual(
            [call.args[0] for call in self.run.call_args_list],
            [["/test/peek", "--", "/tmp/first"], ["/test/peek", "--", "/tmp/second"]],
        )
        del os.environ["NAUTILUS_SCRIPT_SELECTED_URIS"]
        self.assertEqual(ADAPTER.main(), 2)
        self.assertEqual(self.run.call_count, 2)


class ReportingTests(unittest.TestCase):
    def test_stderr_is_plain_text_and_notification_body_is_html_escaped(self):
        message = "native <b>failure</b> & 'single' \"double\""
        stderr = io.StringIO()
        with mock.patch.object(shutil, "which", return_value="notify-send"), mock.patch.object(
            subprocess, "run", return_value=subprocess.CompletedProcess([], 0)
        ) as run, contextlib.redirect_stderr(stderr):
            ADAPTER.report_error(message)
        self.assertIn(message, stderr.getvalue())
        run.assert_called_once()
        argv = run.call_args.args[0]
        self.assertIsInstance(argv, list)
        self.assertEqual(Path(argv[0]).name, "notify-send")
        self.assertIn(html.escape(message), argv)
        self.assertEqual(run.call_args.kwargs["timeout"], 5)
        self.assertFalse(run.call_args.kwargs.get("shell", False))

    def test_notification_failures_never_replace_the_original_error(self):
        failures = (
            FileNotFoundError("notify-send missing"),
            PermissionError("notify-send denied"),
            subprocess.TimeoutExpired(["notify-send"], 5),
        )
        for failure in failures:
            with self.subTest(failure=failure):
                stderr = io.StringIO()
                with mock.patch.object(shutil, "which", return_value="notify-send"), mock.patch.object(
                    subprocess, "run", side_effect=failure
                ), contextlib.redirect_stderr(stderr):
                    ADAPTER.report_error("original error")
                self.assertIn("original error", stderr.getvalue())

    def test_nonzero_notification_status_is_ignored(self):
        stderr = io.StringIO()
        with mock.patch.object(shutil, "which", return_value="notify-send"), mock.patch.object(
            subprocess, "run", return_value=subprocess.CompletedProcess(["notify-send"], 1)
        ) as run, contextlib.redirect_stderr(stderr):
            ADAPTER.report_error("original error")
        self.assertIn("original error", stderr.getvalue())
        run.assert_called_once()
        self.assertFalse(run.call_args.kwargs["check"])

    def test_unavailable_notifier_is_not_launched(self):
        stderr = io.StringIO()
        with mock.patch.object(shutil, "which", return_value=None) as which, mock.patch.object(
            subprocess, "run"
        ) as run, contextlib.redirect_stderr(stderr):
            ADAPTER.report_error("original error")
        self.assertIn("original error", stderr.getvalue())
        which.assert_called_once_with("notify-send")
        run.assert_not_called()


class SubprocessTests(unittest.TestCase):
    def setUp(self):
        fixtures = tempfile.TemporaryDirectory(prefix="peek-nautilus-process-")
        self.addCleanup(fixtures.cleanup)
        self.root = Path(fixtures.name)
        self.record = self.root / "argv.json"
        self.executable = self.root / "peek stub ' quoted"
        self.assertTrue(Path(sys.executable).is_absolute())
        self.executable.write_text(
            "#!" + sys.executable + "\n"
            "import json, os, sys, time\n"
            "from pathlib import Path\n"
            "Path(os.environ['PEEK_STUB_RECORD']).write_text(json.dumps(sys.argv), encoding='utf-8')\n"
            "gate = os.environ.get('PEEK_STUB_GATE')\n"
            "deadline = time.monotonic() + 8\n"
            "while gate and not Path(gate).exists():\n"
            "    if time.monotonic() >= deadline:\n"
            "        sys.exit(99)\n"
            "    time.sleep(0.01)\n"
            "sys.stderr.buffer.write(os.environ.get('PEEK_STUB_STDERR', '').encode('utf-8'))\n"
            "if os.environ.get('PEEK_STUB_INVALID_UTF8'):\n"
            "    sys.stderr.buffer.write(b'\\xff')\n"
            "sys.exit(int(os.environ.get('PEEK_STUB_STATUS', '0')))\n",
            encoding="utf-8",
        )
        self.executable.chmod(0o700)

    def environment(self, selection, **updates):
        environment = os.environ.copy()
        for name in tuple(environment):
            if name.startswith("PEEK_STUB_"):
                del environment[name]
        environment.pop("NAUTILUS_SCRIPT_SELECTED_URIS", None)
        environment.update(
            PATH="",  # No real desktop notification can run.
            PYTHONDONTWRITEBYTECODE="1",
            PEEK_EXECUTABLE=str(self.executable),
            PEEK_STUB_RECORD=str(self.record),
        )
        if selection is not None:
            environment["NAUTILUS_SCRIPT_SELECTED_URIS"] = selection
        environment.update(updates)
        return environment

    def invoke(self, selection, **updates):
        self.record.unlink(missing_ok=True)
        return subprocess.run(
            [sys.executable, "-B", str(SCRIPT_PATH)],
            cwd=self.root,
            env=self.environment(selection, **updates),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=10,
            check=False,
            shell=False,
        )

    def assert_argv(self, path):
        self.assertEqual(
            json.loads(self.record.read_text(encoding="utf-8")),
            [str(self.executable), "--", str(path)],
        )

    def test_special_filename_is_one_literal_argument_and_shell_injection_is_inert(self):
        path = self.root / "-quotes ' \" ; touch injected; $(touch substituted) `touch backtick` & <b> + %20\t\n日本語.txt"
        path.touch()
        result = self.invoke(path.as_uri() + "\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assert_argv(path)
        for name in ("injected", "substituted", "backtick"):
            self.assertFalse((self.root / name).exists(), name)

    def test_directory_is_handed_off_unchanged(self):
        directory = self.root / "directory with spaces 日本語"
        directory.mkdir()
        result = self.invoke(directory.as_uri())
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assert_argv(directory)

    def test_symlink_and_dot_segments_are_not_resolved_or_normalized(self):
        (self.root / "target").mkdir()
        (self.root / "link").symlink_to(self.root / "target", target_is_directory=True)
        spelling = str(self.root) + "/link/.././missing"
        # URI conversion must not perform a filesystem existence check, either.
        result = self.invoke("file://" + spelling)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assert_argv(spelling)

    def test_multiple_selection_warns_and_ignores_invalid_later_item(self):
        path = self.root / "first"
        path.touch()
        result = self.invoke(path.as_uri() + "\ninvalid-later\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(result.stderr.strip())
        self.assert_argv(path)

    def test_each_process_uses_fresh_selection_and_override_environment(self):
        first = self.root / "first"
        second = self.root / "second"
        alternate = self.root / "alternate peek"
        shutil.copyfile(self.executable, alternate)
        alternate.chmod(0o700)
        result = self.invoke(first.as_uri())
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assert_argv(first)
        result = self.invoke(second.as_uri(), PEEK_EXECUTABLE=str(alternate))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            json.loads(self.record.read_text(encoding="utf-8")),
            [str(alternate), "--", str(second)],
        )
        result = self.invoke(None)
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertFalse(self.record.exists())

    def test_selection_and_override_errors_never_spawn_child(self):
        cases = (
            (None, {}, 2),
            ("", {}, 2),
            ("\n", {}, 2),
            ("\nfile:///tmp/valid", {}, 2),
            ("file:///tmp/%GG\nfile:///tmp/valid", {}, 2),
            ("file://remote/tmp/file", {}, 2),
            ("file:///tmp/file?", {}, 2),
            ("file:///tmp/file#", {}, 2),
            ("file:///tmp/file", {"PEEK_EXECUTABLE": "relative-peek"}, 1),
            ("file:///tmp/file", {"PEEK_EXECUTABLE": ""}, 1),
            ("file:///tmp/file", {"PEEK_EXECUTABLE": str(self.root / "missing-executable")}, 1),
        )
        for selection, environment, status in cases:
            with self.subTest(selection=selection, environment=environment):
                result = self.invoke(selection, **environment)
                self.assertEqual(result.returncode, status, result.stderr)
                self.assertTrue(result.stderr.strip())
                self.assertFalse(self.record.exists())

    def test_unlaunchable_executables_never_spawn_child(self):
        non_executable = self.root / "non-executable peek"
        shutil.copyfile(self.executable, non_executable)
        non_executable.chmod(0o600)
        directory = self.root / "directory executable"
        directory.mkdir(mode=0o700)
        invalid_format = self.root / "invalid executable format"
        # A shell fallback would create the record; exec must fail instead.
        invalid_format.write_text("printf launched > argv.json\n", encoding="utf-8")
        invalid_format.chmod(0o700)
        selected = self.root / "selected file"
        selected.touch()
        for executable in (non_executable, directory, invalid_format):
            with self.subTest(executable=executable):
                result = self.invoke(selected.as_uri(), PEEK_EXECUTABLE=str(executable))
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertIn("Cannot launch Peek at", result.stderr)
                self.assertIn(str(executable), result.stderr)
                self.assertFalse(self.record.exists(), "failed launch must not execute a child")

    def test_native_failure_and_invalid_utf8_stderr_are_reported(self):
        path = self.root / "file"
        result = self.invoke(
            path.as_uri(),
            PEEK_STUB_STATUS="23",
            PEEK_STUB_STDERR="native <b>failure</b>\n",
            PEEK_STUB_INVALID_UTF8="1",
        )
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("native <b>failure</b>", result.stderr)
        self.assertIn("\ufffd", result.stderr)
        self.assert_argv(path)

    def test_script_waits_until_child_dismissal(self):
        path = self.root / "file"
        gate = self.root / "dismiss"
        process = subprocess.Popen(
            [sys.executable, "-B", str(SCRIPT_PATH)],
            cwd=self.root,
            env=self.environment(path.as_uri(), PEEK_STUB_GATE=str(gate)),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            shell=False,
        )
        try:
            deadline = time.monotonic() + 5
            while not self.record.exists() and process.poll() is None and time.monotonic() < deadline:
                time.sleep(0.01)
            self.assertTrue(self.record.exists(), "child did not start before the deadline")
            # A fire-and-forget adapter would exit while the child is still gated.
            time.sleep(0.1)
            self.assertIsNone(process.poll(), "script exited before child dismissal")
            gate.touch()
            _, stderr = process.communicate(timeout=10)
            self.assertEqual(process.returncode, 0, stderr)
            self.assert_argv(path)
        finally:
            gate.touch()
            if process.poll() is None:
                process.kill()
            process.communicate(timeout=10)


class NativeIntegrationTests(unittest.TestCase):
    def assert_native_path_error(self, path, diagnostic):
        executable = os.environ["PEEK_TEST_EXECUTABLE"]
        self.assertTrue(Path(executable).is_absolute())
        self.assertTrue(Path(executable).is_file())
        environment = os.environ.copy()
        environment.update(
            NAUTILUS_SCRIPT_SELECTED_URIS=path.as_uri(),
            PEEK_EXECUTABLE=executable,
            PATH="",
            PYTHONDONTWRITEBYTECODE="1",
        )
        result = subprocess.run(
            [sys.executable, "-B", str(SCRIPT_PATH)],
            cwd=path.parent,
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=10,
            check=False,
            shell=False,
        )
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn(f"peek: {diagnostic}: {path}", result.stderr)

    @unittest.skipUnless(os.environ.get("PEEK_TEST_EXECUTABLE"), "PEEK_TEST_EXECUTABLE not set")
    def test_missing_path_native_diagnostic_is_propagated(self):
        with tempfile.TemporaryDirectory(prefix="peek-nautilus-native-") as directory:
            missing = Path(directory) / "missing ' 日本語.txt"
            self.assert_native_path_error(missing, "path does not exist or cannot be accessed")

    @unittest.skipUnless(os.environ.get("PEEK_TEST_EXECUTABLE"), "PEEK_TEST_EXECUTABLE not set")
    def test_fifo_native_diagnostic_is_propagated_without_opening(self):
        with tempfile.TemporaryDirectory(prefix="peek-nautilus-native-") as directory:
            fifo = Path(directory) / "named pipe"
            os.mkfifo(fifo)
            self.assert_native_path_error(fifo, "unsupported file type")

    @unittest.skipUnless(os.environ.get("PEEK_TEST_EXECUTABLE"), "PEEK_TEST_EXECUTABLE not set")
    def test_broken_symlink_native_diagnostic_is_propagated(self):
        with tempfile.TemporaryDirectory(prefix="peek-nautilus-native-") as directory:
            link = Path(directory) / "broken symlink"
            link.symlink_to(Path(directory) / "missing target")
            self.assert_native_path_error(link, "path does not exist or cannot be accessed")


if __name__ == "__main__":
    unittest.main()
