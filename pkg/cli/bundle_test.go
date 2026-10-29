package cli

import (
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"testing"

	"ps4-recomp/pkg/sfo"
)

// The generated bundle carries the Game Mode keys and a strict, deep ad-hoc signature that binds
// Info.plist and seals the resources.
func TestPackageAppBundleSignedWithGameMode(t *testing.T) {
	if runtime.GOOS != "darwin" {
		t.Skip("app bundles are macOS only")
	}
	if _, err := exec.LookPath("codesign"); err != nil {
		t.Skip("codesign not available")
	}
	out := t.TempDir()
	bin := filepath.Join(out, "ps4_app")
	if err := copyFile("/usr/bin/true", bin); err != nil {
		t.Fatal(err)
	}
	if err := os.Chmod(bin, 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(out, "guest_image.bin"), []byte("guest"), 0o644); err != nil {
		t.Fatal(err)
	}
	bundle, err := packageAppBundle(&Config{OutDir: out}, "eboot", bin)
	if err != nil {
		t.Fatalf("packageAppBundle: %v", err)
	}
	if err := signAppBundle(bundle); err != nil {
		t.Fatalf("signAppBundle: %v", err)
	}
	// Finder info on a copied resource must not break the strict signature
	res := filepath.Join(bundle, "Contents", "Resources", "guest_image.bin")
	if o, err := exec.Command("xattr", "-wx", "com.apple.FinderInfo", "54455854"+strings.Repeat("0", 56), res).CombinedOutput(); err != nil {
		t.Fatalf("xattr: %v: %s", err, o)
	}
	if err := signAppBundle(bundle); err != nil {
		t.Fatalf("signAppBundle with Finder info on a resource: %v", err)
	}
	plist := filepath.Join(bundle, "Contents", "Info.plist")
	if o, err := exec.Command("plutil", "-lint", plist).CombinedOutput(); err != nil {
		t.Fatalf("Info.plist does not parse: %v: %s", err, o)
	}
	for key, want := range map[string]string{
		"LSSupportsGameMode":        "true",
		"LSApplicationCategoryType": "public.app-category.games",
		"CFBundleExecutable":        "eboot",
		"CFBundleIdentifier":        "org.ps4recomp.eboot",
	} {
		o, err := exec.Command("plutil", "-extract", key, "raw", plist).Output()
		if err != nil || strings.TrimSpace(string(o)) != want {
			t.Errorf("Info.plist %s = %q (%v), want %q", key, strings.TrimSpace(string(o)), err, want)
		}
	}
	if o, err := exec.Command("codesign", "--verify", "--deep", "--strict", bundle).CombinedOutput(); err != nil {
		t.Fatalf("bundle signature does not verify: %v: %s", err, o)
	}
	o, _ := exec.Command("codesign", "-dv", bundle).CombinedOutput()
	if !strings.Contains(string(o), "Info.plist entries=") || !strings.Contains(string(o), "Sealed Resources version=") {
		t.Errorf("signature does not bind Info.plist and resources:\n%s", o)
	}
	// the seal covers the resources: a changed resource must fail verification
	if err := os.WriteFile(filepath.Join(bundle, "Contents", "Resources", "guest_image.bin"), []byte("other"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := exec.Command("codesign", "--verify", "--deep", "--strict", bundle).Run(); err == nil {
		t.Errorf("verification passed after a sealed resource changed")
	}
	// packaging again re-signs the bundle
	if _, err := packageAppBundle(&Config{OutDir: out}, "eboot", bin); err != nil {
		t.Fatalf("second packageAppBundle: %v", err)
	}
}

func TestInfoPlistFromSFO(t *testing.T) {
	p := appInfoPlist("eboot", true, bundleMeta{BundleID: "org.ps4recomp.CUSA04529", DisplayName: "Uncharted 4: A Thief’s End & <Co>", Version: "01.32"})
	for _, want := range []string{
		"<string>org.ps4recomp.CUSA04529</string>",
		"<string>Uncharted 4: A Thief’s End &amp; &lt;Co&gt;</string>",
		"<key>CFBundleShortVersionString</key>\n    <string>01.32</string>",
		"<key>CFBundleExecutable</key>\n    <string>eboot</string>",
	} {
		if !strings.Contains(p, want) {
			t.Errorf("plist lacks %q:\n%s", want, p)
		}
	}
}

func TestBundleMetaFallback(t *testing.T) {
	m := bundleMetaFor(t.TempDir(), "eboot")
	if m.BundleID != "org.ps4recomp.eboot" || m.DisplayName != "eboot" || m.Version != "1.0" {
		t.Errorf("fallback = %+v", m)
	}
}

func writeSFO(t *testing.T, entries ...sfo.Entry) string {
	t.Helper()
	dir := t.TempDir()
	if err := os.MkdirAll(filepath.Join(dir, "sce_sys"), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, "sce_sys", "param.sfo"), sfo.Build(entries), 0o644); err != nil {
		t.Fatal(err)
	}
	return dir
}

func TestBundleMetaFromSFO(t *testing.T) {
	dir := writeSFO(t,
		sfo.Entry{Key: "TITLE_ID", Str: "CUSA04529"},
		sfo.Entry{Key: "TITLE", Str: "Uncharted 4: A Thief’s End"},
		sfo.Entry{Key: "APP_VER", Str: "01.32"},
		sfo.Entry{Key: "VERSION", Str: "01.00"})
	m := bundleMetaFor(dir, "eboot")
	if m.BundleID != "org.ps4recomp.CUSA04529" || m.DisplayName != "Uncharted 4: A Thief’s End" || m.Version != "01.32" {
		t.Errorf("meta = %+v", m)
	}
	if id := titleIDOf(dir); id != "CUSA04529" {
		t.Errorf("titleIDOf = %q", id)
	}
}

// Real dumps carry VERSION but no APP_VER; the bundle id keeps only [A-Za-z0-9.-].
func TestBundleMetaVersionFallbackAndSanitizedID(t *testing.T) {
	dir := writeSFO(t,
		sfo.Entry{Key: "TITLE_ID", Str: "CUSA 00/557!"},
		sfo.Entry{Key: "TITLE", Str: "Game"},
		sfo.Entry{Key: "VERSION", Str: "01.05"})
	m := bundleMetaFor(dir, "eboot")
	if m.BundleID != "org.ps4recomp.CUSA00557" || m.Version != "01.05" {
		t.Errorf("meta = %+v", m)
	}
}
