package httpapi

import (
	"encoding/json"
	"log/slog"
	"net/http"
	"net/http/httptest"
	"os"
	"testing"

	"github.com/tf4fun/voice-memo/server/internal/config"
	"github.com/tf4fun/voice-memo/server/internal/store"
)

func newTestServer(t *testing.T) (http.Handler, *store.Store) {
	t.Helper()

	st, err := store.Open(t.TempDir())
	if err != nil {
		t.Fatalf("open store: %v", err)
	}
	t.Cleanup(func() { st.Close() })

	cfg := config.Config{
		Addr:      ":0",
		DataDir:   t.TempDir(),
		LogLevel:  slog.LevelError,
		MaxBytes:  32 << 20,
		MaxMemoMS: 300000,
		ASR:       config.ASRConfig{Provider: "fake", Concurrency: 1},
	}
	logger := slog.New(slog.NewTextHandler(os.Stderr, &slog.HandlerOptions{Level: slog.LevelError}))
	return New(st, cfg, logger), st
}

func TestHealthReportsOK(t *testing.T) {
	handler, _ := newTestServer(t)

	req := httptest.NewRequest(http.MethodGet, "/api/v1/health", nil)
	rec := httptest.NewRecorder()
	handler.ServeHTTP(rec, req)

	if rec.Code != http.StatusOK {
		t.Fatalf("status = %d, want 200", rec.Code)
	}
	if ct := rec.Header().Get("Content-Type"); ct != "application/json; charset=utf-8" {
		t.Errorf("Content-Type = %q", ct)
	}

	var body healthResponse
	if err := json.Unmarshal(rec.Body.Bytes(), &body); err != nil {
		t.Fatalf("decode body: %v", err)
	}
	if body.Service != "memo-server" {
		t.Errorf("service = %q", body.Service)
	}
	if body.Version != Version {
		t.Errorf("version = %q, want %q", body.Version, Version)
	}
	if body.DB != "ok" {
		t.Errorf("db = %q", body.DB)
	}
	if body.Schema != 1 {
		t.Errorf("schema = %d, want 1", body.Schema)
	}
	if body.Time == 0 {
		t.Error("time should be set")
	}
}

func TestHealthFailsWhenDatabaseIsClosed(t *testing.T) {
	handler, st := newTestServer(t)
	if err := st.Close(); err != nil {
		t.Fatalf("close store: %v", err)
	}

	req := httptest.NewRequest(http.MethodGet, "/api/v1/health", nil)
	rec := httptest.NewRecorder()
	handler.ServeHTTP(rec, req)

	if rec.Code != http.StatusServiceUnavailable {
		t.Fatalf("status = %d, want 503", rec.Code)
	}
}

func TestHealthRejectsOtherMethods(t *testing.T) {
	handler, _ := newTestServer(t)

	req := httptest.NewRequest(http.MethodPost, "/api/v1/health", nil)
	rec := httptest.NewRecorder()
	handler.ServeHTTP(rec, req)

	if rec.Code != http.StatusMethodNotAllowed {
		t.Fatalf("status = %d, want 405", rec.Code)
	}
}

func TestUnknownPathIs404(t *testing.T) {
	handler, _ := newTestServer(t)

	req := httptest.NewRequest(http.MethodGet, "/nope", nil)
	rec := httptest.NewRecorder()
	handler.ServeHTTP(rec, req)

	if rec.Code != http.StatusNotFound {
		t.Fatalf("status = %d, want 404", rec.Code)
	}
}
