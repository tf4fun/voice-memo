// Package audio reads the MEMO container the device uploads.
//
// The container itself is the canonical asset: it is validated and stored as-is
// so the device can play it back byte for byte. PCM exists only when a
// transcription request needs it, decoded into memory and released afterwards.
package audio

import (
	"encoding/binary"
	"fmt"
	"io"
	"time"

	"github.com/pion/opus"

	"github.com/tf4fun/voice-memo/server/internal/domain"
)

// Stats summarises a decode so the caller can cross-check the header.
type Stats struct {
	Frames   uint32
	Samples  int // per channel
	Duration time.Duration
	WAVBytes int64
}

// DecodeOpusToWAV reads a container from r and writes a RIFF/WAVE file to w.
// The writer must support seeking, because the RIFF sizes are only known after
// the frames are decoded.
func DecodeOpusToWAV(r io.Reader, w io.WriteSeeker) (Stats, error) {
	header, err := domain.ParseHeader(r)
	if err != nil {
		return Stats{}, err
	}

	decoder, err := opus.NewDecoderWithOutput(int(header.SampleRate), int(header.Channels))
	if err != nil {
		return Stats{}, fmt.Errorf("create opus decoder: %w", err)
	}

	if err := writeWAVHeader(w, header.SampleRate, header.Channels, 0); err != nil {
		return Stats{}, err
	}

	// Opus packets may carry up to 120 ms regardless of the sender's frame size.
	pcm := make([]int16, 5760*int(header.Channels))
	frames := domain.NewFrameReader(r)

	var (
		samplesPerChannel int
		written           int64
	)
	for {
		packet, err := frames.Next()
		if err == io.EOF {
			break
		}
		if err != nil {
			return Stats{}, err
		}

		n, err := decoder.DecodeToInt16(packet, pcm)
		if err != nil {
			return Stats{}, fmt.Errorf("decode frame %d: %w", frames.Count(), err)
		}
		if n == 0 {
			continue
		}

		payload := pcm[:n*int(header.Channels)]
		if err := binary.Write(w, binary.LittleEndian, payload); err != nil {
			return Stats{}, fmt.Errorf("write pcm: %w", err)
		}
		samplesPerChannel += n
		written += int64(len(payload) * 2)
	}

	if frames.Count() != header.FrameCount {
		return Stats{}, fmt.Errorf("%w: header declares %d frames, stream has %d",
			domain.ErrFrameMismatch, header.FrameCount, frames.Count())
	}
	if !header.MatchesDuration() {
		return Stats{}, fmt.Errorf("%w: declared %d ms, %d frames of %d ms",
			domain.ErrFrameMismatch, header.DurationMS, header.FrameCount, header.FrameMS)
	}

	if err := writeWAVHeader(w, header.SampleRate, header.Channels, written); err != nil {
		return Stats{}, err
	}

	return Stats{
		Frames:   frames.Count(),
		Samples:  samplesPerChannel,
		Duration: time.Duration(samplesPerChannel) * time.Second / time.Duration(header.SampleRate),
		WAVBytes: written + 44,
	}, nil
}

