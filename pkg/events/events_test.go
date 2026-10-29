// SPDX-License-Identifier: GPL-2.0-or-later

package events

import (
	"bytes"
	"encoding/json"
	"errors"
	"strings"
	"testing"
)

func TestJSONLines(t *testing.T) {
	var buf bytes.Buffer
	w := New(&buf, "import", true)
	w.Progress("extract", 5, 10, "files", "eboot.bin")
	w.Warning("slow disk")
	err := w.Error("retail_pkg", "encrypted with console keys")
	w.Done(map[string]string{"title_id": "CUSA00557"})
	var code *CodeError
	if !errors.As(err, &code) || code.Code != "retail_pkg" {
		t.Fatalf("Error() = %v", err)
	}
	lines := strings.Split(strings.TrimSpace(buf.String()), "\n")
	if len(lines) != 4 {
		t.Fatalf("%d lines: %q", len(lines), buf.String())
	}
	want := []string{"progress", "warning", "error", "done"}
	for i, l := range lines {
		var m map[string]any
		if err := json.Unmarshal([]byte(l), &m); err != nil {
			t.Fatalf("line %d not JSON: %v", i, err)
		}
		if m["event"] != want[i] || m["op"] != "import" {
			t.Errorf("line %d = %v", i, m)
		}
	}
	var p map[string]any
	json.Unmarshal([]byte(lines[0]), &p)
	if p["done"].(float64) != 5 || p["total"].(float64) != 10 || p["unit"] != "files" || p["stage"] != "extract" {
		t.Errorf("progress = %v", p)
	}
}

func TestHumanMode(t *testing.T) {
	var buf bytes.Buffer
	w := New(&buf, "build", false)
	w.Progress("compile", 3, 4, "stage", "clang")
	w.Done(nil)
	if strings.Contains(buf.String(), "{") || !strings.Contains(buf.String(), "clang") {
		t.Errorf("human output = %q", buf.String())
	}
}
