#!/usr/bin/env python3
"""Source contracts for realtime-safe FilterEngine configuration handoff."""

from __future__ import annotations

import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
HEADER = (ROOT / "FilterEngine.h").read_text(encoding="utf-8")
SOURCE = (ROOT / "FilterEngine.cpp").read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    """Return a C++ function body, accounting for nested braces."""
    signature_index = source.index(signature)
    opening_brace = source.index("{", signature_index)
    depth = 0
    for index in range(opening_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening_brace + 1 : index]
    raise AssertionError(f"unterminated function: {signature}")


class FilterEngineHandoffTests(unittest.TestCase):
    PROCESS_SIGNATURES = (
        "void FilterEngine::process(float* output",
        "void FilterEngine::process(float** output",
        "void FilterEngine::process(double* output",
        "void FilterEngine::process(double** output",
    )

    def test_pending_and_retired_slots_are_lock_free_atomics(self) -> None:
        self.assertIn("#include <atomic>", HEADER)
        self.assertIn(
            "std::atomic<FilterConfiguration*> pendingConfig;", HEADER
        )
        self.assertIn(
            "std::atomic<FilterConfiguration*> retiredConfig;", HEADER
        )
        self.assertIn("is_always_lock_free", SOURCE)
        self.assertNotIn("FilterConfiguration* nextConfig;", HEADER)
        self.assertNotIn("FilterConfiguration* previousConfig;", HEADER)

    def test_writer_release_publishes_and_audio_acquire_snapshots(self) -> None:
        load_body = function_body(SOURCE, "void FilterEngine::loadConfig(")
        self.assertIn("pendingConfig.compare_exchange_strong(", load_body)
        self.assertIn("std::memory_order_release", load_body)
        self.assertIn("if (!hasInitialConfiguration)", load_body)
        self.assertIn("currentConfig = config;", load_body)

        for signature in self.PROCESS_SIGNATURES:
            process_body = function_body(SOURCE, signature)
            self.assertEqual(process_body.count("pendingConfig.load("), 1)
            self.assertIn("std::memory_order_acquire", process_body)
            self.assertIn("FilterConfiguration* const active = currentConfig;", process_body)

    def test_all_process_overloads_share_one_noexcept_commit_helper(self) -> None:
        self.assertIn(
            "commitCompletedTransition(FilterConfiguration* pending) noexcept;",
            HEADER,
        )
        helper_start = SOURCE.index("void FilterEngine::commitCompletedTransition(")
        helper_open = SOURCE.index("{", helper_start)
        helper_signature = SOURCE[helper_start:helper_open]
        helper_body = function_body(
            SOURCE, "void FilterEngine::commitCompletedTransition("
        )
        self.assertIn("noexcept", helper_signature)
        self.assertIn("pendingConfig.compare_exchange_strong(", helper_body)
        self.assertIn("retiredConfig.store(retired, std::memory_order_release);", helper_body)
        self.assertLess(
            helper_body.index("retiredConfig.store("),
            helper_body.index("ReleaseSemaphore("),
        )
        self.assertEqual(SOURCE.count("commitCompletedTransition(pending);"), 4)

    def test_audio_callbacks_do_not_allocate_wait_delete_or_log(self) -> None:
        forbidden = (
            ".resize(",
            "vector<",
            "std::vector<",
            "new ",
            "delete ",
            "MemoryHelper::alloc(",
            "MemoryHelper::free(",
            "WaitFor",
            "Sleep(",
            "EnterCriticalSection(",
            "LeaveCriticalSection(",
            "LogF(",
            "LogFStatic(",
            "TraceF(",
        )
        for signature in self.PROCESS_SIGNATURES:
            process_body = function_body(SOURCE, signature)
            for token in forbidden:
                self.assertNotIn(token, process_body, f"{signature}: {token}")
            self.assertNotIn("resizeBuffers(", process_body)

        pointer_setup = function_body(SOURCE, "void FilterEngine::resizeBuffers(")
        self.assertIn("inputBufPointers.resize(inputChannelCount);", pointer_setup)
        self.assertIn("outputBufPointers.resize(outputChannelCount);", pointer_setup)

    def test_notification_thread_reclaims_only_after_audio_release(self) -> None:
        thread_body = function_body(
            SOURCE, "unsigned long __stdcall FilterEngine::notificationThread("
        )
        self.assertIn("DWORD retirementWait = WaitForMultipleObjects(", thread_body)
        self.assertIn("if (retirementWait == WAIT_OBJECT_0)", thread_body)
        self.assertLess(
            thread_body.index("DWORD retirementWait"),
            thread_body.index("engine->reclaimRetiredConfiguration();"),
        )
        self.assertLess(
            thread_body.index("engine->reclaimRetiredConfiguration();"),
            thread_body.index(
                "ReleaseSemaphore(engine->loadSemaphore",
                thread_body.index("engine->reclaimRetiredConfiguration();"),
            ),
        )

    def test_cleanup_clears_all_slots_without_double_free(self) -> None:
        cleanup_body = function_body(
            SOURCE, "void FilterEngine::cleanupConfigurations()"
        )
        self.assertIn("currentConfig = nullptr;", cleanup_body)
        self.assertIn("pendingConfig.exchange(", cleanup_body)
        self.assertIn("retiredConfig.exchange(", cleanup_body)
        self.assertIn("if (pending != active)", cleanup_body)
        self.assertIn("if (retired != active && retired != pending)", cleanup_body)

    def test_reinitialize_stops_waiter_and_resets_reload_token(self) -> None:
        initialize_body = function_body(SOURCE, "void FilterEngine::initialize(")
        self.assertLess(
            initialize_body.index("stopNotificationThread();"),
            initialize_body.index("cleanupConfigurations();"),
        )
        self.assertIn("CloseHandle(loadSemaphore);", initialize_body)
        self.assertIn(
            "loadSemaphore = CreateSemaphore(NULL, 1, 1, NULL);",
            initialize_body,
        )

        stop_body = function_body(
            SOURCE, "void FilterEngine::stopNotificationThread()"
        )
        self.assertIn("SetEvent(shutdownEvent);", stop_body)
        self.assertIn("WaitForSingleObject(threadHandle, INFINITE)", stop_body)
        self.assertIn("shutdownEvent = NULL;", stop_body)

        load_file_body = function_body(SOURCE, "void FilterEngine::loadConfigFile(")
        self.assertIn("WaitForSingleObject(shutdownEvent, 0)", load_file_body)
        self.assertIn("Configuration reload cancelled during shutdown", load_file_body)


if __name__ == "__main__":
    unittest.main()
