#!/usr/bin/env python3
"""Regression tests for contract diagnostics and non-mutating tool imports."""
from __future__ import annotations

import importlib.util
import io
import pathlib
import re
import runpy
import shutil
import subprocess
import tempfile
import unittest
from contextlib import redirect_stdout
from unittest.mock import patch

import check_cli_contract as cli
import check_core_timing_guard as timing
import check_idf_example_contract as idf
from contract_common import check_argument_contract, function_body, function_definition, strip_non_code

ROOT = pathlib.Path(__file__).resolve().parents[1]


class ContractToolTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("g++"), "g++ is required for native IDF console behavior tests")
    def test_native_idf_console_and_parsers(self) -> None:
        source = (ROOT / "examples/espidf_basic/main/main.cpp").read_text(encoding="utf-8")
        line_length = re.search(r"constexpr size_t LINE_LEN\s*=\s*\d+U?;", source)
        console_state = re.search(r"struct ConsoleInput\s*\{.*?\n\};", source, re.DOTALL)
        self.assertIsNotNone(line_length, "missing IDF line length constant")
        self.assertIsNotNone(console_state, "missing IDF console input state")
        functions = ("trim", "mapI2c", "parseU32Bounded", "parseFloatArg", "parseFloatRangeArg",
                     "parseBoolArg", "parseResolutionText", "parseResistance", "parseTerminalMode")
        extracted = [line_length.group()]
        extracted.extend(function_definition(source, name, "ESP-IDF CLI") for name in functions)
        extracted.extend((console_state.group(), function_definition(source, "pollConsole", "ESP-IDF CLI")))
        with tempfile.TemporaryDirectory(prefix="mcp45hvx1-idf-console-") as tmp:
            workspace = pathlib.Path(tmp)
            (workspace / "idf_console_under_test.h").write_text("\n".join(extracted), encoding="utf-8")
            executable = workspace / "idf-console-test.exe"
            subprocess.run([
                "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Iinclude", f"-I{workspace}",
                "tests/test_idf_console.cpp", "-o", str(executable),
            ], cwd=ROOT, check=True, timeout=60)
            subprocess.run([str(executable)], cwd=ROOT, check=True, timeout=10)

    def test_literals_and_comments_cannot_hide_framework_calls(self) -> None:
        sources = (
            'const char* url = "https://x/y"; millis();',
            'const char* pattern = "/*"; Serial.println(1); const char* end = "*/";',
            '// a comment with an unmatched " quote\nmicros();',
            '/* a comment with an unmatched " quote */ delay(1);',
            'auto text = R"tag(" // /* Serial.println(1);)tag"; yield();',
            "auto a = 1'000; delayMicroseconds(1); auto b = 2'000;",
        )
        calls = ("millis", "Serial", "micros", "delay", "yield", "delayMicroseconds")
        for source, call in zip(sources, calls):
            with self.subTest(call=call):
                code = strip_non_code(source)
                self.assertIn(call, code)
                self.assertEqual(len(source), len(code))
                with tempfile.TemporaryDirectory() as tmp:
                    root = pathlib.Path(tmp)
                    (root / "src").mkdir()
                    (root / "src" / "fault.cpp").write_text(source, encoding="utf-8")
                    output = io.StringIO()
                    with patch.object(timing, "ROOT", root), redirect_stdout(output):
                        self.assertEqual(timing.main(), 1)
                    self.assertIn(call, output.getvalue())

    def test_documented_framework_names_and_split_tokens_are_ignored(self) -> None:
        code = strip_non_code('/* Serial millis() */ "micros()"; R"(Wire // delay())";')
        for token in ("Serial", "millis", "micros", "Wire", "delay"):
            self.assertNotIn(token, code)
        self.assertNotIn("millis", strip_non_code("mi/**/llis()"))

    def test_missing_functions_have_an_explicit_diagnostic(self) -> None:
        for name in ("handleGeneralCall", "handleCommand"):
            with self.subTest(name=name), redirect_stdout(io.StringIO()) as output:
                with self.assertRaises(SystemExit) as failure:
                    function_body(f"void renamed() {{}}\n{name}();", name, "Example")
                self.assertEqual(failure.exception.code, 1)
                self.assertIn(f"missing {name}() definition", output.getvalue())

    def test_function_body_does_not_absorb_later_handlers(self) -> None:
        source = 'void first() { puts("}"); /* } */ if (true) { ok(); } }\nvoid second() { bad(); }'
        body = function_body(source, "first", "Example")
        self.assertIn("ok()", body)
        self.assertNotIn("bad()", body)

    def test_cli_propagates_nonzero_idf_contract_result(self) -> None:
        with patch.object(idf, "main", return_value=7), redirect_stdout(io.StringIO()) as output:
            self.assertEqual(cli.main(), 7)
        self.assertNotIn("CLI contract PASSED", output.getvalue())

    def test_argument_guards_and_mode_aliases_are_enforced_in_both_examples(self) -> None:
        for path, variable, mode_parser in (
            ("examples/01_basic_bringup_cli/main.cpp", "command", "parseMode"),
            ("examples/espidf_basic/main/main.cpp", "cmd", "parseTerminalMode"),
        ):
            source = (ROOT / path).read_text(encoding="utf-8")
            check_argument_contract(source, path, variable, mode_parser)
            for before, after, diagnostic in (
                ("requireNoArgs(", "removedGuard(", "requireNoArgs"),
                ('"rheostat_bw"', '"changed_alias"', "terminal-mode aliases differ"),
                ("void handleCommand(", "void renamedCommand(", "missing handleCommand() definition"),
            ):
                with self.subTest(path=path, mutation=before), redirect_stdout(io.StringIO()) as output:
                    with self.assertRaises(SystemExit):
                        check_argument_contract(source.replace(before, after), path, variable, mode_parser)
                    self.assertIn(diagnostic, output.getvalue())


class VersionScriptTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        for relative in ("scripts/generate_version.py", "library.json", "Doxyfile",
                         "idf_component.yml", "include/MCP45HVX1/Version.h"):
            destination = self.root / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / relative, destination)
        self.script = self.root / "scripts/generate_version.py"
        self.header = self.root / "include/MCP45HVX1/Version.h"
        self.header.write_text("deliberately stale\n", encoding="utf-8")

    def contents(self) -> dict[str, bytes]:
        return {path.relative_to(self.root).as_posix(): path.read_bytes()
                for path in self.root.rglob("*") if path.is_file() and "__pycache__" not in path.parts}

    def test_import_and_runpy_leave_stale_generated_files_untouched(self) -> None:
        before = self.contents()
        runpy.run_path(str(self.script))
        self.assertEqual(self.contents(), before)
        spec = importlib.util.spec_from_file_location("version_import_test", self.script)
        self.assertIsNotNone(spec)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        self.assertEqual(self.contents(), before)

    def test_check_is_read_only_and_sync_preserves_manifest_newline(self) -> None:
        manifest = self.root / "idf_component.yml"
        manifest.write_text("description: Keep me\nversion: 0.0.0\n", encoding="utf-8")
        module = runpy.run_path(str(self.script))
        before = self.contents()
        with redirect_stdout(io.StringIO()):
            self.assertEqual(module["main"](["check"]), 1)
            self.assertEqual(self.contents(), before)
            self.assertEqual(module["main"](["sync"]), 0)
            self.assertEqual(module["main"](["check"]), 0)
        self.assertTrue(manifest.read_bytes().endswith(b"\n"))
        self.assertTrue(manifest.read_text(encoding="utf-8").startswith("description: Keep me\nversion: \""))

    def test_scons_loading_still_syncs_and_injects_build_metadata(self) -> None:
        class Environment(dict):
            def Append(self, **values: object) -> None:
                self.update(values)

        env = Environment(PROJECT_DIR=str(self.root))
        with redirect_stdout(io.StringIO()):
            runpy.run_path(str(self.script), init_globals={"Import": lambda _: None, "env": env})
        self.assertIn("AUTO-GENERATED", self.header.read_text(encoding="utf-8"))
        self.assertEqual(len(env["CPPDEFINES"]), 5)


if __name__ == "__main__":
    unittest.main()
