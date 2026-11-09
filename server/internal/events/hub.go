// Package events is a tiny fan-out hub for server-sent events.
//
// The device keeps an SSE connection open while it is showing the list, so a
// transcription finishing reaches the screen in milliseconds instead of waiting
// for the next poll. Slow subscribers are dropped rather than allowed to stall
// the upload path.
package events

import (
	"github.com/tf4fun/voice-memo/server/internal/domain"
	"sync"
)

// Event is one change worth pushing to connected devices.
type Event struct {
	Type       string `json:"type"`
	ID         string `json:"id"`
	Status     string `json:"status"`
	Snippet    string `json:"snippet,omitempty"`
	RecordedAt int64  `json:"recorded_at,omitempty"`
	DurationMS int64  `json:"duration_ms,omitempty"`
}

// Hub broadcasts events to every subscriber.
type Hub struct {
	mu   sync.RWMutex
	subs map[chan Event]struct{}
}

// NewHub returns an empty hub.
func NewHub() *Hub {
	return &Hub{subs: make(map[chan Event]struct{})}
}

// Subscribe registers a listener. The returned cancel func must always be
// called; it detaches the channel and closes it.
func (h *Hub) Subscribe() (<-chan Event, func()) {
	ch := make(chan Event, 8)
	h.mu.Lock()
	h.subs[ch] = struct{}{}
	h.mu.Unlock()

	var once sync.Once
	cancel := func() {
		once.Do(func() {
			h.mu.Lock()
			if _, ok := h.subs[ch]; ok {
				delete(h.subs, ch)
				close(ch)
			}
			h.mu.Unlock()
		})
	}
	return ch, cancel
}

// Publish delivers an event without blocking. A subscriber whose buffer is
// full is closed: the device re-reads the list when it reconnects.
func (h *Hub) Publish(ev Event) {
	ev.Snippet = (domain.Memo{Transcript: ev.Snippet}).Snippet(40)
	h.mu.Lock()
	defer h.mu.Unlock()
	for ch := range h.subs {
		select {
		case ch <- ev:
		default:
			// Force reconnect/reconciliation rather than silently losing the
			// last update on an otherwise healthy long-lived stream.
			delete(h.subs, ch)
			close(ch)
		}
	}
}

// Subscribers reports the current listener count, for logs and tests.
func (h *Hub) Subscribers() int {
	h.mu.RLock()
	defer h.mu.RUnlock()
	return len(h.subs)
}
