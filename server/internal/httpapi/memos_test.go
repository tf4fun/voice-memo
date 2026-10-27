package httpapi

import (
	"bytes"
	"encoding/json"
	"log/slog"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"testing"

	"github.com/tf4fun/voice-memo/server/internal/config"
	"github.com/tf4fun/voice-memo/server/internal/store"
)

type testEnv struct {
	handler http.Handler
	store   *store.Store
	dataDir string
}

func newEnv(t *testing.T) *testEnv {
	t.Helper()

	dataDir := t.TempDir()
	st, err := store.Open(dataDir)
	if err != nil {
		t.Fatalf("open store: %v", err)
	}
	t.Cleanup(func() { st.Close() })

	cfg := config.Config{
		Addr:      ":0",
		DataDir:   dataDir,
		LogLevel:  slog.LevelError,
		MaxBytes:  32 << 20,
		MaxMemoMS: 300000,
		ASR:       config.ASRConfig{Provider: "fake", Model: "fake-1", Concurrency: 1},
	}
	logger := slog.New(slog.NewTextHandler(os.Stderr, &slog.HandlerOptions{Level: slog.LevelError}))
	return &testEnv{handler: New(st, cfg, logger), store: st, dataDir: dataDir}
}

// containerWithID returns the fixture container with its header memo id and
// frame count left as-is, only the id replaced.
func containerWithID(t *testing.T, memoID string) []byte {
	t.Helper()
	raw, err := os.ReadFile("../audio/testdata/tone-1s.memo")
	if err != nil {
		t.Fatalf("read fixture: %v", err)
	}
	patched := append([]byte(nil), raw...)
	// The header id field is 36 bytes: a dashed UUID fits exactly.
	copy(patched[24:60], memoID)
	return patched
}

func (e *testEnv) do(t *testing.T, method, path string, body []byte, headers map[string]string) *httptest.ResponseRecorder {
	t.Helper()
	var reader *bytes.Reader
	if body == nil {
		reader = bytes.NewReader(nil)
	} else {
		reader = bytes.NewReader(body)
	}
	req := httptest.NewRequest(method, path, reader)
	for k, v := range headers {
		req.Header.Set(k, v)
	}
	rec := httptest.NewRecorder()
	e.handler.ServeHTTP(rec, req)
	return rec
}

func (e *testEnv) upload(t *testing.T, memoID string) *httptest.ResponseRecorder {
	t.Helper()
	return e.do(t, http.MethodPost, "/api/v1/memos", containerWithID(t, memoID), map[string]string{
		"X-Device-Id": "A1B2C3D4E5F6",
		"X-Memo-Id":   memoID,
	})
}

func decodeBody(t *testing.T, rec *httptest.ResponseRecorder, target any) {
	t.Helper()
	if err := json.Unmarshal(rec.Body.Bytes(), target); err != nil {
		t.Fatalf("decode body %q: %v", rec.Body.String(), err)
	}
}

func TestUploadAndReplay(t *testing.T) {
	env := newEnv(t)
	id := "11111111-1111-4111-8111-111111111111"

	rec := env.upload(t, id)
	if rec.Code != http.StatusCreated {
		t.Fatalf("first upload status = %d, body = %s", rec.Code, rec.Body)
	}

	rec = env.upload(t, id)
	if rec.Code != http.StatusOK {
		t.Fatalf("replay status = %d, want 200, body = %s", rec.Code, rec.Body)
	}

	depth, err := env.store.QueueDepth(t.Context())
	if err != nil {
		t.Fatalf("queue depth: %v", err)
	}
	if depth != 1 {
		t.Errorf("a replay must not enqueue a second job, depth = %d", depth)
	}
}

func TestUploadRequiresDeviceAndValidID(t *testing.T) {
	env := newEnv(t)
	id := "22222222-2222-4222-8222-222222222222"

	rec := env.do(t, http.MethodPost, "/api/v1/memos", containerWithID(t, id), map[string]string{
		"X-Memo-Id": id,
	})
	if rec.Code != http.StatusBadRequest {
		t.Errorf("missing device status = %d, want 400", rec.Code)
	}

	rec = env.do(t, http.MethodPost, "/api/v1/memos", containerWithID(t, id), map[string]string{
		"X-Device-Id": "A1B2",
		"X-Memo-Id":   "not-a-uuid",
	})
	if rec.Code != http.StatusBadRequest {
		t.Errorf("bad memo id status = %d, want 400", rec.Code)
	}
}

