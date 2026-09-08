#!/usr/bin/env python3
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from test_toolchain import BUILD_TIMEOUT, PPC, ToolchainTestCase


class RuntimeStdlibTests(ToolchainTestCase):
    PROGRAM = r'''
        bring std.fs
        bring std.system
        bring std.io
        bring std.time
        bring std.text

        launch:
            fs_write("roundtrip.txt", "runtime-ok")
            say fs_exists("roundtrip.txt")
            say fs_exists("definitely-missing-punpun-file")
            say fs_read("roundtrip.txt")
            say system_platform()
            say contains(system_current_dir(), "punpun-runtime-")
            say system_env_has("PATH")
            say system_env_or("PUNPUN_TEST_VARIABLE_THAT_MUST_NOT_EXIST_91C5", "fallback")
            say io_read_line()
            say fs_make_dir("nested")
            let nested_path = fs_join("nested", "renamed.txt")
            fs_write(nested_path, "move-me")
            say fs_rename(nested_path, fs_join("nested", "final.txt"))
            say fs_remove(fs_join("nested", "final.txt"))
            say text_is_utf8("Pun🐧")
            say text_codepoints("Pun🐧")
            delay_ms(0)
        done
    '''

    def _exercise(self, extra=()):
        directory = self.project(prefix="punpun-runtime-")
        self.write(directory, "prog.pp", self.PROGRAM)
        result = self.run_command(
            [PPC, "go", "prog.pp", *extra],
            cwd=directory,
            timeout=BUILD_TIMEOUT,
            stdin="stdin works\n",
        )
        self.assertOk(result)
        self.assertEqual(
            result.stdout.splitlines(),
            ["yes", "no", "runtime-ok", "linux", "yes", "yes", "fallback", "stdin works",
             "yes", "yes", "yes", "yes", "4"],
        )

    def test_runtime_stdlib_direct_x86_backend(self):
        self._exercise()

    def test_runtime_stdlib_c_backend_parity(self):
        self._exercise(("--cc-backend",))

    ASYNC_PROGRAM = r'''
        bring std::async;
        launch {
            let left = delayed_i64(20, 25);
            let right = delayed_i64(22, 25);
            let text_task = delayed_text("task-ok", 1);
            say(await left + await right);
            say(await text_task);
        }
    '''

    def _exercise_async_stdlib(self, extra=()):
        directory = self.project(prefix="punpun-async-stdlib-")
        self.write(directory, "prog.pp", self.ASYNC_PROGRAM)
        result = self.run_command([PPC, "go", "prog.pp", *extra], cwd=directory, timeout=BUILD_TIMEOUT)
        self.assertOk(result)
        self.assertEqual(result.stdout.splitlines(), ["42", "task-ok"])

    def test_async_stdlib_direct_and_c_backend(self):
        self._exercise_async_stdlib()
        self._exercise_async_stdlib(("--cc-backend",))
