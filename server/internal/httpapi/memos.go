package httpapi

import (
	"context"
	"encoding/json"
	"errors"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"time"

	"github.com/tf4fun/voice-memo/server/internal/audio"
	"github.com/tf4fun/voice-memo/server/internal/domain"
	"github.com/tf4fun/voice-memo/server/internal/events"
	"github.com/tf4fun/voice-memo/server/internal/store"
)

var memoIDPattern = regexp.MustCompile(`^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$`)

// memoJSON is the wire shape of one memo.
type memoJSON struct {
	ID         string     `json:"id"`
	DeviceID   string     `json:"device_id"`
	RecordedAt int64      `json:"recorded_at"`
	UploadedAt int64      `json:"uploaded_at"`
	DurationMS int64      `json:"duration_ms"`
	SizeBytes  int64      `json:"size_bytes,omitempty"`
	Codec      string     `json:"codec,omitempty"`
	Status     string     `json:"status"`
	Lang       string     `json:"lang,omitempty"`
	Snippet    string     `json:"snippet,omitempty"`
	Transcript string     `json:"transcript,omitempty"`
	EditedAt   int64      `json:"edited_at,omitempty"`
	Error      string     `json:"error,omitempty"`
	ASR        *asrJSON   `json:"asr,omitempty"`
	Audio      *audioJSON `json:"audio,omitempty"`
}

type asrJSON struct {
	Provider string `json:"provider"`
	Model    string `json:"model"`
	TraceID  string `json:"trace_id,omitempty"`
}

type audioJSON struct {
	Path  string `json:"path"`
	Bytes int64  `json:"bytes"`
}

func detailJSON(m domain.Memo) memoJSON {
	out := memoJSON{
		ID:         m.ID,
		DeviceID:   m.DeviceID,
		RecordedAt: m.RecordedAt,
		UploadedAt: m.UploadedAt,
		DurationMS: m.DurationMS,
		SizeBytes:  m.SizeBytes,
		Codec:      m.Codec,
		Status:     m.Status,
		Lang:       m.Lang,
		Transcript: m.Transcript,
		EditedAt:   m.EditedAt,
		Error:      m.Error,
	}
	if m.ASRProvider != "" {
		out.ASR = &asrJSON{Provider: m.ASRProvider, Model: m.ASRModel, TraceID: m.ASRTraceID}
	}
	if m.AudioPath != "" {
		out.Audio = &audioJSON{
			Path:  "/api/v1/memos/" + m.ID + "/audio",
			Bytes: fileSize(m.AudioPath),
		}
	}
	return out
}

func listJSON(m domain.Memo) memoJSON {
	return memoJSON{
		ID:         m.ID,
		DeviceID:   m.DeviceID,
		RecordedAt: m.RecordedAt,
		UploadedAt: m.UploadedAt,
		DurationMS: m.DurationMS,
		Status:     m.Status,
		Lang:       m.Lang,
		Snippet:    m.Snippet(40),
	}
}

func fileSize(path string) int64 {
	info, err := os.Stat(path)
	if err != nil {
		return 0
	}
	return info.Size()
}

// handlePing is the cheap reachability probe used after discovery.
func (s *Server) handlePing(w http.ResponseWriter, _ *http.Request) {
	writeJSON(w, http.StatusOK, map[string]any{"service": "memo", "api": 1})
}

// handleServerInfo advertises the limits and the clock, so the device does not
// hard-code them.
func (s *Server) handleServerInfo(w http.ResponseWriter, _ *http.Request) {
	depth, _ := s.store.QueueDepth(context.Background())
	writeJSON(w, http.StatusOK, map[string]any{
		"service":     "memo-server",
		"version":     Version,
		"api":         1,
		"server_time": time.Now().Unix(),
		"asr": map[string]string{
			"provider": s.cfg.ASR.Provider,
			"model":    s.cfg.ASR.Model,
		},
		"limits": map[string]int64{
			"max_memo_ms": s.cfg.MaxMemoMS,
			"max_bytes":   s.cfg.MaxBytes,
		},
		"queue_depth": depth,
	})
}

