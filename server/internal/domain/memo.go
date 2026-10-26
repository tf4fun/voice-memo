// Package domain holds the types shared by the API, the store and the workers,
// plus the on-wire recording container.
package domain

import (
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"time"
)

// Memo statuses. The set is closed: anything else in the database is a bug.
const (
	StatusUploaded     = "uploaded"
	StatusTranscribing = "transcribing"
	StatusReady        = "ready"
	StatusFailed       = "failed"
)

// Memo is one recording as stored.
type Memo struct {
	ID          string
	DeviceID    string
	RecordedAt  int64
	UploadedAt  int64
	DurationMS  int64
	FrameCount  int64
	SizeBytes   int64
	Codec       string
	Status      string
	Transcript  string
	Lang        string
	ASRProvider string
	ASRModel    string
	ASRTraceID  string
	EditedAt    int64
	Error       string
	AudioPath   string
	ClockSkew   int64
	DeletedAt   int64
}

// Snippet returns the leading characters of the transcript for list responses,
// cut on a rune boundary, or an empty string while there is nothing to show.
func (m Memo) Snippet(runes int) string {
	if m.Transcript == "" {
		return ""
	}
	count := 0
	for i := range m.Transcript {
		if count == runes {
			return m.Transcript[:i] + "…"
		}
		count++
	}
	return m.Transcript
}

const (
	// HeaderSize is the fixed container header, see docs/technical/server.md.
	HeaderSize = 80
	// ContainerVersion is the only version this build understands.
	ContainerVersion = 1
	// CodecOpus is the only codec the container carries.
	CodecOpus = 1
	// MaxFrameDurationMS bounds the accepted frame length.
	MaxFrameDurationMS = 60
)

var (
	// ErrBadContainer covers every structural rejection.
	ErrBadContainer = errors.New("bad container")
	// ErrFrameMismatch means the declared frame count or duration disagrees
	// with what is actually present.
	ErrFrameMismatch = errors.New("frame mismatch")
	// ErrTooLong means the declared duration exceeds the configured limit.
	ErrTooLong = errors.New("duration over limit")
)

// Header is the parsed container header.
type Header struct {
	Version     uint8
	Codec       uint8
	SampleRate  uint16
	Channels    uint8
	FrameMS     uint8
	DurationMS  uint32
	RecordedAt  int64
	MemoID      string
	FrameCount  uint32
}

// ParseHeader reads and validates the 80-byte header. It does not read frames.
func ParseHeader(r io.Reader) (Header, error) {
	var raw [HeaderSize]byte
	if _, err := io.ReadFull(r, raw[:]); err != nil {
		return Header{}, fmt.Errorf("%w: short header: %v", ErrBadContainer, err)
	}
	if string(raw[0:4]) != "MEMO" {
		return Header{}, fmt.Errorf("%w: unexpected magic %q", ErrBadContainer, raw[0:4])
	}

	h := Header{
		Version:    raw[4],
		Codec:      raw[5],
		SampleRate: binary.LittleEndian.Uint16(raw[6:8]),
		Channels:   raw[8],
		FrameMS:    raw[9],
		DurationMS: binary.LittleEndian.Uint32(raw[12:16]),
		RecordedAt: int64(binary.LittleEndian.Uint64(raw[16:24])),
		MemoID:     trimNUL(raw[24:60]),
		FrameCount: binary.LittleEndian.Uint32(raw[60:64]),
	}

	switch {
	case h.Version != ContainerVersion:
		return Header{}, fmt.Errorf("%w: version %d", ErrBadContainer, h.Version)
	case h.Codec != CodecOpus:
		return Header{}, fmt.Errorf("%w: codec %d", ErrBadContainer, h.Codec)
	case h.SampleRate < 8000 || h.SampleRate > 48000:
		return Header{}, fmt.Errorf("%w: sample rate %d", ErrBadContainer, h.SampleRate)
	case h.Channels != 1 && h.Channels != 2:
		return Header{}, fmt.Errorf("%w: channels %d", ErrBadContainer, h.Channels)
	case h.FrameMS == 0 || h.FrameMS > MaxFrameDurationMS:
		return Header{}, fmt.Errorf("%w: frame duration %d ms", ErrBadContainer, h.FrameMS)
	case h.MemoID == "":
		return Header{}, fmt.Errorf("%w: empty memo id", ErrBadContainer)
	}
	return h, nil
}

// Duration returns the duration implied by the frame count.
func (h Header) Duration() time.Duration {
	return time.Duration(h.FrameCount) * time.Duration(h.FrameMS) * time.Millisecond
}

// MatchesDuration reports whether the declared duration agrees with the frames
// within the tolerance the design allows (5 percent).
func (h Header) MatchesDuration() bool {
	implied := int64(h.FrameCount) * int64(h.FrameMS)
	declared := int64(h.DurationMS)
	if implied == 0 {
		return declared == 0
	}
	diff := implied - declared
	if diff < 0 {
		diff = -diff
	}
	return diff*100 <= implied*5
}

// FrameReader iterates the length-prefixed frames that follow the header.
type FrameReader struct {
	r     io.Reader
	count uint32
}

// NewFrameReader wraps the stream after the header.
func NewFrameReader(r io.Reader) *FrameReader { return &FrameReader{r: r} }

// Next returns the next frame payload, or io.EOF after the last one. A zero
// length frame is an error: it can only come from a truncated file.
func (f *FrameReader) Next() ([]byte, error) {
	var length [2]byte
	if _, err := io.ReadFull(f.r, length[:]); err != nil {
		if errors.Is(err, io.EOF) {
			return nil, io.EOF
		}
		return nil, fmt.Errorf("%w: short frame header: %v", ErrBadContainer, err)
	}
	size := binary.LittleEndian.Uint16(length[:])
	if size == 0 {
		return nil, fmt.Errorf("%w: zero-length frame", ErrBadContainer)
	}
	payload := make([]byte, size)
	if _, err := io.ReadFull(f.r, payload); err != nil {
		return nil, fmt.Errorf("%w: truncated frame: %v", ErrBadContainer, err)
	}
	f.count++
	return payload, nil
}

// Count reports how many frames have been read.
func (f *FrameReader) Count() uint32 { return f.count }

func trimNUL(b []byte) string {
	for i, c := range b {
		if c == 0 {
			return string(b[:i])
		}
	}
	return string(b)
}
