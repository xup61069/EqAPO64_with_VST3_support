#!/usr/bin/env python3
"""Source contracts for the out-of-process VST cold-start lifecycle."""

from __future__ import annotations

import ctypes
import os
import pathlib
import struct
import subprocess
import tempfile
import unittest
from ctypes import wintypes


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


class OutProcVSTLifecycleTests(unittest.TestCase):
    def test_named_runtime_cold_starts_on_a_monitor_thread(self) -> None:
        runtime = read("filters/OutProcVSTPluginFilter.cpp")
        process_body = runtime[
            runtime.index("void OutProcVSTPluginFilter::process") :
            runtime.index("void OutProcVSTPluginFilter::closeHost")
        ]

        self.assertIn("writeConfigFile()", runtime)
        self.assertIn("namedHostMonitorProc", runtime)
        self.assertIn("CreateThread(NULL, 0, namedHostMonitorProc", runtime)
        self.assertIn('L" --session \\""', runtime)
        self.assertIn('L" --vst-config \\""', runtime)
        self.assertIn('L" --parent-pid " << GetCurrentProcessId()', runtime)
        self.assertNotIn("CreateProcessW", process_body)
        self.assertNotIn("CreateThread", process_body)
        self.assertIn("WaitForSingleObject(hostReadyEvent, 0)", process_body)

        host = read("EqApoOutProcHost/EqApoOutProcHost.cpp")
        named_host = host[
            host.index("static int runNamedVstHost") :
            host.index("static int runHostMain")
        ]
        initialize = named_host.index("initializeVst(session.header, dspState.vst)")
        publish_ready = named_host.index("SetEvent(session.hostReady)")
        self.assertLess(initialize, publish_ready)

    def test_headless_and_gui_hosts_share_one_lease_and_handoff(self) -> None:
        host = read("EqApoOutProcHost/EqApoOutProcHost.cpp")
        editor = read("Editor/guis/VSTPluginFilterGUI.cpp")

        for token in ("HostLease", "HostHandoff", "HostReady"):
            self.assertIn(token, host)
        self.assertIn("runNamedVstHost(vstConfigPath, sessionId, ownerProcessId)", host)
        self.assertIn("WAIT_ABANDONED_0 + 3", host)
        self.assertIn("OpenProcess(SYNCHRONIZE, FALSE, ownerProcessId)", host)

        runtime = read("filters/OutProcVSTPluginFilter.cpp")
        self.assertIn('CreateEventW(&securityAttributes, TRUE, FALSE, makeObjectName(L"HostHandoff")', runtime)

        reset_ready = runtime.index("ResetEvent(context->hostReadyEvent)")
        release_lease = runtime.index("ReleaseMutex(context->hostLeaseMutex)", reset_ready)
        self.assertLess(reset_ready, release_lease)

        handoff = editor.index('signalOutProcPanel(L"HostHandoff")')
        launch = editor.index("QProcess::startDetached", handoff)
        self.assertLess(handoff, launch)

    def test_runtime_owner_is_a_cross_thread_lifetime_token(self) -> None:
        runtime = read("filters/OutProcVSTPluginFilter.cpp")
        header = read("filters/OutProcVSTPluginFilter.h")

        self.assertIn('makeObjectName(L"RuntimeOwner")', runtime)
        self.assertIn("ownerError == ERROR_ALREADY_EXISTS", runtime)
        self.assertIn("CloseHandle(runtimeOwnerToken)", runtime)
        self.assertNotIn("ReleaseMutex(runtimeOwner", runtime)
        self.assertNotIn("runtimeOwnerAcquired", header)

    def test_security_attributes_are_caller_local(self) -> None:
        runtime = read("filters/OutProcVSTPluginFilter.cpp")
        host = read("EqApoOutProcHost/EqApoOutProcHost.cpp")

        self.assertNotIn("static SECURITY_ATTRIBUTES attributes", runtime)
        self.assertNotIn("static SECURITY_ATTRIBUTES attributes", host)
        self.assertIn("SECURITY_ATTRIBUTES securityAttributes = {};", runtime)
        self.assertIn("SECURITY_ATTRIBUTES securityAttributes = {};", host)

    def test_gui_audio_host_reconnects_across_runtime_reload(self) -> None:
        host = read("EqApoOutProcHost/EqApoOutProcHost.cpp")
        audio_thread = host[
            host.index("static DWORD WINAPI guiAudioThreadProc") :
            host.index("static void resizeGuiWindowToEditor")
        ]

        shutdown_case = audio_thread[audio_thread.index("case WAIT_OBJECT_0 + 1:") :]
        self.assertIn("closeGuiAudioSession();", shutdown_case)
        self.assertIn("WaitForSingleObject(context->stopEvent, 50)", shutdown_case)
        self.assertIn("SetEvent(context->hostReadyEvent)", audio_thread)
        self.assertIn("ResetEvent(context->hostReadyEvent)", shutdown_case)

    def test_parameter_parsers_are_bounds_checked_and_numeric_strict(self) -> None:
        parser = read("helpers/VSTParameterParser.h")
        factories = (
            read("filters/VSTPluginFilterFactory.cpp"),
            read("filters/OutProcVSTPluginFilterFactory.cpp"),
        )

        self.assertIn("index + 2 < parts.size()", parser)
        self.assertIn("index += 3", parser)
        self.assertIn("VSTParseFiniteFloat", parser)
        for factory in factories:
            self.assertIn("VSTConsumeParameter(parts, i, paramMap)", factory)
            self.assertNotIn("x <= parts.size()", factory)
            self.assertNotIn("isdigit(value", factory)
            self.assertIn("++i;", factory)

    def test_load_failures_reach_vst_diagnostics(self) -> None:
        in_process = read("filters/VSTPluginFilterFactory.cpp")
        out_process = read("EqApoOutProcHost/EqApoOutProcHost.cpp")

        self.assertIn("VSTDiag(", in_process)
        self.assertIn("out-of-process VST load failed", out_process)
        self.assertIn("out-of-process GUI VST load failed", out_process)

    @unittest.skipUnless(os.name == "nt", "Win32 named IPC smoke test")
    def test_built_host_cold_starts_and_hands_off(self) -> None:
        host_path = ROOT / "EqApoOutProcHost/x64/Release/EqApoOutProcHost.exe"
        if not host_path.is_file():
            self.skipTest("build EqApoOutProcHost Release x64 to run the IPC smoke test")

        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.CreateFileMappingW.argtypes = (
            wintypes.HANDLE,
            wintypes.LPVOID,
            wintypes.DWORD,
            wintypes.DWORD,
            wintypes.DWORD,
            wintypes.LPCWSTR,
        )
        kernel32.CreateFileMappingW.restype = wintypes.HANDLE
        kernel32.CreateEventW.argtypes = (
            wintypes.LPVOID,
            wintypes.BOOL,
            wintypes.BOOL,
            wintypes.LPCWSTR,
        )
        kernel32.CreateEventW.restype = wintypes.HANDLE
        kernel32.CreateMutexW.argtypes = (
            wintypes.LPVOID,
            wintypes.BOOL,
            wintypes.LPCWSTR,
        )
        kernel32.CreateMutexW.restype = wintypes.HANDLE
        kernel32.MapViewOfFile.argtypes = (
            wintypes.HANDLE,
            wintypes.DWORD,
            wintypes.DWORD,
            wintypes.DWORD,
            ctypes.c_size_t,
        )
        kernel32.MapViewOfFile.restype = wintypes.LPVOID
        kernel32.WaitForSingleObject.argtypes = (wintypes.HANDLE, wintypes.DWORD)
        kernel32.WaitForSingleObject.restype = wintypes.DWORD
        kernel32.SetEvent.argtypes = (wintypes.HANDLE,)
        kernel32.SetEvent.restype = wintypes.BOOL
        kernel32.ReleaseMutex.argtypes = (wintypes.HANDLE,)
        kernel32.ReleaseMutex.restype = wintypes.BOOL
        kernel32.UnmapViewOfFile.argtypes = (wintypes.LPCVOID,)
        kernel32.UnmapViewOfFile.restype = wintypes.BOOL
        kernel32.CloseHandle.argtypes = (wintypes.HANDLE,)
        kernel32.CloseHandle.restype = wintypes.BOOL

        class AudioHeader(ctypes.Structure):
            _fields_ = (
                ("magic", ctypes.c_uint32),
                ("version", ctypes.c_uint32),
                ("sample_rate", ctypes.c_uint32),
                ("channel_count", ctypes.c_uint32),
                ("max_frames", ctypes.c_uint32),
                ("frame_count", ctypes.c_uint32),
                ("smoothing_samples", ctypes.c_uint32),
                ("dsp_type", ctypes.c_uint32),
                ("gain_db", ctypes.c_double),
                ("biquad_a0", ctypes.c_double),
                ("biquad_a1", ctypes.c_double),
                ("biquad_a2", ctypes.c_double),
                ("biquad_b1", ctypes.c_double),
                ("biquad_b2", ctypes.c_double),
                ("request_seq", ctypes.c_uint64),
                ("response_seq", ctypes.c_uint64),
                ("status", ctypes.c_uint32),
                ("host_state", ctypes.c_uint32),
            )

        session_id = f"test-{os.getpid()}"
        prefix = f"Global\\EqApoOutProcVST_{session_id}_"
        channel_count = 2
        max_frames = 16
        mapping_size = ctypes.sizeof(AudioHeader) + channel_count * max_frames * 16
        handles: list[int] = []
        view = None
        child: subprocess.Popen[bytes] | None = None

        def checked(handle: int, label: str) -> int:
            if not handle:
                self.skipTest(f"could not create {label}, Win32 error {ctypes.get_last_error()}")
            handles.append(handle)
            return handle

        try:
            mapping = checked(
                kernel32.CreateFileMappingW(
                    wintypes.HANDLE(-1), None, 0x04, 0, mapping_size, prefix + "Map"
                ),
                "global mapping",
            )
            request = checked(kernel32.CreateEventW(None, False, False, prefix + "Request"), "request event")
            response = checked(kernel32.CreateEventW(None, False, False, prefix + "Response"), "response event")
            shutdown = checked(kernel32.CreateEventW(None, True, False, prefix + "Shutdown"), "shutdown event")
            handoff = checked(kernel32.CreateEventW(None, True, False, prefix + "HostHandoff"), "handoff event")
            lease = checked(kernel32.CreateMutexW(None, False, prefix + "HostLease"), "host lease")
            ready = checked(kernel32.CreateEventW(None, True, False, prefix + "HostReady"), "ready event")

            view = kernel32.MapViewOfFile(mapping, 0xF001F, 0, 0, mapping_size)
            self.assertTrue(view, f"MapViewOfFile failed: {ctypes.get_last_error()}")
            ctypes.memset(view, 0, mapping_size)
            header = AudioHeader.from_address(view)
            header.magic = 0x4F504147
            header.version = 2
            header.sample_rate = 48000
            header.channel_count = channel_count
            header.max_frames = max_frames
            header.frame_count = max_frames
            header.dsp_type = 3

            with tempfile.TemporaryDirectory(prefix="eqapo-vst-host-") as temp_dir:
                config_path = pathlib.Path(temp_dir) / "smoke.opvs"
                library = "Z:\\EqApo-smoke-does-not-exist.dll".encode("utf-16-le")
                payload = (
                    struct.pack("<II", 0x4F505653, 2)
                    + struct.pack("<I", len(library) // 2)
                    + library
                    + struct.pack("<i", 0)
                    + struct.pack("<I", 0)
                    + struct.pack("<I", 0)
                )
                config_path.write_bytes(payload)

                child = subprocess.Popen(
                    [
                        str(host_path),
                        "--session",
                        session_id,
                        "--vst-config",
                        str(config_path),
                    ],
                    cwd=host_path.parent,
                    creationflags=0x08000000,
                )

                self.assertEqual(kernel32.WaitForSingleObject(ready, 5000), 0)
                self.assertEqual(kernel32.WaitForSingleObject(lease, 0), 258)

                header.request_seq = 1
                self.assertTrue(kernel32.SetEvent(request))
                self.assertEqual(kernel32.WaitForSingleObject(response, 5000), 0)
                self.assertEqual(header.status, 1)

                self.assertTrue(kernel32.SetEvent(handoff))
                self.assertEqual(child.wait(timeout=5), 20)
                child = None

                lease_result = kernel32.WaitForSingleObject(lease, 1000)
                self.assertIn(lease_result, (0, 128))
                self.assertTrue(kernel32.ReleaseMutex(lease))
        finally:
            if child is not None and child.poll() is None:
                kernel32.SetEvent(shutdown)
                try:
                    child.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    child.kill()
            if view:
                kernel32.UnmapViewOfFile(view)
            for handle in reversed(handles):
                kernel32.CloseHandle(handle)


if __name__ == "__main__":
    unittest.main()
