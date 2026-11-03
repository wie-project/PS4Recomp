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

// The generated bundle carries the Game Mode keys and valid bundle structure.
func TestPackageAppBundleWithGameMode(t *testing.T) {
	if runtime.GOOS != "darwin" {
		t.Skip("app bundles are macOS only")
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
	// Check that guest_image.bin is located in SharedSupport, NOT Resources
	guestInShared := filepath.Join(bundle, "Contents", "SharedSupport", "guest_image.bin")
	if _, err := os.Stat(guestInShared); err != nil {
		t.Errorf("expected guest_image.bin in SharedSupport: %v", err)
	}
	guestInResources := filepath.Join(bundle, "Contents", "Resources", "guest_image.bin")
	if _, err := os.Stat(guestInResources); !os.IsNotExist(err) {
		t.Errorf("guest_image.bin must not be in Resources")
	}

	// packaging again succeeds
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
