package jobs

import (
	"context"
	"errors"
	"fmt"
	"testing"
	"time"

	"github.com/tf4fun/voice-memo/server/internal/asr"
	"github.com/tf4fun/voice-memo/server/internal/domain"
	"github.com/tf4fun/voice-memo/server/internal/store"
)

func newRunner(t *testing.T, transcriber asr.Transcriber) (*Runner, *store.Store, *time.Time) {
	t.Helper()

	st, err := store.Open(t.TempDir())
	if err != nil {
		t.Fatalf("open store: %v", err)
	}
	t.Cleanup(func() { st.Close() })

	now := time.Unix(1_700_000_000, 0)
	runner := &Runner{
		Store:       st,
		Transcriber: transcriber,
		Now:         func() time.Time { return now },
	}
	return runner, st, &now
}

func queueMemo(t *testing.T, st *store.Store, id string, now int64) {
	t.Helper()
	ctx := context.Background()
	m := domain.Memo{
		ID:         id,
		DeviceID:   "A1B2",
		RecordedAt: now,
		UploadedAt: now,
		DurationMS: 1000,
		FrameCount: 50,
		Codec:      "opus",
		Status:     domain.StatusUploaded,
		AudioPath:  "/tmp/" + id + ".wav",
	}
	if _, err := st.InsertMemo(ctx, m); err != nil {
		t.Fatalf("insert memo: %v", err)
	}
	if err := st.EnqueueJob(ctx, id, now); err != nil {
		t.Fatalf("enqueue: %v", err)
	}
}

func TestRunOnceStoresTranscript(t *testing.T) {
	runner, st, now := newRunner(t, &asr.Fake{Text: "转写完成"})
	queueMemo(t, st, "m1", now.Unix())

	worked, err := runner.RunOnce(context.Background())
	if err != nil || !worked {
		t.Fatalf("RunOnce = %v, %v", worked, err)
	}

	memo, err := st.Memo(context.Background(), "m1")
	if err != nil {
		t.Fatalf("Memo: %v", err)
	}
	if memo.Status != domain.StatusReady || memo.Transcript != "转写完成" {
		t.Errorf("memo = %+v", memo)
	}
	if depth, _ := st.QueueDepth(context.Background()); depth != 0 {
		t.Errorf("queue depth = %d, want 0", depth)
	}
}

func TestRunOnceIdleWhenNothingDue(t *testing.T) {
	runner, _, _ := newRunner(t, &asr.Fake{})
	worked, err := runner.RunOnce(context.Background())
	if err != nil || worked {
		t.Fatalf("RunOnce = %v, %v; expected idle", worked, err)
	}
}

func TestPermanentFailureStopsRetrying(t *testing.T) {
	permanent := &asr.Fake{Err: fmt.Errorf("%w: bad audio", asr.ErrPermanent)}
	runner, st, now := newRunner(t, permanent)
	queueMemo(t, st, "m1", now.Unix())

	if _, err := runner.RunOnce(context.Background()); err != nil {
		t.Fatalf("RunOnce: %v", err)
	}

	memo, err := st.Memo(context.Background(), "m1")
	if err != nil {
		t.Fatalf("Memo: %v", err)
	}
	if memo.Status != domain.StatusFailed {
		t.Errorf("status = %q, want failed", memo.Status)
	}
	if memo.Error == "" {
		t.Error("failure reason should be stored")
	}
	if depth, _ := st.QueueDepth(context.Background()); depth != 0 {
		t.Errorf("queue depth = %d, want 0", depth)
	}
}

func TestTransientFailureIsRescheduled(t *testing.T) {
	transient := &asr.Fake{Err: errors.New("provider timeout")}
	runner, st, now := newRunner(t, transient)
	queueMemo(t, st, "m1", now.Unix())

	if _, err := runner.RunOnce(context.Background()); err != nil {
		t.Fatalf("RunOnce: %v", err)
	}

	memo, err := st.Memo(context.Background(), "m1")
	if err != nil {
		t.Fatalf("Memo: %v", err)
	}
	if memo.Status != domain.StatusUploaded {
		t.Errorf("status = %q, want uploaded (retry pending)", memo.Status)
	}
	if depth, _ := st.QueueDepth(context.Background()); depth != 1 {
		t.Errorf("queue depth = %d, want 1", depth)
	}

	// Not due yet.
	if worked, _ := runner.RunOnce(context.Background()); worked {
		t.Error("job should not be due immediately after a backoff")
	}

	// After the backoff it is due again, and the third attempt gives up.
	*now = now.Add(31 * time.Second)
	if worked, err := runner.RunOnce(context.Background()); err != nil || !worked {
		t.Fatalf("second attempt = %v, %v", worked, err)
	}
}

func TestGivesUpAfterMaxAttempts(t *testing.T) {
	transient := &asr.Fake{Err: errors.New("still broken")}
	runner, st, now := newRunner(t, transient)
	queueMemo(t, st, "m1", now.Unix())

	for attempt := 1; attempt <= MaxAttempts; attempt++ {
		if _, err := runner.RunOnce(context.Background()); err != nil {
			t.Fatalf("attempt %d: %v", attempt, err)
		}
		*now = now.Add(11 * time.Minute)
	}

	memo, err := st.Memo(context.Background(), "m1")
	if err != nil {
		t.Fatalf("Memo: %v", err)
	}
	if memo.Status != domain.StatusFailed {
		t.Errorf("status after %d attempts = %q, want failed", MaxAttempts, memo.Status)
	}
}

func TestDeletedMemoDropsItsJob(t *testing.T) {
	runner, st, now := newRunner(t, &asr.Fake{})
	queueMemo(t, st, "m1", now.Unix())
	if _, err := st.SoftDelete(context.Background(), "m1", now.Unix()); err != nil {
		// SoftDelete already removes the job; re-add one to simulate the race.
		t.Logf("soft delete: %v", err)
	}
	if err := st.EnqueueJob(context.Background(), "m1", now.Unix()); err != nil {
		t.Fatalf("enqueue: %v", err)
	}

	if worked, err := runner.RunOnce(context.Background()); err != nil || !worked {
		t.Fatalf("RunOnce = %v, %v", worked, err)
	}
	if depth, _ := st.QueueDepth(context.Background()); depth != 0 {
		t.Errorf("queue depth = %d, want 0", depth)
	}
}
