package httpapi

import (
	"encoding/json"
	"fmt"
	"net/http"
	"time"
)

// handleEvents streams memo changes as server-sent events.
//
// The device subscribes while the list is on screen: a transcription finishing
// then arrives as an event instead of being discovered by a poll. The stream is
// read-only and carries no device data, so it needs no authentication on a LAN.
func (s *Server) handleEvents(w http.ResponseWriter, r *http.Request) {
	flusher, ok := w.(http.Flusher)
	if !ok {
		writeError(w, http.StatusInternalServerError, "internal", "streaming unsupported")
		return
	}

	ch, cancel := s.events.Subscribe()
	defer cancel()
	controller := http.NewResponseController(w)
	write := func(text string) bool {
		_ = controller.SetWriteDeadline(time.Now().Add(5 * time.Second))
		if _, err := fmt.Fprint(w, text); err != nil {
			return false
		}
		flusher.Flush()
		return true
	}
	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("Connection", "keep-alive")
	w.WriteHeader(http.StatusOK)

	// Start the client with a hint so it knows the stream is live.
	if !write("event: ready\ndata: {}\n\n") {
		return
	}

	heartbeat := time.NewTicker(20 * time.Second)
	defer heartbeat.Stop()

	ctx := r.Context()
	for {
		select {
		case <-ctx.Done():
			return
		case <-heartbeat.C:
			if !write(": keepalive\n\n") {
				return
			}
		case ev, open := <-ch:
			if !open {
				return
			}
			body, err := json.Marshal(ev)
			if err != nil {
				continue
			}
			if !write(fmt.Sprintf("event: memo\ndata: %s\n\n", body)) {
				return
			}
		}
	}
}
