package store

import (
	"context"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"strings"
	"time"
)

// GarbageCollect selects only old partial uploads and unreferenced MEMO files.
// It never expires recordings or follows symlinks. The 24h age floor exceeds the
// server's 120s request deadline, so an active upload cannot be a stale partial.
// New audio is linked from that partial and therefore cannot qualify either.
func (s *Store) GarbageCollect(ctx context.Context, before time.Time, apply bool) ([]string, error) {
	if before.After(time.Now().Add(-24 * time.Hour)) {
		return nil, fmt.Errorf("gc cutoff must be at least 24h old")
	}
	referenced := make(map[string]bool)
	rows, err := s.db.QueryContext(ctx, `SELECT audio_path FROM memos`)
	if err != nil {
		return nil, err
	}
	for rows.Next() {
		var name string
		if err := rows.Scan(&name); err != nil {
			rows.Close()
			return nil, err
		}
		full, err := filepath.Abs(name)
		if err != nil {
			rows.Close()
			return nil, err
		}
		referenced[filepath.Clean(full)] = true
	}
	err = rows.Err()
	rows.Close()
	if err != nil {
		return nil, err
	}
	var candidates []string
	for _, kind := range []string{"incoming", "audio"} {
		root := filepath.Join(s.dataDir, kind)
		if err := filepath.WalkDir(root, func(path string, entry fs.DirEntry, walkErr error) error {
			if walkErr != nil {
				return walkErr
			}
			if err := ctx.Err(); err != nil {
				return err
			}
			if entry.IsDir() || entry.Type()&os.ModeSymlink != 0 {
				return nil
			}
			ext := ".memo"
			if kind == "incoming" {
				ext = ".part"
			}
			if !strings.HasSuffix(entry.Name(), ext) {
				return nil
			}
			info, err := entry.Info()
			if err != nil {
				return err
			}
			if !info.Mode().IsRegular() || !info.ModTime().Before(before) {
				return nil
			}
			full, err := filepath.Abs(path)
			if err != nil {
				return err
			}
			if referenced[filepath.Clean(full)] {
				return nil
			}
			if apply {
				// Re-check immediately before deletion; changed files are kept.
				current, err := os.Lstat(path)
				if err != nil {
					return err
				}
				if !os.SameFile(info, current) || !current.Mode().IsRegular() ||
					!current.ModTime().Equal(info.ModTime()) || current.Size() != info.Size() {
					return nil
				}
				if err := os.Remove(path); err != nil {
					return err
				}
			}
			candidates = append(candidates, path)
			return nil
		}); err != nil {
			return candidates, err
		}
	}
	return candidates, nil
}
