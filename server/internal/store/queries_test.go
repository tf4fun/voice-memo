package store

import (
	"context"
	"errors"
	"testing"

	"github.com/tf4fun/voice-memo/server/internal/domain"
)

func newStore(t *testing.T) *Store {
	t.Helper()
	st, err := Open(t.TempDir())
	if err != nil {
		t.Fatalf("Open: %v", err)
	}
	t.Cleanup(func() { st.Close() })
	return st
}

func sampleMemo(id string, recordedAt int64) domain.Memo {
	return domain.Memo{
		ID:         id,
		DeviceID:   "A1B2C3D4E5F6",
		RecordedAt: recordedAt,
		UploadedAt: recordedAt + 5,
		DurationMS: 1000,
		FrameCount: 50,
		SizeBytes:  4096,
		Codec:      "opus",
		Status:     domain.StatusUploaded,
		AudioPath:  "audio/A1B2C3D4E5F6/202610/" + id + ".wav",
	}
}

func TestInsertMemoIsIdempotent(t *testing.T) {
	st := newStore(t)
	ctx := context.Background()
	m := sampleMemo("m1", 1000)

	created, err := st.InsertMemo(ctx, m)
	if err != nil || !created {
		t.Fatalf("first insert: created=%v err=%v", created, err)
	}
	created, err = st.InsertMemo(ctx, m)
	if err != nil {
		t.Fatalf("second insert: %v", err)
	}
	if created {
		t.Fatal("a repeated memo id must not create a second row")
	}

	got, err := st.Memo(ctx, "m1")
	if err != nil {
		t.Fatalf("Memo: %v", err)
	}
	if got.ID != m.ID || got.DeviceID != m.DeviceID {
		t.Errorf("memo = %+v", got)
	}
}

func TestMemoNotFound(t *testing.T) {
	st := newStore(t)
	if _, err := st.Memo(context.Background(), "nope"); !errors.Is(err, ErrNotFound) {
		t.Fatalf("error = %v, want ErrNotFound", err)
	}
}

func TestListMemosOrdersAndPaginates(t *testing.T) {
	st := newStore(t)
	ctx := context.Background()
	for i, ts := range []int64{100, 300, 200} {
		if _, err := st.InsertMemo(ctx, sampleMemo(string(rune('a'+i)), ts)); err != nil {
			t.Fatalf("insert: %v", err)
		}
	}

	page, cursor, err := st.ListMemos(ctx, 2, "", "")
	if err != nil {
		t.Fatalf("ListMemos: %v", err)
	}
	if len(page) != 2 || page[0].RecordedAt != 300 || page[1].RecordedAt != 200 {
		t.Fatalf("first page = %+v", page)
	}
	if cursor == "" {
		t.Fatal("expected a continuation cursor")
	}

	rest, next, err := st.ListMemos(ctx, 2, cursor, "")
	if err != nil {
		t.Fatalf("ListMemos page 2: %v", err)
	}
	if len(rest) != 1 || rest[0].RecordedAt != 100 {
		t.Fatalf("second page = %+v", rest)
	}
	if next != "" {
		t.Errorf("last page should not offer a cursor, got %q", next)
	}
}

func TestListMemosFiltersByDevice(t *testing.T) {
	st := newStore(t)
	ctx := context.Background()

	first := sampleMemo("m1", 100)
	first.DeviceID = "AAA"
	second := sampleMemo("m2", 200)
	second.DeviceID = "BBB"
	for _, m := range []domain.Memo{first, second} {
		if _, err := st.InsertMemo(ctx, m); err != nil {
			t.Fatalf("insert: %v", err)
		}
	}

	page, _, err := st.ListMemos(ctx, 10, "", "AAA")
	if err != nil {
		t.Fatalf("ListMemos: %v", err)
	}
	if len(page) != 1 || page[0].DeviceID != "AAA" {
		t.Fatalf("filtered page = %+v", page)
	}
}

func TestCursorRoundTripAndRejection(t *testing.T) {
	cursor := EncodeCursor(1761463020, "abc")
	ts, id, err := DecodeCursor(cursor)
	if err != nil || ts != 1761463020 || id != "abc" {
		t.Fatalf("decode = %d %q %v", ts, id, err)
	}
	for _, bad := range []string{"!!!!", "bm9jb2xvbg", "MTIzOg"} {
		if _, _, err := DecodeCursor(bad); err == nil {
			t.Errorf("cursor %q should be rejected", bad)
		}
	}
}

