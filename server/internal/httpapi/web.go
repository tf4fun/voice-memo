package httpapi

import (
	"bytes"
	"context"
	"embed"
	"errors"
	"fmt"
	"html/template"
	"io"
	"net/http"
	"net/url"
	"os"
	"time"

	"github.com/tf4fun/voice-memo/server/internal/audio"
	"github.com/tf4fun/voice-memo/server/internal/domain"
	"github.com/tf4fun/voice-memo/server/internal/store"
)

//go:embed web/*
var webFiles embed.FS

var webTemplate = template.Must(template.New("index.html").Funcs(template.FuncMap{
	"status": func(status string) string {
		switch status {
		case domain.StatusUploaded:
			return "等待转写"
		case domain.StatusTranscribing:
			return "转写中"
		case domain.StatusReady:
			return "已转写"
		default:
			return "转写失败"
		}
	},
	"clock":    func(ts int64) string { return time.Unix(ts, 0).UTC().Format("2006-01-02 15:04 UTC") },
	"datetime": func(ts int64) string { return time.Unix(ts, 0).UTC().Format(time.RFC3339) },
	"duration": func(ms int64) string { return fmt.Sprintf("%d:%02d", ms/60000, ms/1000%60) },
	"snippet":  func(m domain.Memo) string { return m.Snippet(90) },
	"number":   func(i int) string { return fmt.Sprintf("%02d", i+1) },
}).ParseFS(webFiles, "web/index.html"))

type webPage struct {
	Items        []domain.Memo
	Detail       *domain.Memo
	Device       string
	Newer, Older string
	Error        string
}

func renderWeb(w http.ResponseWriter, status int, data webPage) {
	var buf bytes.Buffer
	if err := webTemplate.Execute(&buf, data); err != nil {
		http.Error(w, "页面暂时无法显示", http.StatusInternalServerError)
		return
	}
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.Header().Set("Cache-Control", "no-store")
	w.Header().Set("X-Content-Type-Options", "nosniff")
	w.Header().Set("Content-Security-Policy", "default-src 'self'; script-src 'self'; style-src 'self'; media-src 'self'; img-src 'self' data:; object-src 'none'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'")
	w.WriteHeader(status)
	_, _ = w.Write(buf.Bytes())
}

func (s *Server) handleWebList(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	device, cursor, direction := q.Get("device"), q.Get("cursor"), q.Get("direction")
	if (device != "" && !deviceIDPattern.MatchString(device)) || len(cursor) >= 128 ||
		(direction != "" && direction != "older" && direction != "newer") {
		renderWeb(w, http.StatusBadRequest, webPage{Error: "筛选或分页参数无效，请返回录音档案重试。"})
		return
	}
	if cursor != "" {
		if _, _, err := store.DecodeCursor(cursor); err != nil {
			renderWeb(w, http.StatusBadRequest, webPage{Error: "分页位置无效，请返回录音档案重试。"})
			return
		}
	}
	if direction == "newer" && cursor == "" {
		renderWeb(w, http.StatusBadRequest, webPage{Error: "分页位置无效，请返回录音档案重试。"})
		return
	}
	page, err := s.store.ListMemoPage(r.Context(), 20, cursor, device, direction)
	if err != nil {
		s.log.Error("web: list recordings", "error", err)
		renderWeb(w, http.StatusInternalServerError, webPage{Error: "录音读取失败，请稍后重试。"})
		return
	}
	link := func(cursor, direction string) string {
		if cursor == "" {
			return ""
		}
		query := url.Values{"cursor": {cursor}, "direction": {direction}}
		if device != "" {
			query.Set("device", device)
		}
		return "/?" + query.Encode()
	}
	renderWeb(w, http.StatusOK, webPage{Items: page.Items, Device: device,
		Newer: link(page.Previous, "newer"), Older: link(page.Next, "older")})
}

func (s *Server) handleWebDetail(w http.ResponseWriter, r *http.Request) {
	m, err := s.store.Memo(r.Context(), r.PathValue("id"))
	if errors.Is(err, store.ErrNotFound) {
		renderWeb(w, http.StatusNotFound, webPage{Error: "这条录音不存在，或已被删除。"})
		return
	}
	if err != nil {
		renderWeb(w, http.StatusInternalServerError, webPage{Error: "录音读取失败，请稍后重试。"})
		return
	}
	renderWeb(w, http.StatusOK, webPage{Detail: &m})
}

