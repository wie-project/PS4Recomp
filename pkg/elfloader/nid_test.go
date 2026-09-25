package elfloader

import "testing"

func TestCalculateNID(t *testing.T) {
	got := CalculateNID("_Z19testLibraryFunctionPcmi")
	if got != "66ZEpOB7184" {
		t.Fatalf("NID=%q", got)
	}
	if NIDPrefix("66ZEpOB7184#A#A") != "66ZEpOB7184" {
		t.Fatal(NIDPrefix("66ZEpOB7184#A#A"))
	}
}

func TestDecodeSCEID(t *testing.T) {
	id, ok := decodeSCEID("A")
	if !ok || id != 0 {
		t.Fatalf("A -> %d %v want 0", id, ok)
	}
	id, ok = decodeSCEID("B")
	if !ok || id != 1 {
		t.Fatalf("B -> %d %v want 1", id, ok)
	}
	id, ok = decodeSCEID("d")
	if !ok || id != 29 {
		t.Fatalf("d -> %d %v want 29", id, ok)
	}
}

func TestHogwartsImportLibNames(t *testing.T) {
	path := "/Volumes/Samsung T7/Hogwarts Legacy Deluxe Edition/extracted/eboot.bin"
	loaded, err := LoadELF(path)
	if err != nil {
		t.Skip(err)
	}
	if len(loaded.ImportLibs) == 0 {
		t.Fatal("expected DT_SCE_IMPORT_LIB names")
	}
	name := "WfAiBW8Wcek#d#c"
	lib := loaded.LibraryForNID(name)
	if lib == "d" || lib == "plaintext" {
		t.Fatalf("LibraryForNID(%s)=%q ImportLibs=%v", name, lib, loaded.ImportLibs)
	}
	if len(lib) < 4 {
		t.Fatalf("LibraryForNID(%s)=%q looks like an encoded id", name, lib)
	}
	t.Logf("%s -> %s (libs=%d)", name, lib, len(loaded.ImportLibs))
}

func TestResolveNID(t *testing.T) {
	// Test known Sony system NIDs
	tests := []struct {
		sym  string
		want string
	}{
		{"zr094EQ39Ww#BA#+", "__cxa_pure_virtual"},
		{"9BcDykPmo1I#-#K", "__error"},
		{"E6ao34wPw+U", "stat"},
	}
	for _, tc := range tests {
		got, ok := ResolveNID(tc.sym)
		if !ok || got != tc.want {
			t.Errorf("ResolveNID(%q) = %q, %v; want %q, true", tc.sym, got, ok, tc.want)
		}
	}
}

