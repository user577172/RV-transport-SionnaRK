#
# SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#
"""
Unit tests for the Link Adaptation MAC plugin.

The LA plugin is pure C and requires the full OAI build to compile.
These tests validate the plugin structure, source file contracts,
and documentation integrity to catch issues before integration.
"""
import pytest
import re
import yaml
from pathlib import Path

PLUGIN_DIR = Path(__file__).parent.parent.parent
LA_SRC_DIR = PLUGIN_DIR / "openairinterface5g" / "openair2" / "LAYER2" / "NR_MAC_gNB"

REQUIRED_SYMBOLS = [
    "link_adaptation_init",
    "link_adaptation_shutdown",
    "link_adaptation_get_mcs_from_bler",
]

LA_VARIANTS = {
    "link_adaptation_orig": "link_adaptation_orig.c",
    "link_adaptation_log": "link_adaptation_log.c",
    "link_adaptation_olla": "link_adaptation_olla.c",
    "link_adaptation_mcs_hist_olla": "link_adaptation_mcs_hist_olla.c",
    "link_adaptation_mcs_hist_log": "link_adaptation_mcs_hist_log.c",
}


class TestPluginStructure:
    """Validate that all required plugin files exist."""

    def test_cmake_exists(self):
        assert (PLUGIN_DIR / "CMakeLists.txt").is_file()

    def test_tutorial_yaml_exists(self):
        assert (PLUGIN_DIR / "tutorial.yaml").is_file()

    def test_defs_header_exists(self):
        assert (LA_SRC_DIR / "link_adaptation_defs.h").is_file()

    def test_extern_header_exists(self):
        assert (LA_SRC_DIR / "link_adaptation_extern.h").is_file()

    def test_loader_exists(self):
        assert (LA_SRC_DIR / "link_adaptation_load.c").is_file()

    @pytest.mark.parametrize("variant,source", LA_VARIANTS.items())
    def test_variant_source_exists(self, variant, source):
        assert (LA_SRC_DIR / source).is_file(), f"Missing source for variant {variant}"

    @pytest.mark.parametrize("variant", LA_VARIANTS.keys())
    def test_variant_header_exists(self, variant):
        header = LA_SRC_DIR / f"{variant}.h"
        if variant != "link_adaptation_mcs_hist_log":
            assert header.is_file(), f"Missing header {header.name}"


class TestInterfaceContract:
    """Validate that source files export the required function signatures."""

    def test_defs_typedefs(self):
        content = (LA_SRC_DIR / "link_adaptation_defs.h").read_text()
        assert "link_adaptation_initfunc_t" in content
        assert "link_adaptation_shutdownfunc_t" in content
        assert "link_adaptation_mcsfunc_t" in content

    def test_extern_interface_struct(self):
        content = (LA_SRC_DIR / "link_adaptation_extern.h").read_text()
        assert "link_adaptation_interface_t" in content
        assert "load_link_adaptation_lib" in content
        assert "free_link_adaptation_lib" in content

    def test_loader_binds_all_symbols(self):
        content = (LA_SRC_DIR / "link_adaptation_load.c").read_text()
        for sym in REQUIRED_SYMBOLS:
            assert sym in content, f"Loader missing symbol binding for {sym}"

    def test_loader_uses_shlib_loader(self):
        content = (LA_SRC_DIR / "link_adaptation_load.c").read_text()
        assert "load_module_version_shlib" in content

    @pytest.mark.parametrize("variant,source", LA_VARIANTS.items())
    def test_variant_exports_required_functions(self, variant, source):
        content = (LA_SRC_DIR / source).read_text()
        for sym in REQUIRED_SYMBOLS:
            assert sym in content, (
                f"Variant {variant} missing required function {sym}"
            )


class TestCMake:
    """Validate CMakeLists.txt declares expected targets."""

    @pytest.fixture(autouse=True)
    def cmake_content(self):
        self.content = (PLUGIN_DIR / "CMakeLists.txt").read_text()

    def test_registers_mac_plugins_src(self):
        assert "MAC_PLUGINS_SRC" in self.content
        assert "PARENT_SCOPE" in self.content

    def test_declares_module_libraries(self):
        for variant in LA_VARIANTS:
            assert variant in self.content, f"CMake missing MODULE target {variant}"

    def test_adds_asn1_dependencies(self):
        assert "asn1_nr_rrc_hdrs" in self.content
        assert "asn1_lte_rrc_hdrs" in self.content


class TestTutorialManifest:
    """Validate tutorial.yaml is well-formed and matches expected schema."""

    @pytest.fixture(autouse=True)
    def manifest(self):
        with open(PLUGIN_DIR / "tutorial.yaml") as f:
            self.data = yaml.safe_load(f)

    def test_has_name(self):
        assert self.data["name"] == "link_adaptation"

    def test_has_description(self):
        assert len(self.data.get("description", "")) > 0

    def test_has_build_section(self):
        assert "build" in self.data

    def test_has_unit_tests(self):
        assert self.data["tests"]["unit"]["enabled"] is True
        assert "pytest" in self.data["tests"]["unit"]["command"]

    def test_has_integration_tests(self):
        assert self.data["tests"]["integration"]["enabled"] is True


class TestDocumentation:
    """Validate documentation files and symlinks are intact."""

    DOC_DIR = PLUGIN_DIR / "doc"

    def test_index_rst_exists(self):
        assert (self.DOC_DIR / "index.rst").is_file()

    def test_index_has_no_todo_marker(self):
        content = (self.DOC_DIR / "index.rst").read_text()
        assert "[TODO]" not in content

    def test_index_has_ref_label(self):
        content = (self.DOC_DIR / "index.rst").read_text()
        assert ".. _link_adaptation:" in content

    def test_usage_rst_exists(self):
        assert (self.DOC_DIR / "usage.rst").is_file()

    def test_oai_la_rst_exists(self):
        assert (self.DOC_DIR / "oai-la" / "oai-la.rst").is_file()

    def test_olla_rst_exists(self):
        assert (self.DOC_DIR / "olla" / "olla.rst").is_file()

    @pytest.mark.parametrize("symlink", [
        "oai-la/link_adaptation_extern.h",
        "oai-la/link_adaptation_orig.c",
        "oai-la/link_adaptation_log.c",
        "oai-la/link_adaptation_mcs_hist_log.c",
        "oai-la/mac_plugins.c",
        "oai-la/mac_plugins.h",
        "olla/link_adaptation_olla.c",
        "olla/link_adaptation_mcs_hist_olla.c",
        "olla/link_adaptation_mcs_hist_log.c",
    ])
    def test_doc_symlink_resolves(self, symlink):
        path = self.DOC_DIR / symlink
        assert path.exists(), f"Broken symlink: {symlink}"


class TestMacPlugins:
    """Validate the MAC plugin entry point files."""

    MAC_DIR = PLUGIN_DIR.parent / "common" / "src"

    def test_mac_plugins_c_exists(self):
        assert (self.MAC_DIR / "mac_plugins.c").is_file()

    def test_mac_plugins_h_exists(self):
        assert (self.MAC_DIR / "mac_plugins.h").is_file()

    def test_mac_plugins_loads_la(self):
        content = (self.MAC_DIR / "mac_plugins.c").read_text()
        assert "load_link_adaptation_lib" in content
        assert "free_link_adaptation_lib" in content

    def test_mac_plugins_header_includes_extern(self):
        content = (self.MAC_DIR / "mac_plugins.h").read_text()
        assert "link_adaptation_extern.h" in content
