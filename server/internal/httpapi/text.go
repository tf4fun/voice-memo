package httpapi

import (
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"net/http"
	"strconv"
	"strings"
	"unicode/utf8"

	"github.com/tf4fun/voice-memo/server/internal/store"
)

// handleText exposes bounded display windows without changing the full-detail
// representation. Offsets count bytes in the display text; revisions prevent
// mixing chunks after an edit. NUL is replaced for C-string consumers.
func (s *Server) handleText(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	limit, offset := 2048, 0
	var err error
	if raw := q.Get("limit"); raw != "" {
		limit, err = strconv.Atoi(raw)
	}
	if err != nil || limit < 4 || limit > 4096 {
		writeError(w, http.StatusBadRequest, "invalid_json", "limit must be 4..4096 bytes")
		return
	}
	if raw := q.Get("offset"); raw != "" {
		offset, err = strconv.Atoi(raw)
	}
	if err != nil || offset < 0 {
		writeError(w, http.StatusBadRequest, "invalid_json", "invalid text offset")
		return
	}
	direction := q.Get("direction")
	if direction == "" {
		direction = "next"
	}
	if direction != "next" && direction != "previous" {
		writeError(w, http.StatusBadRequest, "invalid_json", "invalid text direction")
		return
	}
	memo, err := s.store.Memo(r.Context(), r.PathValue("id"))
	if errors.Is(err, store.ErrNotFound) {
		writeError(w, http.StatusNotFound, "not_found", "unknown memo")
		return
	}
	if err != nil {
		writeError(w, http.StatusInternalServerError, "internal", "query failed")
		return
	}
	text := strings.ReplaceAll(strings.ToValidUTF8(memo.Transcript, "\uFFFD"), "\x00", "\uFFFD")
	sum := sha256.Sum256([]byte(text))
	revision := hex.EncodeToString(sum[:])
	if expected := q.Get("revision"); expected != "" && expected != revision {
		writeError(w, http.StatusConflict, "transcript_changed", "restart text reading")
		return
	}
	if offset > len(text) || (offset < len(text) && !utf8.RuneStart(text[offset])) {
		writeError(w, http.StatusBadRequest, "invalid_json", "offset must be a character boundary")
		return
	}
	start, end := offset, offset
	if direction == "previous" {
		start = max(0, end-limit)
		for start < end && !utf8.RuneStart(text[start]) {
			start++
		}
	} else {
		end = min(len(text), start+limit)
		for end > start && end < len(text) && !utf8.RuneStart(text[end]) {
			end--
		}
	}
	writeJSON(w, http.StatusOK, map[string]any{
		"id": memo.ID, "status": memo.Status, "revision": revision,
		"offset": start, "end": end, "total": len(text), "text": text[start:end],
	})
}
