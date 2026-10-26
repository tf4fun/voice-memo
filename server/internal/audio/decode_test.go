package audio

import (
	"encoding/binary"
	"errors"
	"io"
	"os"
	"path/filepath"
	"testing"

	"github.com/tf4fun/voice-memo/server/internal/domain"
)

const fixture = "testdata/tone-1s.memo"

func decodeFixture(t *testing.T, path string) (Stats, []byte) {
	t.Helper()

	in, err := os.Open(path)
	if err != nil {
		t.Fatalf("open fixture: %v", err)
	}
	defer in.Close()

	outPath := filepath.Join(t.TempDir(), "out.wav")
	out, err := os.Create(outPath)
	if err != nil {
		t.Fatalf("create wav: %v", err)
	}
	defer out.Close()

	stats, err := DecodeOpusToWAV(in, out)
	if err != nil {
		t.Fatalf("decode: %v", err)
	}
	if err := out.Close(); err != nil {
		t.Fatalf("close wav: %v", err)
	}

	data, err := os.ReadFile(outPath)
	if err != nil {
		t.Fatalf("read wav: %v", err)
	}
	return stats, data
}

func TestDecodeFixtureProducesWAV(t *testing.T) {
	in, err := os.Open(fixture)
	if err != nil {
		t.Fatalf("open fixture: %v", err)
	}
	header, err := domain.ParseHeader(in)
	in.Close()
	if err != nil {
		t.Fatalf("parse fixture header: %v", err)
	}

	stats, wav := decodeFixture(t, fixture)

	if stats.Frames != header.FrameCount {
		t.Errorf("frames = %d, want %d", stats.Frames, header.FrameCount)
	}
	// Every frame must have produced its full sample count: a short decode would
	// mean the decoder rejected or padded a packet.
	wantSamples := int(header.FrameCount) * (int(header.SampleRate) * int(header.FrameMS) / 1000)
	if stats.Samples != wantSamples {
		t.Errorf("samples = %d, want %d", stats.Samples, wantSamples)
	}
	if stats.WAVBytes != int64(len(wav)) {
		t.Errorf("WAVBytes = %d, file is %d", stats.WAVBytes, len(wav))
	}
	if string(wav[0:4]) != "RIFF" || string(wav[8:12]) != "WAVE" {
		t.Fatalf("not a RIFF/WAVE file: % x", wav[0:12])
	}
	if got, want := binary.LittleEndian.Uint32(wav[24:28]), uint32(16000); got != want {
		t.Errorf("sample rate in header = %d, want %d", got, want)
	}
	if got, want := binary.LittleEndian.Uint32(wav[40:44]), uint32(len(wav)-44); got != want {
		t.Errorf("data chunk size = %d, want %d", got, want)
	}

	// A tone must decode to something audible, not silence.
	var peak int16
	for i := 44; i+1 < len(wav); i += 2 {
		sample := int16(binary.LittleEndian.Uint16(wav[i : i+2]))
		if sample < 0 {
			sample = -sample
		}
		if sample > peak {
			peak = sample
		}
	}
	if peak < 1000 {
		t.Errorf("decoded peak amplitude = %d, expected a real tone", peak)
	}
}

func TestDecodeRejectsTruncatedFrame(t *testing.T) {
	raw, err := os.ReadFile(fixture)
	if err != nil {
		t.Fatalf("read fixture: %v", err)
	}
	truncated := filepath.Join(t.TempDir(), "truncated.memo")
	if err := os.WriteFile(truncated, raw[:len(raw)-8], 0o600); err != nil {
		t.Fatalf("write truncated: %v", err)
	}

	in, err := os.Open(truncated)
	if err != nil {
		t.Fatalf("open: %v", err)
	}
	defer in.Close()

	out, err := os.Create(filepath.Join(t.TempDir(), "out.wav"))
	if err != nil {
		t.Fatalf("create: %v", err)
	}
	defer out.Close()

	_, err = DecodeOpusToWAV(in, out)
	if !errors.Is(err, domain.ErrBadContainer) {
		t.Fatalf("error = %v, want ErrBadContainer", err)
	}
}

func TestDecodeRejectsFrameCountMismatch(t *testing.T) {
	raw, err := os.ReadFile(fixture)
	if err != nil {
		t.Fatalf("read fixture: %v", err)
	}
	patched := make([]byte, len(raw))
	copy(patched, raw)
	// Declare one frame more than the file carries.
	binary.LittleEndian.PutUint32(patched[60:64], binary.LittleEndian.Uint32(raw[60:64])+1)
	path := filepath.Join(t.TempDir(), "mismatch.memo")
	if err := os.WriteFile(path, patched, 0o600); err != nil {
		t.Fatalf("write: %v", err)
	}

	in, err := os.Open(path)
	if err != nil {
		t.Fatalf("open: %v", err)
	}
	defer in.Close()

	out, err := os.Create(filepath.Join(t.TempDir(), "out.wav"))
	if err != nil {
		t.Fatalf("create: %v", err)
	}
	defer out.Close()

	_, err = DecodeOpusToWAV(in, out)
	if !errors.Is(err, domain.ErrFrameMismatch) {
		t.Fatalf("error = %v, want ErrFrameMismatch", err)
	}
}

func TestDecodeRejectsShortHeader(t *testing.T) {
	in := &shortReader{data: []byte("MEM")}
	out, err := os.Create(filepath.Join(t.TempDir(), "out.wav"))
	if err != nil {
		t.Fatalf("create: %v", err)
	}
	defer out.Close()

	if _, err := DecodeOpusToWAV(in, out); !errors.Is(err, domain.ErrBadContainer) {
		t.Fatalf("error = %v, want ErrBadContainer", err)
	}
}

type shortReader struct {
	data []byte
	pos  int
}

func (s *shortReader) Read(p []byte) (int, error) {
	if s.pos >= len(s.data) {
		return 0, io.EOF
	}
	n := copy(p, s.data[s.pos:])
	s.pos += n
	return n, nil
}
