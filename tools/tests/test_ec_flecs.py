#!/usr/bin/env python3
"""Host-side compile, execution, and header-isolation tests for OpenTS's static C++20 EC layer."""
from __future__ import annotations

import pathlib
import re
import subprocess
import tempfile
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]


class StaticEcFlecsTests(unittest.TestCase):
    def test_compile_and_run_ec_cpp20_suite_over_flecs(self) -> None:
        flecs_c = REPO_ROOT / "thirdparty" / "flecs" / "distr" / "flecs.c"
        flecs_inc = REPO_ROOT / "thirdparty" / "flecs" / "include"
        code_dir = REPO_ROOT / "code"
        ectest_cpp = REPO_ROOT / "tests" / "ec" / "ectest.cpp"
        world_cpp = code_dir / "ec" / "world.cpp"
        crc_cpp = code_dir / "crc.cpp"

        self.assertTrue(flecs_c.is_file(), f"Missing flecs source: {flecs_c}")
        self.assertTrue(ectest_cpp.is_file(), f"Missing EC test source: {ectest_cpp}")
        self.assertTrue(world_cpp.is_file(), f"Missing EC world source: {world_cpp}")

        common_defs = [
            "-DFLECS_CUSTOM_BUILD",
            "-DFLECS_CPP",
            "-DFLECS_OS_API_IMPL",
        ]

        with tempfile.TemporaryDirectory(prefix="opents_ectest_") as tmp:
            tmp_dir = pathlib.Path(tmp)
            flecs_o = tmp_dir / "flecs.o"
            crc_o = tmp_dir / "crc.o"
            exe = tmp_dir / "ectest"

            subprocess.run(
                [
                    "gcc",
                    "-O1",
                    "-c",
                    str(flecs_c),
                    f"-I{flecs_inc}",
                    *common_defs,
                    "-o",
                    str(flecs_o),
                ],
                check=True,
                capture_output=True,
                text=True,
            )

            # Compile legacy Westwood crc.cpp without strict modern warning flags.
            crc_comp = subprocess.run(
                [
                    "g++",
                    "-std=c++20",
                    "-O2",
                    "-c",
                    str(crc_cpp),
                    f"-I{code_dir}",
                    "-o",
                    str(crc_o),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(crc_comp.returncode, 0, msg=f"crc.cpp compile failed:\n{crc_comp.stderr}")

            # Compile all EC headers, world.cpp, and ectest.cpp with strict -Wall -Wextra -Werror.
            comp = subprocess.run(
                [
                    "g++",
                    "-std=c++20",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(ectest_cpp),
                    str(world_cpp),
                    str(crc_o),
                    str(flecs_o),
                    f"-I{code_dir}",
                    f"-I{flecs_inc}",
                    *common_defs,
                    "-Dflecs_STATIC",
                    "-lpthread",
                    "-o",
                    str(exe),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(comp.returncode, 0, msg=f"g++ compile failed:\n{comp.stdout}\n{comp.stderr}")

            res = subprocess.run([str(exe)], check=False, capture_output=True, text=True)
            self.assertEqual(res.returncode, 0, msg=f"ectest failed:\n{res.stdout}\n{res.stderr}")
            self.assertIn("0 failed", res.stdout)

    def test_public_ec_headers_compile_without_flecs_include_path(self) -> None:
        code_dir = REPO_ROOT / "code"
        probe_src = """
#include "ec/fnv1a.h"
#include "ec/entity.h"
#include "ec/engine_hooks.h"

struct DummyOwner {
    EC::TypedEntity<ObjectClass> Components;
    DummyOwner() : Components(nullptr) {}
};

int VerifyFastPath() {
    DummyOwner d;
    if (d.Components.Is_Materialized()) {
        return 1;
    }
    return static_cast<int>(EC::Fnv1a_64("ObjectClass") & 0x7FFFFFFF);
}
"""
        with tempfile.TemporaryDirectory(prefix="opents_ec_hdr_") as tmp:
            tmp_dir = pathlib.Path(tmp)
            src_path = tmp_dir / "probe.cpp"
            obj_path = tmp_dir / "probe.o"
            src_path.write_text(probe_src, encoding="utf-8")

            # Deliberately omit -Ithirdparty/flecs/include to prove <flecs.h> never leaks.
            comp = subprocess.run(
                [
                    "g++",
                    "-std=c++20",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-c",
                    str(src_path),
                    f"-I{code_dir}",
                    "-o",
                    str(obj_path),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                comp.returncode,
                0,
                msg=f"Public EC headers leaked <flecs.h> or failed without flecs include path:\n{comp.stderr}",
            )

    def test_empty_default_game_components_registry_compiles_and_runs(self) -> None:
        flecs_c = REPO_ROOT / "thirdparty" / "flecs" / "distr" / "flecs.c"
        flecs_inc = REPO_ROOT / "thirdparty" / "flecs" / "include"
        code_dir = REPO_ROOT / "code"
        world_cpp = code_dir / "ec" / "world.cpp"
        crc_cpp = code_dir / "crc.cpp"

        empty_probe_src = """
#include "ec/registry.h"

class ObjectClass {
public:
    EC::TypedEntity<ObjectClass> Components{this};
};
class CellClass {
public:
    EC::TypedEntity<CellClass> Components{this};
};
struct ILocomotion {
    virtual ~ILocomotion() = default;
    virtual EC::TypedEntity<LocomotionClass> & Components() noexcept = 0;
    virtual EC::TypedEntity<LocomotionClass> const & Components() const noexcept = 0;
};
class LocomotionClass : public ILocomotion {
public:
    EC::TypedEntity<LocomotionClass> Holder{this};
    EC::TypedEntity<LocomotionClass> & Components() noexcept override { return Holder; }
    EC::TypedEntity<LocomotionClass> const & Components() const noexcept override { return Holder; }
};

int main() {
    static_assert(EC::GameComponents::count == 0);
    EC::GameComponents::Register_All(EC::World());
    EC::GameComponents::AI_All(EC::World());
    EC::GameComponents::PostAI_All(EC::World());
    ObjectClass obj;
    LocomotionClass carrier;
    LocomotionClass carried;
    carrier.Components().Set_Disabled(true);
    EC::GameComponents::Dispatch_Begin_Piggyback(carrier, &carried);
    if (!carried.Components().Is_Disabled()) return 1;
    EC::GameComponents::Dispatch_End_Piggyback(carrier, &carried);
    if (!carried.Components().Is_Disabled()) return 2;
    carrier.Components().Set_Disabled(false);
    EC::GameComponents::Dispatch_End_Piggyback(carrier, &carried);
    if (carried.Components().Is_Disabled()) return 3;
    CRCEngine crc;
    obj.Components.Compute_CRC(crc);
    EC::World_Shutdown();
    return 0;
}
"""
        common_defs = [
            "-DFLECS_CUSTOM_BUILD",
            "-DFLECS_CPP",
            "-DFLECS_OS_API_IMPL",
        ]
        with tempfile.TemporaryDirectory(prefix="opents_ec_empty_") as tmp:
            tmp_dir = pathlib.Path(tmp)
            flecs_o = tmp_dir / "flecs.o"
            crc_o = tmp_dir / "crc.o"
            src_path = tmp_dir / "empty_probe.cpp"
            exe = tmp_dir / "empty_probe"
            src_path.write_text(empty_probe_src, encoding="utf-8")

            subprocess.run(
                ["gcc", "-O1", "-c", str(flecs_c), f"-I{flecs_inc}", *common_defs, "-o", str(flecs_o)],
                check=True,
                capture_output=True,
                text=True,
            )
            subprocess.run(
                ["g++", "-std=c++20", "-O2", "-c", str(crc_cpp), f"-I{code_dir}", "-o", str(crc_o)],
                check=True,
                capture_output=True,
                text=True,
            )
            comp = subprocess.run(
                [
                    "g++",
                    "-std=c++20",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(src_path),
                    str(world_cpp),
                    str(crc_o),
                    str(flecs_o),
                    f"-I{code_dir}",
                    f"-I{flecs_inc}",
                    *common_defs,
                    "-Dflecs_STATIC",
                    "-lpthread",
                    "-o",
                    str(exe),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(comp.returncode, 0, msg=f"empty registry compile failed:\n{comp.stderr}")
            run_res = subprocess.run([str(exe)], check=False, capture_output=True, text=True)
            self.assertEqual(run_res.returncode, 0, msg=f"empty registry run failed: rc={run_res.returncode}")

    def test_toolkit_header_isolation_matches_cmake_policy(self) -> None:
        code_dir = REPO_ROOT / "code"
        flecs_re = re.compile(r'^[ \t]*#[ \t]*include[ \t]*[<"](flecs\.h|flecs/)', re.MULTILINE)
        allowed_private_headers = {
            "code/ec/world.h",
            "code/ec/entity_inl.h",
            "code/ec/registry.h",
        }
        violations: list[str] = []

        for path in sorted(code_dir.rglob("*")):
            if not path.is_file():
                continue
            rel = path.relative_to(REPO_ROOT).as_posix()
            text = path.read_text(encoding="utf-8", errors="replace")
            matches = [m.group(0).strip() for m in flecs_re.finditer(text)]
            if not matches:
                continue

            if path.suffix in {".h", ".hh", ".hpp"}:
                if rel not in allowed_private_headers:
                    for hit in matches:
                        violations.append(f"{rel}: {hit}")
            elif path.suffix in {".c", ".cpp"}:
                if not rel.startswith("code/ec/"):
                    for hit in matches:
                        violations.append(f"{rel}: {hit}")

        self.assertEqual(violations, [], msg="flecs header isolation violations:\n" + "\n".join(violations))


if __name__ == "__main__":
    unittest.main()
