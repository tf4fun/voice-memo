// Command memogen builds a MEMO container from an Ogg Opus file.
//
// It exists so tests and manual checks have real Opus frames to work with: the
// device is the normal producer, but it is not always plugged in.
//
//	memogen -in tone.ogg -out tone.memo -frame-ms 20 -memo-id <uuid>
package main

import (
	"bytes"
	"encoding/binary"
	"flag"
	"fmt"
	"os"
	"time"

	"github.com/pion/opus/pkg/oggreader"
)

func main() {
	in := flag.String("in", "", "input Ogg Opus file")
	out := flag.String("out", "", "output MEMO container")
	frameMS := flag.Int("frame-ms", 20, "Opus frame duration carried in the container header")
	memoID := flag.String("memo-id", "00000000-0000-4000-8000-000000000000", "memo id written into the header")
	flag.Parse()

	if err := run(*in, *out, *frameMS, *memoID); err != nil {
		fmt.Fprintln(os.Stderr, "memogen:", err)
		os.Exit(1)
	}
}

func isOggHeaderPacket(packet []byte) bool {
	return bytes.HasPrefix(packet, []byte("OpusHead")) ||
		bytes.HasPrefix(packet, []byte("OpusTags"))
}

func run(inPath, outPath string, frameMS int, memoID string) error {
	if inPath == "" || outPath == "" {
		return fmt.Errorf("-in and -out are required")
	}

	f, err := os.Open(inPath)
	if err != nil {
		return err
	}
	defer f.Close()

	ogg, header, err := oggreader.NewWith(f)
	if err != nil {
		return fmt.Errorf("read ogg header: %w", err)
	}

	var frames [][]byte
	for {
		packet, _, err := ogg.ParseNextPacket()
		if err != nil || packet == nil {
			break
		}
		// The Ogg reader hands back the OpusHead and OpusTags header packets
		// before the audio; only what follows belongs in the container.
		if isOggHeaderPacket(packet) {
			continue
		}
		frames = append(frames, packet)
	}
	if len(frames) == 0 {
		return fmt.Errorf("no opus packets found")
	}

	// Ogg granule positions are 48 kHz based and include end padding, so the
	// frame count times the frame duration is the honest figure.
	durationMS := len(frames) * frameMS

	out, err := os.Create(outPath)
	if err != nil {
		return err
	}
	defer out.Close()

	buf := make([]byte, 80)
	copy(buf[0:4], "MEMO")
	buf[4] = 1 // version
	buf[5] = 1 // codec: opus
	binary.LittleEndian.PutUint16(buf[6:8], uint16(header.SampleRate))
	buf[8] = uint8(header.Channels)
	buf[9] = uint8(frameMS)
	binary.LittleEndian.PutUint32(buf[12:16], uint32(durationMS))
	binary.LittleEndian.PutUint64(buf[16:24], uint64(time.Now().Unix()))
	copy(buf[24:60], memoID) // 36-byte field: a dashed UUID fits exactly
	binary.LittleEndian.PutUint32(buf[60:64], uint32(len(frames)))
	if _, err := out.Write(buf); err != nil {
		return err
	}

	for _, frame := range frames {
		if len(frame) > 0xFFFF {
			return fmt.Errorf("frame of %d bytes exceeds the uint16 length prefix", len(frame))
		}
		var length [2]byte
		binary.LittleEndian.PutUint16(length[:], uint16(len(frame)))
		if _, err := out.Write(length[:]); err != nil {
			return err
		}
		if _, err := out.Write(frame); err != nil {
			return err
		}
	}

	fmt.Printf("wrote %s: %d frames, %d ms, %d Hz, %d ch\n",
		outPath, len(frames), durationMS, header.SampleRate, header.Channels)
	return nil
}
