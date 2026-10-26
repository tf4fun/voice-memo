package store

import (
	"context"
	"os"
	"path/filepath"
	"testing"
)

func TestOpenCreatesLayoutAndSchema(t *testing.T) {
	dir := t.TempDir()

	st, err := Open(dir)
	if err != nil {
		t.Fatalf("Open: %v", err)
	}
	defer st.Close()

	for _, sub := range []string{"audio", "incoming", "rejected", "memo.db"} {
		if _, err := os.Stat(filepath.Join(dir, sub)); err != nil {
			t.Errorf("expected %s to exist: %v", sub, err)
		}
	}

	version, err := st.SchemaVersion(context.Background())
	if err != nil {
		t.Fatalf("SchemaVersion: %v", err)
	}
	if version != schemaVersion {
		t.Errorf("schema version = %d, want %d", version, schemaVersion)
	}
}

func TestOpenIsIdempotent(t *testing.T) {
	dir := t.TempDir()

	first, err := Open(dir)
	if err != nil {
		t.Fatalf("first Open: %v", err)
	}
	if err := first.Close(); err != nil {
		t.Fatalf("close: %v", err)
	}

	second, err := Open(dir)
	if err != nil {
		t.Fatalf("second Open: %v", err)
	}
	defer second.Close()

	if err := second.Ping(context.Background()); err != nil {
		t.Errorf("Ping after reopen: %v", err)
	}
}

func TestTablesExist(t *testing.T) {
	st, err := Open(t.TempDir())
	if err != nil {
		t.Fatalf("Open: %v", err)
	}
	defer st.Close()

	ctx := context.Background()
	for _, table := range []string{"devices", "memos", "jobs"} {
		var name string
		err := st.DB().QueryRowContext(ctx,
			"SELECT name FROM sqlite_master WHERE type='table' AND name=?", table).Scan(&name)
		if err != nil {
			t.Errorf("table %s missing: %v", table, err)
		}
	}
}

func TestCloseTwiceIsSafe(t *testing.T) {
	st, err := Open(t.TempDir())
	if err != nil {
		t.Fatalf("Open: %v", err)
	}
	if err := st.Close(); err != nil {
		t.Fatalf("first Close: %v", err)
	}
	if err := st.Close(); err != nil {
		t.Fatalf("second Close: %v", err)
	}
}
