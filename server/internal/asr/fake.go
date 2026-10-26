package asr

import (
	"context"
	"time"
)

// Fake is a deterministic provider used by tests and offline development.
type Fake struct {
	// Text is returned verbatim. It defaults to a short Chinese sentence so
	// list and detail screens have something realistic to render.
	Text string
	// Delay simulates provider latency.
	Delay time.Duration
	// Err, when set, is returned instead of a result.
	Err error
}

// Name identifies the provider.
func (f *Fake) Name() string { return "fake" }

// Transcribe returns the configured text.
func (f *Fake) Transcribe(ctx context.Context, in Input) (Result, error) {
	if f.Delay > 0 {
		select {
		case <-time.After(f.Delay):
		case <-ctx.Done():
			return Result{}, ctx.Err()
		}
	}
	if f.Err != nil {
		return Result{}, f.Err
	}
	text := f.Text
	if text == "" {
		text = "这是一条测试转写结果。"
	}
	return Result{
		Text:     text,
		Language: "zh",
		Provider: "fake",
		Model:    "fake-1",
		TraceID:  "fake-trace",
	}, nil
}
