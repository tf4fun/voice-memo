package domain

import (
	"bytes"
	"errors"
	"io"
	"os"
	"testing"
)

const fixture = "../audio/testdata/tone-1s.memo"

func TestParseHeaderFromFixture(t *testing.T) {
	f, err := os.Open(fixture)
	if err != nil {
		t.Fatalf("open fixture: %v", err)
	}
	defer f.Close()

	h, err := ParseHeader(f)
	if err != nil {
		t.Fatalf("ParseHeader: %v", err)
	}
	if h.Version != ContainerVersion || h.Codec != CodecOpus {
		t.Errorf("version/codec = %d/%d", h.Version, h.Codec)
	}
	if h.SampleRate != 16000 || h.Channels != 1 {
		t.Errorf("audio params = %d Hz / %d ch", h.SampleRate, h.Channels)
	}
	if h.FrameMS != 20 {
		t.Errorf("frame ms = %d", h.FrameMS)
	}
	if h.FrameCount == 0 || h.MemoID == "" {
		t.Errorf("frame count = %d, memo id = %q", h.FrameCount, h.MemoID)
	}
	if !h.MatchesDuration() {
		t.Errorf("fixture duration should agree with its frame count")
	}
}

func TestParseHeaderRejectsBadMagic(t *testing.T) {
	raw := make([]byte, HeaderSize)
	copy(raw, "NOPE")
	if _, err := ParseHeader(bytes.NewReader(raw)); !errors.Is(err, ErrBadContainer) {
		t.Fatalf("error = %v, want ErrBadContainer", err)
	}
}

func TestParseHeaderRejectsUnknownVersionAndCodec(t *testing.T) {
	base := make([]byte, HeaderSize)
	copy(base, "MEMO")
	base[4], base[5] = 1, 1
	base[6], base[7] = 0x80, 0x3e // 16000
	base[8] = 1
	base[9] = 20
	copy(base[24:60], "id")

	badVersion := append([]byte(nil), base...)
	badVersion[4] = 9
	if _, err := ParseHeader(bytes.NewReader(badVersion)); !errors.Is(err, ErrBadContainer) {
		t.Errorf("bad version: error = %v", err)
	}

	badCodec := append([]byte(nil), base...)
	badCodec[5] = 7
	if _, err := ParseHeader(bytes.NewReader(badCodec)); !errors.Is(err, ErrBadContainer) {
		t.Errorf("bad codec: error = %v", err)
	}

	badRate := append([]byte(nil), base...)
	badRate[6], badRate[7] = 0, 0
	if _, err := ParseHeader(bytes.NewReader(badRate)); !errors.Is(err, ErrBadContainer) {
		t.Errorf("bad sample rate: error = %v", err)
	}
}

func TestMatchesDurationTolerance(t *testing.T) {
	cases := []struct {
		frames   uint32
		frameMS  uint8
		declared uint32
		want     bool
	}{
		{100, 20, 2000, true},
		{100, 20, 2040, true},  // +2 %
		{100, 20, 1900, true},  // -5 %
		{100, 20, 1800, false}, // -10 %
		{0, 20, 0, true},
	}
	for _, c := range cases {
		h := Header{FrameCount: c.frames, FrameMS: c.frameMS, DurationMS: c.declared}
		if got := h.MatchesDuration(); got != c.want {
			t.Errorf("frames=%d frameMS=%d declared=%d: got %v want %v",
				c.frames, c.frameMS, c.declared, got, c.want)
		}
	}
}

func TestFrameReaderRejectsZeroLengthFrame(t *testing.T) {
	r := NewFrameReader(bytes.NewReader([]byte{0, 0}))
	if _, err := r.Next(); !errors.Is(err, ErrBadContainer) {
		t.Fatalf("error = %v, want ErrBadContainer", err)
	}
}

func TestFrameReaderStopsAtEOF(t *testing.T) {
	payload := []byte{1, 2, 3}
	stream := append([]byte{byte(len(payload)), 0}, payload...)
	r := NewFrameReader(bytes.NewReader(stream))

	got, err := r.Next()
	if err != nil || !bytes.Equal(got, payload) {
		t.Fatalf("first frame = %v, %v", got, err)
	}
	if _, err := r.Next(); !errors.Is(err, io.EOF) {
		t.Fatalf("error = %v, want io.EOF", err)
	}
}

func TestSnippetCutsOnRuneBoundary(t *testing.T) {
	m := Memo{Transcript: "下午三点和张工对了下进度"}
	if got, want := m.Snippet(4), "下午三点…"; got != want {
		t.Errorf("Snippet = %q, want %q", got, want)
	}
	if got := (Memo{}).Snippet(4); got != "" {
		t.Errorf("empty transcript should give an empty snippet, got %q", got)
	}
	if got := (Memo{Transcript: "短"}).Snippet(10); got != "短" {
		t.Errorf("short transcript should not be truncated, got %q", got)
	}
}
