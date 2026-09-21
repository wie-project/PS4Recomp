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
