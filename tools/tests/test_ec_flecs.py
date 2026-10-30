#!/usr/bin/env python3
"""Host-side compile, execution, and architectural invariant tests for OpenTS's static C++20 EC layer."""
from __future__ import annotations

import pathlib
import re
import subprocess
import tempfile
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]


class StaticEcFlecsTests(unittest.TestCase):
    def _compile_common_objects(self, tmp_dir: pathlib.Path) -> tuple[pathlib.Path, pathlib.Path, list[str]]:
        flecs_c = REPO_ROOT / "thirdparty" / "flecs" / "distr" / "flecs.c"
        flecs_inc = REPO_ROOT / "thirdparty" / "flecs" / "include"
        code_dir = REPO_ROOT / "code"
        crc_cpp = code_dir / "crc.cpp"

        common_defs = [
            "-DFLECS_CUSTOM_BUILD",
            "-DFLECS_CPP",
            "-DFLECS_OS_API_IMPL",
        ]

        flecs_o = tmp_dir / "flecs.o"
        crc_o = tmp_dir / "crc.o"

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
        return flecs_o, crc_o, common_defs

    def test_compile_and_run_ec_cpp20_suite_over_flecs(self) -> None:
        flecs_inc = REPO_ROOT / "thirdparty" / "flecs" / "include"
        code_dir = REPO_ROOT / "code"
        ectest_cpp = REPO_ROOT / "tests" / "ec" / "ectest.cpp"
        world_cpp = code_dir / "ec" / "world.cpp"

        self.assertTrue(ectest_cpp.is_file(), f"Missing EC test source: {ectest_cpp}")
        self.assertTrue(world_cpp.is_file(), f"Missing EC world source: {world_cpp}")

        with tempfile.TemporaryDirectory(prefix="opents_ectest_") as tmp:
            tmp_dir = pathlib.Path(tmp)
            flecs_o, crc_o, common_defs = self._compile_common_objects(tmp_dir)
            exe = tmp_dir / "ectest"

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

    def test_compile_and_run_produce_cash_component_suite(self) -> None:
        flecs_inc = REPO_ROOT / "thirdparty" / "flecs" / "include"
        code_dir = REPO_ROOT / "code"
        producecash_cpp = REPO_ROOT / "tests" / "producecash" / "producecashtest.cpp"
        world_cpp = code_dir / "ec" / "world.cpp"

        self.assertTrue(producecash_cpp.is_file(), f"Missing ProduceCash test source: {producecash_cpp}")

        with tempfile.TemporaryDirectory(prefix="opents_producecash_") as tmp:
            tmp_dir = pathlib.Path(tmp)
            flecs_o, crc_o, common_defs = self._compile_common_objects(tmp_dir)
            exe = tmp_dir / "producecashtest"

            comp = subprocess.run(
                [
                    "g++",
                    "-std=c++20",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(producecash_cpp),
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
            self.assertEqual(comp.returncode, 0, msg=f"producecashtest compile failed:\n{comp.stdout}\n{comp.stderr}")

            res = subprocess.run([str(exe)], check=False, capture_output=True, text=True)
            self.assertEqual(res.returncode, 0, msg=f"producecashtest failed:\n{res.stdout}\n{res.stderr}")
            self.assertIn("0 failed", res.stdout)

    def test_engine_hooks_and_empty_registry_compile_and_run_with_1to1_entities(self) -> None:
        flecs_inc = REPO_ROOT / "thirdparty" / "flecs" / "include"
        code_dir = REPO_ROOT / "code"
        world_cpp = code_dir / "ec" / "world.cpp"

        probe_src = """
#define EC_CUSTOM_REGISTRY_COMPONENTS
#include "ec/engine_hooks.h"
#include "ec/registry.h"

class ObjectClass {
public:
    flecs::entity Entity{EC::Create_Entity(this)};
    ~ObjectClass() { EC::Destroy_Entity(Entity); }
};
class CellClass {
public:
    flecs::entity Entity{EC::Create_Entity(this)};
    ~CellClass() { EC::Destroy_Entity(Entity); }
};
struct ILocomotion {
    virtual ~ILocomotion() = default;
    virtual flecs::entity Get_Entity() const noexcept = 0;
};
class LocomotionClass : public ILocomotion {
public:
    flecs::entity Entity{EC::Create_Entity(this)};
    ~LocomotionClass() { EC::Destroy_Entity(Entity); }
    flecs::entity Get_Entity() const noexcept override { return Entity; }
};

int main() {
    static_assert(EC::GameComponents::count == 0);
    EC::GameComponents::Register_All(EC::World());
    EC::GameComponents::AI_All(EC::World());
    EC::GameComponents::PostAI_All(EC::World());
    {
        ObjectClass obj;
        CellClass cell;
        LocomotionClass carrier;
        LocomotionClass carried;
        if (!obj.Entity.is_alive() || !cell.Entity.is_alive()) return 1;
        if (EC::Has_User_Components(obj.Entity)) return 2;
        carrier.Entity.add(flecs::Disabled);
        if (EC::Has_User_Components(carrier.Entity)) return 3;
        EC::GameComponents::Dispatch_Begin_Piggyback(carrier, &carried);
        if (!carried.Entity.has(flecs::Disabled)) return 4;
        EC::GameComponents::Dispatch_End_Piggyback(carrier, &carried);
        if (!carried.Entity.has(flecs::Disabled)) return 5;
        carrier.Entity.remove(flecs::Disabled);
        EC::GameComponents::Dispatch_End_Piggyback(carrier, &carried);
        if (carried.Entity.has(flecs::Disabled)) return 6;
        CRCEngine crc;
        EC::GameComponents::Compute_Entity_CRC(obj.Entity, crc);
    }
    EC::World_Shutdown();
    return 0;
}
"""
        with tempfile.TemporaryDirectory(prefix="opents_ec_empty_") as tmp:
            tmp_dir = pathlib.Path(tmp)
            flecs_o, crc_o, common_defs = self._compile_common_objects(tmp_dir)
            src_path = tmp_dir / "empty_probe.cpp"
            exe = tmp_dir / "empty_probe"
            src_path.write_text(probe_src, encoding="utf-8")

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

    def test_pure_flecs_entity_root_handles_and_no_lazy_wrappers(self) -> None:
        self.assertFalse(
            (REPO_ROOT / "code" / "ec" / "entity_inl.h").exists(),
            "Obsolete lazy wrapper header code/ec/entity_inl.h must not exist",
        )

        forbidden_re = re.compile(
            r"\b(TypedEntity|EntityView|DetachedEntitySlot|CompHolder|"
            r"Is_Materialized|Ensure_Materialized|PendingDisabled|"
            r"Materialize_Loaded_Owner|Ensure_Produce_Cash|World_Reset)\b"
        )
        violations: list[str] = []
        for root_dir in (REPO_ROOT / "code", REPO_ROOT / "tests"):
            for path in sorted(root_dir.rglob("*")):
                if not path.is_file() or path.suffix not in {".h", ".hh", ".hpp", ".c", ".cpp"}:
                    continue
                rel = path.relative_to(REPO_ROOT).as_posix()
                text = path.read_text(encoding="utf-8", errors="replace")
                for m in forbidden_re.finditer(text):
                    violations.append(f"{rel}: {m.group(0)}")

        self.assertEqual(violations, [], msg="Lazy wrapper remnants found:\n" + "\n".join(violations))

        for rel_hdr in ("code/object.h", "code/cell.h", "code/loco.h"):
            content = (REPO_ROOT / rel_hdr).read_text(encoding="utf-8")
            self.assertRegex(
                content,
                r"\bflecs::entity\s+Entity\s*;",
                msg=f"{rel_hdr} must store `flecs::entity Entity;` directly",
            )


if __name__ == "__main__":
    unittest.main()
