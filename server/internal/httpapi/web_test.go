package httpapi

import (
	"bytes"
	"context"
	"encoding/binary"
	"fmt"
	"html"
	"net/http/httptest"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"testing"

	"github.com/tf4fun/voice-memo/server/internal/config"
)

func TestWebListPaginationFilterAndEscaping(t *testing.T) {
	e := newEnv(t)
	get := func(path string) string {
		t.Helper()
		r := e.do(t, "GET", path, nil, nil)
		if r.Code != 200 || !strings.HasPrefix(r.Header().Get("Content-Type"), "text/html") {
			t.Fatalf("GET %s: %d %s", path, r.Code, r.Body.String())
		}
		if r.Header().Get("Cache-Control") != "no-store" || r.Header().Get("Content-Security-Policy") == "" {
			t.Fatal("missing private-page headers")
		}
		return r.Body.String()
	}
	if !strings.Contains(get("/"), "还没有录音") {
		t.Fatal("missing empty state")
	}
	for i := 1; i <= 21; i++ {
		id := fmt.Sprintf("00000000-0000-4000-8000-%012d", i)
		if r := e.upload(t, id); r.Code != 201 {
			t.Fatalf("upload: %d", r.Code)
		}
		if err := e.store.UpdateTranscript(context.Background(), id, "灵感 </textarea><script>alert(1)</script>", "zh", 1); err != nil {
			t.Fatal(err)
		}
	}
	body := get("/?device=A1B2C3D4E5F6")
	if strings.Count(body, `class="memo-card"`) != 20 || strings.Contains(body, "<script>alert") || !strings.Contains(body, "&lt;script&gt;") {
		t.Fatal("list bound or HTML escaping failed")
	}
	links := regexp.MustCompile(`href="([^"]+)">较早录音`).FindStringSubmatch(body)
	if len(links) != 2 {
		t.Fatal("missing older link")
	}
	older := html.UnescapeString(links[1])
	if !strings.Contains(older, "device=A1B2C3D4E5F6") {
		t.Fatal("filter lost on pagination")
	}
	last := get(older)
	if strings.Count(last, `class="memo-card"`) != 1 || !strings.Contains(last, "较新录音") {
		t.Fatal("last page or reverse navigation missing")
	}
	if !strings.Contains(get("/?device=other-device"), "这台设备暂无录音") {
		t.Fatal("device filtering failed")
	}
	for _, path := range []string{"/?cursor=invalid!", "/?direction=newer", "/?direction=sideways", "/?device=bad%20device"} {
		if r := e.do(t, "GET", path, nil, nil); r.Code != 400 {
			t.Errorf("%s: %d", path, r.Code)
		}
	}
	detail := get("/memos/00000000-0000-4000-8000-000000000001")
	if strings.Contains(detail, "</textarea><script>") || !strings.Contains(detail, "&lt;/textarea&gt;") || !strings.Contains(detail, "audio.wav") {
		t.Fatal("detail escaping or playback URL failed")
	}
	if r := e.do(t, "GET", "/memos/missing", nil, nil); r.Code != 404 {
		t.Fatal("missing detail not 404")
	}
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	req := httptest.NewRequest("GET", "/", nil).WithContext(ctx)
	rec := httptest.NewRecorder()
	e.handler.ServeHTTP(rec, req)
	if rec.Code != 500 || !strings.Contains(rec.Body.String(), "录音读取失败") {
		t.Fatal("database failure not rendered")
	}
}

func TestWebAssetsAndBrowserOriginProtection(t *testing.T) {
	e := newEnv(t)
	for _, file := range []string{"app.css", "app.js"} {
		r := e.do(t, "GET", "/assets/"+file, nil, nil)
		if r.Code != 200 || r.Body.Len() == 0 || r.Header().Get("X-Content-Type-Options") != "nosniff" {
			t.Fatalf("asset %s unavailable", file)
		}
	}
	if r := e.do(t, "GET", "/assets/index.html", nil, nil); r.Code != 404 {
		t.Fatal("template exposed as asset")
	}
	id := "00000000-0000-4000-8000-000000000001"
	e.upload(t, id)
	for _, headers := range []map[string]string{{"Origin": "https://other.example"}, {"Sec-Fetch-Site": "cross-site"}} {
		r := e.do(t, "DELETE", "/api/v1/memos/"+id, nil, headers)
		if r.Code != 403 {
			t.Fatalf("cross-site mutation: %d", r.Code)
		}
	}
	if _, err := e.store.Memo(context.Background(), id); err != nil {
		t.Fatal("cross-site request deleted memo")
	}
	r := e.do(t, "PATCH", "/api/v1/memos/"+id, []byte(`{"transcript":"已修改"}`), map[string]string{"Sec-Fetch-Site": "same-origin"})
	if r.Code != 200 {
		t.Fatalf("same-origin edit: %d", r.Code)
	}
	r = e.do(t, "DELETE", "/api/v1/memos/"+id, nil, nil)
	if r.Code != 204 {
		t.Fatalf("device request without browser headers: %d", r.Code)
	}
}

