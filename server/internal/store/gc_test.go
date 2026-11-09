package store

import (
	"context"
	"os"
	"path/filepath"
	"testing"
	"time"
)

func TestGarbageCollectionKeepsReferencedRecentRejectedAndSymlinks(t *testing.T) {
	st := newStore(t)
	old := time.Now().Add(-48 * time.Hour)
	create := func(name string, stale bool) string {
		path := filepath.Join(st.DataDir(), name)
		if err := os.MkdirAll(filepath.Dir(path), 0700); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(path, []byte("preserve"), 0600); err != nil {
			t.Fatal(err)
		}
		if stale {
			if err := os.Chtimes(path, old, old); err != nil {
				t.Fatal(err)
			}
		}
		return path
	}
	referenced := create("audio/device/live.memo", true)
	tombstone := create("audio/device/deleted.memo", true)
	orphan := create("audio/device/orphan.memo", true)
	partial := create("incoming/stale.part", true)
	recent := create("incoming/active.part", false)
	newAudio := create("audio/device/new.memo", false)
	rejected := create("rejected/diagnostic.memo", true)
	unknown := create("audio/device/unknown.wav", true)
	for id, path := range map[string]string{"live": referenced, "deleted": tombstone} {
		memo := sampleMemo(id, 1000)
		memo.AudioPath = path
		if _, err := st.InsertMemo(t.Context(), memo); err != nil {
			t.Fatal(err)
		}
	}
	if _, err := st.SoftDelete(t.Context(), "deleted", 2000); err != nil {
		t.Fatal(err)
	}
	external := filepath.Join(t.TempDir(), "outside.memo")
	if err := os.WriteFile(external, []byte("outside"), 0600); err != nil {
		t.Fatal(err)
	}
	if err := os.Symlink(filepath.Dir(external), filepath.Join(st.DataDir(), "audio", "link")); err != nil {
		t.Fatal(err)
	}
	cutoff := time.Now().Add(-25 * time.Hour)
	files, err := st.GarbageCollect(t.Context(), cutoff, false)
	if err != nil || len(files) != 2 {
		t.Fatalf("preview: %v %v", files, err)
	}
	for _, path := range []string{orphan, partial} {
		if _, err := os.Stat(path); err != nil {
			t.Fatal("preview deleted file", err)
		}
	}
	files, err = st.GarbageCollect(t.Context(), cutoff, true)
	if err != nil || len(files) != 2 {
		t.Fatalf("apply: %v %v", files, err)
	}
	for _, path := range []string{orphan, partial} {
		if _, err := os.Stat(path); !os.IsNotExist(err) {
			t.Fatal("candidate survived", path, err)
		}
	}
	for _, path := range []string{referenced, tombstone, recent, newAudio, rejected, unknown, external} {
		if _, err := os.Stat(path); err != nil {
			t.Fatal("protected file removed", path, err)
		}
	}
	if _, err := st.GarbageCollect(context.Background(), time.Now(), true); err == nil {
		t.Fatal("unsafe cutoff accepted")
	}
}