func TestUploadRejectsMismatchedHeaderID(t *testing.T) {
	env := newEnv(t)
	body := containerWithID(t, "33333333-3333-4333-8333-333333333333")

	rec := env.do(t, http.MethodPost, "/api/v1/memos", body, map[string]string{
		"X-Device-Id": "A1B2",
		"X-Memo-Id":   "44444444-4444-4444-8444-444444444444",
	})
	if rec.Code != http.StatusBadRequest {
		t.Fatalf("status = %d, want 400", rec.Code)
	}
}

func TestUploadRejectsCorruptContainer(t *testing.T) {
	env := newEnv(t)
	id := "55555555-5555-4555-8555-555555555555"
	body := containerWithID(t, id)
	body[0] = 'X' // break the magic

	rec := env.do(t, http.MethodPost, "/api/v1/memos", body, map[string]string{
		"X-Device-Id": "A1B2",
		"X-Memo-Id":   id,
	})
	if rec.Code != http.StatusBadRequest {
		t.Fatalf("status = %d, want 400, body = %s", rec.Code, rec.Body)
	}
	var errBody struct {
		Error struct {
			Code string `json:"code"`
		} `json:"error"`
	}
	decodeBody(t, rec, &errBody)
	if errBody.Error.Code != "bad_container" {
		t.Errorf("error code = %q", errBody.Error.Code)
	}
}

func TestListDetailPatchDelete(t *testing.T) {
	env := newEnv(t)
	id := "66666666-6666-4666-8666-666666666666"
	if rec := env.upload(t, id); rec.Code != http.StatusCreated {
		t.Fatalf("upload: %d %s", rec.Code, rec.Body)
	}

	rec := env.do(t, http.MethodGet, "/api/v1/memos?limit=10", nil, nil)
	if rec.Code != http.StatusOK {
		t.Fatalf("list status = %d", rec.Code)
	}
	var list struct {
		Items []struct {
			ID      string `json:"id"`
			Status  string `json:"status"`
			Snippet string `json:"snippet"`
		} `json:"items"`
		Next string `json:"next_cursor"`
	}
	decodeBody(t, rec, &list)
	if len(list.Items) != 1 || list.Items[0].ID != id {
		t.Fatalf("list = %+v", list)
	}
	if list.Items[0].Snippet != "" {
		t.Errorf("no transcript yet, snippet = %q", list.Items[0].Snippet)
	}

	rec = env.do(t, http.MethodPatch, "/api/v1/memos/"+id,
		[]byte(`{"transcript":"手动修正的文本","lang":"zh"}`), nil)
	if rec.Code != http.StatusOK {
		t.Fatalf("patch status = %d, body = %s", rec.Code, rec.Body)
	}

	rec = env.do(t, http.MethodGet, "/api/v1/memos/"+id, nil, nil)
	var detail struct {
		Transcript string `json:"transcript"`
		Status     string `json:"status"`
		EditedAt   int64  `json:"edited_at"`
		Audio      *struct {
			Path string `json:"path"`
		} `json:"audio"`
	}
	decodeBody(t, rec, &detail)
	if detail.Transcript != "手动修正的文本" || detail.EditedAt == 0 {
		t.Errorf("detail after patch = %+v", detail)
	}
	if detail.Audio == nil || detail.Audio.Path == "" {
		t.Errorf("detail should advertise the audio path: %+v", detail)
	}

	rec = env.do(t, http.MethodDelete, "/api/v1/memos/"+id, nil, nil)
	if rec.Code != http.StatusNoContent {
		t.Fatalf("delete status = %d", rec.Code)
	}
	rec = env.do(t, http.MethodGet, "/api/v1/memos/"+id, nil, nil)
	if rec.Code != http.StatusNotFound {
		t.Errorf("deleted memo status = %d, want 404", rec.Code)
	}
}

