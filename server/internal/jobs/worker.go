// Package jobs runs transcription work in the background.
//
// There is no external queue: the jobs table is the queue, and a single writer
// loop claims from it. That keeps restart recovery free — the state that would
// otherwise need a broker is already in SQLite.
package jobs

import (
	"context"
	"errors"
	"log/slog"
	"time"

	"github.com/tf4fun/voice-memo/server/internal/asr"
	"github.com/tf4fun/voice-memo/server/internal/domain"
	"github.com/tf4fun/voice-memo/server/internal/store"
)

// MaxAttempts is how many times a memo is retried before it is marked failed.
const MaxAttempts = 3

// backoff is the delay before attempt n+1.
var backoff = []time.Duration{30 * time.Second, 2 * time.Minute, 10 * time.Minute}

// Runner drives the transcription loop.
type Runner struct {
	Store       *store.Store
	Transcriber asr.Transcriber
	Log         *slog.Logger
	Poll        time.Duration
	// Now is injectable so tests do not sleep.
	Now func() time.Time
}

// Run processes jobs until ctx is cancelled.
func (r *Runner) Run(ctx context.Context) {
	poll := r.Poll
	if poll <= 0 {
		poll = 2 * time.Second
	}
	ticker := time.NewTicker(poll)
	defer ticker.Stop()

	for {
		for {
			worked, err := r.RunOnce(ctx)
			if err != nil {
				r.log().Error("transcription job failed", "error", err)
				break
			}
			if !worked {
				break
			}
		}
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
		}
	}
}

// RunOnce handles at most one due job. It reports whether work was done.
func (r *Runner) RunOnce(ctx context.Context) (bool, error) {
	now := r.now().Unix()

	memoID, attempts, ok, err := r.Store.ClaimJob(ctx, now)
	if err != nil {
		return false, err
	}
	if !ok {
		return false, nil
	}

	memo, err := r.Store.Memo(ctx, memoID)
	if errors.Is(err, store.ErrNotFound) {
		// The memo was deleted while its job was queued.
		return true, r.Store.FinishJob(ctx, memoID)
	}
	if err != nil {
		return true, err
	}

	if err := r.Store.SetStatus(ctx, memoID, domain.StatusTranscribing, ""); err != nil {
		return true, err
	}

	result, transcribeErr := r.Transcriber.Transcribe(ctx, asr.Input{
		AudioPath: memo.AudioPath,
		Duration:  time.Duration(memo.DurationMS) * time.Millisecond,
		Language:  memo.Lang,
	})

	if transcribeErr != nil {
		permanent := asr.IsPermanent(transcribeErr) || attempts >= MaxAttempts
		if permanent {
			r.log().Warn("giving up on transcription",
				"memo", memoID, "attempts", attempts, "error", transcribeErr)
			if err := r.Store.SetStatus(ctx, memoID, domain.StatusFailed, transcribeErr.Error()); err != nil {
				return true, err
			}
			return true, r.Store.FinishJob(ctx, memoID)
		}

		delay := backoff[len(backoff)-1]
		if attempts-1 < len(backoff) {
			delay = backoff[attempts-1]
		}
		r.log().Info("retrying transcription",
			"memo", memoID, "attempt", attempts, "in", delay.String(), "error", transcribeErr)
		if err := r.Store.SetStatus(ctx, memoID, domain.StatusUploaded, transcribeErr.Error()); err != nil {
			return true, err
		}
		return true, r.Store.RescheduleJob(ctx, memoID, r.now().Add(delay).Unix())
	}

	if err := r.Store.RecordTranscript(ctx, memoID, result.Text, result.Language,
		result.Provider, result.Model, result.TraceID); err != nil {
		return true, err
	}
	r.log().Info("transcription ready",
		"memo", memoID, "provider", result.Provider, "chars", len(result.Text))
	return true, r.Store.FinishJob(ctx, memoID)
}

func (r *Runner) now() time.Time {
	if r.Now != nil {
		return r.Now()
	}
	return time.Now()
}

func (r *Runner) log() *slog.Logger {
	if r.Log != nil {
		return r.Log
	}
	return slog.Default()
}
