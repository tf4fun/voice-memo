package httpapi

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"net/url"
	"strings"
	"testing"
	"unicode/utf8"

	"github.com/tf4fun/voice-memo/server/internal/domain"
)

type textWindow struct {
	ID, Status, Revision, Text string
	Offset, End, Total         int
}

func textFixture(t *testing.T, e *testEnv, text string) string {
	t.Helper()
	id := "11111111-1111-4111-8111-111111111111"
	_, err := e.store.InsertMemo(context.Background(), domain.Memo{ID: id, DeviceID: "A", Status: domain.StatusReady, Transcript: text, Codec: "opus", AudioPath: "audio/a.memo"})
	if err != nil {
		t.Fatal(err)
	}
	return id
}
func TestTextWindowsReconstructLongUnicodeBothDirections(t *testing.T) {
	e := newEnv(t)
	want := strings.Repeat("中文a😀\n", 12000)
	id := textFixture(t, e, want)
	path := "/api/v1/memos/" + id + "/text?limit=2048"
	offset := 0
	revision := ""
	var forward strings.Builder
	var last textWindow
	for {
		rec := e.do(t, http.MethodGet, fmt.Sprintf("%s&offset=%d&revision=%s", path, offset, revision), nil, nil)
		if rec.Code != 200 {
			t.Fatal(rec.Code, rec.Body.String())
		}
		var chunk textWindow
		if err := json.Unmarshal(rec.Body.Bytes(), &chunk); err != nil {
			t.Fatal(err)
		}
		if !utf8.ValidString(chunk.Text) || len(chunk.Text) > 2048 || chunk.Offset != offset || chunk.End-chunk.Offset != len(chunk.Text) || chunk.Total != len(want) {
			t.Fatal(chunk)
		}
		if revision != "" && chunk.Revision != revision {
			t.Fatal("revision changed")
		}
		revision = chunk.Revision
		forward.WriteString(chunk.Text)
		last = chunk
		if chunk.End == chunk.Total {
			break
		}
		if chunk.End <= offset {
			t.Fatal("no progress")
		}
		offset = chunk.End
	}
	if forward.String() != want {
		t.Fatal("long forward read truncated or corrupted")
	}
	reverse := last.Text
	for last.Offset > 0 {
		rec := e.do(t, http.MethodGet, fmt.Sprintf("%s&offset=%d&direction=previous&revision=%s", path, last.Offset, revision), nil, nil)
		if rec.Code != 200 {
			t.Fatal(rec.Body.String())
		}
		var chunk textWindow
		json.Unmarshal(rec.Body.Bytes(), &chunk)
		if chunk.End != last.Offset || chunk.Offset >= last.Offset || !utf8.ValidString(chunk.Text) {
			t.Fatal("invalid previous window")
		}
		reverse = chunk.Text + reverse
		last = chunk
	}
	if reverse != want {
		t.Fatal("backward reconstruction changed text")
	}
	if err := e.store.UpdateTranscript(context.Background(), id, "edited", "", 1); err != nil {
		t.Fatal(err)
	}
	rec := e.do(t, http.MethodGet, path+"&revision="+revision, nil, nil)
	if rec.Code != http.StatusConflict {
		t.Fatal("stale text version accepted")
	}
}
func TestTextWindowBoundsEmptyStatusAndDeleted(t *testing.T) {
	e := newEnv(t)
	id := textFixture(t, e, "中\x00文")
	base := "/api/v1/memos/" + id + "/text"
	rec := e.do(t, http.MethodGet, base, nil, nil)
	var chunk textWindow
	json.Unmarshal(rec.Body.Bytes(), &chunk)
	if rec.Code != 200 || chunk.Text != "中\uFFFD文" || chunk.Status != "ready" {
		t.Fatal(rec.Body.String())
	}
	for _, q := range []string{"limit=0", "limit=3", "limit=4097", "offset=-1", "offset=9999", "offset=1", "offset=x", "direction=bad"} {
		if rec := e.do(t, http.MethodGet, base+"?"+q, nil, nil); rec.Code != 400 {
			t.Fatalf("%s=%d", q, rec.Code)
		}
	}
	if err := e.store.UpdateTranscript(context.Background(), id, "", "", 1); err != nil {
		t.Fatal(err)
	}
	rec = e.do(t, http.MethodGet, base, nil, nil)
	json.Unmarshal(rec.Body.Bytes(), &chunk)
	if rec.Code != 200 || chunk.Text != "" || chunk.Total != 0 || chunk.End != 0 {
		t.Fatal(rec.Body.String())
	}
	if _, err := e.store.SoftDelete(context.Background(), id, 2); err != nil {
		t.Fatal(err)
	}
	if rec := e.do(t, http.MethodGet, base, nil, nil); rec.Code != 404 {
		t.Fatal("deleted text exposed")
	}
}
func TestListHTTPBidirectionalCursors(t *testing.T) {
	e := newEnv(t)
	for i := 0; i < 50; i++ {
		_, err := e.store.InsertMemo(context.Background(), domain.Memo{ID: fmt.Sprintf("%08d-1111-4111-8111-111111111111", i), DeviceID: "A", RecordedAt: 0, Status: domain.StatusReady, AudioPath: "a.memo", Codec: "opus"})
		if err != nil {
			t.Fatal(err)
		}
	}
	type page struct {
		Items    []memoJSON
		Next     string `json:"next_cursor"`
		Previous string `json:"previous_cursor"`
	}
	get := func(q string) page {
		rec := e.do(t, http.MethodGet, "/api/v1/memos?limit=24"+q, nil, nil)
		if rec.Code != 200 {
			t.Fatal(rec.Body.String())
		}
		var p page
		if err := json.Unmarshal(rec.Body.Bytes(), &p); err != nil {
			t.Fatal(err)
		}
		return p
	}
	first := get("")
	second := get("&cursor=" + url.QueryEscape(first.Next))
	third := get("&cursor=" + url.QueryEscape(second.Next))
	if len(third.Items) != 2 || third.Next != "" || third.Previous == "" {
		t.Fatal("wrong last page")
	}
	back := get("&direction=newer&cursor=" + url.QueryEscape(third.Previous))
	if len(back.Items) != 24 || back.Items[0].ID != second.Items[0].ID {
		t.Fatal("wrong previous page")
	}
	for _, q := range []string{"direction=bad", "direction=newer", "cursor=!!!!"} {
		if rec := e.do(t, http.MethodGet, "/api/v1/memos?"+q, nil, nil); rec.Code != 400 {
			t.Fatal(q, rec.Code)
		}
	}
}
