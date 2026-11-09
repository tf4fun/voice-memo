package events

import (
	"encoding/json"
	"strings"
	"testing"
)

func TestPayloadFitsFirmwareEvenWithEscapedText(t *testing.T) {
	hub := NewHub()
	ch, cancel := hub.Subscribe()
	defer cancel()
	hub.Publish(Event{Type: "memo", ID: "11111111-1111-4111-8111-111111111111", Status: "transcribing",
		Snippet: strings.Repeat("\x00<\"你好", 1000), RecordedAt: 9223372036854775807, DurationMS: 9223372036854775807})
	ev := <-ch
	data, err := json.Marshal(ev)
	if err != nil || len(data)+len("data: ") >= 512 {
		t.Fatalf("payload %d bytes: %v", len(data), err)
	}
	if len([]rune(ev.Snippet)) > 41 {
		t.Fatal("unbounded snippet")
	}
}
func TestSlowSubscriberClosesForReconciliation(t *testing.T) {
	hub := NewHub()
	ch, cancel := hub.Subscribe()
	defer cancel()
	for i := 0; i < 9; i++ {
		hub.Publish(Event{Type: "memo"})
	}
	if hub.Subscribers() != 0 {
		t.Fatal("slow subscriber retained")
	}
	count := 0
	for range ch {
		count++
	}
	if count != 8 {
		t.Fatalf("buffered events: %d", count)
	}
}