// writeWAVHeader writes a canonical 44-byte PCM header. Passing zero dataBytes
// first, then the real size after decoding, is cheaper than buffering audio.
func writeWAVHeader(w io.WriteSeeker, sampleRate uint16, channels uint8, dataBytes int64) error {
	const headerBytes = 44
	byteRate := int64(sampleRate) * int64(channels) * 2
	blockAlign := uint16(channels) * 2

	buf := make([]byte, headerBytes)
	copy(buf[0:4], "RIFF")
	binary.LittleEndian.PutUint32(buf[4:8], uint32(int64(headerBytes-8)+dataBytes))
	copy(buf[8:12], "WAVE")
	copy(buf[12:16], "fmt ")
	binary.LittleEndian.PutUint32(buf[16:20], 16) // PCM chunk size
	binary.LittleEndian.PutUint16(buf[20:22], 1)  // PCM format
	binary.LittleEndian.PutUint16(buf[22:24], uint16(channels))
	binary.LittleEndian.PutUint32(buf[24:28], uint32(sampleRate))
	binary.LittleEndian.PutUint32(buf[28:32], uint32(byteRate))
	binary.LittleEndian.PutUint16(buf[32:34], blockAlign)
	binary.LittleEndian.PutUint16(buf[34:36], 16) // bits per sample
	copy(buf[36:40], "data")
	binary.LittleEndian.PutUint32(buf[40:44], uint32(dataBytes))

	if _, err := w.Seek(0, io.SeekStart); err != nil {
		return fmt.Errorf("seek to wav header: %w", err)
	}
	if _, err := w.Write(buf); err != nil {
		return fmt.Errorf("write wav header: %w", err)
	}
	if _, err := w.Seek(0, io.SeekEnd); err != nil {
		return fmt.Errorf("seek to wav end: %w", err)
	}
	return nil
}

// memSeeker is an in-memory io.WriteSeeker. The WAV header is only complete
// once every frame has been decoded, so a plain buffer is not enough.
type memSeeker struct {
	buf []byte
	off int64
}

func (m *memSeeker) Write(p []byte) (int, error) {
	end := m.off + int64(len(p))
	if end > int64(len(m.buf)) {
		m.buf = append(m.buf, make([]byte, end-int64(len(m.buf)))...)
	}
	copy(m.buf[m.off:], p)
	m.off = end
	return len(p), nil
}

func (m *memSeeker) Seek(offset int64, whence int) (int64, error) {
	switch whence {
	case io.SeekStart:
		m.off = offset
	case io.SeekCurrent:
		m.off += offset
	case io.SeekEnd:
		m.off = int64(len(m.buf)) + offset
	}
	if m.off < 0 {
		return 0, fmt.Errorf("negative seek offset")
	}
	return m.off, nil
}

// DecodeOpusToWAVBytes decodes a container into a WAV image held in memory.
//
// The device's upload is the canonical asset (Opus), so the PCM image only
// exists for the duration of a transcription request and never touches disk.
func DecodeOpusToWAVBytes(r io.Reader) ([]byte, Stats, error) {
	mem := &memSeeker{}
	stats, err := DecodeOpusToWAV(r, mem)
	if err != nil {
		return nil, Stats{}, err
	}
	return mem.buf, stats, nil
}

// ValidateContainer walks the container without decoding it, confirming that
// the header matches the frames that follow. Upload uses it so the stored file
// can stay Opus while still rejecting malformed input.
func ValidateContainer(r io.Reader) (Stats, error) {
	header, err := domain.ParseHeader(r)
	if err != nil {
		return Stats{}, err
	}

	frames := domain.NewFrameReader(r)
	var (
		count  uint32
		bytes_ int64
	)
	for {
		packet, err := frames.Next()
		if err == io.EOF {
			break
		}
		if err != nil {
			return Stats{}, err
		}
		count++
		bytes_ += int64(len(packet))
	}

	if count == 0 {
		return Stats{}, fmt.Errorf("%w: empty recording", domain.ErrBadContainer)
	}
	if header.FrameCount != count {
		return Stats{}, fmt.Errorf("%w: header says %d frames, found %d", domain.ErrFrameMismatch, header.FrameCount, count)
	}
	if !header.MatchesDuration() {
		return Stats{}, fmt.Errorf("%w: declared duration disagrees with frames", domain.ErrFrameMismatch)
	}

	perFrame := int(header.FrameMS) * int(header.SampleRate) / 1000
	return Stats{
		Frames:   count,
		Samples:  int(count) * perFrame,
		Duration: time.Duration(count*uint32(header.FrameMS)) * time.Millisecond,
		WAVBytes: bytes_,
	}, nil
}
