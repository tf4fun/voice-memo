// Package asr turns a decoded recording into text.
//
// The interface is deliberately narrow: one file in, one string out. Providers
// differ in transport and in how they fail, so failures are classified here
// instead of being matched as strings by the caller.
package asr

import (
	"context"
	"errors"
	"time"
)

// Input is one recording to transcribe.
type Input struct {
	AudioPath string
	Duration  time.Duration
	Language  string // empty means auto-detect
}

// Result is what a provider returned.
type Result struct {
	Text     string
	Language string
	Provider string
	Model    string
	TraceID  string
}

// Transcriber is implemented by every provider.
type Transcriber interface {
	Name() string
	Transcribe(ctx context.Context, in Input) (Result, error)
}

// ErrPermanent marks a failure that will not succeed on retry, such as a
// rejected credential or a malformed file. Wrap it with %w.
var ErrPermanent = errors.New("permanent transcription failure")

// IsPermanent reports whether err should stop the retry loop.
func IsPermanent(err error) bool { return errors.Is(err, ErrPermanent) }