func TestPatchRejectsUnknownFields(t *testing.T) {
	env := newEnv(t)
	id := "77777777-7777-4777-8777-777777777777"
	if rec := env.upload(t, id); rec.Code != http.StatusCreated {
		t.Fatalf("upload: %d", rec.Code)
	}

	rec := env.do(t, http.MethodPatch, "/api/v1/memos/"+id, []byte(`{"titel":"typo"}`), nil)
	if rec.Code != http.StatusBadRequest {
		t.Fatalf("status = %d, want 400", rec.Code)
	}
}

func TestAudioSupportsRange(t *testing.T) {
	env := newEnv(t)
	id := "88888888-8888-4888-8888-888888888888"
	if rec := env.upload(t, id); rec.Code != http.StatusCreated {
		t.Fatalf("upload: %d %s", rec.Code, rec.Body)
	}

	rec := env.do(t, http.MethodGet, "/api/v1/memos/"+id+"/audio", nil, nil)
	if rec.Code != http.StatusOK {
		t.Fatalf("audio status = %d", rec.Code)
	}
	/* 服务端存的是设备上传的 Opus 容器原件：转码只在转写时于内存里发生。 */
	if ct := rec.Header().Get("Content-Type"); ct != "application/octet-stream" {
		t.Errorf("content type = %q", ct)
	}
	body := rec.Body.Bytes()
	if len(body) < 80 || string(body[0:4]) != "MEMO" {
		t.Fatalf("audio is not a memo container: % x", body[:min(12, len(body))])
	}

	req := httptest.NewRequest(http.MethodGet, "/api/v1/memos/"+id+"/audio", nil)
	req.Header.Set("Range", "bytes=0-99")
	rangeRec := httptest.NewRecorder()
	env.handler.ServeHTTP(rangeRec, req)
	if rangeRec.Code != http.StatusPartialContent {
		t.Fatalf("range status = %d, want 206", rangeRec.Code)
	}
	if rangeRec.Body.Len() != 100 {
		t.Errorf("range body = %d bytes, want 100", rangeRec.Body.Len())
	}
}

func TestTranscribeEndpointRequeues(t *testing.T) {
	env := newEnv(t)
	id := "99999999-9999-4999-8999-999999999999"
	if rec := env.upload(t, id); rec.Code != http.StatusCreated {
		t.Fatalf("upload: %d", rec.Code)
	}

	rec := env.do(t, http.MethodPost, "/api/v1/memos/"+id+"/transcribe", nil, nil)
	if rec.Code != http.StatusAccepted {
		t.Fatalf("transcribe status = %d, body = %s", rec.Code, rec.Body)
	}
}

func TestServerInfoAndPing(t *testing.T) {
	env := newEnv(t)

	rec := env.do(t, http.MethodGet, "/api/v1/server", nil, nil)
	if rec.Code != http.StatusOK {
		t.Fatalf("server info status = %d", rec.Code)
	}
	var info struct {
		Service string `json:"service"`
		Limits  struct {
			MaxMemoMS int64 `json:"max_memo_ms"`
			MaxBytes  int64 `json:"max_bytes"`
		} `json:"limits"`
	}
	decodeBody(t, rec, &info)
	if info.Limits.MaxMemoMS != 300000 || info.Limits.MaxBytes != 32<<20 {
		t.Errorf("limits = %+v", info.Limits)
	}

	rec = env.do(t, http.MethodGet, "/api/v1/ping", nil, nil)
	if rec.Code != http.StatusOK {
		t.Errorf("ping status = %d", rec.Code)
	}
}

func TestAudioFilesLiveUnderTheDevice(t *testing.T) {
	env := newEnv(t)
	id := "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"
	if rec := env.upload(t, id); rec.Code != http.StatusCreated {
		t.Fatalf("upload: %d", rec.Code)
	}

	var found bool
	err := filepath.Walk(env.dataDir, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		if !info.IsDir() && filepath.Base(path) == id+".memo" {
			found = true
		}
		return nil
	})
	if err != nil {
		t.Fatalf("walk: %v", err)
	}
	if !found {
		t.Errorf("no %s.memo under %s", id, env.dataDir)
	}
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}