// handleUpload accepts one finished recording.
func (s *Server) handleUpload(w http.ResponseWriter, r *http.Request) {
	deviceID := strings.TrimSpace(r.Header.Get("X-Device-Id"))
	if deviceID == "" {
		writeError(w, http.StatusBadRequest, "missing_device", "X-Device-Id is required")
		return
	}
	memoID := strings.TrimSpace(r.Header.Get("X-Memo-Id"))
	if !memoIDPattern.MatchString(memoID) {
		writeError(w, http.StatusBadRequest, "bad_container", "X-Memo-Id must be a UUID")
		return
	}

	ctx := r.Context()
	if existing, err := s.store.Memo(ctx, memoID); err == nil {
		// Idempotent replay: the device can retry bluntly after a timeout.
		writeJSON(w, http.StatusOK, map[string]any{
			"id":          existing.ID,
			"status":      existing.Status,
			"duration_ms": existing.DurationMS,
		})
		return
	} else if !errors.Is(err, store.ErrNotFound) {
		s.log.Error("upload: lookup memo", "error", err)
		writeError(w, http.StatusInternalServerError, "internal", "lookup failed")
		return
	}

	if err := os.MkdirAll(s.cfg.IncomingDir(), 0o755); err != nil {
		s.log.Error("upload: create incoming dir", "error", err)
		writeError(w, http.StatusInternalServerError, "internal", "storage unavailable")
		return
	}

	body := http.MaxBytesReader(w, r.Body, s.cfg.MaxBytes)
	partPath := filepath.Join(s.cfg.IncomingDir(), memoID+".part")
	part, err := os.Create(partPath)
	if err != nil {
		s.log.Error("upload: create part file", "error", err)
		writeError(w, http.StatusInternalServerError, "internal", "storage unavailable")
		return
	}
	written, copyErr := io.Copy(part, body)
	closeErr := part.Close()
	defer os.Remove(partPath)

	if copyErr != nil {
		var tooBig *http.MaxBytesError
		if errors.As(copyErr, &tooBig) {
			writeError(w, http.StatusRequestEntityTooLarge, "payload_too_large",
				"upload exceeds the configured limit")
			return
		}
		writeError(w, http.StatusBadRequest, "bad_container", "upload was interrupted")
		return
	}
	if closeErr != nil {
		s.log.Error("upload: close part file", "error", closeErr)
		writeError(w, http.StatusInternalServerError, "internal", "storage unavailable")
		return
	}

	in, err := os.Open(partPath)
	if err != nil {
		s.log.Error("upload: reopen part file", "error", err)
		writeError(w, http.StatusInternalServerError, "internal", "storage unavailable")
		return
	}
	header, err := domain.ParseHeader(in)
	in.Close()
	if err != nil {
		writeContainerError(w, err)
		return
	}
	if header.MemoID != memoID {
		writeError(w, http.StatusBadRequest, "bad_container",
			"X-Memo-Id does not match the container header")
		return
	}
	if int64(header.DurationMS) > s.cfg.MaxMemoMS {
		writeError(w, http.StatusBadRequest, "over_limit", "memo is longer than the addressable limit")
		return
	}

	now := time.Now().Unix()
	recordedAt := header.RecordedAt
	skew := int64(0)
	if recordedAt == 0 || abs(now-recordedAt) > 600 {
		skew = recordedAt - now
		recordedAt = now
	}

	/* 存原件（Opus 容器）：设备端能直接播放，转写时再按需在内存里解码成 WAV。 */
	audioPath := filepath.Join(s.cfg.AudioDir(), deviceID,
		time.Unix(recordedAt, 0).UTC().Format("200601"), memoID+".memo")
	if err := os.MkdirAll(filepath.Dir(audioPath), 0o755); err != nil {
		s.log.Error("upload: create audio dir", "error", err)
		writeError(w, http.StatusInternalServerError, "internal", "storage unavailable")
		return
	}

	in, err = os.Open(partPath)
	if err != nil {
		s.log.Error("upload: reopen part file", "error", err)
		writeError(w, http.StatusInternalServerError, "internal", "storage unavailable")
		return
	}
	defer in.Close()

	stats, validateErr := audio.ValidateContainer(in)
	if validateErr != nil {
		s.keepRejected(partPath, memoID)
		writeContainerError(w, validateErr)
		return
	}

	/* 校验通过后把临时文件挪到正式位置，内容原样保留。 */
	if err := os.Rename(partPath, audioPath); err != nil {
		s.log.Error("upload: move audio into place", "error", err)
		writeError(w, http.StatusInternalServerError, "internal", "storage unavailable")
		return
	}

	if err := s.store.TouchDevice(ctx, deviceID, "", now); err != nil {
		s.log.Error("upload: touch device", "error", err)
	}

	memo := domain.Memo{
		ID:         memoID,
		DeviceID:   deviceID,
		RecordedAt: recordedAt,
		UploadedAt: now,
		DurationMS: int64(header.DurationMS),
		FrameCount: int64(stats.Frames),
		SizeBytes:  written,
		Codec:      "opus",
		Status:     domain.StatusUploaded,
		AudioPath:  audioPath,
		ClockSkew:  skew,
	}
	created, err := s.store.InsertMemo(ctx, memo)
	if err != nil {
		os.Remove(audioPath)
		s.log.Error("upload: insert memo", "error", err)
		writeError(w, http.StatusInternalServerError, "internal", "storage unavailable")
		return
	}
	if !created {
		os.Remove(audioPath)
		existing, err := s.store.Memo(ctx, memoID)
		if err != nil {
			writeError(w, http.StatusInternalServerError, "internal", "lookup failed")
			return
		}
		writeJSON(w, http.StatusOK, map[string]any{
			"id": existing.ID, "status": existing.Status, "duration_ms": existing.DurationMS,
		})
		return
	}

	if err := s.store.EnqueueJob(ctx, memoID, now); err != nil {
		s.log.Error("upload: enqueue job", "error", err)
	}

	s.events.Publish(events.Event{
		Type:       "memo",
		ID:         memo.ID,
		Status:     memo.Status,
		RecordedAt: memo.RecordedAt,
		DurationMS: memo.DurationMS,
	})

	writeJSON(w, http.StatusCreated, map[string]any{
		"id":          memoID,
		"status":      domain.StatusUploaded,
		"duration_ms": memo.DurationMS,
	})
}

