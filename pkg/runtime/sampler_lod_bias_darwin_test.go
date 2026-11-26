// SPDX-License-Identifier: GPL-2.0-or-later

package recompruntime

import (
	"os"
	"os/exec"
	"path/filepath"
	"testing"
)

func TestMetalSamplerLodBias(t *testing.T) {
	clang, err := exec.LookPath("clang++")
	if err != nil {
		t.Skip("clang++ is required for the native Metal regression")
	}
	bin := filepath.Join(t.TempDir(), "sampler-lod-bias")
	cmd := exec.Command(clang, "-std=c++17", "-fobjc-arc", "-O2", "-Wl,-dead_strip",
		"-Imodules/libSceGnmDriver", "-Icore", metalTestSource(t, "sampler_lod_bias.mm"),
		"modules/libSceGnmDriver/ps4_gnm_resources.mm",
		"-framework", "Metal", "-framework", "Foundation", "-o", bin)
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("build native Metal regression: %v\n%s", err, out)
	}
	t.Run("descriptor", func(t *testing.T) { runMetalTestBinary(t, bin, "--descriptor-only") })
	t.Run("render", func(t *testing.T) { runMetalTestBinary(t, bin) })
}

// TestGcnSamplerLodBiasInShader: with gcn_profile::needs_sampler_lod_bias (a GPU that ignores the
// Metal sampler LOD bias) the recompiled sample instructions add the S# bias and the shader is
// specialized on it.
func TestGcnSamplerLodBiasInShader(t *testing.T) {
	clang, err := exec.LookPath("clang++")
	if err != nil {
		t.Skip("clang++ is required for the native regression")
	}
	source := metalTestSource(t, "sampler_lod_bias_shader.mm")
	root := filepath.Dir(filepath.Dir(filepath.Dir(source)))
	gcn := filepath.Join(root, "3rdparty", "gcn-recompiler")
	libs := []string{filepath.Join(gcn, "libgcn_recompiler.a"), filepath.Join(root, "3rdparty", "spirv-cross", "libspirv-cross.a")}
	for _, l := range libs {
		if _, err := os.Stat(l); err != nil {
			t.Skipf("run scripts/build.sh first: %v", err)
		}
	}
	bin := filepath.Join(t.TempDir(), "sampler-lod-bias-shader")
	args := append([]string{"-std=c++17", "-fobjc-arc", "-O2", "-I" + filepath.Join(gcn, "include"), source}, libs...)
	args = append(args, "-framework", "Metal", "-framework", "Foundation", "-o", bin)
	if out, err := exec.Command(clang, args...).CombinedOutput(); err != nil {
		t.Fatalf("build native regression: %v\n%s", err, out)
	}
	if out, err := exec.Command(bin).CombinedOutput(); err != nil {
		t.Fatalf("native regression: %v\n%s", err, out)
	}
}