func TestWebWAVPlaybackRangesAndCleanup(t *testing.T) {
	tmp := t.TempDir()
	t.Setenv("TMPDIR", tmp)
	e := newEnv(t)
	id := "00000000-0000-4000-8000-000000000001"
	e.upload(t, id)
	path := "/api/v1/memos/" + id + "/audio.wav"
	r := e.do(t, "GET", path, nil, nil)
	// The fixture has 51 twenty-millisecond frames at 16 kHz mono.
	if r.Code != 200 || r.Header().Get("Content-Type") != "audio/wav" || r.Body.Len() != 32684 || !bytes.HasPrefix(r.Body.Bytes(), []byte("RIFF")) {
		t.Fatalf("WAV: %d, bytes=%d", r.Code, r.Body.Len())
	}
	rangeResponse := e.do(t, "GET", path, nil, map[string]string{"Range": "bytes=0-43"})
	if rangeResponse.Code != 206 || !bytes.Equal(rangeResponse.Body.Bytes(), r.Body.Bytes()[:44]) {
		t.Fatal("WAV range failed")
	}
	head := e.do(t, "HEAD", path, nil, nil)
	if head.Code != 200 || head.Body.Len() != 0 || head.Header().Get("Content-Length") != "32684" {
		t.Fatal("WAV HEAD failed")
	}
	original := e.do(t, "GET", "/api/v1/memos/"+id+"/audio", nil, nil)
	if original.Code != 200 || !bytes.HasPrefix(original.Body.Bytes(), []byte("MEMO")) {
		t.Fatal("firmware audio changed")
	}
	if files, _ := filepath.Glob(filepath.Join(tmp, "memo-preview-*")); len(files) != 0 {
		t.Fatal("temporary WAV leaked")
	}
	m, err := e.store.Memo(context.Background(), id)
	if err != nil {
		t.Fatal(err)
	}
	// Uploads allow a 5% declared-duration tolerance. Playback must use the
	// frame-derived size, so a valid rounded header is still playable.
	raw := containerWithID(t, id)
	binary.LittleEndian.PutUint32(raw[12:16], 1040)
	if err := os.WriteFile(m.AudioPath, raw, 0600); err != nil {
		t.Fatal(err)
	}
	if r := e.do(t, "GET", path, nil, nil); r.Code != 200 || r.Body.Len() != 32684 {
		t.Fatal("valid duration tolerance rejected")
	}
	if err := os.WriteFile(m.AudioPath, []byte("corrupt"), 0600); err != nil {
		t.Fatal(err)
	}
	if r := e.do(t, "GET", path, nil, nil); r.Code != 422 {
		t.Fatalf("corrupt audio: %d", r.Code)
	}
	if r := e.do(t, "GET", "/api/v1/memos/missing/audio.wav", nil, nil); r.Code != 404 {
		t.Fatalf("missing audio: %d", r.Code)
	}
}

func TestWebWAVResourceBounds(t *testing.T) {
	e := newEnv(t)
	id := "00000000-0000-4000-8000-000000000001"
	e.upload(t, id)
	s := &Server{store: e.store, cfg: config.Config{MaxMemoMS: 300000, MaxBytes: 32 << 20}, wavSlot: make(chan struct{}, 1)}
	s.wavSlot <- struct{}{}
	r := httptest.NewRequest("GET", "/", nil)
	r.SetPathValue("id", id)
	w := httptest.NewRecorder()
	s.handleWebAudio(w, r)
	if w.Code != 503 || w.Header().Get("Retry-After") != "2" {
		t.Fatal("concurrent conversion not bounded")
	}
	<-s.wavSlot
	f, err := os.CreateTemp(t.TempDir(), "bounded")
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	ctx, cancel := context.WithCancel(context.Background())
	writer := &webWAVWriter{ctx: ctx, file: f, limit: 4}
	if _, err := writer.Write([]byte("12345")); err == nil {
		t.Fatal("PCM limit ignored")
	}
	if _, err := writer.Write([]byte("1234")); err != nil {
		t.Fatal(err)
	}
	if _, err := writer.Seek(0, 0); err != nil {
		t.Fatal(err)
	}
	cancel()
	if _, err := writer.Write([]byte("1")); err == nil {
		t.Fatal("cancelled conversion continued")
	}
	cancelled := httptest.NewRecorder()
	s.handleWebAudio(cancelled, r.WithContext(ctx))
	if cancelled.Code < 400 || len(s.wavSlot) != 0 {
		t.Fatal("cancelled conversion retained slot")
	}
}
