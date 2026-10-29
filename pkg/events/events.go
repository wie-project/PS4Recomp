// SPDX-License-Identifier: GPL-2.0-or-later

package events

import (
	"encoding/json"
	"fmt"
	"io"
	"sync"
)

// CodeError is the error returned by Writer.Error; Code is the machine-readable reason.
type CodeError struct{ Code, Msg string }

func (e *CodeError) Error() string { return e.Msg }

// Writer emits events for one operation (op). It is safe for concurrent use.
type Writer struct {
	mu   sync.Mutex
	w    io.Writer
	op   string
	json bool
}

// New returns a writer for operation op.
func New(w io.Writer, op string, jsonMode bool) *Writer { return &Writer{w: w, op: op, json: jsonMode} }

type event struct {
	Event   string `json:"event"`
	Op      string `json:"op"`
	Stage   string `json:"stage,omitempty"`
	Done    *int64 `json:"done,omitempty"`
	Total   *int64 `json:"total,omitempty"`
	Unit    string `json:"unit,omitempty"`
	Code    string `json:"code,omitempty"`
	Message string `json:"message,omitempty"`
	Result  any    `json:"result,omitempty"`
}

func (w *Writer) emit(e event, human string) {
	w.mu.Lock()
	defer w.mu.Unlock()
	if w.json {
		e.Op = w.op
		b, err := json.Marshal(e)
		if err != nil {
			b, _ = json.Marshal(event{Event: "error", Op: w.op, Code: "io", Message: err.Error()})
		}
		w.w.Write(append(b, '\n'))
		return
	}
	fmt.Fprintln(w.w, human)
}

// Progress reports done of total units in a stage.
func (w *Writer) Progress(stage string, done, total int64, unit, msg string) {
	w.emit(event{Event: "progress", Stage: stage, Done: &done, Total: &total, Unit: unit, Message: msg},
		fmt.Sprintf("[%s] %s %d/%d %s %s", w.op, stage, done, total, unit, msg))
}

// Warning reports a non-fatal problem.
func (w *Writer) Warning(msg string) {
	w.emit(event{Event: "warning", Message: msg}, fmt.Sprintf("[%s] warning: %s", w.op, msg))
}

// Error reports a fatal problem and returns it as a *CodeError for the caller to return.
func (w *Writer) Error(code, msg string) error {
	w.emit(event{Event: "error", Code: code, Message: msg}, fmt.Sprintf("[%s] error (%s): %s", w.op, code, msg))
	return &CodeError{Code: code, Msg: msg}
}

// Done reports success with an optional result object.
func (w *Writer) Done(result any) {
	w.emit(event{Event: "done", Result: result}, fmt.Sprintf("[%s] done", w.op))
}