func handleWebAsset(w http.ResponseWriter, r *http.Request) {
	name := r.PathValue("file")
	types := map[string]string{"app.css": "text/css; charset=utf-8", "app.js": "text/javascript; charset=utf-8"}
	contentType, ok := types[name]
	if !ok {
		http.NotFound(w, r)
		return
	}
	data, err := webFiles.ReadFile("web/" + name)
	if err != nil {
		http.NotFound(w, r)
		return
	}
	w.Header().Set("Content-Type", contentType)
	w.Header().Set("X-Content-Type-Options", "nosniff")
	w.Header().Set("Cache-Control", "no-cache")
	http.ServeContent(w, r, name, time.Time{}, bytes.NewReader(data))
}

// WAV previews use a temporary file instead of retaining PCM in RAM. At most
// one preview is decoded/served at once; firmware's original audio stays Opus.
func (s *Server) handleWebAudio(w http.ResponseWriter, r *http.Request) {
	m, err := s.store.Memo(r.Context(), r.PathValue("id"))
	if errors.Is(err, store.ErrNotFound) {
		writeError(w, http.StatusNotFound, "not_found", "unknown memo")
		return
	}
	if err != nil {
		writeError(w, 500, "internal", "query failed")
		return
	}
	select {
	case s.wavSlot <- struct{}{}:
		defer func() { <-s.wavSlot }()
	default:
		w.Header().Set("Retry-After", "2")
		writeError(w, http.StatusServiceUnavailable, "busy", "another audio preview is active; retry shortly")
		return
	}
	in, err := os.Open(m.AudioPath)
	if err != nil {
		writeError(w, 404, "not_found", "audio is missing")
		return
	}
	defer in.Close()
	header, err := domain.ParseHeader(in)
	durationMS := int64(header.FrameCount) * int64(header.FrameMS)
	if err != nil || !header.MatchesDuration() || int64(header.DurationMS) > s.cfg.MaxMemoMS || durationMS > s.cfg.MaxMemoMS {
		writeError(w, 422, "bad_container", "audio cannot be decoded")
		return
	}
	if _, err := in.Seek(0, io.SeekStart); err != nil {
		writeError(w, 500, "internal", "audio seek failed")
		return
	}
	out, err := os.CreateTemp("", "memo-preview-*.wav")
	if err != nil {
		writeError(w, 500, "internal", "temporary audio unavailable")
		return
	}
	defer os.Remove(out.Name())
	defer out.Close()
	// A mismatched/corrupt stream cannot expand beyond its declared PCM size.
	limit := int64(header.SampleRate)*int64(header.Channels)*2*durationMS/1000 + 44
	stats, err := audio.DecodeOpusToWAV(io.LimitReader(in, s.cfg.MaxBytes+1), &webWAVWriter{ctx: r.Context(), file: out, limit: limit})
	if err != nil || stats.WAVBytes != limit {
		writeError(w, http.StatusUnprocessableEntity, "bad_container", "audio cannot be decoded")
		return
	}
	w.Header().Set("Content-Type", "audio/wav")
	w.Header().Set("Cache-Control", "no-store")
	w.Header().Set("X-Content-Type-Options", "nosniff")
	http.ServeContent(w, r, m.ID+".wav", time.Unix(m.RecordedAt, 0), out)
}

type webWAVWriter struct {
	ctx   context.Context
	file  *os.File
	limit int64
}

func (w *webWAVWriter) Write(p []byte) (int, error) {
	if err := w.ctx.Err(); err != nil {
		return 0, err
	}
	offset, err := w.file.Seek(0, io.SeekCurrent)
	if err != nil {
		return 0, err
	}
	if int64(len(p)) > w.limit-offset {
		return 0, fmt.Errorf("decoded audio exceeds declared size")
	}
	return w.file.Write(p)
}

func (w *webWAVWriter) Seek(offset int64, whence int) (int64, error) {
	return w.file.Seek(offset, whence)
}
