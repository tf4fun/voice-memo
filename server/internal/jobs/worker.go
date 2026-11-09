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
	"sync"
	"time"

	"github.com/tf4fun/voice-memo/server/internal/asr"
	"github.com/tf4fun/voice-memo/server/internal/domain"
	"github.com/tf4fun/voice-memo/server/internal/events"
	"github.com/tf4fun/voice-memo/server/internal/store"
)

// MaxAttempts is how many times a memo is retried before it is marked failed.
const MaxAttempts = 3

// Leave time to persist the result before the 120-second database claim expires.
const AttemptTimeout = 90 * time.Second

// backoff is the delay before attempt n+1.
var backoff = []time.Duration{30 * time.Second, 2 * time.Minute}

// Runner drives the transcription loop.
type Runner struct {
	Store       *store.Store
	Transcriber asr.Transcriber
	Log         *slog.Logger
	Poll        time.Duration
	// Now is injectable so tests do not sleep.
	Now func() time.Time
	// Events is optional; when set, status changes are pushed to subscribers.
	Events *events.Hub
}

// RunPool starts the configured bounded workers and joins every worker on exit.
func (r *Runner) RunPool(ctx context.Context, concurrency int) {
	if concurrency < 1 || concurrency > 4 {
		panic("worker concurrency outside 1..4")
	}
	var workers sync.WaitGroup
	for i := 0; i < concurrency; i++ {
		workers.Add(1)
		go func() { defer workers.Done(); r.Run(ctx) }()
	}
	workers.Wait()
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
	if err := ctx.Err(); err != nil {
		return false, err
	}
	now := r.now().Unix()

	claim, ok, err := r.Store.ClaimJobWithToken(ctx, now)
	memoID, attempts := claim.MemoID, claim.Attempts
	if err != nil {
		return false, err
	}
	if !ok {
		return false, nil
	}

	memo, err := r.Store.Memo(ctx, memoID)
	if errors.Is(err, store.ErrNotFound) {
		// The memo was deleted while its job was queued.
		return true, r.Store.DiscardClaim(ctx, claim)
	}
	if err != nil {
		return true, err
	}

	if applied, err := r.Store.ApplyClaim(ctx, claim, domain.StatusTranscribing, "", nil, nil); err != nil || !applied {
		return true, err
	}
	r.publish(memoID, domain.StatusTranscribing, "")

	attemptCtx, cancel := context.WithTimeout(ctx, AttemptTimeout)
	result, transcribeErr := r.Transcriber.Transcribe(attemptCtx, asr.Input{
		AudioPath: memo.AudioPath,
		Duration:  time.Duration(memo.DurationMS) * time.Millisecond,
		Language:  memo.Lang,
	})

	if transcribeErr == nil {
		transcribeErr = attemptCtx.Err()
	}
	cancel()
	if ctx.Err() != nil {
		return true, ctx.Err()
	} // Restart recovery retains this job.
	if transcribeErr != nil {
		permanent := asr.IsPermanent(transcribeErr) || attempts >= MaxAttempts
		if permanent {
			r.log().Warn("giving up on transcription",
				"memo", memoID, "attempts", attempts, "error", transcribeErr)
			applied, err := r.Store.ApplyClaim(ctx, claim, domain.StatusFailed, transcribeErr.Error(), nil, nil)
			if applied {
				r.publish(memoID, domain.StatusFailed, "")
			}
			return true, err
		}

		delay := backoff[len(backoff)-1]
		if attempts-1 < len(backoff) {
			delay = backoff[attempts-1]
		}
		r.log().Info("retrying transcription",
			"memo", memoID, "attempt", attempts, "in", delay.String(), "error", transcribeErr)
		nextAt := r.now().Add(delay).Unix()
		applied, err := r.Store.ApplyClaim(ctx, claim, domain.StatusUploaded, transcribeErr.Error(), nil, &nextAt)
		if applied {
			r.publish(memoID, domain.StatusUploaded, "")
		}
		return true, err
	}

	resultMemo := domain.Memo{Transcript: result.Text, Lang: result.Language, ASRProvider: result.Provider, ASRModel: result.Model, ASRTraceID: result.TraceID}
	applied, err := r.Store.ApplyClaim(ctx, claim, domain.StatusReady, "", &resultMemo, nil)
	if applied {
		r.publish(memoID, domain.StatusReady, result.Text)
	}
	return true, err
}

// publish pushes a status change when a hub is configured. It reports the
// snippet so the device can render the row without another request.
func (r *Runner) publish(memoID, status, snippet string) {
	if r.Events == nil {
		return
	}
	ev := events.Event{Type: "memo", ID: memoID, Status: status, Snippet: (domain.Memo{Transcript: snippet}).Snippet(40)}
	if memo, err := r.Store.Memo(context.Background(), memoID); err == nil {
		ev.RecordedAt = memo.RecordedAt
		ev.DurationMS = memo.DurationMS
	}
	r.Events.Publish(ev)
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
