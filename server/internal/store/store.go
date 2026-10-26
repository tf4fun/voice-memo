// Package store owns the SQLite database and the on-disk layout.
//
// It is a concrete type rather than an interface: there is one implementation,
// and tests use a real temporary database instead of a mock.
package store

import (
	"context"
	"database/sql"
	_ "embed"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"time"

	_ "modernc.org/sqlite"
)

//go:embed schema.sql
var schemaV1 string

// schemaVersion is the version this build expects. A database newer than this
// is refused instead of being silently upgraded backwards.
const schemaVersion = 1

// Store is the handle to the database and the data directory.
type Store struct {
	db      *sql.DB
	dataDir string
}

// Open prepares the data directory and migrates the database to schemaVersion.
func Open(dataDir string) (*Store, error) {
	for _, dir := range []string{
		dataDir,
		filepath.Join(dataDir, "audio"),
		filepath.Join(dataDir, "incoming"),
		filepath.Join(dataDir, "rejected"),
	} {
		if err := os.MkdirAll(dir, 0o755); err != nil {
			return nil, fmt.Errorf("create %s: %w", dir, err)
		}
	}

	dbPath := filepath.Join(dataDir, "memo.db")
	db, err := sql.Open("sqlite", dbPath)
	if err != nil {
		return nil, fmt.Errorf("open database: %w", err)
	}
	// A single writer keeps SQLite happy; readers share the same pool.
	db.SetMaxOpenConns(1)
	db.SetConnMaxLifetime(0)

	s := &Store{db: db, dataDir: dataDir}
	if err := s.configure(); err != nil {
		db.Close()
		return nil, err
	}
	if err := s.migrate(); err != nil {
		db.Close()
		return nil, err
	}
	return s, nil
}

func (s *Store) configure() error {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	for _, pragma := range []string{
		"PRAGMA journal_mode = WAL",
		"PRAGMA synchronous = NORMAL",
		"PRAGMA foreign_keys = ON",
		"PRAGMA busy_timeout = 5000",
	} {
		if _, err := s.db.ExecContext(ctx, pragma); err != nil {
			return fmt.Errorf("%s: %w", pragma, err)
		}
	}
	return nil
}

func (s *Store) migrate() error {
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	var version int
	if err := s.db.QueryRowContext(ctx, "PRAGMA user_version").Scan(&version); err != nil {
		return fmt.Errorf("read schema version: %w", err)
	}
	switch {
	case version == schemaVersion:
		return nil
	case version > schemaVersion:
		return fmt.Errorf("database schema is version %d, this build understands %d",
			version, schemaVersion)
	}

	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return fmt.Errorf("begin migration: %w", err)
	}
	defer tx.Rollback()

	if _, err := tx.ExecContext(ctx, schemaV1); err != nil {
		return fmt.Errorf("apply schema v1: %w", err)
	}
	if _, err := tx.ExecContext(ctx, fmt.Sprintf("PRAGMA user_version = %d", schemaVersion)); err != nil {
		return fmt.Errorf("stamp schema version: %w", err)
	}
	if err := tx.Commit(); err != nil {
		return fmt.Errorf("commit migration: %w", err)
	}
	return nil
}

// ErrClosed is returned when the store is used after Close.
var ErrClosed = errors.New("store is closed")

// Ping verifies the database still answers.
func (s *Store) Ping(ctx context.Context) error {
	if s == nil || s.db == nil {
		return ErrClosed
	}
	var one int
	return s.db.QueryRowContext(ctx, "SELECT 1").Scan(&one)
}

// SchemaVersion reports the version recorded in the database.
func (s *Store) SchemaVersion(ctx context.Context) (int, error) {
	if s == nil || s.db == nil {
		return 0, ErrClosed
	}
	var version int
	err := s.db.QueryRowContext(ctx, "PRAGMA user_version").Scan(&version)
	return version, err
}

// DB exposes the pool for the query helpers added in later milestones.
// The pool is nil once the store is closed.
func (s *Store) DB() *sql.DB { return s.db }

// DataDir is the root of the on-disk state.
func (s *Store) DataDir() string { return s.dataDir }

// Close releases the database. It is safe to call twice.
func (s *Store) Close() error {
	if s == nil || s.db == nil {
		return nil
	}
	err := s.db.Close()
	s.db = nil
	if err != nil && !errors.Is(err, sql.ErrConnDone) {
		return err
	}
	return nil
}