// keepRejected preserves a container that failed to decode, for diagnosis.
func (s *Server) keepRejected(partPath, memoID string) {
	dir := filepath.Join(s.cfg.DataDir, "rejected")
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return
	}
	_ = os.Rename(partPath, filepath.Join(dir, memoID+".memo"))
}

func (s *Server) handleList(w http.ResponseWriter, r *http.Request) {
	limit := 20
	if raw := r.URL.Query().Get("limit"); raw != "" {
		parsed, err := strconv.Atoi(raw)
		if err != nil || parsed <= 0 {
			writeError(w, http.StatusBadRequest, "invalid_json", "limit must be a positive integer")
			return
		}
		limit = parsed
	}

	memos, next, err := s.store.ListMemos(r.Context(), limit,
		r.URL.Query().Get("cursor"), r.URL.Query().Get("device"))
	if err != nil {
		if strings.Contains(err.Error(), "invalid cursor") {
			writeError(w, http.StatusBadRequest, "invalid_json", "invalid cursor")
			return
		}
		s.log.Error("list: query", "error", err)
		writeError(w, http.StatusInternalServerError, "internal", "query failed")
		return
	}

	items := make([]memoJSON, 0, len(memos))
	for _, m := range memos {
		items = append(items, listJSON(m))
	}
	writeJSON(w, http.StatusOK, map[string]any{"items": items, "next_cursor": next})
}

func (s *Server) handleGet(w http.ResponseWriter, r *http.Request) {
	memo, err := s.store.Memo(r.Context(), r.PathValue("id"))
	if errors.Is(err, store.ErrNotFound) {
		writeError(w, http.StatusNotFound, "not_found", "unknown memo")
		return
	}
	if err != nil {
		s.log.Error("get: query", "error", err)
		writeError(w, http.StatusInternalServerError, "internal", "query failed")
		return
	}
	writeJSON(w, http.StatusOK, detailJSON(memo))
}