func TestTranscriptLifecycle(t *testing.T) {
	st := newStore(t)
	ctx := context.Background()
	m := sampleMemo("m1", 1000)
	if _, err := st.InsertMemo(ctx, m); err != nil {
		t.Fatalf("insert: %v", err)
	}

	if err := st.SetStatus(ctx, "m1", domain.StatusTranscribing, ""); err != nil {
		t.Fatalf("SetStatus: %v", err)
	}
	if err := st.RecordTranscript(ctx, "m1", "下午三点对进度", "zh", "siliconflow", "model", "trace"); err != nil {
		t.Fatalf("RecordTranscript: %v", err)
	}

	got, err := st.Memo(ctx, "m1")
	if err != nil {
		t.Fatalf("Memo: %v", err)
	}
	if got.Status != domain.StatusReady || got.Transcript == "" || got.ASRTraceID != "trace" {
		t.Errorf("memo after transcription = %+v", got)
	}
	if got.Snippet(2) != "下午…" {
		t.Errorf("snippet = %q", got.Snippet(2))
	}
}

func TestSoftDeleteHidesMemoAndDropsJob(t *testing.T) {
	st := newStore(t)
	ctx := context.Background()
	if _, err := st.InsertMemo(ctx, sampleMemo("m1", 1000)); err != nil {
		t.Fatalf("insert: %v", err)
	}
	if err := st.EnqueueJob(ctx, "m1", 1000); err != nil {
		t.Fatalf("enqueue: %v", err)
	}

	path, err := st.SoftDelete(ctx, "m1", 2000)
	if err != nil {
		t.Fatalf("SoftDelete: %v", err)
	}
	if path == "" {
		t.Error("SoftDelete should return the audio path")
	}
	if _, err := st.Memo(ctx, "m1"); !errors.Is(err, ErrNotFound) {
		t.Errorf("deleted memo should be gone, got %v", err)
	}
	page, _, err := st.ListMemos(ctx, 10, "", "")
	if err != nil {
		t.Fatalf("ListMemos: %v", err)
	}
	if len(page) != 0 {
		t.Errorf("deleted memo still listed: %+v", page)
	}
	if depth, err := st.QueueDepth(ctx); err != nil || depth != 0 {
		t.Errorf("queue depth = %d, err = %v", depth, err)
	}
}

func TestJobClaimBackoffAndRecovery(t *testing.T) {
	st := newStore(t)
	ctx := context.Background()
	if _, err := st.InsertMemo(ctx, sampleMemo("m1", 1000)); err != nil {
		t.Fatalf("insert: %v", err)
	}
	if err := st.EnqueueJob(ctx, "m1", 1000); err != nil {
		t.Fatalf("enqueue: %v", err)
	}

	memoID, attempts, ok, err := st.ClaimJob(ctx, 1000)
	if err != nil || !ok || memoID != "m1" || attempts != 1 {
		t.Fatalf("claim = %q %d %v %v", memoID, attempts, ok, err)
	}
	// The claim pushed next_at out, so it is not due again immediately.
	if _, _, ok, err := st.ClaimJob(ctx, 1000); err != nil || ok {
		t.Fatalf("second claim should find nothing: ok=%v err=%v", ok, err)
	}

	if err := st.SetStatus(ctx, "m1", domain.StatusTranscribing, ""); err != nil {
		t.Fatalf("SetStatus: %v", err)
	}
	recovered, err := st.RecoverJobs(ctx, 5000)
	if err != nil {
		t.Fatalf("RecoverJobs: %v", err)
	}
	if recovered != 1 {
		t.Errorf("recovered = %d, want 1", recovered)
	}
	got, err := st.Memo(ctx, "m1")
	if err != nil {
		t.Fatalf("Memo: %v", err)
	}
	if got.Status != domain.StatusUploaded {
		t.Errorf("status after recovery = %q", got.Status)
	}
}

func TestTouchDeviceIsIdempotent(t *testing.T) {
	st := newStore(t)
	ctx := context.Background()
	for i := 0; i < 2; i++ {
		if err := st.TouchDevice(ctx, "A1B2", "Passport", int64(100+i)); err != nil {
			t.Fatalf("TouchDevice: %v", err)
		}
	}
	var count int
	if err := st.DB().QueryRowContext(ctx, `SELECT COUNT(*) FROM devices`).Scan(&count); err != nil {
		t.Fatalf("count: %v", err)
	}
	if count != 1 {
		t.Errorf("device rows = %d, want 1", count)
	}
}