// handlePatch corrects the transcript by hand. Unknown fields are rejected so a
// typo in a management script fails loudly.
func (s *Server) handlePatch(w http.ResponseWriter, r *http.Request) {
	var body struct {
		Transcript *string `json:"transcript"`
		Lang       *string `json:"lang"`
	}
	dec := json.NewDecoder(http.MaxBytesReader(w, r.Body, 1<<20))
	dec.DisallowUnknownFields()
	if err := dec.Decode(&body); err != nil {
		writeError(w, http.StatusBadRequest, "invalid_json", "expected {\"transcript\":\"...\"}")
		return
	}
	if body.Transcript == nil && body.Lang == nil {
		writeError(w, http.StatusBadRequest, "invalid_json", "nothing to update")
		return
	}

	ctx := r.Context()
	id := r.PathValue("id")
	memo, err := s.store.Memo(ctx, id)
	if errors.Is(err, store.ErrNotFound) {
		writeError(w, http.StatusNotFound, "not_found", "unknown memo")
		return
	}
	if err != nil {
		writeError(w, http.StatusInternalServerError, "internal", "query failed")
		return
	}

	transcript := memo.Transcript
	if body.Transcript != nil {
		transcript = *body.Transcript
	}
	lang := memo.Lang
	if body.Lang != nil {
		lang = *body.Lang
	}

	if err := s.store.UpdateTranscript(ctx, id, transcript, lang, time.Now().Unix()); err != nil {
		s.log.Error("patch: update", "error", err)
		writeError(w, http.StatusInternalServerError, "internal", "update failed")
		return
	}

	updated, err := s.store.Memo(ctx, id)
	if err != nil {
		writeError(w, http.StatusInternalServerError, "internal", "query failed")
		return
	}
	writeJSON(w, http.StatusOK, detailJSON(updated))
}

// handleDelete removes a memo and its audio. It is not reversible.
func (s *Server) handleDelete(w http.ResponseWriter, r *http.Request) {
	path, err := s.store.SoftDelete(r.Context(), r.PathValue("id"), time.Now().Unix())
	if errors.Is(err, store.ErrNotFound) {
		writeError(w, http.StatusNotFound, "not_found", "unknown memo")
		return
	}
	if err != nil {
		s.log.Error("delete: store", "error", err)
		writeError(w, http.StatusInternalServerError, "internal", "delete failed")
		return
	}
	if path != "" {
		if err := os.Remove(path); err != nil && !errors.Is(err, os.ErrNotExist) {
			s.log.Warn("delete: audio file", "path", path, "error", err)
		}
	}
	w.WriteHeader(http.StatusNoContent)
}

// handleAudio streams the canonical WAV, with range support for resume.
func (s *Server) handleAudio(w http.ResponseWriter, r *http.Request) {
	memo, err := s.store.Memo(r.Context(), r.PathValue("id"))
	if errors.Is(err, store.ErrNotFound) {
		writeError(w, http.StatusNotFound, "not_found", "unknown memo")
		return
	}
	if err != nil {
		writeError(w, http.StatusInternalServerError, "internal", "query failed")
		return
	}

	file, err := os.Open(memo.AudioPath)
	if err != nil {
		s.log.Error("audio: open", "path", memo.AudioPath, "error", err)
		writeError(w, http.StatusNotFound, "not_found", "audio is missing")
		return
	}
	defer file.Close()

	info, err := file.Stat()
	if err != nil {
		writeError(w, http.StatusInternalServerError, "internal", "audio stat failed")
		return
	}

	w.Header().Set("Content-Type", "audio/wav")
	w.Header().Set("Accept-Ranges", "bytes")
	w.Header().Set("Content-Type", "application/octet-stream")
	http.ServeContent(w, r, memo.ID+".memo", info.ModTime(), file)
}

// handleTranscribe re-arms a failed or stale transcription.
func (s *Server) handleTranscribe(w http.ResponseWriter, r *http.Request) {
	ctx := r.Context()
	id := r.PathValue("id")
	if _, err := s.store.Memo(ctx, id); errors.Is(err, store.ErrNotFound) {
		writeError(w, http.StatusNotFound, "not_found", "unknown memo")
		return
	} else if err != nil {
		writeError(w, http.StatusInternalServerError, "internal", "query failed")
		return
	}

	if err := s.store.SetStatus(ctx, id, domain.StatusUploaded, ""); err != nil {
		writeError(w, http.StatusInternalServerError, "internal", "update failed")
		return
	}
	if err := s.store.EnqueueJob(ctx, id, time.Now().Unix()); err != nil {
		writeError(w, http.StatusInternalServerError, "internal", "enqueue failed")
		return
	}
	writeJSON(w, http.StatusAccepted, map[string]any{"id": id, "status": domain.StatusUploaded})
}

func writeContainerError(w http.ResponseWriter, err error) {
	switch {
	case errors.Is(err, domain.ErrFrameMismatch):
		writeError(w, http.StatusBadRequest, "frame_mismatch", err.Error())
	case errors.Is(err, domain.ErrTooLong):
		writeError(w, http.StatusBadRequest, "over_limit", err.Error())
	default:
		writeError(w, http.StatusBadRequest, "bad_container", err.Error())
	}
}

func abs(v int64) int64 {
	if v < 0 {
		return -v
	}
	return v
}
